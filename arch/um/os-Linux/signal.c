/*
 * Copyright (C) 2004 PathScale, Inc
 * Licensed under the GPL
 */

#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <sys/mman.h>
#include "user_util.h"
#include "user.h"
#include "signal_kern.h"
#include "sysdep/sigcontext.h"
#include "sysdep/signal.h"
#include "sigcontext.h"
#include "mode.h"
#include "os.h"

/* These are the asynchronous signals.  SIGVTALRM and SIGARLM are handled
 * together under SIGVTALRM_BIT.  SIGPROF is excluded because we want to
 * be able to profile all of UML, not just the non-critical sections.  If
 * profiling is not thread-safe, then that is not my problem.  We can disable
 * profiling when SMP is enabled in that case.
 */
#define SIGIO_BIT 0
#define SIGIO_MASK (1 << SIGIO_BIT)

#define SIGVTALRM_BIT 1
#define SIGVTALRM_MASK (1 << SIGVTALRM_BIT)

#define SIGALRM_BIT 2
#define SIGALRM_MASK (1 << SIGALRM_BIT)

static int signals_enabled = 1;
static int pending = 0;

void sig_handler(ARCH_SIGHDLR_PARAM)
{
	struct sigcontext *sc;
	int enabled;

	/* Must be the first thing that this handler does - x86_64 stores
	 * the sigcontext in %rdx, and we need to save it before it has a
	 * chance to get trashed.
	 */

	ARCH_GET_SIGCONTEXT(sc, sig);

	enabled = signals_enabled;
	if(!enabled && (sig == SIGIO)){
		pending |= SIGIO_MASK;
		return;
	}

	block_signals();

	CHOOSE_MODE_PROC(sig_handler_common_tt, sig_handler_common_skas,
			 sig, sc);

	set_signals(enabled);
}

extern int timer_irq_inited;

static void real_alarm_handler(int sig, struct sigcontext *sc)
{
	if(!timer_irq_inited){
		signals_enabled = 1;
		return;
	}

	if(sig == SIGALRM)
		switch_timers(0);

	CHOOSE_MODE_PROC(sig_handler_common_tt, sig_handler_common_skas,
			 sig, sc);

	if(sig == SIGALRM)
		switch_timers(1);

}

void alarm_handler(ARCH_SIGHDLR_PARAM)
{
	struct sigcontext *sc;
	int enabled;

	ARCH_GET_SIGCONTEXT(sc, sig);

	enabled = signals_enabled;
	if(!signals_enabled){
		if(sig == SIGVTALRM)
			pending |= SIGVTALRM_MASK;
		else pending |= SIGALRM_MASK;

		return;
	}

	block_signals();

	real_alarm_handler(sig, sc);
	set_signals(enabled);
}

extern void do_boot_timer_handler(struct sigcontext * sc);

void boot_timer_handler(ARCH_SIGHDLR_PARAM)
{
	struct sigcontext *sc;
	int enabled;

	ARCH_GET_SIGCONTEXT(sc, sig);

	enabled = signals_enabled;
	if(!enabled){
		if(sig == SIGVTALRM)
			pending |= SIGVTALRM_MASK;
		else pending |= SIGALRM_MASK;
		return;
	}

	block_signals();

	do_boot_timer_handler(sc);
	set_signals(enabled);
}

void set_sigstack(void *sig_stack, int size)
{
	stack_t stack = ((stack_t) { .ss_flags	= 0,
				     .ss_sp	= (__ptr_t) sig_stack,
				     .ss_size 	= size - sizeof(void *) });

	if(sigaltstack(&stack, NULL) != 0)
		panic("enabling signal stack failed, errno = %d\n", errno);
}

void remove_sigstack(void)
{
	stack_t stack = ((stack_t) { .ss_flags	= SS_DISABLE,
				     .ss_sp	= NULL,
				     .ss_size	= 0 });

	if(sigaltstack(&stack, NULL) != 0)
		panic("disabling signal stack failed, errno = %d\n", errno);
}

void set_handler(int sig, void (*handler)(int), int flags, ...)
{
	struct sigaction action;
	va_list ap;
	sigset_t sig_mask;
	int mask;

	va_start(ap, flags);
	action.sa_handler = handler;
	sigemptyset(&action.sa_mask);
	while((mask = va_arg(ap, int)) != -1){
		sigaddset(&action.sa_mask, mask);
	}
	va_end(ap);
	action.sa_flags = flags;
	action.sa_restorer = NULL;
	if(sigaction(sig, &action, NULL) < 0)
		panic("sigaction failed - errno = %d\n", errno);

	sigemptyset(&sig_mask);
	sigaddset(&sig_mask, sig);
	if(sigprocmask(SIG_UNBLOCK, &sig_mask, NULL) < 0)
		panic("sigprocmask failed - errno = %d\n", errno);
}

int change_sig(int signal, int on)
{
	sigset_t sigset, old;

	sigemptyset(&sigset);
	sigaddset(&sigset, signal);
	sigprocmask(on ? SIG_UNBLOCK : SIG_BLOCK, &sigset, &old);
	return(!sigismember(&old, signal));
}

void block_signals(void)
{
	signals_enabled = 0;
}

void unblock_signals(void)
{
	int save_pending;

	if(signals_enabled == 1)
		return;

	/* We loop because the IRQ handler returns with interrupts off.  So,
	 * interrupts may have arrived and we need to re-enable them and
	 * recheck pending.
	 */
	while(1){
		/* Save and reset save_pending after enabling signals.  This
		 * way, pending won't be changed while we're reading it.
		 */
		signals_enabled = 1;

		save_pending = pending;
		if(save_pending == 0)
			return;

		pending = 0;

		/* We have pending interrupts, so disable signals, as the
		 * handlers expect them off when they are called.  They will
		 * be enabled again above.
		 */

		signals_enabled = 0;

		/* Deal with SIGIO first because the alarm handler might
		 * schedule, leaving the pending SIGIO stranded until we come
		 * back here.
		 */
		if(save_pending & SIGIO_MASK)
			CHOOSE_MODE_PROC(sig_handler_common_tt,
					 sig_handler_common_skas, SIGIO, NULL);

		if(save_pending & SIGALRM_MASK)
			real_alarm_handler(SIGALRM, NULL);

		if(save_pending & SIGVTALRM_MASK)
			real_alarm_handler(SIGVTALRM, NULL);
	}
}

int get_signals(void)
{
	return signals_enabled;
}

int set_signals(int enable)
{
	int ret;
	if(signals_enabled == enable)
		return enable;

	ret = signals_enabled;
	if(enable)
		unblock_signals();
	else block_signals();

	return ret;
}

void os_usr1_signal(int on)
{
	change_sig(SIGUSR1, on);
}
