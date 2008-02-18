/* MN10300 Signal handling
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>
#include <linux/stddef.h>
#include <linux/tty.h>
#include <linux/personality.h>
#include <linux/suspend.h>
#include <asm/cacheflush.h>
#include <asm/ucontext.h>
#include <asm/uaccess.h>
#include <asm/fpu.h>
#include "sigframe.h"

#define DEBUG_SIG 0

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

/*
 * atomically swap in the new signal mask, and wait for a signal.
 */
asmlinkage long sys_sigsuspend(int history0, int history1, old_sigset_t mask)
{
	mask &= _BLOCKABLE;
	spin_lock_irq(&current->sighand->siglock);
	current->saved_sigmask = current->blocked;
	siginitset(&current->blocked, mask);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	current->state = TASK_INTERRUPTIBLE;
	schedule();
	set_thread_flag(TIF_RESTORE_SIGMASK);
	return -ERESTARTNOHAND;
}

/*
 * set signal action syscall
 */
asmlinkage long sys_sigaction(int sig,
			      const struct old_sigaction __user *act,
			      struct old_sigaction __user *oact)
{
	struct k_sigaction new_ka, old_ka;
	int ret;

	if (act) {
		old_sigset_t mask;
		if (verify_area(VERIFY_READ, act, sizeof(*act)) ||
		    __get_user(new_ka.sa.sa_handler, &act->sa_handler) ||
		    __get_user(new_ka.sa.sa_restorer, &act->sa_restorer))
			return -EFAULT;
		__get_user(new_ka.sa.sa_flags, &act->sa_flags);
		__get_user(mask, &act->sa_mask);
		siginitset(&new_ka.sa.sa_mask, mask);
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		if (verify_area(VERIFY_WRITE, oact, sizeof(*oact)) ||
		    __put_user(old_ka.sa.sa_handler, &oact->sa_handler) ||
		    __put_user(old_ka.sa.sa_restorer, &oact->sa_restorer))
			return -EFAULT;
		__put_user(old_ka.sa.sa_flags, &oact->sa_flags);
		__put_user(old_ka.sa.sa_mask.sig[0], &oact->sa_mask);
	}

	return ret;
}

/*
 * set alternate signal stack syscall
 */
asmlinkage long sys_sigaltstack(const stack_t __user *uss, stack_t *uoss)
{
	return do_sigaltstack(uss, uoss, __frame->sp);
}

/*
 * do a signal return; undo the signal stack.
 */
static int restore_sigcontext(struct pt_regs *regs,
			      struct sigcontext __user *sc, long *_d0)
{
	unsigned int err = 0;

	if (is_using_fpu(current))
		fpu_kill_state(current);

#define COPY(x) err |= __get_user(regs->x, &sc->x)
	COPY(d1); COPY(d2); COPY(d3);
	COPY(a0); COPY(a1); COPY(a2); COPY(a3);
	COPY(e0); COPY(e1); COPY(e2); COPY(e3);
	COPY(e4); COPY(e5); COPY(e6); COPY(e7);
	COPY(lar); COPY(lir);
	COPY(mdr); COPY(mdrq);
	COPY(mcvf); COPY(mcrl); COPY(mcrh);
	COPY(sp); COPY(pc);
#undef COPY

	{
		unsigned int tmpflags;
#ifndef CONFIG_MN10300_USING_JTAG
#define USER_EPSW (EPSW_FLAG_Z | EPSW_FLAG_N | EPSW_FLAG_C | EPSW_FLAG_V | \
		   EPSW_T | EPSW_nAR)
#else
#define USER_EPSW (EPSW_FLAG_Z | EPSW_FLAG_N | EPSW_FLAG_C | EPSW_FLAG_V | \
		   EPSW_nAR)
#endif
		err |= __get_user(tmpflags, &sc->epsw);
		regs->epsw = (regs->epsw & ~USER_EPSW) |
		  (tmpflags & USER_EPSW);
		regs->orig_d0 = -1;		/* disable syscall checks */
	}

	{
		struct fpucontext *buf;
		err |= __get_user(buf, &sc->fpucontext);
		if (buf) {
			if (verify_area(VERIFY_READ, buf, sizeof(*buf)))
				goto badframe;
			err |= fpu_restore_sigcontext(buf);
		}
	}

	err |= __get_user(*_d0, &sc->d0);
	return err;

