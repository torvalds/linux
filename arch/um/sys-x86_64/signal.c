/*
 * Copyright (C) 2003 PathScale, Inc.
 * Copyright (C) 2003 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <linux/personality.h>
#include <linux/ptrace.h>
#include <asm/unistd.h>
#include <asm/uaccess.h>
#include <asm/ucontext.h>
#include "frame_kern.h"
#include "skas.h"

void copy_sc(struct uml_pt_regs *regs, void *from)
{
	struct sigcontext *sc = from;

#define GETREG(regs, regno, sc, regname)				\
	(regs)->gp[(regno) / sizeof(unsigned long)] = (sc)->regname

	GETREG(regs, R8, sc, r8);
	GETREG(regs, R9, sc, r9);
	GETREG(regs, R10, sc, r10);
	GETREG(regs, R11, sc, r11);
	GETREG(regs, R12, sc, r12);
	GETREG(regs, R13, sc, r13);
	GETREG(regs, R14, sc, r14);
	GETREG(regs, R15, sc, r15);
	GETREG(regs, RDI, sc, di);
	GETREG(regs, RSI, sc, si);
	GETREG(regs, RBP, sc, bp);
	GETREG(regs, RBX, sc, bx);
	GETREG(regs, RDX, sc, dx);
	GETREG(regs, RAX, sc, ax);
	GETREG(regs, RCX, sc, cx);
	GETREG(regs, RSP, sc, sp);
	GETREG(regs, RIP, sc, ip);
	GETREG(regs, EFLAGS, sc, flags);
	GETREG(regs, CS, sc, cs);

#undef GETREG
}

static int copy_sc_from_user(struct pt_regs *regs,
			     struct sigcontext __user *from,
			     struct _fpstate __user *fpp)
{
	struct user_i387_struct fp;
	int err = 0;

#define GETREG(regs, regno, sc, regname)				\
	__get_user((regs)->regs.gp[(regno) / sizeof(unsigned long)],	\
		   &(sc)->regname)

	err |= GETREG(regs, R8, from, r8);
	err |= GETREG(regs, R9, from, r9);
	err |= GETREG(regs, R10, from, r10);
	err |= GETREG(regs, R11, from, r11);
	err |= GETREG(regs, R12, from, r12);
	err |= GETREG(regs, R13, from, r13);
	err |= GETREG(regs, R14, from, r14);
	err |= GETREG(regs, R15, from, r15);
	err |= GETREG(regs, RDI, from, di);
	err |= GETREG(regs, RSI, from, si);
	err |= GETREG(regs, RBP, from, bp);
	err |= GETREG(regs, RBX, from, bx);
	err |= GETREG(regs, RDX, from, dx);
	err |= GETREG(regs, RAX, from, ax);
	err |= GETREG(regs, RCX, from, cx);
	err |= GETREG(regs, RSP, from, sp);
	err |= GETREG(regs, RIP, from, ip);
	err |= GETREG(regs, EFLAGS, from, flags);
	err |= GETREG(regs, CS, from, cs);
	if (err)
		return 1;

#undef GETREG

	err = copy_from_user(&fp, fpp, sizeof(struct user_i387_struct));
	if (err)
		return 1;

	err = restore_fp_registers(userspace_pid[current_thread_info()->cpu],
				   (unsigned long *) &fp);
	if (err < 0) {
		printk(KERN_ERR "copy_sc_from_user - "
		       "restore_fp_registers failed, errno = %d\n",
		       -err);
		return 1;
	}

	return 0;
}

static int copy_sc_to_user(struct sigcontext __user *to,
			   struct _fpstate __user *to_fp, struct pt_regs *regs,
			   unsigned long mask, unsigned long sp)
{
	struct faultinfo * fi = &current->thread.arch.faultinfo;
	struct user_i387_struct fp;
	int err = 0;

	err |= __put_user(0, &to->gs);
	err |= __put_user(0, &to->fs);

#define PUTREG(regs, regno, sc, regname)				\
	__put_user((regs)->regs.gp[(regno) / sizeof(unsigned long)],	\
		   &(sc)->regname)

	err |= PUTREG(regs, RDI, to, di);
	err |= PUTREG(regs, RSI, to, si);
	err |= PUTREG(regs, RBP, to, bp);
	/*
	 * Must use original RSP, which is passed in, rather than what's in
	 * the pt_regs, because that's already been updated to point at the
	 * signal frame.
	 */
	err |= __put_user(sp, &to->sp);
	err |= PUTREG(regs, RBX, to, bx);
	err |= PUTREG(regs, RDX, to, dx);
	err |= PUTREG(regs, RCX, to, cx);
	err |= PUTREG(regs, RAX, to, ax);
	err |= PUTREG(regs, R8, to, r8);
	err |= PUTREG(regs, R9, to, r9);
	err |= PUTREG(regs, R10, to, r10);
	err |= PUTREG(regs, R11, to, r11);
	err |= PUTREG(regs, R12, to, r12);
	err |= PUTREG(regs, R13, to, r13);
	err |= PUTREG(regs, R14, to, r14);
	err |= PUTREG(regs, R15, to, r15);
	err |= PUTREG(regs, CS, to, cs); /* XXX x86_64 doesn't do this */

	err |= __put_user(fi->cr2, &to->cr2);
	err |= __put_user(fi->error_code, &to->err);
	err |= __put_user(fi->trap_no, &to->trapno);

	err |= PUTREG(regs, RIP, to, ip);
	err |= PUTREG(regs, EFLAGS, to, flags);
