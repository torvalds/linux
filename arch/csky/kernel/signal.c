// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/syscalls.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>
#include <linux/stddef.h>
#include <linux/highuid.h>
#include <linux/personality.h>
#include <linux/tty.h>
#include <linux/binfmts.h>
#include <linux/tracehook.h>
#include <linux/freezer.h>
#include <linux/uaccess.h>

#include <asm/setup.h>
#include <asm/pgtable.h>
#include <asm/traps.h>
#include <asm/ucontext.h>
#include <asm/vdso.h>

#include <abi/regdef.h>

#ifdef CONFIG_CPU_HAS_FPU
#include <abi/fpu.h>

static int restore_fpu_state(struct sigcontext *sc)
{
	int err = 0;
	struct user_fp user_fp;

	err = copy_from_user(&user_fp, &sc->sc_user_fp, sizeof(user_fp));

	restore_from_user_fp(&user_fp);

	return err;
}

static int save_fpu_state(struct sigcontext *sc)
{
	struct user_fp user_fp;

	save_to_user_fp(&user_fp);

	return copy_to_user(&sc->sc_user_fp, &user_fp, sizeof(user_fp));
}
#else
static inline int restore_fpu_state(struct sigcontext *sc) { return 0; }
static inline int save_fpu_state(struct sigcontext *sc) { return 0; }
#endif

struct rt_sigframe {
	int sig;
	struct siginfo *pinfo;
	void *puc;
	struct siginfo info;
	struct ucontext uc;
};

static int
restore_sigframe(struct pt_regs *regs,
		 struct sigcontext *sc, int *pr2)
{
	int err = 0;

	/* Always make any pending restarted system calls return -EINTR */
	current_thread_info()->task->restart_block.fn = do_no_restart_syscall;

	err |= copy_from_user(regs, &sc->sc_pt_regs, sizeof(struct pt_regs));

	err |= restore_fpu_state(sc);

	*pr2 = regs->a0;
	return err;
}

