#include "inproc_backend.hpp"
#ifdef SENTRY_WITH_INPROC_BACKEND

#include "../unwind.hpp"

using namespace sentry;
using namespace backends;

#define SIGNAL_DEF(Sig, Desc) \
    { Sig, #Sig, Desc }

struct SignalSlot {
    int signum;
    const char *signame;
    const char *sigdesc;
};

static const size_t MAX_FRAMES = 128;

// we need quite a bit of space for backtrace generation
static const size_t SIGNAL_COUNT = 6;
static const size_t SIGNAL_STACK_SIZE = 65536;
static struct sigaction g_sigaction;
static struct sigaction g_previous_handlers[SIGNAL_COUNT];
stack_t g_signal_stack;

static const SignalSlot SIGNAL_DEFINITIONS[SIGNAL_COUNT] = {
    SIGNAL_DEF(SIGILL, "IllegalInstruction"),
    SIGNAL_DEF(SIGTRAP, "Trap"),
    SIGNAL_DEF(SIGABRT, "Abort"),
    SIGNAL_DEF(SIGBUS, "BusError"),
    SIGNAL_DEF(SIGFPE, "FloatingPointException"),
    SIGNAL_DEF(SIGSEGV, "Segfault"),
};

static void reset_signal_handlers() {
    for (size_t i = 0; i < SIGNAL_COUNT; i++) {
        sigaction(SIGNAL_DEFINITIONS[i].signum, &g_previous_handlers[i], 0);
    }
}

static void invoke_signal_handler(int signum,
                                  siginfo_t *info,
                                  void *user_context) {
    for (int i = 0; i < SIGNAL_COUNT; ++i) {
        if (SIGNAL_DEFINITIONS[i].signum == signum) {
            struct sigaction *handler = &g_previous_handlers[i];
            if (handler->sa_handler == SIG_DFL) {
                raise(signum);
            } else if (handler->sa_flags & SA_SIGINFO) {
                handler->sa_sigaction(signum, info, user_context);
            } else if (handler->sa_handler != SIG_IGN) {
                // This handler can only handle to signal number (ANSI C)
                void (*func)(int) = handler->sa_handler;
                func(signum);
            }
        }
    }
}

static void handle_signal(int signum, siginfo_t *info, void *user_context) {
    sentry_ucontext_t uctx;
    uctx.siginfo = info;
    uctx.user_context = (ucontext_t *)user_context;

    void *backtrace[MAX_FRAMES];
    size_t frames = unwinders::unwind_stack_backtrace(
        nullptr, &uctx, &backtrace[0], MAX_FRAMES);

    reset_signal_handlers();
    invoke_signal_handler(signum, info, user_context);
}

InprocBackend::InprocBackend() {
    g_signal_stack.ss_sp = malloc(SIGNAL_STACK_SIZE);
    g_signal_stack.ss_size = SIGNAL_STACK_SIZE;
    g_signal_stack.ss_flags = 0;
    memset(g_previous_handlers, 0, sizeof(g_previous_handlers));
    sigemptyset(&g_sigaction.sa_mask);
    g_sigaction.sa_sigaction = handle_signal;
    g_sigaction.sa_flags = SA_SIGINFO | SA_ONSTACK;
}

InprocBackend::~InprocBackend() {
}

void InprocBackend::start() {
    sigaltstack(&g_signal_stack, 0);

    for (size_t i = 0; i < SIGNAL_COUNT; ++i) {
        if (sigaction(SIGNAL_DEFINITIONS[i].signum, nullptr,
                      &g_previous_handlers[i]) == -1) {
            return;
        }
    }

    for (size_t i = 0; i < SIGNAL_COUNT; ++i) {
        sigaction(SIGNAL_DEFINITIONS[i].signum, &g_sigaction, nullptr);
    }

    // not that any of this matters ...
    std::atomic_signal_fence(std::memory_order_release);
}

void InprocBackend::flush_scope_state(const sentry::Scope &scope) {
}

void InprocBackend::add_breadcrumb(sentry::Value breadcrumb) {
}

#endif
