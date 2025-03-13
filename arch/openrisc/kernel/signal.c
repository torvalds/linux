// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * OpenRISC signal.c
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * Modifications for the OpenRISC architecture:
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 */

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>
#include <linux/stddef.h>
#include <linux/resume_user_mode.h>

#include <asm/fpu.h>
#include <asm/processor.h>
#include <asm/syscall.h>
#include <asm/ucontext.h>
#include <linux/uaccess.h>

struct rt_sigframe {
	struct siginfo info;
	struct ucontext uc;
	unsigned char retcode[16];	/* trampoline code */
};

asmlinkage long _sys_rt_sigreturn(struct pt_regs *regs);

asmlinkage int do_work_pending(struct pt_regs *regs, unsigned int thread_flags,
			       int syscall);

#ifdef CONFIG_FPU
static long restore_fp_state(struct sigcontext __user *sc)
{
	long err;

	err = __copy_from_user(&current->thread.fpcsr, &sc->fpcsr, sizeof(unsigned long));
	if (unlikely(err))
		return err;

	/* Restore the FPU state */
	restore_fpu(current);

	return 0;
}

static long save_fp_state(struct sigcontext __user *sc)
{
	long err;

	/* Sync the user FPU state so we can copy to sigcontext */
	save_fpu(current);

	err = __copy_to_user(&sc->fpcsr, &current->thread.fpcsr, sizeof(unsigned long));

	return err;
}
#else
#define save_fp_state(sc) (0)
#define restore_fp_state(sc) (0)
#endif

static int restore_sigcontext(struct pt_regs *regs,
			      struct sigcontext __user *sc)
{
	int err = 0;

	/* Always make any pending restarted system calls return -EINTR */
	current->restart_block.fn = do_no_restart_syscall;

	/*
	 * Restore the regs from &sc->regs.
	 * (sc is already checked since the sigframe was
	 *  checked in sys_sigreturn previously)
	 */
	err |= __copy_from_user(regs, sc->regs.gpr, 32 * sizeof(unsigned long));
	err |= __copy_from_user(&regs->pc, &sc->regs.pc, sizeof(unsigned long));
	err |= __copy_from_user(&regs->sr, &sc->regs.sr, sizeof(unsigned long));
	err |= restore_fp_state(sc);

	/* make sure the SM-bit is cleared so user-mode cannot fool us */
	regs->sr &= ~SPR_SR_SM;

	regs->orig_gpr11 = -1;	/* Avoid syscall restart checks */

	/* TODO: the other ports use regs->orig_XX to disable syscall checks
	 * after this completes, but we don't use that mechanism. maybe we can
	 * use it now ?
	 */

	return err;
}

asmlinkage long _sys_rt_sigreturn(struct pt_regs *regs)
{
	struct rt_sigframe __user *frame = (struct rt_sigframe __user *)regs->sp;
	sigset_t set;

	/*
	 * Since we stacked the signal on a dword boundary,
	 * then frame should be dword aligned here.  If it's
	 * not, then the user is trying to mess with us.
	 */
	if (((unsigned long)frame) & 3)
		goto badframe;

	if (!access_ok(frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	set_current_blocked(&set);

	if (restore_sigcontext(regs, &frame->uc.uc_mcontext))
		goto badframe;

	if (restore_altstack(&frame->uc.uc_stack))
		goto badframe;

	return regs->gpr[11];

badframe:
	force_sig(SIGSEGV);
	return 0;
}

/*
 * Set up a signal frame.
 */

static int setup_sigcontext(struct pt_regs *regs, struct sigcontext __user *sc)
{
	int err = 0;

	/* copy the regs */
	/* There should be no need to save callee-saved registers here...
	 * ...but we save them anyway.  Revisit this
	 */
	err |= __copy_to_user(sc->regs.gpr, regs, 32 * sizeof(unsigned long));
	err |= __copy_to_user(&sc->regs.pc, &regs->pc, sizeof(unsigned long));
	err |= __copy_to_user(&sc->regs.sr, &regs->sr, sizeof(unsigned long));
	err |= save_fp_state(sc);

	return err;
}

static inline unsigned long align_sigframe(unsigned long sp)
{
	return sp & ~3UL;
}

/*
 * Work out where the signal frame should go.  It's either on the user stack
 * or the alternate stack.
 */

static inline void __user *get_sigframe(struct ksignal *ksig,
					struct pt_regs *regs, size_t frame_size)
{
	unsigned long sp = regs->sp;

	/* redzone */
	sp -= STACK_FRAME_OVERHEAD;
	sp = sigsp(sp, ksig);
	sp = align_sigframe(sp - frame_size);

	return (void __user *)sp;
}

/* grab and setup a signal frame.
 *
 * basically we stack a lot of state info, and arrange for the
 * user-mode program to return to the kernel using either a
 * trampoline which performs the syscall sigreturn, or a provided
 * user-mode trampoline.
 */
static int setup_rt_frame(struct ksignal *ksig, sigset_t *set,
			  struct pt_regs *regs)
{
	struct rt_sigframe __user *frame;
	unsigned long return_ip;
	int err = 0;

	frame = get_sigframe(ksig, regs, sizeof(*frame));

	if (!access_ok(frame, sizeof(*frame)))
		return -EFAULT;

	/* Create siginfo.  */
	if (ksig->ka.sa.sa_flags & SA_SIGINFO)
		err |= copy_siginfo_to_user(&frame->info, &ksig->info);

	/* Create the ucontext.  */
	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(NULL, &frame->uc.uc_link);
	err |= __save_altstack(&frame->uc.uc_stack, regs->sp);
	err |= setup_sigcontext(regs, &frame->uc.uc_mcontext);

	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));

	if (err)
		return -EFAULT;

	/* trampoline - the desired return ip is the retcode itself */
	return_ip = (unsigned long)&frame->retcode;
	/* This is:
		l.ori r11,r0,__NR_sigreturn
		l.sys 1
	 */
	err |= __put_user(0xa960,             (short __user *)(frame->retcode + 0));
	err |= __put_user(__NR_rt_sigreturn,  (short __user *)(frame->retcode + 2));
	err |= __put_user(0x20000001, (unsigned long __user *)(frame->retcode + 4));
	err |= __put_user(0x15000000, (unsigned long __user *)(frame->retcode + 8));

	if (err)
		return -EFAULT;

	/* Set up registers for signal handler */
	regs->pc = (unsigned long)ksig->ka.sa.sa_handler; /* what we enter NOW */
	regs->gpr[9] = (unsigned long)return_ip;     /* what we enter LATER */
	regs->gpr[3] = (unsigned long)ksig->sig;           /* arg 1: signo */
	regs->gpr[4] = (unsigned long)&frame->info;  /* arg 2: (siginfo_t*) */
	regs->gpr[5] = (unsigned long)&frame->uc;    /* arg 3: ucontext */

	/* actually move the usp to reflect the stacked frame */
	regs->sp = (unsigned long)frame;

	return 0;
}

