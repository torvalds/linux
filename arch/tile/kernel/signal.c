/*
 * Copyright (C) 1991, 1992  Linus Torvalds
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/unistd.h>
#include <linux/stddef.h>
#include <linux/personality.h>
#include <linux/suspend.h>
#include <linux/ptrace.h>
#include <linux/elf.h>
#include <linux/compat.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <asm/processor.h>
#include <asm/ucontext.h>
#include <asm/sigframe.h>
#include <asm/syscalls.h>
#include <arch/interrupts.h>

#define DEBUG_SIG 0

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))


long _sys_sigaltstack(const stack_t __user *uss,
		      stack_t __user *uoss, struct pt_regs *regs)
{
	return do_sigaltstack(uss, uoss, regs->sp);
}


/*
 * Do a signal return; undo the signal stack.
 */

int restore_sigcontext(struct pt_regs *regs,
		       struct sigcontext __user *sc, long *pr0)
{
	int err = 0;
	int i;

	/* Always make any pending restarted system calls return -EINTR */
	current_thread_info()->restart_block.fn = do_no_restart_syscall;

	/*
	 * Enforce that sigcontext is like pt_regs, and doesn't mess
	 * up our stack alignment rules.
	 */
	BUILD_BUG_ON(sizeof(struct sigcontext) != sizeof(struct pt_regs));
	BUILD_BUG_ON(sizeof(struct sigcontext) % 8 != 0);

	for (i = 0; i < sizeof(struct pt_regs)/sizeof(long); ++i)
		err |= __get_user(regs->regs[i], &sc->gregs[i]);

	regs->faultnum = INT_SWINT_1_SIGRETURN;

	err |= __get_user(*pr0, &sc->gregs[0]);
	return err;
}

/* sigreturn() returns long since it restores r0 in the interrupted code. */
long _sys_rt_sigreturn(struct pt_regs *regs)
{
	struct rt_sigframe __user *frame =
		(struct rt_sigframe __user *)(regs->sp);
	sigset_t set;
	long r0;

	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	if (restore_sigcontext(regs, &frame->uc.uc_mcontext, &r0))
		goto badframe;

	if (do_sigaltstack(&frame->uc.uc_stack, NULL, regs->sp) == -EFAULT)
		goto badframe;

	return r0;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

/*
 * Set up a signal frame.
 */

int setup_sigcontext(struct sigcontext __user *sc, struct pt_regs *regs)
{
	int i, err = 0;

	for (i = 0; i < sizeof(struct pt_regs)/sizeof(long); ++i)
		err |= __put_user(regs->regs[i], &sc->gregs[i]);

	return err;
}

/*
 * Determine which stack to use..
 */
static inline void __user *get_sigframe(struct k_sigaction *ka,
					struct pt_regs *regs,
					size_t frame_size)
{
	unsigned long sp;

	/* Default to using normal stack */
	sp = regs->sp;

	/*
	 * If we are on the alternate signal stack and would overflow
	 * it, don't.  Return an always-bogus address instead so we
	 * will die with SIGSEGV.
	 */
	if (on_sig_stack(sp) && !likely(on_sig_stack(sp - frame_size)))
		return (void __user __force *)-1UL;

	/* This is the X/Open sanctioned signal stack switching.  */
	if (ka->sa.sa_flags & SA_ONSTACK) {
		if (sas_ss_flags(sp) == 0)
			sp = current->sas_ss_sp + current->sas_ss_size;
	}

	sp -= frame_size;
	/*
	 * Align the stack pointer according to the TILE ABI,
	 * i.e. so that on function entry (sp & 15) == 0.
	 */
	sp &= -16UL;
	return (void __user *) sp;
}

static int setup_rt_frame(int sig, struct k_sigaction *ka, siginfo_t *info,
			   sigset_t *set, struct pt_regs *regs)
{
	unsigned long restorer;
	struct rt_sigframe __user *frame;
	int err = 0;
	int usig;

	frame = get_sigframe(ka, regs, sizeof(*frame));

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto give_sigsegv;

	usig = current_thread_info()->exec_domain
		&& current_thread_info()->exec_domain->signal_invmap
		&& sig < 32
		? current_thread_info()->exec_domain->signal_invmap[sig]
		: sig;

	/* Always write at least the signal number for the stack backtracer. */
	if (ka->sa.sa_flags & SA_SIGINFO) {
		/* At sigreturn time, restore the callee-save registers too. */
		err |= copy_siginfo_to_user(&frame->info, info);
		regs->flags |= PT_FLAGS_RESTORE_REGS;
	} else {
		err |= __put_user(info->si_signo, &frame->info.si_signo);
	}

	/* Create the ucontext.  */
	err |= __clear_user(&frame->save_area, sizeof(frame->save_area));
	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(NULL, &frame->uc.uc_link);
	err |= __put_user((void __user *)(current->sas_ss_sp),
			  &frame->uc.uc_stack.ss_sp);
	err |= __put_user(sas_ss_flags(regs->sp),
			  &frame->uc.uc_stack.ss_flags);
	err |= __put_user(current->sas_ss_size, &frame->uc.uc_stack.ss_size);
	err |= setup_sigcontext(&frame->uc.uc_mcontext, regs);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));
	if (err)
		goto give_sigsegv;

	restorer = VDSO_BASE;
	if (ka->sa.sa_flags & SA_RESTORER)
		restorer = (unsigned long) ka->sa.sa_restorer;

	/*
	 * Set up registers for signal handler.
	 * Registers that we don't modify keep the value they had from
	 * user-space at the time we took the signal.
	 * We always pass siginfo and mcontext, regardless of SA_SIGINFO,
	 * since some things rely on this (e.g. glibc's debug/segfault.c).
	 */
	regs->pc = (unsigned long) ka->sa.sa_handler;
	regs->ex1 = PL_ICS_EX1(USER_PL, 1); /* set crit sec in handler */
	regs->sp = (unsigned long) frame;
	regs->lr = restorer;
	regs->regs[0] = (unsigned long) usig;
	regs->regs[1] = (unsigned long) &frame->info;
	regs->regs[2] = (unsigned long) &frame->uc;
	regs->flags |= PT_FLAGS_CALLER_SAVES;

	/*
	 * Notify any tracer that was single-stepping it.
	 * The tracer may want to single-step inside the
	 * handler too.
	 */
	if (test_thread_flag(TIF_SINGLESTEP))
		ptrace_notify(SIGTRAP);

	return 0;

