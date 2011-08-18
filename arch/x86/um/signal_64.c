/*
 * Copyright (C) 2003 PathScale, Inc.
 * Copyright (C) 2003 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <linux/personality.h>
#include <linux/ptrace.h>
#include <linux/kernel.h>
#include <asm/unistd.h>
#include <asm/uaccess.h>
#include <asm/ucontext.h>
#include "frame_kern.h"
#include "skas.h"

static int copy_sc_from_user(struct pt_regs *regs,
			     struct sigcontext __user *from)
{
	struct sigcontext sc;
	struct user_i387_struct fp;
	void __user *buf;
	int err;

	err = copy_from_user(&sc, from, sizeof(sc));
	if (err)
		return err;

#define GETREG(regno, regname) regs->regs.gp[HOST_##regno] = sc.regname

	GETREG(R8, r8);
	GETREG(R9, r9);
	GETREG(R10, r10);
	GETREG(R11, r11);
	GETREG(R12, r12);
	GETREG(R13, r13);
	GETREG(R14, r14);
	GETREG(R15, r15);
	GETREG(DI, di);
	GETREG(SI, si);
	GETREG(BP, bp);
	GETREG(BX, bx);
	GETREG(DX, dx);
	GETREG(AX, ax);
	GETREG(CX, cx);
	GETREG(SP, sp);
	GETREG(IP, ip);
	GETREG(EFLAGS, flags);
	GETREG(CS, cs);
#undef GETREG

	buf = sc.fpstate;

	err = copy_from_user(&fp, buf, sizeof(struct user_i387_struct));
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
			   unsigned long mask)
{
	struct faultinfo * fi = &current->thread.arch.faultinfo;
	struct sigcontext sc;
	struct user_i387_struct fp;
	int err = 0;
	memset(&sc, 0, sizeof(struct sigcontext));

#define PUTREG(regno, regname) sc.regname = regs->regs.gp[HOST_##regno]

	PUTREG(DI, di);
	PUTREG(SI, si);
	PUTREG(BP, bp);
	PUTREG(SP, sp);
	PUTREG(BX, bx);
	PUTREG(DX, dx);
	PUTREG(CX, cx);
	PUTREG(AX, ax);
	PUTREG(R8, r8);
	PUTREG(R9, r9);
	PUTREG(R10, r10);
	PUTREG(R11, r11);
	PUTREG(R12, r12);
	PUTREG(R13, r13);
	PUTREG(R14, r14);
	PUTREG(R15, r15);
	PUTREG(CS, cs); /* XXX x86_64 doesn't do this */

	sc.cr2 = fi->cr2;
	sc.err = fi->error_code;
	sc.trapno = fi->trap_no;

	PUTREG(IP, ip);
	PUTREG(EFLAGS, flags);
#undef PUTREG

	sc.oldmask = mask;

	err = copy_to_user(to, &sc, sizeof(struct sigcontext));
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

int setup_signal_stack_si(unsigned long stack_top, int sig,
			  struct k_sigaction *ka, struct pt_regs * regs,
			  siginfo_t *info, sigset_t *set)
{
	struct rt_sigframe __user *frame;
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

	/* Create the ucontext.  */
	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(0, &frame->uc.uc_link);
	err |= __put_user(me->sas_ss_sp, &frame->uc.uc_stack.ss_sp);
	err |= __put_user(sas_ss_flags(PT_REGS_RSP(regs)),
			  &frame->uc.uc_stack.ss_flags);
	err |= __put_user(me->sas_ss_size, &frame->uc.uc_stack.ss_size);
	err |= copy_sc_to_user(&frame->uc.uc_mcontext, &frame->fpstate, regs,
			       set->sig[0]);
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
		return err;

	if (err)
		return err;

	/* Set up registers for signal handler */
	{
		struct exec_domain *ed = current_thread_info()->exec_domain;
		if (unlikely(ed && ed->signal_invmap && sig < 32))
			sig = ed->signal_invmap[sig];
	}

	PT_REGS_RSP(regs) = (unsigned long) frame;
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
	set_current_blocked(&set);

	if (copy_sc_from_user(&current->thread.regs, &uc->uc_mcontext))
		goto segfault;

	/* Avoid ERESTART handling */
	PT_REGS_SYSCALL_NR(&current->thread.regs) = -1;
	return PT_REGS_SYSCALL_RET(&current->thread.regs);

 segfault:
	force_sig(SIGSEGV, current);
	return 0;
}
