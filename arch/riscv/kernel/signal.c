// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2009 Sunplus Core Technology Co., Ltd.
 *  Chen Liqin <liqin.chen@sunplusct.com>
 *  Lennox Wu <lennox.wu@sunplusct.com>
 * Copyright (C) 2012 Regents of the University of California
 */

#include <linux/signal.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <linux/tracehook.h>
#include <linux/linkage.h>

#include <asm/ucontext.h>
#include <asm/vdso.h>
#include <asm/switch_to.h>
#include <asm/csr.h>

#define DEBUG_SIG 0

struct rt_sigframe {
	struct siginfo info;
	struct ucontext uc;
};

#ifdef CONFIG_FPU
static long restore_fp_state(struct pt_regs *regs,
			     union __riscv_fp_state *sc_fpregs)
{
	long err;
	struct __riscv_d_ext_state __user *state = &sc_fpregs->d;
	size_t i;

	err = __copy_from_user(&current->thread.fstate, state, sizeof(*state));
	if (unlikely(err))
		return err;

	fstate_restore(current, regs);

	/* We support no other extension state at this time. */
	for (i = 0; i < ARRAY_SIZE(sc_fpregs->q.reserved); i++) {
		u32 value;

		err = __get_user(value, &sc_fpregs->q.reserved[i]);
		if (unlikely(err))
			break;
		if (value != 0)
			return -EINVAL;
	}

	return err;
}

static long save_fp_state(struct pt_regs *regs,
			  union __riscv_fp_state *sc_fpregs)
{
	long err;
	struct __riscv_d_ext_state __user *state = &sc_fpregs->d;
	size_t i;

	fstate_save(current, regs);
	err = __copy_to_user(state, &current->thread.fstate, sizeof(*state));
	if (unlikely(err))
		return err;

	/* We support no other extension state at this time. */
	for (i = 0; i < ARRAY_SIZE(sc_fpregs->q.reserved); i++) {
		err = __put_user(0, &sc_fpregs->q.reserved[i]);
		if (unlikely(err))
			break;
	}

	return err;
}
#else
#define save_fp_state(task, regs) (0)
#define restore_fp_state(task, regs) (0)
#endif

static long restore_sigcontext(struct pt_regs *regs,
	struct sigcontext __user *sc)
{
	long err;
	/* sc_regs is structured the same as the start of pt_regs */
	err = __copy_from_user(regs, &sc->sc_regs, sizeof(sc->sc_regs));
	/* Restore the floating-point state. */
	if (has_fpu)
		err |= restore_fp_state(regs, &sc->sc_fpregs);
	return err;
}

SYSCALL_DEFINE0(rt_sigreturn)
{
	struct pt_regs *regs = current_pt_regs();
	struct rt_sigframe __user *frame;
	struct task_struct *task;
	sigset_t set;

	/* Always make any pending restarted system calls return -EINTR */
	current->restart_block.fn = do_no_restart_syscall;

	frame = (struct rt_sigframe __user *)regs->sp;

	if (!access_ok(frame, sizeof(*frame)))
		goto badframe;

	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	set_current_blocked(&set);

	if (restore_sigcontext(regs, &frame->uc.uc_mcontext))
		goto badframe;

	if (restore_altstack(&frame->uc.uc_stack))
		goto badframe;

	return regs->a0;

badframe:
	task = current;
	if (show_unhandled_signals) {
		pr_info_ratelimited(
			"%s[%d]: bad frame in %s: frame=%p pc=%p sp=%p\n",
			task->comm, task_pid_nr(task), __func__,
			frame, (void *)regs->sepc, (void *)regs->sp);
	}
	force_sig(SIGSEGV);
	return 0;
}

static long setup_sigcontext(struct rt_sigframe __user *frame,
	struct pt_regs *regs)
{
	struct sigcontext __user *sc = &frame->uc.uc_mcontext;
	long err;
	/* sc_regs is structured the same as the start of pt_regs */
	err = __copy_to_user(&sc->sc_regs, regs, sizeof(sc->sc_regs));
	/* Save the floating-point state. */
	if (has_fpu)
		err |= save_fp_state(regs, &sc->sc_fpregs);
	return err;
}

static inline void __user *get_sigframe(struct ksignal *ksig,
	struct pt_regs *regs, size_t framesize)
{
	unsigned long sp;
	/* Default to using normal stack */
	sp = regs->sp;

	/*
	 * If we are on the alternate signal stack and would overflow it, don't.
	 * Return an always-bogus address instead so we will die with SIGSEGV.
	 */
	if (on_sig_stack(sp) && !likely(on_sig_stack(sp - framesize)))
		return (void __user __force *)(-1UL);

