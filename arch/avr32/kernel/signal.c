/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * Based on linux/arch/sh/kernel/signal.c
 *  Copyright (C) 1999, 2000  Niibe Yutaka & Kaz Kojima
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>
#include <linux/tracehook.h>

#include <asm/uaccess.h>
#include <asm/ucontext.h>
#include <asm/syscalls.h>

struct rt_sigframe
{
	struct siginfo info;
	struct ucontext uc;
	unsigned long retcode;
};

static int
restore_sigcontext(struct pt_regs *regs, struct sigcontext __user *sc)
{
	int err = 0;

#define COPY(x)		err |= __get_user(regs->x, &sc->x)
	COPY(sr);
	COPY(pc);
	COPY(lr);
	COPY(sp);
	COPY(r12);
	COPY(r11);
	COPY(r10);
	COPY(r9);
	COPY(r8);
	COPY(r7);
	COPY(r6);
	COPY(r5);
	COPY(r4);
	COPY(r3);
	COPY(r2);
	COPY(r1);
	COPY(r0);
#undef	COPY

	/*
	 * Don't allow anyone to pretend they're running in supervisor
	 * mode or something...
	 */
	err |= !valid_user_regs(regs);

	return err;
}


asmlinkage int sys_rt_sigreturn(struct pt_regs *regs)
{
	struct rt_sigframe __user *frame;
	sigset_t set;

	/* Always make any pending restarted system calls return -EINTR */
	current_thread_info()->restart_block.fn = do_no_restart_syscall;

	frame = (struct rt_sigframe __user *)regs->sp;
	pr_debug("SIG return: frame = %p\n", frame);

	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;

	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	set_current_blocked(&set);

	if (restore_sigcontext(regs, &frame->uc.uc_mcontext))
		goto badframe;

	if (restore_altstack(&frame->uc.uc_stack))
		goto badframe;

	pr_debug("Context restored: pc = %08lx, lr = %08lx, sp = %08lx\n",
		 regs->pc, regs->lr, regs->sp);

	return regs->r12;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

static int
setup_sigcontext(struct sigcontext __user *sc, struct pt_regs *regs)
{
	int err = 0;

#define COPY(x)		err |= __put_user(regs->x, &sc->x)
	COPY(sr);
	COPY(pc);
	COPY(lr);
	COPY(sp);
	COPY(r12);
	COPY(r11);
	COPY(r10);
	COPY(r9);
	COPY(r8);
	COPY(r7);
	COPY(r6);
	COPY(r5);
	COPY(r4);
	COPY(r3);
	COPY(r2);
	COPY(r1);
	COPY(r0);
#undef	COPY

	return err;
}

static inline void __user *
get_sigframe(struct k_sigaction *ka, struct pt_regs *regs, int framesize)
{
	unsigned long sp = regs->sp;

	if ((ka->sa.sa_flags & SA_ONSTACK) && !sas_ss_flags(sp))
		sp = current->sas_ss_sp + current->sas_ss_size;

	return (void __user *)((sp - framesize) & ~3);
}

static int
setup_rt_frame(int sig, struct k_sigaction *ka, siginfo_t *info,
	       sigset_t *set, struct pt_regs *regs)
{
	struct rt_sigframe __user *frame;
	int err = 0;

	frame = get_sigframe(ka, regs, sizeof(*frame));
	err = -EFAULT;
	if (!access_ok(VERIFY_WRITE, frame, sizeof (*frame)))
		goto out;

	/*
	 * Set up the return code:
	 *
	 *	mov	r8, __NR_rt_sigreturn
	 *	scall
	 *
	 * Note: This will blow up since we're using a non-executable
	 * stack. Better use SA_RESTORER.
	 */
#if __NR_rt_sigreturn > 127
# error __NR_rt_sigreturn must be < 127 to fit in a short mov
#endif
	err = __put_user(0x3008d733 | (__NR_rt_sigreturn << 20),
			 &frame->retcode);

	err |= copy_siginfo_to_user(&frame->info, info);

	/* Set up the ucontext */
	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(NULL, &frame->uc.uc_link);
	err |= __save_altstack(&frame->uc.uc_stack, regs->sp);
	err |= setup_sigcontext(&frame->uc.uc_mcontext, regs);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));

	if (err)
		goto out;

	regs->r12 = sig;
	regs->r11 = (unsigned long) &frame->info;
	regs->r10 = (unsigned long) &frame->uc;
	regs->sp = (unsigned long) frame;
	if (ka->sa.sa_flags & SA_RESTORER)
		regs->lr = (unsigned long)ka->sa.sa_restorer;
	else {
		printk(KERN_NOTICE "[%s:%d] did not set SA_RESTORER\n",
		       current->comm, current->pid);
		regs->lr = (unsigned long) &frame->retcode;
	}

	pr_debug("SIG deliver [%s:%d]: sig=%d sp=0x%lx pc=0x%lx->0x%p lr=0x%lx\n",
		 current->comm, current->pid, sig, regs->sp,
		 regs->pc, ka->sa.sa_handler, regs->lr);

	regs->pc = (unsigned long) ka->sa.sa_handler;

