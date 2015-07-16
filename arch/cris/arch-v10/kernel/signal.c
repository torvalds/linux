/*
 *  linux/arch/cris/kernel/signal.c
 *
 *  Based on arch/i386/kernel/signal.c by
 *     Copyright (C) 1991, 1992  Linus Torvalds
 *     1997-11-28  Modified for POSIX.1b signals by Richard Henderson *
 *
 *  Ideas also taken from arch/arm.
 *
 *  Copyright (C) 2000-2007 Axis Communications AB
 *
 *  Authors:  Bjorn Wesen (bjornw@axis.com)
 *
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

#include <asm/processor.h>
#include <asm/ucontext.h>
#include <asm/uaccess.h>
#include <arch/system.h>

#define DEBUG_SIG 0

/* a syscall in Linux/CRIS is a break 13 instruction which is 2 bytes */
/* manipulate regs so that upon return, it will be re-executed */

/* We rely on that pc points to the instruction after "break 13", so the
 * library must never do strange things like putting it in a delay slot.
 */
#define RESTART_CRIS_SYS(regs) regs->r10 = regs->orig_r10; regs->irp -= 2;

void do_signal(int canrestart, struct pt_regs *regs);

/*
 * Do a signal return; undo the signal stack.
 */

struct sigframe {
	struct sigcontext sc;
	unsigned long extramask[_NSIG_WORDS-1];
	unsigned char retcode[8];  /* trampoline code */
};

struct rt_sigframe {
	struct siginfo *pinfo;
	void *puc;
	struct siginfo info;
	struct ucontext uc;
	unsigned char retcode[8];  /* trampoline code */
};


static int
restore_sigcontext(struct pt_regs *regs, struct sigcontext __user *sc)
{
	unsigned int err = 0;
	unsigned long old_usp;

        /* Always make any pending restarted system calls return -EINTR */
	current->restart_block.fn = do_no_restart_syscall;

	/* restore the regs from &sc->regs (same as sc, since regs is first)
	 * (sc is already checked for VERIFY_READ since the sigframe was
	 *  checked in sys_sigreturn previously)
	 */

	if (__copy_from_user(regs, sc, sizeof(struct pt_regs)))
                goto badframe;

	/* make sure the U-flag is set so user-mode cannot fool us */

	regs->dccr |= 1 << 8;

	/* restore the old USP as it was before we stacked the sc etc.
	 * (we cannot just pop the sigcontext since we aligned the sp and
	 *  stuff after pushing it)
	 */

	err |= __get_user(old_usp, &sc->usp);

	wrusp(old_usp);

	/* TODO: the other ports use regs->orig_XX to disable syscall checks
	 * after this completes, but we don't use that mechanism. maybe we can
	 * use it now ?
	 */

	return err;

badframe:
	return 1;
}

asmlinkage int sys_sigreturn(void)
{
	struct pt_regs *regs = current_pt_regs();
	struct sigframe __user *frame = (struct sigframe *)rdusp();
	sigset_t set;

        /*
         * Since we stacked the signal on a dword boundary,
         * then frame should be dword aligned here.  If it's
         * not, then the user is trying to mess with us.
         */
        if (((long)frame) & 3)
                goto badframe;

	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__get_user(set.sig[0], &frame->sc.oldmask)
	    || (_NSIG_WORDS > 1
		&& __copy_from_user(&set.sig[1], frame->extramask,
				    sizeof(frame->extramask))))
		goto badframe;

	set_current_blocked(&set);

	if (restore_sigcontext(regs, &frame->sc))
		goto badframe;

	/* TODO: SIGTRAP when single-stepping as in arm ? */

	return regs->r10;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

