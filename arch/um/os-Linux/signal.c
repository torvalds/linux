// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015 Anton Ivanov (aivanov@{brocade.com,kot-begemot.co.uk})
 * Copyright (C) 2015 Thomas Meyer (thomas@m3y3r.de)
 * Copyright (C) 2004 PathScale, Inc
 * Copyright (C) 2004 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <strings.h>
#include <as-layout.h>
#include <kern_util.h>
#include <os.h>
#include <sysdep/mcontext.h>
#include <um_malloc.h>
#include <sys/ucontext.h>
#include <timetravel.h>

void (*sig_info[NSIG])(int, struct siginfo *, struct uml_pt_regs *) = {
	[SIGTRAP]	= relay_signal,
	[SIGFPE]	= relay_signal,
	[SIGILL]	= relay_signal,
	[SIGWINCH]	= winch,
	[SIGBUS]	= bus_handler,
	[SIGSEGV]	= segv_handler,
	[SIGIO]		= sigio_handler,
};

static void sig_handler_common(int sig, struct siginfo *si, mcontext_t *mc)
{
	struct uml_pt_regs r;
	int save_errno = errno;

	r.is_user = 0;
	if (sig == SIGSEGV) {
		/* For segfaults, we want the data from the sigcontext. */
		get_regs_from_mc(&r, mc);
		GET_FAULTINFO_FROM_MC(r.faultinfo, mc);
	}

	/* enable signals if sig isn't IRQ signal */
	if ((sig != SIGIO) && (sig != SIGWINCH))
		unblock_signals_trace();

	(*sig_info[sig])(sig, si, &r);

	errno = save_errno;
}

/*
 * These are the asynchronous signals.  SIGPROF is excluded because we want to
 * be able to profile all of UML, not just the non-critical sections.  If
 * profiling is not thread-safe, then that is not my problem.  We can disable
 * profiling when SMP is enabled in that case.
 */
#define SIGIO_BIT 0
#define SIGIO_MASK (1 << SIGIO_BIT)

#define SIGALRM_BIT 1
#define SIGALRM_MASK (1 << SIGALRM_BIT)

int signals_enabled;
#ifdef UML_CONFIG_UML_TIME_TRAVEL_SUPPORT
static int signals_blocked;
#else
#define signals_blocked 0
#endif
static unsigned int signals_pending;
static unsigned int signals_active = 0;

static void sig_handler(int sig, struct siginfo *si, mcontext_t *mc)
{
	int enabled = signals_enabled;

	if ((signals_blocked || !enabled) && (sig == SIGIO)) {
		/*
		 * In TT_MODE_EXTERNAL, need to still call time-travel
		 * handlers unless signals are also blocked for the
		 * external time message processing. This will mark
		 * signals_pending by itself (only if necessary.)
		 */
		if (!signals_blocked && time_travel_mode == TT_MODE_EXTERNAL)
			sigio_run_timetravel_handlers();
		else
			signals_pending |= SIGIO_MASK;
		return;
	}

	block_signals_trace();

	sig_handler_common(sig, si, mc);

	um_set_signals_trace(enabled);
}

static void timer_real_alarm_handler(mcontext_t *mc)
{
	struct uml_pt_regs regs;

	if (mc != NULL)
		get_regs_from_mc(&regs, mc);
	else
		memset(&regs, 0, sizeof(regs));
	timer_handler(SIGALRM, NULL, &regs);
}

static void timer_alarm_handler(int sig, struct siginfo *unused_si, mcontext_t *mc)
{
	int enabled;

	enabled = signals_enabled;
	if (!signals_enabled) {
		signals_pending |= SIGALRM_MASK;
		return;
	}

	block_signals_trace();

	signals_active |= SIGALRM_MASK;

	timer_real_alarm_handler(mc);

	signals_active &= ~SIGALRM_MASK;

	um_set_signals_trace(enabled);
}

void deliver_alarm(void) {
    timer_alarm_handler(SIGALRM, NULL, NULL);
}

void timer_set_signal_handler(void)
{
	set_handler(SIGALRM);
}

void set_sigstack(void *sig_stack, int size)
{
	stack_t stack = {
		.ss_flags = 0,
		.ss_sp = sig_stack,
		.ss_size = size
	};

	if (sigaltstack(&stack, NULL) != 0)
		panic("enabling signal stack failed, errno = %d\n", errno);
}

static void sigusr1_handler(int sig, struct siginfo *unused_si, mcontext_t *mc)
{
	uml_pm_wake();
}

void register_pm_wake_signal(void)
{
	set_handler(SIGUSR1);
}

static void (*handlers[_NSIG])(int sig, struct siginfo *si, mcontext_t *mc) = {
	[SIGSEGV] = sig_handler,
	[SIGBUS] = sig_handler,
	[SIGILL] = sig_handler,
	[SIGFPE] = sig_handler,
	[SIGTRAP] = sig_handler,

	[SIGIO] = sig_handler,
	[SIGWINCH] = sig_handler,
	[SIGALRM] = timer_alarm_handler,

	[SIGUSR1] = sigusr1_handler,
};

static void hard_handler(int sig, siginfo_t *si, void *p)
{
	ucontext_t *uc = p;
	mcontext_t *mc = &uc->uc_mcontext;
	unsigned long pending = 1UL << sig;

	do {
		int nested, bail;

		/*
		 * pending comes back with one bit set for each
		 * interrupt that arrived while setting up the stack,
		 * plus a bit for this interrupt, plus the zero bit is
		 * set if this is a nested interrupt.
		 * If bail is true, then we interrupted another
		 * handler setting up the stack.  In this case, we
		 * have to return, and the upper handler will deal
		 * with this interrupt.
		 */
		bail = to_irq_stack(&pending);
		if (bail)
			return;

		nested = pending & 1;
		pending &= ~1;

		while ((sig = ffs(pending)) != 0){
			sig--;
			pending &= ~(1 << sig);
			(*handlers[sig])(sig, (struct siginfo *)si, mc);
		}

		/*
		 * Again, pending comes back with a mask of signals
		 * that arrived while tearing down the stack.  If this
		 * is non-zero, we just go back, set up the stack
		 * again, and handle the new interrupts.
		 */
		if (!nested)
			pending = from_irq_stack(nested);
	} while (pending);
}