	/* This is the X/Open sanctioned signal stack switching. */
	sp = sigsp(sp, ksig) - framesize;

	/* Align the stack frame. */
	sp &= ~0xfUL;

	return (void __user *)sp;
}


static int setup_rt_frame(struct ksignal *ksig, sigset_t *set,
	struct pt_regs *regs)
{
	struct rt_sigframe __user *frame;
	long err = 0;

	frame = get_sigframe(ksig, regs, sizeof(*frame));
	if (!access_ok(frame, sizeof(*frame)))
		return -EFAULT;

	err |= copy_siginfo_to_user(&frame->info, &ksig->info);

	/* Create the ucontext. */
	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(NULL, &frame->uc.uc_link);
	err |= __save_altstack(&frame->uc.uc_stack, regs->sp);
	err |= setup_sigcontext(frame, regs);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));
	if (err)
		return -EFAULT;

	/* Set up to return from userspace. */
	regs->ra = (unsigned long)VDSO_SYMBOL(
		current->mm->context.vdso, rt_sigreturn);

	/*
	 * Set up registers for signal handler.
	 * Registers that we don't modify keep the value they had from
	 * user-space at the time we took the signal.
	 * We always pass siginfo and mcontext, regardless of SA_SIGINFO,
	 * since some things rely on this (e.g. glibc's debug/segfault.c).
	 */
	regs->sepc = (unsigned long)ksig->ka.sa.sa_handler;
	regs->sp = (unsigned long)frame;
	regs->a0 = ksig->sig;                     /* a0: signal number */
	regs->a1 = (unsigned long)(&frame->info); /* a1: siginfo pointer */
	regs->a2 = (unsigned long)(&frame->uc);   /* a2: ucontext pointer */

#if DEBUG_SIG
	pr_info("SIG deliver (%s:%d): sig=%d pc=%p ra=%p sp=%p\n",
		current->comm, task_pid_nr(current), ksig->sig,
		(void *)regs->sepc, (void *)regs->ra, frame);
#endif

	return 0;
}

static void handle_signal(struct ksignal *ksig, struct pt_regs *regs)
{
	sigset_t *oldset = sigmask_to_save();
	int ret;

	/* Are we from a system call? */
	if (regs->scause == EXC_SYSCALL) {
		/* Avoid additional syscall restarting via ret_from_exception */
		regs->scause = -1UL;

		/* If so, check system call restarting.. */
		switch (regs->a0) {
		case -ERESTART_RESTARTBLOCK:
		case -ERESTARTNOHAND:
			regs->a0 = -EINTR;
			break;

		case -ERESTARTSYS:
			if (!(ksig->ka.sa.sa_flags & SA_RESTART)) {
				regs->a0 = -EINTR;
				break;
			}
			/* fallthrough */
		case -ERESTARTNOINTR:
                        regs->a0 = regs->orig_a0;
			regs->sepc -= 0x4;
			break;
		}
	}

	/* Set up the stack frame */
	ret = setup_rt_frame(ksig, oldset, regs);

	signal_setup_done(ret, ksig, 0);
}

static void do_signal(struct pt_regs *regs)
{
	struct ksignal ksig;

	if (get_signal(&ksig)) {
		/* Actually deliver the signal */
		handle_signal(&ksig, regs);
		return;
	}

	/* Did we come from a system call? */
	if (regs->scause == EXC_SYSCALL) {
		/* Avoid additional syscall restarting via ret_from_exception */
		regs->scause = -1UL;

		/* Restart the system call - no handlers present */
		switch (regs->a0) {
		case -ERESTARTNOHAND:
		case -ERESTARTSYS:
		case -ERESTARTNOINTR:
                        regs->a0 = regs->orig_a0;
			regs->sepc -= 0x4;
			break;
		case -ERESTART_RESTARTBLOCK:
                        regs->a0 = regs->orig_a0;
			regs->a7 = __NR_restart_syscall;
			regs->sepc -= 0x4;
			break;
		}
	}

	/*
	 * If there is no signal to deliver, we just put the saved
	 * sigmask back.
	 */
	restore_saved_sigmask();
}

/*
 * notification of userspace execution resumption
 * - triggered by the _TIF_WORK_MASK flags
 */
asmlinkage void do_notify_resume(struct pt_regs *regs,
	unsigned long thread_info_flags)
{
	/* Handle pending signal delivery */
	if (thread_info_flags & _TIF_SIGPENDING)
		do_signal(regs);

	if (thread_info_flags & _TIF_NOTIFY_RESUME) {
		clear_thread_flag(TIF_NOTIFY_RESUME);
		tracehook_notify_resume(regs);
	}
}
