/*
 * Copyright (C) 2004 PathScale, Inc
 * Copyright (C) 2004 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <signal.h>
#include <strings.h>
#include "kern_util.h"
#include "os.h"
#include "sysdep/barrier.h"
#include "sysdep/sigcontext.h"
#include "user.h"

/* Copied from linux/compiler-gcc.h since we can't include it directly */
#define barrier() __asm__ __volatile__("": : :"memory")

/*
 * These are the asynchronous signals.  SIGPROF is excluded because we want to
 * be able to profile all of UML, not just the non-critical sections.  If
 * profiling is not thread-safe, then that is not my problem.  We can disable
 * profiling when SMP is enabled in that case.
 */
#define SIGIO_BIT 0
#define SIGIO_MASK (1 << SIGIO_BIT)

#define SIGVTALRM_BIT 1
#define SIGVTALRM_MASK (1 << SIGVTALRM_BIT)

static int signals_enabled;
static unsigned int pending;

void sig_handler(int sig, struct sigcontext *sc)
{
	int enabled;

	enabled = signals_enabled;
	if (!enabled && (sig == SIGIO)) {
		pending |= SIGIO_MASK;
		return;
	}

	block_signals();

	sig_handler_common_skas(sig, sc);

	set_signals(enabled);
}

static void real_alarm_handler(struct sigcontext *sc)
{
	struct uml_pt_regs regs;

	if (sc != NULL)
		copy_sc(&regs, sc);
	regs.is_user = 0;
	unblock_signals();
	timer_handler(SIGVTALRM, &regs);
}

void alarm_handler(int sig, struct sigcontext *sc)
{
	int enabled;

	enabled = signals_enabled;
	if (!signals_enabled) {
		pending |= SIGVTALRM_MASK;
		return;
	}

	block_signals();

	real_alarm_handler(sc);
	set_signals(enabled);
}

void timer_init(void)
{
	set_handler(SIGVTALRM, (__sighandler_t) alarm_handler,
		    SA_ONSTACK | SA_RESTART, SIGUSR1, SIGIO, SIGWINCH, -1);
}

void set_sigstack(void *sig_stack, int size)
{
	stack_t stack = ((stack_t) { .ss_flags	= 0,
				     .ss_sp	= (__ptr_t) sig_stack,
				     .ss_size 	= size - sizeof(void *) });

	if (sigaltstack(&stack, NULL) != 0)
		panic("enabling signal stack failed, errno = %d\n", errno);
}

void remove_sigstack(void)
{
	stack_t stack = ((stack_t) { .ss_flags	= SS_DISABLE,
				     .ss_sp	= NULL,
				     .ss_size	= 0 });

	if (sigaltstack(&stack, NULL) != 0)
		panic("disabling signal stack failed, errno = %d\n", errno);
}

void (*handlers[_NSIG])(int sig, struct sigcontext *sc);

void handle_signal(int sig, struct sigcontext *sc)
{
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
			(*handlers[sig])(sig, sc);
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

extern void hard_handler(int sig);

void set_handler(int sig, void (*handler)(int), int flags, ...)
{
	struct sigaction action;
	va_list ap;
	sigset_t sig_mask;
	int mask;

	handlers[sig] = (void (*)(int, struct sigcontext *)) handler;
	action.sa_handler = hard_handler;

	sigemptyset(&action.sa_mask);

	va_start(ap, flags);
	while ((mask = va_arg(ap, int)) != -1)
		sigaddset(&action.sa_mask, mask);
	va_end(ap);

	action.sa_flags = flags;
	action.sa_restorer = NULL;
	if (sigaction(sig, &action, NULL) < 0)
		panic("sigaction failed - errno = %d\n", errno);

	sigemptyset(&sig_mask);
	sigaddset(&sig_mask, sig);
	if (sigprocmask(SIG_UNBLOCK, &sig_mask, NULL) < 0)
		panic("sigprocmask failed - errno = %d\n", errno);
}

int change_sig(int signal, int on)
{
	sigset_t sigset, old;

	sigemptyset(&sigset);
	sigaddset(&sigset, signal);
	if (sigprocmask(on ? SIG_UNBLOCK : SIG_BLOCK, &sigset, &old) < 0)
		return -errno;
	return !sigismember(&old, signal);
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

	/*
	 * We loop because the IRQ handler returns with interrupts off.  So,
	 * interrupts may have arrived and we need to re-enable them and
	 * recheck pending.
	 */
	while(1) {
		/*
		 * Save and reset save_pending after enabling signals.  This
		 * way, pending won't be changed while we're reading it.
		 */
		signals_enabled = 1;

		/*
		 * Setting signals_enabled and reading pending must
		 * happen in this order.
		 */
		barrier();

		save_pending = pending;
		if (save_pending == 0)
			return;

		pending = 0;

		/*
		 * We have pending interrupts, so disable signals, as the
		 * handlers expect them off when they are called.  They will
		 * be enabled again above.
		 */

		signals_enabled = 0;

		/*
		 * Deal with SIGIO first because the alarm handler might
		 * schedule, leaving the pending SIGIO stranded until we come
		 * back here.
		 */
		if (save_pending & SIGIO_MASK)
			sig_handler_common_skas(SIGIO, NULL);

		if (save_pending & SIGVTALRM_MASK)
			real_alarm_handler(NULL);
	}
}

int get_signals(void)
{
	return signals_enabled;
}

int set_signals(int enable)
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