out:
	return err;
}

static inline void setup_syscall_restart(struct pt_regs *regs)
{
	if (regs->r12 == -ERESTART_RESTARTBLOCK)
		regs->r8 = __NR_restart_syscall;
	else
		regs->r12 = regs->r12_orig;
	regs->pc -= 2;
}

static inline void
handle_signal(unsigned long sig, struct k_sigaction *ka, siginfo_t *info,
	      struct pt_regs *regs, int syscall)
{
	int ret;

	/*
	 * Set up the stack frame
	 */
	ret = setup_rt_frame(sig, ka, info, sigmask_to_save(), regs);

	/*
	 * Check that the resulting registers are sane
	 */
	ret |= !valid_user_regs(regs);

	/*
	 * Block the signal if we were successful.
	 */
	if (ret != 0)
		force_sigsegv(sig, current);
	else
		signal_delivered(sig, info, ka, regs, 0);
}

/*
 * Note that 'init' is a special process: it doesn't get signals it
 * doesn't want to handle. Thus you cannot kill init even with a
 * SIGKILL even by mistake.
 */
static void do_signal(struct pt_regs *regs, int syscall)
{
	siginfo_t info;
	int signr;
	struct k_sigaction ka;

	/*
	 * We want the common case to go fast, which is why we may in
	 * certain cases get here from kernel mode. Just return
	 * without doing anything if so.
	 */
	if (!user_mode(regs))
		return;

	signr = get_signal_to_deliver(&info, &ka, regs, NULL);
	if (syscall) {
		switch (regs->r12) {
		case -ERESTART_RESTARTBLOCK:
		case -ERESTARTNOHAND:
			if (signr > 0) {
				regs->r12 = -EINTR;
				break;
			}
			/* fall through */
		case -ERESTARTSYS:
			if (signr > 0 && !(ka.sa.sa_flags & SA_RESTART)) {
				regs->r12 = -EINTR;
				break;
			}
			/* fall through */
		case -ERESTARTNOINTR:
			setup_syscall_restart(regs);
		}
	}

	if (signr == 0) {
		/* No signal to deliver -- put the saved sigmask back */
		restore_saved_sigmask();
		return;
	}

	handle_signal(signr, &ka, &info, regs, syscall);
}

asmlinkage void do_notify_resume(struct pt_regs *regs, struct thread_info *ti)
{
	int syscall = 0;

	if ((sysreg_read(SR) & MODE_MASK) == MODE_SUPERVISOR)
		syscall = 1;

	if (ti->flags & _TIF_SIGPENDING)
		do_signal(regs, syscall);

	if (ti->flags & _TIF_NOTIFY_RESUME) {
		clear_thread_flag(TIF_NOTIFY_RESUME);
		tracehook_notify_resume(regs);
	}
}
