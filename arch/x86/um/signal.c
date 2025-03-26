/*
 * Copyright (C) 2003 PathScale, Inc.
 * Copyright (C) 2003 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */


#include <linux/personality.h>
#include <linux/ptrace.h>
#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <asm/unistd.h>
#include <linux/uaccess.h>
#include <asm/ucontext.h>
#include <frame_kern.h>
#include <registers.h>
#include <skas.h>

#include <linux/regset.h>
#include <asm/sigframe.h>

#ifdef CONFIG_X86_32
struct _xstate_64 {
	struct _fpstate_64		fpstate;
	struct _header			xstate_hdr;
	struct _ymmh_state		ymmh;
	/* New processor state extensions go here: */
};
#else
#define _xstate_64 _xstate
#endif

static int copy_sc_from_user(struct pt_regs *regs,
			     struct sigcontext __user *from)
{
	struct _xstate_64 __user *from_fp64;
	struct sigcontext sc;
	int err;

	/* Always make any pending restarted system calls return -EINTR */
	current->restart_block.fn = do_no_restart_syscall;

	err = copy_from_user(&sc, from, sizeof(sc));
	if (err)
		return err;

#define GETREG(regno, regname) regs->regs.gp[HOST_##regno] = sc.regname

#ifdef CONFIG_X86_32
	GETREG(GS, gs);
	GETREG(FS, fs);
	GETREG(ES, es);
	GETREG(DS, ds);
#endif
	GETREG(DI, di);
	GETREG(SI, si);
	GETREG(BP, bp);
	GETREG(SP, sp);
	GETREG(BX, bx);
	GETREG(DX, dx);
	GETREG(CX, cx);
	GETREG(AX, ax);
	GETREG(IP, ip);

#ifdef CONFIG_X86_64
	GETREG(R8, r8);
	GETREG(R9, r9);
	GETREG(R10, r10);
	GETREG(R11, r11);
	GETREG(R12, r12);
	GETREG(R13, r13);
	GETREG(R14, r14);
	GETREG(R15, r15);
#endif

	GETREG(CS, cs);
	GETREG(EFLAGS, flags);
#ifdef CONFIG_X86_32
	GETREG(SS, ss);
#endif

#undef GETREG

#ifdef CONFIG_X86_32
	from_fp64 = ((void __user *)sc.fpstate) +
		    offsetof(struct _fpstate_32, _fxsr_env);
#else
	from_fp64 = (void __user *)sc.fpstate;
#endif

	err = copy_from_user(regs->regs.fp, from_fp64, host_fp_size);
	if (err)
		return 1;

#ifdef CONFIG_X86_32
	/* Data is duplicated and this copy is the important one */
	err = copy_regset_from_user(current,
				    task_user_regset_view(current),
				    REGSET_FP_LEGACY, 0,
				    sizeof(struct user_i387_struct),
				    (void __user *)sc.fpstate);
	if (err < 0)
		return err;
#endif

	return 0;
}