asmlinkage int
do_rt_sigreturn(void)
{
	sigset_t set;
	int a0;
	struct pt_regs *regs = current_pt_regs();
	struct rt_sigframe *frame = (struct rt_sigframe *)(regs->usp);

	if (verify_area(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	sigdelsetmask(&set, (sigmask(SIGKILL) | sigmask(SIGSTOP)));
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	if (restore_sigframe(regs, &frame->uc.uc_mcontext, &a0))
		goto badframe;

	return a0;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

static int setup_sigframe(struct sigcontext *sc, struct pt_regs *regs)
{
	int err = 0;

	err |= copy_to_user(&sc->sc_pt_regs, regs, sizeof(struct pt_regs));
	err |= save_fpu_state(sc);

	return err;
}

static inline void *
get_sigframe(struct k_sigaction *ka, struct pt_regs *regs, size_t frame_size)
{
	unsigned long usp;

	/* Default to using normal stack.  */
	usp = regs->usp;

	/* This is the X/Open sanctioned signal stack switching.  */
	if ((ka->sa.sa_flags & SA_ONSTACK) && !sas_ss_flags(usp)) {
		if (!on_sig_stack(usp))
			usp = current->sas_ss_sp + current->sas_ss_size;
	}
	return (void *)((usp - frame_size) & -8UL);
}

static int
setup_rt_frame(struct ksignal *ksig, sigset_t *set, struct pt_regs *regs)
{
	struct rt_sigframe *frame;
	int err = 0;

	struct csky_vdso *vdso = current->mm->context.vdso;

	frame = get_sigframe(&ksig->ka, regs, sizeof(*frame));
	if (!frame)
		return 1;

	err |= __put_user(ksig->sig, &frame->sig);
	err |= __put_user(&frame->info, &frame->pinfo);
	err |= __put_user(&frame->uc, &frame->puc);
	err |= copy_siginfo_to_user(&frame->info, &ksig->info);

	/* Create the ucontext.  */
	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(0, &frame->uc.uc_link);
	err |= __put_user((void *)current->sas_ss_sp,
			&frame->uc.uc_stack.ss_sp);
	err |= __put_user(sas_ss_flags(regs->usp),
			&frame->uc.uc_stack.ss_flags);
	err |= __put_user(current->sas_ss_size, &frame->uc.uc_stack.ss_size);
	err |= setup_sigframe(&frame->uc.uc_mcontext, regs);
	err |= copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));

	if (err)
		goto give_sigsegv;

	/* Set up registers for signal handler */
	regs->usp = (unsigned long)frame;
	regs->pc = (unsigned long)ksig->ka.sa.sa_handler;
	regs->lr = (unsigned long)vdso->rt_signal_retcode;

adjust_stack:
	regs->a0 = ksig->sig; /* first arg is signo */
	regs->a1 = (unsigned long)(&(frame->info));
	regs->a2 = (unsigned long)(&(frame->uc));
	return err;

give_sigsegv:
	if (ksig->sig == SIGSEGV)
		ksig->ka.sa.sa_handler = SIG_DFL;
	force_sig(SIGSEGV, current);
	goto adjust_stack;
}

/*
 * OK, we're invoking a handler
 */
static int
handle_signal(struct ksignal *ksig, struct pt_regs *regs)
{
	int ret;
	sigset_t *oldset = sigmask_to_save();

	/*
	 * set up the stack frame, regardless of SA_SIGINFO,
	 * and pass info anyway.
	 */
	ret = setup_rt_frame(ksig, oldset, regs);

	if (ret != 0) {
		force_sigsegv(ksig->sig, current);
		return ret;
	}

	/* Block the signal if we were successful. */
	spin_lock_irq(&current->sighand->siglock);
	sigorsets(&current->blocked, &current->blocked, &ksig->ka.sa.sa_mask);
	if (!(ksig->ka.sa.sa_flags & SA_NODEFER))
		sigaddset(&current->blocked, ksig->sig);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	return 0;
}

/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 *
 * Note that we go through the signals twice: once to check the signals
 * that the kernel can handle, and then we build all the user-level signal
 * handling stack-frames in one go after that.
 */
static void do_signal(struct pt_regs *regs, int syscall)
{
	unsigned int retval = 0, continue_addr = 0, restart_addr = 0;
	struct ksignal ksig;

	/*
	 * We want the common case to go fast, which
	 * is why we may in certain cases get here from
	 * kernel mode. Just return without doing anything
	 * if so.
	 */
	if (!user_mode(regs))
		return;

	current->thread.esp0 = (unsigned long)regs;

	/*
	 * If we were from a system call, check for system call restarting...
	 */
	if (syscall) {
		continue_addr = regs->pc;
#if defined(__CSKYABIV2__)
		restart_addr = continue_addr - 4;
#else
		restart_addr = continue_addr - 2;
#endif
		retval = regs->a0;

		/*
		 * Prepare for system call restart.  We do this here so that a
		 * debugger will see the already changed.
		 */
		switch (retval) {
		case -ERESTARTNOHAND:
		case -ERESTARTSYS:
		case -ERESTARTNOINTR:
			regs->a0 = regs->orig_a0;
			regs->pc = restart_addr;
			break;
		case -ERESTART_RESTARTBLOCK:
			regs->a0 = -EINTR;
			break;
		}
	}

	if (try_to_freeze())
		goto no_signal;

	/*
	 * Get the signal to deliver.  When running under ptrace, at this
	 * point the debugger may change all our registers ...
	 */
	if (get_signal(&ksig)) {
		/*
		 * Depending on the signal settings we may need to revert the
		 * decision to restart the system call.  But skip this if a
		 * debugger has chosen to restart at a different PC.
		 */
		if (regs->pc == restart_addr) {
			if (retval == -ERESTARTNOHAND ||
			    (retval == -ERESTARTSYS &&
			     !(ksig.ka.sa.sa_flags & SA_RESTART))) {
				regs->a0 = -EINTR;
				regs->pc = continue_addr;
			}
		}

		/* Whee!  Actually deliver the signal.  */
		if (handle_signal(&ksig, regs) == 0) {
			/*
			 * A signal was successfully delivered; the saved
			 * sigmask will have been stored in the signal frame,
			 * and will be restored by sigreturn, so we can simply
			 * clear the TIF_RESTORE_SIGMASK flag.
			 */
			if (test_thread_flag(TIF_RESTORE_SIGMASK))
				clear_thread_flag(TIF_RESTORE_SIGMASK);
		}
		return;
	}

no_signal:
	if (syscall) {
		/*
		 * Handle restarting a different system call.  As above,
		 * if a debugger has chosen to restart at a different PC,
		 * ignore the restart.
		 */
		if (retval == -ERESTART_RESTARTBLOCK
				&& regs->pc == continue_addr) {
#if defined(__CSKYABIV2__)
			regs->regs[3] = __NR_restart_syscall;
			regs->pc -= 4;
#else
			regs->regs[9] = __NR_restart_syscall;
			regs->pc -= 2;
#endif
		}

		/*
		 * If there's no signal to deliver, we just put the saved
		 * sigmask back.
		 */
		if (test_thread_flag(TIF_RESTORE_SIGMASK)) {
			clear_thread_flag(TIF_RESTORE_SIGMASK);
			sigprocmask(SIG_SETMASK, &current->saved_sigmask, NULL);
		}
	}
}

asmlinkage void
do_notify_resume(unsigned int thread_flags, struct pt_regs *regs, int syscall)
{
	if (thread_flags & _TIF_SIGPENDING)
		do_signal(regs, syscall);

	if (thread_flags & _TIF_NOTIFY_RESUME) {
		clear_thread_flag(TIF_NOTIFY_RESUME);
		tracehook_notify_resume(regs);
	}
}