give_sigsegv:
	force_sigsegv(sig, current);
	return -EFAULT;
}

/*
 * OK, we're invoking a handler
 */

static int handle_signal(unsigned long sig, siginfo_t *info,
			 struct k_sigaction *ka, sigset_t *oldset,
			 struct pt_regs *regs)
{
	int ret;


	/* Are we from a system call? */
	if (regs->faultnum == INT_SWINT_1) {
		/* If so, check system call restarting.. */
		switch (regs->regs[0]) {
		case -ERESTART_RESTARTBLOCK:
		case -ERESTARTNOHAND:
			regs->regs[0] = -EINTR;
			break;

		case -ERESTARTSYS:
			if (!(ka->sa.sa_flags & SA_RESTART)) {
				regs->regs[0] = -EINTR;
				break;
			}
			/* fallthrough */
		case -ERESTARTNOINTR:
			/* Reload caller-saves to restore r0..r5 and r10. */
			regs->flags |= PT_FLAGS_CALLER_SAVES;
			regs->regs[0] = regs->orig_r0;
			regs->pc -= 8;
		}
	}

	/* Set up the stack frame */
#ifdef CONFIG_COMPAT
	if (is_compat_task())
		ret = compat_setup_rt_frame(sig, ka, info, oldset, regs);
	else
#endif
		ret = setup_rt_frame(sig, ka, info, oldset, regs);
	if (ret == 0) {
		/* This code is only called from system calls or from
		 * the work_pending path in the return-to-user code, and
		 * either way we can re-enable interrupts unconditionally.
		 */
		spin_lock_irq(&current->sighand->siglock);
		sigorsets(&current->blocked,
			  &current->blocked, &ka->sa.sa_mask);
		if (!(ka->sa.sa_flags & SA_NODEFER))
			sigaddset(&current->blocked, sig);
		recalc_sigpending();
		spin_unlock_irq(&current->sighand->siglock);
	}

	return ret;
}

/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 */
void do_signal(struct pt_regs *regs)
{
	siginfo_t info;
	int signr;
	struct k_sigaction ka;
	sigset_t *oldset;

	/*
	 * i386 will check if we're coming from kernel mode and bail out
	 * here.  In my experience this just turns weird crashes into
	 * weird spin-hangs.  But if we find a case where this seems
	 * helpful, we can reinstate the check on "!user_mode(regs)".
	 */

	if (current_thread_info()->status & TS_RESTORE_SIGMASK)
		oldset = &current->saved_sigmask;
	else
		oldset = &current->blocked;

	signr = get_signal_to_deliver(&info, &ka, regs, NULL);
	if (signr > 0) {
		/* Whee! Actually deliver the signal.  */
		if (handle_signal(signr, &info, &ka, oldset, regs) == 0) {
			/*
			 * A signal was successfully delivered; the saved
			 * sigmask will have been stored in the signal frame,
			 * and will be restored by sigreturn, so we can simply
			 * clear the TS_RESTORE_SIGMASK flag.
			 */
			current_thread_info()->status &= ~TS_RESTORE_SIGMASK;
		}

		return;
	}

	/* Did we come from a system call? */
	if (regs->faultnum == INT_SWINT_1) {
		/* Restart the system call - no handlers present */
		switch (regs->regs[0]) {
		case -ERESTARTNOHAND:
		case -ERESTARTSYS:
		case -ERESTARTNOINTR:
			regs->flags |= PT_FLAGS_CALLER_SAVES;
			regs->regs[0] = regs->orig_r0;
			regs->pc -= 8;
			break;

		case -ERESTART_RESTARTBLOCK:
			regs->flags |= PT_FLAGS_CALLER_SAVES;
			regs->regs[TREG_SYSCALL_NR] = __NR_restart_syscall;
			regs->pc -= 8;
			break;
		}
	}

	/* If there's no signal to deliver, just put the saved sigmask back. */
	if (current_thread_info()->status & TS_RESTORE_SIGMASK) {
		current_thread_info()->status &= ~TS_RESTORE_SIGMASK;
		sigprocmask(SIG_SETMASK, &current->saved_sigmask, NULL);
	}
}