static inline void
handle_signal(struct ksignal *ksig, struct pt_regs *regs)
{
	int ret;

	rseq_signal_deliver(ksig, regs);

	ret = setup_rt_frame(ksig, sigmask_to_save(), regs);

	signal_setup_done(ret, ksig, test_thread_flag(TIF_SINGLESTEP));
}

/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 *
 * Also note that the regs structure given here as an argument, is the latest
 * pushed pt_regs. It may or may not be the same as the first pushed registers
 * when the initial usermode->kernelmode transition took place. Therefore
 * we can use user_mode(regs) to see if we came directly from kernel or user
 * mode below.
 */

static int do_signal(struct pt_regs *regs, int syscall)
{
	struct ksignal ksig;
	unsigned long continue_addr = 0;
	unsigned long restart_addr = 0;
	unsigned long retval = 0;
	int restart = 0;

	if (syscall) {
		continue_addr = regs->pc;
		restart_addr = continue_addr - 4;
		retval = regs->gpr[11];

		/*
		 * Setup syscall restart here so that a debugger will
		 * see the already changed PC.
		 */
		switch (retval) {
		case -ERESTART_RESTARTBLOCK:
			restart = -2;
			fallthrough;
		case -ERESTARTNOHAND:
		case -ERESTARTSYS:
		case -ERESTARTNOINTR:
			restart++;
			regs->gpr[11] = regs->orig_gpr11;
			regs->pc = restart_addr;
			break;
		}
	}

	/*
	 * Get the signal to deliver.  During the call to get_signal the
	 * debugger may change all our registers so we may need to revert
	 * the decision to restart the syscall; specifically, if the PC is
	 * changed, don't restart the syscall.
	 */
	if (get_signal(&ksig)) {
		if (unlikely(restart) && regs->pc == restart_addr) {
			if (retval == -ERESTARTNOHAND ||
			    retval == -ERESTART_RESTARTBLOCK
			    || (retval == -ERESTARTSYS
			        && !(ksig.ka.sa.sa_flags & SA_RESTART))) {
				/* No automatic restart */
				regs->gpr[11] = -EINTR;
				regs->pc = continue_addr;
			}
		}
		handle_signal(&ksig, regs);
	} else {
		/* no handler */
		restore_saved_sigmask();
		/*
		 * Restore pt_regs PC as syscall restart will be handled by
		 * kernel without return to userspace
		 */
		if (unlikely(restart) && regs->pc == restart_addr) {
			regs->pc = continue_addr;
			return restart;
		}
	}

	return 0;
}

asmlinkage int
do_work_pending(struct pt_regs *regs, unsigned int thread_flags, int syscall)
{
	do {
		if (likely(thread_flags & _TIF_NEED_RESCHED)) {
			schedule();
		} else {
			if (unlikely(!user_mode(regs)))
				return 0;
			local_irq_enable();
			if (thread_flags & (_TIF_SIGPENDING|_TIF_NOTIFY_SIGNAL)) {
				int restart = do_signal(regs, syscall);
				if (unlikely(restart)) {
					/*
					 * Restart without handlers.
					 * Deal with it without leaving
					 * the kernel space.
					 */
					return restart;
				}
				syscall = 0;
			} else {
				resume_user_mode_work(regs);
			}
		}
		local_irq_disable();
		thread_flags = read_thread_flags();
	} while (thread_flags & _TIF_WORK_MASK);
	return 0;
}