asmlinkage int sys_rt_sigreturn(void)
{
	struct pt_regs *regs = current_pt_regs();
	struct rt_sigframe __user *frame = (struct rt_sigframe *)rdusp();
	sigset_t set;

        /*
         * Since we stacked the signal on a dword boundary,
         * then frame should be dword aligned here.  If it's
         * not, then the user is trying to mess with us.
         */
        if (((long)frame) & 3)
                goto badframe;

	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	set_current_blocked(&set);

	if (restore_sigcontext(regs, &frame->uc.uc_mcontext))
		goto badframe;

	if (restore_altstack(&frame->uc.uc_stack))
		goto badframe;

	return regs->r10;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

/*
 * Set up a signal frame.
 */

static int setup_sigcontext(struct sigcontext __user *sc,
	struct pt_regs *regs, unsigned long mask)
{
	int err = 0;
	unsigned long usp = rdusp();

	/* copy the regs. they are first in sc so we can use sc directly */

	err |= __copy_to_user(sc, regs, sizeof(struct pt_regs));

        /* Set the frametype to CRIS_FRAME_NORMAL for the execution of
           the signal handler. The frametype will be restored to its previous
           value in restore_sigcontext. */
        regs->frametype = CRIS_FRAME_NORMAL;

	/* then some other stuff */

	err |= __put_user(mask, &sc->oldmask);

	err |= __put_user(usp, &sc->usp);

	return err;
}

/* Figure out where we want to put the new signal frame
 * - usually on the stack. */

static inline void __user *
get_sigframe(struct ksignal *ksig, size_t frame_size)
{
	unsigned long sp = sigsp(rdusp(), ksig);

	/* make sure the frame is dword-aligned */

	sp &= ~3;

	return (void __user*)(sp - frame_size);
}

/* grab and setup a signal frame.
 *
 * basically we stack a lot of state info, and arrange for the
 * user-mode program to return to the kernel using either a
 * trampoline which performs the syscall sigreturn, or a provided
 * user-mode trampoline.
 */

static int setup_frame(struct ksignal *ksig, sigset_t *set,
		       struct pt_regs *regs)
{
	struct sigframe __user *frame;
	unsigned long return_ip;
	int err = 0;

	frame = get_sigframe(ksig, sizeof(*frame));

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		return -EFAULT;

	err |= setup_sigcontext(&frame->sc, regs, set->sig[0]);
	if (err)
		return -EFAULT;

	if (_NSIG_WORDS > 1) {
		err |= __copy_to_user(frame->extramask, &set->sig[1],
				      sizeof(frame->extramask));
	}
	if (err)
		return -EFAULT;

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	if (ksig->ka.sa.sa_flags & SA_RESTORER) {
		return_ip = (unsigned long)ksig->ka.sa.sa_restorer;
	} else {
		/* trampoline - the desired return ip is the retcode itself */
		return_ip = (unsigned long)&frame->retcode;
		/* This is movu.w __NR_sigreturn, r9; break 13; */
		err |= __put_user(0x9c5f,         (short __user*)(frame->retcode+0));
		err |= __put_user(__NR_sigreturn, (short __user*)(frame->retcode+2));
		err |= __put_user(0xe93d,         (short __user*)(frame->retcode+4));
	}

	if (err)
		return -EFAULT;

	/* Set up registers for signal handler */

	regs->irp = (unsigned long) ksig->ka.sa.sa_handler;  /* what we enter NOW   */
	regs->srp = return_ip;                          /* what we enter LATER */
	regs->r10 = ksig->sig;                                /* first argument is signo */

	/* actually move the usp to reflect the stacked frame */

	wrusp((unsigned long)frame);

	return 0;
}

static int setup_rt_frame(struct ksignal *ksig, sigset_t *set,
			  struct pt_regs *regs)
{
	struct rt_sigframe __user *frame;
	unsigned long return_ip;
	int err = 0;

	frame = get_sigframe(ksig, sizeof(*frame));

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		return -EFAULT;

	err |= __put_user(&frame->info, &frame->pinfo);
	err |= __put_user(&frame->uc, &frame->puc);
	err |= copy_siginfo_to_user(&frame->info, &ksig->info);
	if (err)
		return -EFAULT;

	/* Clear all the bits of the ucontext we don't use.  */
        err |= __clear_user(&frame->uc, offsetof(struct ucontext, uc_mcontext));

	err |= setup_sigcontext(&frame->uc.uc_mcontext, regs, set->sig[0]);

	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));

	err |= __save_altstack(&frame->uc.uc_stack, rdusp());

	if (err)
		return -EFAULT;

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	if (ksig->ka.sa.sa_flags & SA_RESTORER) {
		return_ip = (unsigned long)ksig->ka.sa.sa_restorer;
	} else {
		/* trampoline - the desired return ip is the retcode itself */
		return_ip = (unsigned long)&frame->retcode;
		/* This is movu.w __NR_rt_sigreturn, r9; break 13; */
		err |= __put_user(0x9c5f, (short __user *)(frame->retcode+0));
		err |= __put_user(__NR_rt_sigreturn,
			(short __user *)(frame->retcode+2));
		err |= __put_user(0xe93d, (short __user *)(frame->retcode+4));
	}

	if (err)
		return -EFAULT;

	/* Set up registers for signal handler */

	/* What we enter NOW   */
	regs->irp = (unsigned long) ksig->ka.sa.sa_handler;
	/* What we enter LATER */
	regs->srp = return_ip;
	/* First argument is signo */
	regs->r10 = ksig->sig;
	/* Second argument is (siginfo_t *) */
	regs->r11 = (unsigned long)&frame->info;
	/* Third argument is unused */
	regs->r12 = 0;

	/* Actually move the usp to reflect the stacked frame */
	wrusp((unsigned long)frame);

	return 0;
}

