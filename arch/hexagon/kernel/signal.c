// SPDX-License-Identifier: GPL-2.0-only
/*
 * Signal support for Hexagon processor
 *
 * Copyright (c) 2010-2012, The Linux Foundation. All rights reserved.
 */

#include <linux/linkage.h>
#include <linux/syscalls.h>
#include <linux/sched/task_stack.h>

#include <asm/registers.h>
#include <asm/thread_info.h>
#include <asm/unistd.h>
#include <linux/uaccess.h>
#include <asm/ucontext.h>
#include <asm/cacheflush.h>
#include <asm/signal.h>
#include <asm/vdso.h>

struct rt_sigframe {
	unsigned long tramp[2];
	struct siginfo info;
	struct ucontext uc;
};

static void __user *get_sigframe(struct ksignal *ksig, struct pt_regs *regs,
			  size_t frame_size)
{
	unsigned long sp = sigsp(regs->r29, ksig);

	return (void __user *)((sp - frame_size) & ~(sizeof(long long) - 1));
}

static int setup_sigcontext(struct pt_regs *regs, struct sigcontext __user *sc)
{
	unsigned long tmp;
	int err = 0;

	err |= copy_to_user(&sc->sc_regs.r0, &regs->r00,
			    32*sizeof(unsigned long));

	err |= __put_user(regs->sa0, &sc->sc_regs.sa0);
	err |= __put_user(regs->lc0, &sc->sc_regs.lc0);
	err |= __put_user(regs->sa1, &sc->sc_regs.sa1);
	err |= __put_user(regs->lc1, &sc->sc_regs.lc1);
	err |= __put_user(regs->m0, &sc->sc_regs.m0);
	err |= __put_user(regs->m1, &sc->sc_regs.m1);
	err |= __put_user(regs->usr, &sc->sc_regs.usr);
	err |= __put_user(regs->preds, &sc->sc_regs.p3_0);
	err |= __put_user(regs->gp, &sc->sc_regs.gp);
	err |= __put_user(regs->ugp, &sc->sc_regs.ugp);
#if CONFIG_HEXAGON_ARCH_VERSION >= 4
	err |= __put_user(regs->cs0, &sc->sc_regs.cs0);
	err |= __put_user(regs->cs1, &sc->sc_regs.cs1);
#endif
	tmp = pt_elr(regs); err |= __put_user(tmp, &sc->sc_regs.pc);
	tmp = pt_cause(regs); err |= __put_user(tmp, &sc->sc_regs.cause);
	tmp = pt_badva(regs); err |= __put_user(tmp, &sc->sc_regs.badva);

	return err;
}

static int restore_sigcontext(struct pt_regs *regs,
			      struct sigcontext __user *sc)
{
	unsigned long tmp;
	int err = 0;

	err |= copy_from_user(&regs->r00, &sc->sc_regs.r0,
			      32 * sizeof(unsigned long));

	err |= __get_user(regs->sa0, &sc->sc_regs.sa0);
	err |= __get_user(regs->lc0, &sc->sc_regs.lc0);
	err |= __get_user(regs->sa1, &sc->sc_regs.sa1);
	err |= __get_user(regs->lc1, &sc->sc_regs.lc1);
	err |= __get_user(regs->m0, &sc->sc_regs.m0);
	err |= __get_user(regs->m1, &sc->sc_regs.m1);
	err |= __get_user(regs->usr, &sc->sc_regs.usr);
	err |= __get_user(regs->preds, &sc->sc_regs.p3_0);
	err |= __get_user(regs->gp, &sc->sc_regs.gp);
	err |= __get_user(regs->ugp, &sc->sc_regs.ugp);
#if CONFIG_HEXAGON_ARCH_VERSION >= 4
	err |= __get_user(regs->cs0, &sc->sc_regs.cs0);
	err |= __get_user(regs->cs1, &sc->sc_regs.cs1);
#endif
	err |= __get_user(tmp, &sc->sc_regs.pc); pt_set_elr(regs, tmp);

	return err;
}

/*
 * Setup signal stack frame with siginfo structure
 */
static int setup_rt_frame(struct ksignal *ksig, sigset_t *set,
			  struct pt_regs *regs)
{
	int err = 0;
	struct rt_sigframe __user *frame;
	struct hexagon_vdso *vdso = current->mm->context.vdso;

	frame = get_sigframe(ksig, regs, sizeof(struct rt_sigframe));

	if (!access_ok(frame, sizeof(struct rt_sigframe)))
		return -EFAULT;