void set_handler(int sig)
{
	struct sigaction action;
	int flags = SA_SIGINFO | SA_ONSTACK;
	sigset_t sig_mask;

	action.sa_sigaction = hard_handler;

	/* block irq ones */
	sigemptyset(&action.sa_mask);
	sigaddset(&action.sa_mask, SIGIO);
	sigaddset(&action.sa_mask, SIGWINCH);
	sigaddset(&action.sa_mask, SIGALRM);

	if (sig == SIGSEGV)
		flags |= SA_NODEFER;

	if (sigismember(&action.sa_mask, sig))
		flags |= SA_RESTART; /* if it's an irq signal */

	action.sa_flags = flags;
	action.sa_restorer = NULL;
	if (sigaction(sig, &action, NULL) < 0)
		panic("sigaction failed - errno = %d\n", errno);

	sigemptyset(&sig_mask);
	sigaddset(&sig_mask, sig);
	if (sigprocmask(SIG_UNBLOCK, &sig_mask, NULL) < 0)
		panic("sigprocmask failed - errno = %d\n", errno);
}

void send_sigio_to_self(void)
{
	kill(os_getpid(), SIGIO);
}

int change_sig(int signal, int on)
{
	sigset_t sigset;

	sigemptyset(&sigset);
	sigaddset(&sigset, signal);
	if (sigprocmask(on ? SIG_UNBLOCK : SIG_BLOCK, &sigset, NULL) < 0)
		return -errno;

	return 0;
}

void block_signals(void)
{
	signals_enabled = 0;
	/*
	 * This must return with signals disabled, so this barrier
	 * ensures that writes are flushed out before the return.
	 * This might matter if gcc figures out how to inline this and
	 * decides to shuffle this code into the caller.
	 */
	barrier();
}

void unblock_signals(void)
{
	int save_pending;

	if (signals_enabled == 1)
		return;

	signals_enabled = 1;
#ifdef UML_CONFIG_UML_TIME_TRAVEL_SUPPORT
	deliver_time_travel_irqs();
#endif

	/*
	 * We loop because the IRQ handler returns with interrupts off.  So,
	 * interrupts may have arrived and we need to re-enable them and
	 * recheck signals_pending.
	 */
	while (1) {
		/*
		 * Save and reset save_pending after enabling signals.  This
		 * way, signals_pending won't be changed while we're reading it.
		 *
		 * Setting signals_enabled and reading signals_pending must
		 * happen in this order, so have the barrier here.
		 */
		barrier();

		save_pending = signals_pending;
		if (save_pending == 0)
			return;

		signals_pending = 0;

		/*
		 * We have pending interrupts, so disable signals, as the
		 * handlers expect them off when they are called.  They will
		 * be enabled again above. We need to trace this, as we're
		 * expected to be enabling interrupts already, but any more
		 * tracing that happens inside the handlers we call for the
		 * pending signals will mess up the tracing state.
		 */
		signals_enabled = 0;
		um_trace_signals_off();

		/*
		 * Deal with SIGIO first because the alarm handler might
		 * schedule, leaving the pending SIGIO stranded until we come
		 * back here.
		 *
		 * SIGIO's handler doesn't use siginfo or mcontext,
		 * so they can be NULL.
		 */
		if (save_pending & SIGIO_MASK)
			sig_handler_common(SIGIO, NULL, NULL);

		/* Do not reenter the handler */

		if ((save_pending & SIGALRM_MASK) && (!(signals_active & SIGALRM_MASK)))
			timer_real_alarm_handler(NULL);

		/* Rerun the loop only if there is still pending SIGIO and not in TIMER handler */

		if (!(signals_pending & SIGIO_MASK) && (signals_active & SIGALRM_MASK))
			return;

		/* Re-enable signals and trace that we're doing so. */
		um_trace_signals_on();
		signals_enabled = 1;
	}
}

int um_set_signals(int enable)
{
	int ret;
	if (signals_enabled == enable)
		return enable;

	ret = signals_enabled;
	if (enable)
		unblock_signals();
	else block_signals();

	return ret;
}

int um_set_signals_trace(int enable)
{
	int ret;
	if (signals_enabled == enable)
		return enable;

	ret = signals_enabled;
	if (enable)
		unblock_signals_trace();
	else
		block_signals_trace();

	return ret;
}

#ifdef UML_CONFIG_UML_TIME_TRAVEL_SUPPORT
void mark_sigio_pending(void)
{
	signals_pending |= SIGIO_MASK;
}

void block_signals_hard(void)
{
	if (signals_blocked)
		return;
	signals_blocked = 1;
	barrier();
}

void unblock_signals_hard(void)
{
	if (!signals_blocked)
		return;
	/* Must be set to 0 before we check the pending bits etc. */
	signals_blocked = 0;
	barrier();

	if (signals_pending && signals_enabled) {
		/* this is a bit inefficient, but that's not really important */
		block_signals();
		unblock_signals();
	} else if (signals_pending & SIGIO_MASK) {
		/* we need to run time-travel handlers even if not enabled */
		sigio_run_timetravel_handlers();
	}
}
#endif

int os_is_signal_stack(void)
{
	stack_t ss;
	sigaltstack(NULL, &ss);

	return ss.ss_flags & SS_ONSTACK;
}