/*
 * OK, we're invoking a handler
 */

static inline void handle_signal(int canrestart, struct ksignal *ksig,
				 struct pt_regs *regs)
{
	sigset_t *oldset = sigmask_to_save();
	int ret;

	/* Are we from a system call? */
	if (canrestart) {
		/* If so, check system call restarting.. */
		switch (regs->r10) {
		case -ERESTART_RESTARTBLOCK:
		case -ERESTARTNOHAND:
			/* ERESTARTNOHAND means that the syscall should
			 * only be restarted if there was no handler for
			 * the signal, and since we only get here if there
			 * is a handler, we don't restart */
			regs->r10 = -EINTR;
			break;
		case -ERESTARTSYS:
			/* ERESTARTSYS means to restart the syscall if
			 * there is no handler or the handler was
			 * registered with SA_RESTART */
			if (!(ksig->ka.sa.sa_flags & SA_RESTART)) {
				regs->r10 = -EINTR;
				break;
			}
		/* fallthrough */
		case -ERESTARTNOINTR:
			/* ERESTARTNOINTR means that the syscall should
			 * be called again after the signal handler returns. */
			RESTART_CRIS_SYS(regs);
		}
	}

	/* Set up the stack frame */
	if (ksig->ka.sa.sa_flags & SA_SIGINFO)
		ret = setup_rt_frame(ksig, oldset, regs);
	else
		ret = setup_frame(ksig, oldset, regs);

	signal_setup_done(ret, ksig, 0);
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

void do_signal(int canrestart, struct pt_regs *regs)
{
	struct ksignal ksig;

	/*
	 * We want the common case to go fast, which
	 * is why we may in certain cases get here from
	 * kernel mode. Just return without doing anything
	 * if so.
	 */
	if (!user_mode(regs))
		return;

	if (get_signal(&ksig)) {
		/* Whee!  Actually deliver the signal.  */
		handle_signal(canrestart, &ksig, regs);
		return;
	}

	/* Did we come from a system call? */
	if (canrestart) {
		/* Restart the system call - no handlers present */
		if (regs->r10 == -ERESTARTNOHAND ||
		    regs->r10 == -ERESTARTSYS ||
		    regs->r10 == -ERESTARTNOINTR) {
			RESTART_CRIS_SYS(regs);
		}
		if (regs->r10 == -ERESTART_RESTARTBLOCK) {
			regs->r9 = __NR_restart_syscall;
			regs->irp -= 2;
		}
	}

	/* if there's no signal to deliver, we just put the saved sigmask
	 * back */
	restore_saved_sigmask();
}
