// SPDX-License-Identifier: GPL-2.0

#include <linux/signal.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <linux/tracehook.h>

#include <asm/traps.h>
#include <asm/ucontext.h>
#include <asm/vdso.h>

#include <abi/regdef.h>

#ifdef CONFIG_CPU_HAS_FPU
#include <abi/fpu.h>
static int restore_fpu_state(struct sigcontext __user *sc)
{
	int err = 0;
	struct user_fp user_fp;

	err = __copy_from_user(&user_fp, &sc->sc_user_fp, sizeof(user_fp));

	restore_from_user_fp(&user_fp);

	return err;
}

static int save_fpu_state(struct sigcontext __user *sc)
{
	struct user_fp user_fp;

	save_to_user_fp(&user_fp);

	return __copy_to_user(&sc->sc_user_fp, &user_fp, sizeof(user_fp));
}
#else
#define restore_fpu_state(sigcontext)	(0)
#define save_fpu_state(sigcontext)	(0)
#endif

struct rt_sigframe {
	/*
	 * pad[3] is compatible with the same struct defined in
	 * gcc/libgcc/config/csky/linux-unwind.h
	 */
	int pad[3];
	struct siginfo info;
	struct ucontext uc;
};

static long restore_sigcontext(struct pt_regs *regs,
	struct sigcontext __user *sc)
{
	int err = 0;
	unsigned long sr = regs->sr;

	/* sc_pt_regs is structured the same as the start of pt_regs */
	err |= __copy_from_user(regs, &sc->sc_pt_regs, sizeof(struct pt_regs));

	/* BIT(0) of regs->sr is Condition Code/Carry bit */
	regs->sr = (sr & ~1) | (regs->sr & 1);

	/* Restore the floating-point state. */
	err |= restore_fpu_state(sc);

	return err;
}

SYSCALL_DEFINE0(rt_sigreturn)
{
	struct pt_regs *regs = current_pt_regs();
	struct rt_sigframe __user *frame;
	sigset_t set;

	/* Always make any pending restarted system calls return -EINTR */
	current->restart_block.fn = do_no_restart_syscall;

	frame = (struct rt_sigframe __user *)regs->usp;

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
	force_sig(SIGSEGV);
	return 0;
}

static int setup_sigcontext(struct rt_sigframe __user *frame,
	struct pt_regs *regs)
{
	struct sigcontext __user *sc = &frame->uc.uc_mcontext;
	int err = 0;

	err |= __copy_to_user(&sc->sc_pt_regs, regs, sizeof(struct pt_regs));
	err |= save_fpu_state(sc);

	return err;
}

static inline void __user *get_sigframe(struct ksignal *ksig,
	struct pt_regs *regs, size_t framesize)
{
	unsigned long sp;
	/* Default to using normal stack */
	sp = regs->usp;

	/*
	 * If we are on the alternate signal stack and would overflow it, don't.
	 * Return an always-bogus address instead so we will die with SIGSEGV.
	 */
	if (on_sig_stack(sp) && !likely(on_sig_stack(sp - framesize)))
		return (void __user __force *)(-1UL);

	/* This is the X/Open sanctioned signal stack switching. */
	sp = sigsp(sp, ksig) - framesize;

	/* Align the stack frame. */
	sp &= -8UL;

	return (void __user *)sp;
}

static int
setup_rt_frame(struct ksignal *ksig, sigset_t *set, struct pt_regs *regs)
{
	struct rt_sigframe __user *frame;
	int err = 0;

	frame = get_sigframe(ksig, regs, sizeof(*frame));
	if (!access_ok(frame, sizeof(*frame)))
		return -EFAULT;

	err |= copy_siginfo_to_user(&frame->info, &ksig->info);

	/* Create the ucontext. */
	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(NULL, &frame->uc.uc_link);
	err |= __save_altstack(&frame->uc.uc_stack, regs->usp);
	err |= setup_sigcontext(frame, regs);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));
	if (err)
		return -EFAULT;

	/* Set up to return from userspace. */
	regs->lr = (unsigned long)VDSO_SYMBOL(
		current->mm->context.vdso, rt_sigreturn);

	/*
	 * Set up registers for signal handler.
	 * Registers that we don't modify keep the value they had from
	 * user-space at the time we took the signal.
	 * We always pass siginfo and mcontext, regardless of SA_SIGINFO,
	 * since some things rely on this (e.g. glibc's debug/segfault.c).
	 */
	regs->pc  = (unsigned long)ksig->ka.sa.sa_handler;
	regs->usp = (unsigned long)frame;
	regs->a0  = ksig->sig;				/* a0: signal number */
	regs->a1  = (unsigned long)(&(frame->info));	/* a1: siginfo pointer */
	regs->a2  = (unsigned long)(&(frame->uc));	/* a2: ucontext pointer */

	return 0;
}

static void handle_signal(struct ksignal *ksig, struct pt_regs *regs)
{
	sigset_t *oldset = sigmask_to_save();
	int ret;

	rseq_signal_deliver(ksig, regs);

	/* Are we from a system call? */
	if (in_syscall(regs)) {
		/* Avoid additional syscall restarting via ret_from_exception */
		forget_syscall(regs);

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
			fallthrough;
		case -ERESTARTNOINTR:
			regs->a0 = regs->orig_a0;
			regs->pc -= TRAP0_SIZE;
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
	if (in_syscall(regs)) {
		/* Avoid additional syscall restarting via ret_from_exception */
		forget_syscall(regs);

		/* Restart the system call - no handlers present */
		switch (regs->a0) {
		case -ERESTARTNOHAND:
		case -ERESTARTSYS:
		case -ERESTARTNOINTR:
			regs->a0 = regs->orig_a0;
			regs->pc -= TRAP0_SIZE;
			break;
		case -ERESTART_RESTARTBLOCK:
			regs->a0 = regs->orig_a0;
			regs_syscallid(regs) = __NR_restart_syscall;
			regs->pc -= TRAP0_SIZE;
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
	if (thread_info_flags & _TIF_UPROBE)
		uprobe_notify_resume(regs);

	/* Handle pending signal delivery */
	if (thread_info_flags & (_TIF_SIGPENDING | _TIF_NOTIFY_SIGNAL))
		do_signal(regs);

	if (thread_info_flags & _TIF_NOTIFY_RESUME)
		tracehook_notify_resume(regs);
}