badframe:
	return 1;
}

/*
 * standard signal return syscall
 */
asmlinkage long sys_sigreturn(void)
{
	struct sigframe __user *frame = (struct sigframe __user *) __frame->sp;
	sigset_t set;
	long d0;

	if (verify_area(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__get_user(set.sig[0], &frame->sc.oldmask))
		goto badframe;

	if (_NSIG_WORDS > 1 &&
	    __copy_from_user(&set.sig[1], &frame->extramask,
			     sizeof(frame->extramask)))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	if (restore_sigcontext(__frame, &frame->sc, &d0))
		goto badframe;

	return d0;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

/*
 * realtime signal return syscall
 */
asmlinkage long sys_rt_sigreturn(void)
{
	struct rt_sigframe __user *frame =
		(struct rt_sigframe __user *) __frame->sp;
	sigset_t set;
	unsigned long d0;

	if (verify_area(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	if (restore_sigcontext(__frame, &frame->uc.uc_mcontext, &d0))
		goto badframe;

	if (do_sigaltstack(&frame->uc.uc_stack, NULL, __frame->sp) == -EFAULT)
		goto badframe;

	return d0;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

/*
 * store the userspace context into a signal frame
 */
static int setup_sigcontext(struct sigcontext __user *sc,
			    struct fpucontext *fpuctx,
			    struct pt_regs *regs,
			    unsigned long mask)
{
	int tmp, err = 0;

#define COPY(x) err |= __put_user(regs->x, &sc->x)
	COPY(d0); COPY(d1); COPY(d2); COPY(d3);
	COPY(a0); COPY(a1); COPY(a2); COPY(a3);
	COPY(e0); COPY(e1); COPY(e2); COPY(e3);
	COPY(e4); COPY(e5); COPY(e6); COPY(e7);
	COPY(lar); COPY(lir);
	COPY(mdr); COPY(mdrq);
	COPY(mcvf); COPY(mcrl); COPY(mcrh);
	COPY(sp); COPY(epsw); COPY(pc);
#undef COPY

	tmp = fpu_setup_sigcontext(fpuctx);
	if (tmp < 0)
		err = 1;
	else
		err |= __put_user(tmp ? fpuctx : NULL, &sc->fpucontext);

	/* non-iBCS2 extensions.. */
	err |= __put_user(mask, &sc->oldmask);

	return err;
}

/*
 * determine which stack to use..
 */
static inline void __user *get_sigframe(struct k_sigaction *ka,
					struct pt_regs *regs,
					size_t frame_size)
{
	unsigned long sp;

	/* default to using normal stack */
	sp = regs->sp;

	/* this is the X/Open sanctioned signal stack switching.  */
	if (ka->sa.sa_flags & SA_ONSTACK) {
		if (!on_sig_stack(sp))
			sp = current->sas_ss_sp + current->sas_ss_size;
	}

	return (void __user *) ((sp - frame_size) & ~7UL);
}

/*
 * set up a normal signal frame
 */
static int setup_frame(int sig, struct k_sigaction *ka, sigset_t *set,
		       struct pt_regs *regs)
{
	struct sigframe __user *frame;
	int rsig;

	frame = get_sigframe(ka, regs, sizeof(*frame));

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto give_sigsegv;

	rsig = sig;
	if (sig < 32 &&
	    current_thread_info()->exec_domain &&
	    current_thread_info()->exec_domain->signal_invmap)
		rsig = current_thread_info()->exec_domain->signal_invmap[sig];

	if (__put_user(rsig, &frame->sig) < 0 ||
	    __put_user(&frame->sc, &frame->psc) < 0)
		goto give_sigsegv;

	if (setup_sigcontext(&frame->sc, &frame->fpuctx, regs, set->sig[0]))
		goto give_sigsegv;

	if (_NSIG_WORDS > 1) {
		if (__copy_to_user(frame->extramask, &set->sig[1],
				   sizeof(frame->extramask)))
			goto give_sigsegv;
	}

	/* set up to return from userspace.  If provided, use a stub already in
	 * userspace */
	if (ka->sa.sa_flags & SA_RESTORER) {
		if (__put_user(ka->sa.sa_restorer, &frame->pretcode))
			goto give_sigsegv;
	} else {
		if (__put_user((void (*)(void))frame->retcode,
			       &frame->pretcode))
			goto give_sigsegv;
		/* this is mov $,d0; syscall 0 */
		if (__put_user(0x2c, (char *)(frame->retcode + 0)) ||
		    __put_user(__NR_sigreturn, (char *)(frame->retcode + 1)) ||
		    __put_user(0x00, (char *)(frame->retcode + 2)) ||
		    __put_user(0xf0, (char *)(frame->retcode + 3)) ||
		    __put_user(0xe0, (char *)(frame->retcode + 4)))
			goto give_sigsegv;
		flush_icache_range((unsigned long) frame->retcode,
				   (unsigned long) frame->retcode + 5);
	}

	/* set up registers for signal handler */
	regs->sp = (unsigned long) frame;
	regs->pc = (unsigned long) ka->sa.sa_handler;
	regs->d0 = sig;
	regs->d1 = (unsigned long) &frame->sc;

	set_fs(USER_DS);

	/* the tracer may want to single-step inside the handler */
	if (test_thread_flag(TIF_SINGLESTEP))
		ptrace_notify(SIGTRAP);

#if DEBUG_SIG
	printk(KERN_DEBUG "SIG deliver %d (%s:%d): sp=%p pc=%lx ra=%p\n",
	       sig, current->comm, current->pid, frame, regs->pc,
	       frame->pretcode);
#endif

	return 0;

give_sigsegv:
	force_sig(SIGSEGV, current);
	return -EFAULT;
}

/*
 * set up a realtime signal frame
 */
static int setup_rt_frame(int sig, struct k_sigaction *ka, siginfo_t *info,
			  sigset_t *set, struct pt_regs *regs)
{
	struct rt_sigframe __user *frame;
	int rsig;

	frame = get_sigframe(ka, regs, sizeof(*frame));

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto give_sigsegv;

	rsig = sig;
	if (sig < 32 &&
	    current_thread_info()->exec_domain &&
	    current_thread_info()->exec_domain->signal_invmap)
		rsig = current_thread_info()->exec_domain->signal_invmap[sig];

	if (__put_user(rsig, &frame->sig) ||
	    __put_user(&frame->info, &frame->pinfo) ||
	    __put_user(&frame->uc, &frame->puc) ||
	    copy_siginfo_to_user(&frame->info, info))
		goto give_sigsegv;

	/* create the ucontext.  */
	if (__put_user(0, &frame->uc.uc_flags) ||
	    __put_user(0, &frame->uc.uc_link) ||
	    __put_user((void *)current->sas_ss_sp, &frame->uc.uc_stack.ss_sp) ||
	    __put_user(sas_ss_flags(regs->sp), &frame->uc.uc_stack.ss_flags) ||
	    __put_user(current->sas_ss_size, &frame->uc.uc_stack.ss_size) ||
	    setup_sigcontext(&frame->uc.uc_mcontext,
			     &frame->fpuctx, regs, set->sig[0]) ||
	    __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set)))
		goto give_sigsegv;

	/* set up to return from userspace.  If provided, use a stub already in
	 * userspace */
	if (ka->sa.sa_flags & SA_RESTORER) {
		if (__put_user(ka->sa.sa_restorer, &frame->pretcode))
			goto give_sigsegv;
	} else {
		if (__put_user((void(*)(void))frame->retcode,
			       &frame->pretcode) ||
		    /* This is mov $,d0; syscall 0 */
		    __put_user(0x2c, (char *)(frame->retcode + 0)) ||
		    __put_user(__NR_rt_sigreturn,
			       (char *)(frame->retcode + 1)) ||
		    __put_user(0x00, (char *)(frame->retcode + 2)) ||
		    __put_user(0xf0, (char *)(frame->retcode + 3)) ||
		    __put_user(0xe0, (char *)(frame->retcode + 4)))
			goto give_sigsegv;

		flush_icache_range((u_long) frame->retcode,
				   (u_long) frame->retcode + 5);
	}

	/* Set up registers for signal handler */
	regs->sp = (unsigned long) frame;
	regs->pc = (unsigned long) ka->sa.sa_handler;
	regs->d0 = sig;
	regs->d1 = (long) &frame->info;

	set_fs(USER_DS);

	/* the tracer may want to single-step inside the handler */
	if (test_thread_flag(TIF_SINGLESTEP))
		ptrace_notify(SIGTRAP);

#if DEBUG_SIG
	printk(KERN_DEBUG "SIG deliver %d (%s:%d): sp=%p pc=%lx ra=%p\n",
	       sig, current->comm, current->pid, frame, regs->pc,
	       frame->pretcode);
#endif

	return 0;

give_sigsegv:
	force_sig(SIGSEGV, current);
	return -EFAULT;
}

/*
 * handle the actual delivery of a signal to userspace
 */
static int handle_signal(int sig,
			 siginfo_t *info, struct k_sigaction *ka,
			 sigset_t *oldset, struct pt_regs *regs)
{
	int ret;

	/* Are we from a system call? */
	if (regs->orig_d0 >= 0) {
		/* If so, check system call restarting.. */
		switch (regs->d0) {
		case -ERESTART_RESTARTBLOCK:
		case -ERESTARTNOHAND:
			regs->d0 = -EINTR;
			break;

		case -ERESTARTSYS:
			if (!(ka->sa.sa_flags & SA_RESTART)) {
				regs->d0 = -EINTR;
				break;
			}

			/* fallthrough */
		case -ERESTARTNOINTR:
			regs->d0 = regs->orig_d0;
			regs->pc -= 2;
		}
	}

	/* Set up the stack frame */
	if (ka->sa.sa_flags & SA_SIGINFO)
		ret = setup_rt_frame(sig, ka, info, oldset, regs);
	else
		ret = setup_frame(sig, ka, oldset, regs);

	if (ret == 0) {
		spin_lock_irq(&current->sighand->siglock);
		sigorsets(&current->blocked, &current->blocked,
			  &ka->sa.sa_mask);
		if (!(ka->sa.sa_flags & SA_NODEFER))
			sigaddset(&current->blocked, sig);
		recalc_sigpending();
		spin_unlock_irq(&current->sighand->siglock);
	}

	return ret;
}

/*
 * handle a potential signal
 */
static void do_signal(struct pt_regs *regs)
{
	struct k_sigaction ka;
	siginfo_t info;
	sigset_t *oldset;
	int signr;

	/* we want the common case to go fast, which is why we may in certain
	 * cases get here from kernel mode */
	if (!user_mode(regs))
		return;

	if (test_thread_flag(TIF_RESTORE_SIGMASK))
		oldset = &current->saved_sigmask;
	else
		oldset = &current->blocked;

	signr = get_signal_to_deliver(&info, &ka, regs, NULL);
	if (signr > 0) {
		if (handle_signal(signr, &info, &ka, oldset, regs) == 0) {
			/* a signal was successfully delivered; the saved
			 * sigmask will have been stored in the signal frame,
			 * and will be restored by sigreturn, so we can simply
			 * clear the TIF_RESTORE_SIGMASK flag */
			if (test_thread_flag(TIF_RESTORE_SIGMASK))
				clear_thread_flag(TIF_RESTORE_SIGMASK);
		}

		return;
	}

	/* did we come from a system call? */
	if (regs->orig_d0 >= 0) {
		/* restart the system call - no handlers present */
		switch (regs->d0) {
		case -ERESTARTNOHAND:
		case -ERESTARTSYS:
		case -ERESTARTNOINTR:
			regs->d0 = regs->orig_d0;
			regs->pc -= 2;
			break;

		case -ERESTART_RESTARTBLOCK:
			regs->d0 = __NR_restart_syscall;
			regs->pc -= 2;
			break;
		}
	}

	/* if there's no signal to deliver, we just put the saved sigmask
	 * back */
	if (test_thread_flag(TIF_RESTORE_SIGMASK)) {
		clear_thread_flag(TIF_RESTORE_SIGMASK);
		sigprocmask(SIG_SETMASK, &current->saved_sigmask, NULL);
	}
}

/*
 * notification of userspace execution resumption
 * - triggered by current->work.notify_resume
 */
asmlinkage void do_notify_resume(struct pt_regs *regs, u32 thread_info_flags)
{
	/* Pending single-step? */
	if (thread_info_flags & _TIF_SINGLESTEP) {
#ifndef CONFIG_MN10300_USING_JTAG
		regs->epsw |= EPSW_T;
		clear_thread_flag(TIF_SINGLESTEP);
#else
		BUG(); /* no h/w single-step if using JTAG unit */
#endif
	}

	/* deal with pending signal delivery */
	if (thread_info_flags & (_TIF_SIGPENDING | _TIF_RESTORE_SIGMASK))
		do_signal(regs);
}