#undef PUTREG

	err |= __put_user(mask, &to->oldmask);
	if (err)
		return 1;

	err = save_fp_registers(userspace_pid[current_thread_info()->cpu],
				(unsigned long *) &fp);
	if (err < 0) {
		printk(KERN_ERR "copy_sc_from_user - restore_fp_registers "
		       "failed, errno = %d\n", -err);
		return 1;
	}

	if (copy_to_user(to_fp, &fp, sizeof(struct user_i387_struct)))
		return 1;

	return err;
}

struct rt_sigframe
{
	char __user *pretcode;
	struct ucontext uc;
	struct siginfo info;
	struct _fpstate fpstate;
};

#define round_down(m, n) (((m) / (n)) * (n))

int setup_signal_stack_si(unsigned long stack_top, int sig,
			  struct k_sigaction *ka, struct pt_regs * regs,
			  siginfo_t *info, sigset_t *set)
{
	struct rt_sigframe __user *frame;
	unsigned long save_sp = PT_REGS_RSP(regs);
	int err = 0;
	struct task_struct *me = current;

	frame = (struct rt_sigframe __user *)
		round_down(stack_top - sizeof(struct rt_sigframe), 16);
	/* Subtract 128 for a red zone and 8 for proper alignment */
	frame = (struct rt_sigframe __user *) ((unsigned long) frame - 128 - 8);

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto out;

	if (ka->sa.sa_flags & SA_SIGINFO) {
		err |= copy_siginfo_to_user(&frame->info, info);
		if (err)
			goto out;
	}

	/*
	 * Update SP now because the page fault handler refuses to extend
	 * the stack if the faulting address is too far below the current
	 * SP, which frame now certainly is.  If there's an error, the original
	 * value is restored on the way out.
	 * When writing the sigcontext to the stack, we have to write the
	 * original value, so that's passed to copy_sc_to_user, which does
	 * the right thing with it.
	 */
	PT_REGS_RSP(regs) = (unsigned long) frame;

	/* Create the ucontext.  */
	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(0, &frame->uc.uc_link);
	err |= __put_user(me->sas_ss_sp, &frame->uc.uc_stack.ss_sp);
	err |= __put_user(sas_ss_flags(save_sp),
			  &frame->uc.uc_stack.ss_flags);
	err |= __put_user(me->sas_ss_size, &frame->uc.uc_stack.ss_size);
	err |= copy_sc_to_user(&frame->uc.uc_mcontext, &frame->fpstate, regs,
			       set->sig[0], save_sp);
	err |= __put_user(&frame->fpstate, &frame->uc.uc_mcontext.fpstate);
	if (sizeof(*set) == 16) {
		__put_user(set->sig[0], &frame->uc.uc_sigmask.sig[0]);
		__put_user(set->sig[1], &frame->uc.uc_sigmask.sig[1]);
	}
	else
		err |= __copy_to_user(&frame->uc.uc_sigmask, set,
				      sizeof(*set));

	/*
	 * Set up to return from userspace.  If provided, use a stub
	 * already in userspace.
	 */
	/* x86-64 should always use SA_RESTORER. */
	if (ka->sa.sa_flags & SA_RESTORER)
		err |= __put_user(ka->sa.sa_restorer, &frame->pretcode);
	else
		/* could use a vstub here */
		goto restore_sp;

	if (err)
		goto restore_sp;

	/* Set up registers for signal handler */
	{
		struct exec_domain *ed = current_thread_info()->exec_domain;
		if (unlikely(ed && ed->signal_invmap && sig < 32))
			sig = ed->signal_invmap[sig];
	}

	PT_REGS_RDI(regs) = sig;
	/* In case the signal handler was declared without prototypes */
	PT_REGS_RAX(regs) = 0;

	/*
	 * This also works for non SA_SIGINFO handlers because they expect the
	 * next argument after the signal number on the stack.
	 */
	PT_REGS_RSI(regs) = (unsigned long) &frame->info;
	PT_REGS_RDX(regs) = (unsigned long) &frame->uc;
	PT_REGS_RIP(regs) = (unsigned long) ka->sa.sa_handler;
 out:
	return err;

restore_sp:
	PT_REGS_RSP(regs) = save_sp;
	return err;
}

long sys_rt_sigreturn(struct pt_regs *regs)
{
	unsigned long sp = PT_REGS_SP(&current->thread.regs);
	struct rt_sigframe __user *frame =
		(struct rt_sigframe __user *)(sp - 8);
	struct ucontext __user *uc = &frame->uc;
	sigset_t set;

	if (copy_from_user(&set, &uc->uc_sigmask, sizeof(set)))
		goto segfault;

	sigdelsetmask(&set, ~_BLOCKABLE);

	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	if (copy_sc_from_user(&current->thread.regs, &uc->uc_mcontext,
			      &frame->fpstate))
		goto segfault;

	/* Avoid ERESTART handling */
	PT_REGS_SYSCALL_NR(&current->thread.regs) = -1;
	return PT_REGS_SYSCALL_RET(&current->thread.regs);

 segfault:
	force_sig(SIGSEGV, current);
	return 0;
}