	if (copy_siginfo_to_user(&frame->info, &ksig->info))
		return -EFAULT;

	/* The on-stack signal trampoline is no longer executed;
	 * however, the libgcc signal frame unwinding code checks for
	 * the presence of these two numeric magic values.
	 */
	err |= __put_user(0x7800d166, &frame->tramp[0]);
	err |= __put_user(0x5400c004, &frame->tramp[1]);
	err |= setup_sigcontext(regs, &frame->uc.uc_mcontext);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));
	err |= __save_altstack(&frame->uc.uc_stack, user_stack_pointer(regs));
	if (err)
		return -EFAULT;

	/* Load r0/r1 pair with signumber/siginfo pointer... */
	regs->r0100 = ((unsigned long long)((unsigned long)&frame->info) << 32)
		| (unsigned long long)ksig->sig;
	regs->r02 = (unsigned long) &frame->uc;
	regs->r31 = (unsigned long) vdso->rt_signal_trampoline;
	pt_psp(regs) = (unsigned long) frame;
	pt_set_elr(regs, (unsigned long)ksig->ka.sa.sa_handler);

	return 0;
}

/*
 * Setup invocation of signal handler
 */
static void handle_signal(struct ksignal *ksig, struct pt_regs *regs)
{
	int ret;

	/*
	 * If we're handling a signal that aborted a system call,
	 * set up the error return value before adding the signal
	 * frame to the stack.
	 */

	if (regs->syscall_nr >= 0) {
		switch (regs->r00) {
		case -ERESTART_RESTARTBLOCK:
		case -ERESTARTNOHAND:
			regs->r00 = -EINTR;
			break;
		case -ERESTARTSYS:
			if (!(ksig->ka.sa.sa_flags & SA_RESTART)) {
				regs->r00 = -EINTR;
				break;
			}
			fallthrough;
		case -ERESTARTNOINTR:
			regs->r06 = regs->syscall_nr;
			pt_set_elr(regs, pt_elr(regs) - 4);
			regs->r00 = regs->restart_r0;
			break;
		default:
			break;
		}
	}

	/*
	 * Set up the stack frame; not doing the SA_SIGINFO thing.  We
	 * only set up the rt_frame flavor.
	 */
	/* If there was an error on setup, no signal was delivered. */
	ret = setup_rt_frame(ksig, sigmask_to_save(), regs);

	signal_setup_done(ret, ksig, test_thread_flag(TIF_SINGLESTEP));
}

/*
 * Called from return-from-event code.
 */
void do_signal(struct pt_regs *regs)
{
	struct ksignal ksig;

	if (!user_mode(regs))
		return;

	if (get_signal(&ksig)) {
		handle_signal(&ksig, regs);
		return;
	}

	/*
	 * No (more) signals; if we came from a system call, handle the restart.
	 */

	if (regs->syscall_nr >= 0) {
		switch (regs->r00) {
		case -ERESTARTNOHAND:
		case -ERESTARTSYS:
		case -ERESTARTNOINTR:
			regs->r06 = regs->syscall_nr;
			break;
		case -ERESTART_RESTARTBLOCK:
			regs->r06 = __NR_restart_syscall;
			break;
		default:
			goto no_restart;
		}
		pt_set_elr(regs, pt_elr(regs) - 4);
		regs->r00 = regs->restart_r0;
	}

no_restart:
	/* If there's no signal to deliver, put the saved sigmask back */
	restore_saved_sigmask();
}

/*
 * Architecture-specific wrappers for signal-related system calls
 */

asmlinkage int sys_rt_sigreturn(void)
{
	struct pt_regs *regs = current_pt_regs();
	struct rt_sigframe __user *frame;
	sigset_t blocked;

	/* Always make any pending restarted system calls return -EINTR */
	current->restart_block.fn = do_no_restart_syscall;

	frame = (struct rt_sigframe __user *)pt_psp(regs);
	if (!access_ok(frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&blocked, &frame->uc.uc_sigmask, sizeof(blocked)))
		goto badframe;

	set_current_blocked(&blocked);

	if (restore_sigcontext(regs, &frame->uc.uc_mcontext))
		goto badframe;

	/* Restore the user's stack as well */
	pt_psp(regs) = regs->r29;

	regs->syscall_nr = -1;

	if (restore_altstack(&frame->uc.uc_stack))
		goto badframe;

	return regs->r00;

badframe:
	force_sig(SIGSEGV);
	return 0;
}