static int copy_sc_to_user(struct sigcontext __user *to,
			   struct _xstate __user *to_fp, struct pt_regs *regs,
			   unsigned long mask)
{
	struct _xstate_64 __user *to_fp64;
	struct sigcontext sc;
	struct faultinfo * fi = &current->thread.arch.faultinfo;
	int err;
	memset(&sc, 0, sizeof(struct sigcontext));

#define PUTREG(regno, regname) sc.regname = regs->regs.gp[HOST_##regno]

#ifdef CONFIG_X86_32
	PUTREG(GS, gs);
	PUTREG(FS, fs);
	PUTREG(ES, es);
	PUTREG(DS, ds);
#endif
	PUTREG(DI, di);
	PUTREG(SI, si);
	PUTREG(BP, bp);
	PUTREG(SP, sp);
	PUTREG(BX, bx);
	PUTREG(DX, dx);
	PUTREG(CX, cx);
	PUTREG(AX, ax);
#ifdef CONFIG_X86_64
	PUTREG(R8, r8);
	PUTREG(R9, r9);
	PUTREG(R10, r10);
	PUTREG(R11, r11);
	PUTREG(R12, r12);
	PUTREG(R13, r13);
	PUTREG(R14, r14);
	PUTREG(R15, r15);
#endif

	sc.cr2 = fi->cr2;
	sc.err = fi->error_code;
	sc.trapno = fi->trap_no;
	PUTREG(IP, ip);
	PUTREG(CS, cs);
	PUTREG(EFLAGS, flags);
#ifdef CONFIG_X86_32
	PUTREG(SP, sp_at_signal);
	PUTREG(SS, ss);
#endif
#undef PUTREG
	sc.oldmask = mask;
	sc.fpstate = (unsigned long)to_fp;

	err = copy_to_user(to, &sc, sizeof(struct sigcontext));
	if (err)
		return 1;

#ifdef CONFIG_X86_32
	err = copy_regset_to_user(current,
				  task_user_regset_view(current),
				  REGSET_FP_LEGACY, 0,
				  sizeof(struct _fpstate_32), to_fp);
	if (err < 0)
		return err;

	__put_user(X86_FXSR_MAGIC, &to_fp->fpstate.magic);

	BUILD_BUG_ON(offsetof(struct _xstate, xstate_hdr) !=
		     offsetof(struct _xstate_64, xstate_hdr) +
			offsetof(struct _fpstate_32, _fxsr_env));
	to_fp64 = (void __user *)to_fp +
		  offsetof(struct _fpstate_32, _fxsr_env);
#else
	to_fp64 = to_fp;
#endif /* CONFIG_X86_32 */

	if (copy_to_user(to_fp64, regs->regs.fp, host_fp_size))
		return 1;

	/*
	 * Put magic/size values for userspace. We do not bother to verify them
	 * later on, however, userspace needs them should it try to read the
	 * XSTATE data. And ptrace does not fill in these parts.
	 *
	 * Skip this if we do not have an XSTATE frame.
	 */
	if (host_fp_size <= sizeof(to_fp64->fpstate))
		return 0;

	BUILD_BUG_ON(sizeof(int) != FP_XSTATE_MAGIC2_SIZE);
#ifdef CONFIG_X86_32
	__put_user(offsetof(struct _fpstate_32, _fxsr_env) +
		   host_fp_size + FP_XSTATE_MAGIC2_SIZE,
		   &to_fp64->fpstate.sw_reserved.extended_size);
#else
	__put_user(host_fp_size + FP_XSTATE_MAGIC2_SIZE,
		   &to_fp64->fpstate.sw_reserved.extended_size);
#endif
	__put_user(host_fp_size, &to_fp64->fpstate.sw_reserved.xstate_size);

	__put_user(FP_XSTATE_MAGIC1, &to_fp64->fpstate.sw_reserved.magic1);
	__put_user(FP_XSTATE_MAGIC2,
		   (int __user *)((void __user *)to_fp64 + host_fp_size));

	return 0;
}

#ifdef CONFIG_X86_32
static int copy_ucontext_to_user(struct ucontext __user *uc,
				 struct _xstate __user *fp, sigset_t *set,
				 unsigned long sp)
{
	int err = 0;

	err |= __save_altstack(&uc->uc_stack, sp);
	err |= copy_sc_to_user(&uc->uc_mcontext, fp, &current->thread.regs, 0);
	err |= copy_to_user(&uc->uc_sigmask, set, sizeof(*set));
	return err;
}

int setup_signal_stack_sc(unsigned long stack_top, struct ksignal *ksig,
			  struct pt_regs *regs, sigset_t *mask)
{
	size_t math_size = offsetof(struct _fpstate_32, _fxsr_env) +
			   host_fp_size + FP_XSTATE_MAGIC2_SIZE;
	struct sigframe __user *frame;
	void __user *restorer;
	int err = 0, sig = ksig->sig;
	unsigned long fp_to;

	/* This is the same calculation as i386 - ((sp + 4) & 15) == 0 */
	stack_top = ((stack_top + 4) & -16UL) - 4;
	frame = (struct sigframe __user *) stack_top - 1;
	if (!access_ok(frame, sizeof(*frame)))
		return 1;

	/* Add required space for math frame */
	frame = (struct sigframe __user *)((unsigned long)frame - math_size);

	restorer = frame->retcode;
	if (ksig->ka.sa.sa_flags & SA_RESTORER)
		restorer = ksig->ka.sa.sa_restorer;

	err |= __put_user(restorer, (void __user * __user *)&frame->pretcode);
	err |= __put_user(sig, &frame->sig);

	fp_to = (unsigned long)frame + sizeof(*frame);

	err |= copy_sc_to_user(&frame->sc,
			       (struct _xstate __user *)fp_to,
			       regs, mask->sig[0]);
	if (_NSIG_WORDS > 1)
		err |= __copy_to_user(&frame->extramask, &mask->sig[1],
				      sizeof(frame->extramask));

	/*
	 * This is popl %eax ; movl $,%eax ; int $0x80
	 *
	 * WE DO NOT USE IT ANY MORE! It's only left here for historical
	 * reasons and because gdb uses it as a signature to notice
	 * signal handler stack frames.
	 */
	err |= __put_user(0xb858, (short __user *)(frame->retcode+0));
	err |= __put_user(__NR_sigreturn, (int __user *)(frame->retcode+2));
	err |= __put_user(0x80cd, (short __user *)(frame->retcode+6));

	if (err)
		return err;

	PT_REGS_SP(regs) = (unsigned long) frame;
	PT_REGS_IP(regs) = (unsigned long) ksig->ka.sa.sa_handler;
	PT_REGS_AX(regs) = (unsigned long) sig;
	PT_REGS_DX(regs) = (unsigned long) 0;
	PT_REGS_CX(regs) = (unsigned long) 0;
	return 0;
}

int setup_signal_stack_si(unsigned long stack_top, struct ksignal *ksig,
			  struct pt_regs *regs, sigset_t *mask)
{
	size_t math_size = offsetof(struct _fpstate_32, _fxsr_env) +
			   host_fp_size + FP_XSTATE_MAGIC2_SIZE;
	struct rt_sigframe __user *frame;
	void __user *restorer;
	int err = 0, sig = ksig->sig;
	unsigned long fp_to;

	stack_top &= -8UL;
	frame = (struct rt_sigframe __user *) stack_top - 1;
	if (!access_ok(frame, sizeof(*frame)))
		return 1;

	/* Add required space for math frame */
	frame = (struct rt_sigframe __user *)((unsigned long)frame - math_size);

	restorer = frame->retcode;
	if (ksig->ka.sa.sa_flags & SA_RESTORER)
		restorer = ksig->ka.sa.sa_restorer;

	err |= __put_user(restorer, (void __user * __user *)&frame->pretcode);
	err |= __put_user(sig, &frame->sig);
	err |= __put_user(&frame->info, (void __user * __user *)&frame->pinfo);
	err |= __put_user(&frame->uc, (void __user * __user *)&frame->puc);
	err |= copy_siginfo_to_user(&frame->info, &ksig->info);

	fp_to = (unsigned long)frame + sizeof(*frame);

	err |= copy_ucontext_to_user(&frame->uc, (struct _xstate __user *)fp_to,
				     mask, PT_REGS_SP(regs));

	/*
	 * This is movl $,%eax ; int $0x80
	 *
	 * WE DO NOT USE IT ANY MORE! It's only left here for historical
	 * reasons and because gdb uses it as a signature to notice
	 * signal handler stack frames.
	 */
	err |= __put_user(0xb8, (char __user *)(frame->retcode+0));
	err |= __put_user(__NR_rt_sigreturn, (int __user *)(frame->retcode+1));
	err |= __put_user(0x80cd, (short __user *)(frame->retcode+5));

	if (err)
		return err;

	PT_REGS_SP(regs) = (unsigned long) frame;
	PT_REGS_IP(regs) = (unsigned long) ksig->ka.sa.sa_handler;
	PT_REGS_AX(regs) = (unsigned long) sig;
	PT_REGS_DX(regs) = (unsigned long) &frame->info;
	PT_REGS_CX(regs) = (unsigned long) &frame->uc;
	return 0;
}

SYSCALL_DEFINE0(sigreturn)
{
	unsigned long sp = PT_REGS_SP(&current->thread.regs);
	struct sigframe __user *frame = (struct sigframe __user *)(sp - 8);
	sigset_t set;
	struct sigcontext __user *sc = &frame->sc;
	int sig_size = (_NSIG_WORDS - 1) * sizeof(unsigned long);

	if (copy_from_user(&set.sig[0], &sc->oldmask, sizeof(set.sig[0])) ||
	    copy_from_user(&set.sig[1], frame->extramask, sig_size))
		goto segfault;

	set_current_blocked(&set);

	if (copy_sc_from_user(&current->thread.regs, sc))
		goto segfault;

	/* Avoid ERESTART handling */
	PT_REGS_SYSCALL_NR(&current->thread.regs) = -1;
	return PT_REGS_SYSCALL_RET(&current->thread.regs);

 segfault:
	force_sig(SIGSEGV);
	return 0;
}

#else

int setup_signal_stack_si(unsigned long stack_top, struct ksignal *ksig,
			  struct pt_regs *regs, sigset_t *set)
{
	unsigned long math_size = host_fp_size + FP_XSTATE_MAGIC2_SIZE;
	struct rt_sigframe __user *frame;
	int err = 0, sig = ksig->sig;
	unsigned long fp_to;

	frame = (void __user *)stack_top - sizeof(struct rt_sigframe);

	/* Add required space for math frame */
	frame = (void __user *)((unsigned long)frame - math_size);

	/* ABI requires 16 byte boundary alignment */
	frame = (void __user *)round_down((unsigned long)frame, 16);

	/* Subtract 128 for a red zone and 8 for proper alignment */
	frame = (struct rt_sigframe __user *) ((unsigned long) frame - 128 - 8);

	if (!access_ok(frame, sizeof(*frame) + math_size))
		goto out;

	if (ksig->ka.sa.sa_flags & SA_SIGINFO) {
		err |= copy_siginfo_to_user(&frame->info, &ksig->info);
		if (err)
			goto out;
	}

	/* Create the ucontext.  */
	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(NULL, &frame->uc.uc_link);
	err |= __save_altstack(&frame->uc.uc_stack, PT_REGS_SP(regs));

	fp_to = (unsigned long)frame + sizeof(*frame);

	err |= copy_sc_to_user(&frame->uc.uc_mcontext,
			       (struct _xstate __user *)fp_to,
			       regs, set->sig[0]);

	err |= __put_user(fp_to, &frame->uc.uc_mcontext.fpstate);
	if (sizeof(*set) == 16) {
		err |= __put_user(set->sig[0], &frame->uc.uc_sigmask.sig[0]);
		err |= __put_user(set->sig[1], &frame->uc.uc_sigmask.sig[1]);
	}
	else
		err |= __copy_to_user(&frame->uc.uc_sigmask, set,
				      sizeof(*set));

	/*
	 * Set up to return from userspace.  If provided, use a stub
	 * already in userspace.
	 */
	/* x86-64 should always use SA_RESTORER. */
	if (ksig->ka.sa.sa_flags & SA_RESTORER)
		err |= __put_user((void __user *)ksig->ka.sa.sa_restorer,
				  &frame->pretcode);
	else
		/* could use a vstub here */
		return err;

	if (err)
		return err;

	PT_REGS_SP(regs) = (unsigned long) frame;
	PT_REGS_DI(regs) = sig;
	/* In case the signal handler was declared without prototypes */
	PT_REGS_AX(regs) = 0;

	/*
	 * This also works for non SA_SIGINFO handlers because they expect the
	 * next argument after the signal number on the stack.
	 */
	PT_REGS_SI(regs) = (unsigned long) &frame->info;
	PT_REGS_DX(regs) = (unsigned long) &frame->uc;
	PT_REGS_IP(regs) = (unsigned long) ksig->ka.sa.sa_handler;
 out:
	return err;
}
#endif

SYSCALL_DEFINE0(rt_sigreturn)
{
	unsigned long sp = PT_REGS_SP(&current->thread.regs);
	struct rt_sigframe __user *frame =
		(struct rt_sigframe __user *)(sp - sizeof(long));
	struct ucontext __user *uc = &frame->uc;
	sigset_t set;

	if (copy_from_user(&set, &uc->uc_sigmask, sizeof(set)))
		goto segfault;

	set_current_blocked(&set);

	if (copy_sc_from_user(&current->thread.regs, &uc->uc_mcontext))
		goto segfault;

	/* Avoid ERESTART handling */
	PT_REGS_SYSCALL_NR(&current->thread.regs) = -1;
	return PT_REGS_SYSCALL_RET(&current->thread.regs);

 segfault:
	force_sig(SIGSEGV);
	return 0;
}
