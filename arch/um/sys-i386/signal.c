/*
 * Copyright (C) 2004 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <linux/ptrace.h>
#include <asm/unistd.h>
#include <asm/uaccess.h>
#include <asm/ucontext.h>
#include "frame_kern.h"
#include "skas.h"

void copy_sc(struct uml_pt_regs *regs, void *from)
{
	struct sigcontext *sc = from;

	REGS_GS(regs->gp) = sc->gs;
	REGS_FS(regs->gp) = sc->fs;
	REGS_ES(regs->gp) = sc->es;
	REGS_DS(regs->gp) = sc->ds;
	REGS_EDI(regs->gp) = sc->di;
	REGS_ESI(regs->gp) = sc->si;
	REGS_EBP(regs->gp) = sc->bp;
	REGS_SP(regs->gp) = sc->sp;
	REGS_EBX(regs->gp) = sc->bx;
	REGS_EDX(regs->gp) = sc->dx;
	REGS_ECX(regs->gp) = sc->cx;
	REGS_EAX(regs->gp) = sc->ax;
	REGS_IP(regs->gp) = sc->ip;
	REGS_CS(regs->gp) = sc->cs;
	REGS_EFLAGS(regs->gp) = sc->flags;
	REGS_SS(regs->gp) = sc->ss;
}

/*
 * FPU tag word conversions.
 */

static inline unsigned short twd_i387_to_fxsr(unsigned short twd)
{
	unsigned int tmp; /* to avoid 16 bit prefixes in the code */

	/* Transform each pair of bits into 01 (valid) or 00 (empty) */
	tmp = ~twd;
	tmp = (tmp | (tmp>>1)) & 0x5555; /* 0V0V0V0V0V0V0V0V */
	/* and move the valid bits to the lower byte. */
	tmp = (tmp | (tmp >> 1)) & 0x3333; /* 00VV00VV00VV00VV */
	tmp = (tmp | (tmp >> 2)) & 0x0f0f; /* 0000VVVV0000VVVV */
	tmp = (tmp | (tmp >> 4)) & 0x00ff; /* 00000000VVVVVVVV */
	return tmp;
}

static inline unsigned long twd_fxsr_to_i387(struct user_fxsr_struct *fxsave)
{
	struct _fpxreg *st = NULL;
	unsigned long twd = (unsigned long) fxsave->twd;
	unsigned long tag;
	unsigned long ret = 0xffff0000;
	int i;

#define FPREG_ADDR(f, n)	((char *)&(f)->st_space + (n) * 16);

	for (i = 0; i < 8; i++) {
		if (twd & 0x1) {
			st = (struct _fpxreg *) FPREG_ADDR(fxsave, i);

			switch (st->exponent & 0x7fff) {
			case 0x7fff:
				tag = 2;		/* Special */
				break;
			case 0x0000:
				if ( !st->significand[0] &&
				     !st->significand[1] &&
				     !st->significand[2] &&
				     !st->significand[3] ) {
					tag = 1;	/* Zero */
				} else {
					tag = 2;	/* Special */
				}
				break;
			default:
				if (st->significand[3] & 0x8000) {
					tag = 0;	/* Valid */
				} else {
					tag = 2;	/* Special */
				}
				break;
			}
		} else {
			tag = 3;			/* Empty */
		}
		ret |= (tag << (2 * i));
		twd = twd >> 1;
	}
	return ret;
}

static int convert_fxsr_to_user(struct _fpstate __user *buf,
				struct user_fxsr_struct *fxsave)
{
	unsigned long env[7];
	struct _fpreg __user *to;
	struct _fpxreg *from;
	int i;

	env[0] = (unsigned long)fxsave->cwd | 0xffff0000ul;
	env[1] = (unsigned long)fxsave->swd | 0xffff0000ul;
	env[2] = twd_fxsr_to_i387(fxsave);
	env[3] = fxsave->fip;
	env[4] = fxsave->fcs | ((unsigned long)fxsave->fop << 16);
	env[5] = fxsave->foo;
	env[6] = fxsave->fos;

	if (__copy_to_user(buf, env, 7 * sizeof(unsigned long)))
		return 1;

	to = &buf->_st[0];
	from = (struct _fpxreg *) &fxsave->st_space[0];
	for (i = 0; i < 8; i++, to++, from++) {
		unsigned long __user *t = (unsigned long __user *)to;
		unsigned long *f = (unsigned long *)from;

		if (__put_user(*f, t) ||
				__put_user(*(f + 1), t + 1) ||
				__put_user(from->exponent, &to->exponent))
			return 1;
	}
	return 0;
}

static int convert_fxsr_from_user(struct user_fxsr_struct *fxsave,
				  struct _fpstate __user *buf)
{
	unsigned long env[7];
	struct _fpxreg *to;
	struct _fpreg __user *from;
	int i;

	if (copy_from_user( env, buf, 7 * sizeof(long)))
		return 1;

	fxsave->cwd = (unsigned short)(env[0] & 0xffff);
	fxsave->swd = (unsigned short)(env[1] & 0xffff);
	fxsave->twd = twd_i387_to_fxsr((unsigned short)(env[2] & 0xffff));
	fxsave->fip = env[3];
	fxsave->fop = (unsigned short)((env[4] & 0xffff0000ul) >> 16);
	fxsave->fcs = (env[4] & 0xffff);
	fxsave->foo = env[5];
	fxsave->fos = env[6];

	to = (struct _fpxreg *) &fxsave->st_space[0];
	from = &buf->_st[0];
	for (i = 0; i < 8; i++, to++, from++) {
		unsigned long *t = (unsigned long *)to;
		unsigned long __user *f = (unsigned long __user *)from;

		if (__get_user(*t, f) ||
		    __get_user(*(t + 1), f + 1) ||
		    __get_user(to->exponent, &from->exponent))
			return 1;
	}
	return 0;
}

extern int have_fpx_regs;

static int copy_sc_from_user(struct pt_regs *regs,
			     struct sigcontext __user *from)
{
	struct sigcontext sc;
	int err, pid;

	err = copy_from_user(&sc, from, sizeof(sc));
	if (err)
		return err;

	pid = userspace_pid[current_thread_info()->cpu];
	copy_sc(&regs->regs, &sc);
	if (have_fpx_regs) {
		struct user_fxsr_struct fpx;

		err = copy_from_user(&fpx,
			&((struct _fpstate __user *)sc.fpstate)->_fxsr_env[0],
				     sizeof(struct user_fxsr_struct));
		if (err)
			return 1;

		err = convert_fxsr_from_user(&fpx, sc.fpstate);
		if (err)
			return 1;

		err = restore_fpx_registers(pid, (unsigned long *) &fpx);
		if (err < 0) {
			printk(KERN_ERR "copy_sc_from_user - "
			       "restore_fpx_registers failed, errno = %d\n",
			       -err);
			return 1;
		}
	}
	else {
		struct user_i387_struct fp;

		err = copy_from_user(&fp, sc.fpstate,
				     sizeof(struct user_i387_struct));
		if (err)
			return 1;

		err = restore_fp_registers(pid, (unsigned long *) &fp);
		if (err < 0) {
			printk(KERN_ERR "copy_sc_from_user - "
			       "restore_fp_registers failed, errno = %d\n",
			       -err);
			return 1;
		}
	}

	return 0;
}

static int copy_sc_to_user(struct sigcontext __user *to,
			   struct _fpstate __user *to_fp, struct pt_regs *regs,
			   unsigned long sp)
{
	struct sigcontext sc;
	struct faultinfo * fi = &current->thread.arch.faultinfo;
	int err, pid;

	sc.gs = REGS_GS(regs->regs.gp);
	sc.fs = REGS_FS(regs->regs.gp);
	sc.es = REGS_ES(regs->regs.gp);
	sc.ds = REGS_DS(regs->regs.gp);
	sc.di = REGS_EDI(regs->regs.gp);
	sc.si = REGS_ESI(regs->regs.gp);
	sc.bp = REGS_EBP(regs->regs.gp);
	sc.sp = sp;
	sc.bx = REGS_EBX(regs->regs.gp);
	sc.dx = REGS_EDX(regs->regs.gp);
	sc.cx = REGS_ECX(regs->regs.gp);
	sc.ax = REGS_EAX(regs->regs.gp);
	sc.ip = REGS_IP(regs->regs.gp);
	sc.cs = REGS_CS(regs->regs.gp);
	sc.flags = REGS_EFLAGS(regs->regs.gp);
	sc.sp_at_signal = regs->regs.gp[UESP];
	sc.ss = regs->regs.gp[SS];
	sc.cr2 = fi->cr2;
	sc.err = fi->error_code;
	sc.trapno = fi->trap_no;

	to_fp = (to_fp ? to_fp : (struct _fpstate __user *) (to + 1));
	sc.fpstate = to_fp;

	pid = userspace_pid[current_thread_info()->cpu];
	if (have_fpx_regs) {
		struct user_fxsr_struct fpx;

		err = save_fpx_registers(pid, (unsigned long *) &fpx);
		if (err < 0){
			printk(KERN_ERR "copy_sc_to_user - save_fpx_registers "
			       "failed, errno = %d\n", err);
			return 1;
		}

		err = convert_fxsr_to_user(to_fp, &fpx);
		if (err)
			return 1;

		err |= __put_user(fpx.swd, &to_fp->status);
		err |= __put_user(X86_FXSR_MAGIC, &to_fp->magic);
		if (err)
			return 1;

		if (copy_to_user(&to_fp->_fxsr_env[0], &fpx,
				 sizeof(struct user_fxsr_struct)))
			return 1;
	}
	else {
		struct user_i387_struct fp;

		err = save_fp_registers(pid, (unsigned long *) &fp);
		if (copy_to_user(to_fp, &fp, sizeof(struct user_i387_struct)))
			return 1;
	}

	return copy_to_user(to, &sc, sizeof(sc));
}

static int copy_ucontext_to_user(struct ucontext __user *uc,
				 struct _fpstate __user *fp, sigset_t *set,
				 unsigned long sp)
{
	int err = 0;

	err |= put_user(current->sas_ss_sp, &uc->uc_stack.ss_sp);
	err |= put_user(sas_ss_flags(sp), &uc->uc_stack.ss_flags);
	err |= put_user(current->sas_ss_size, &uc->uc_stack.ss_size);
	err |= copy_sc_to_user(&uc->uc_mcontext, fp, &current->thread.regs, sp);
	err |= copy_to_user(&uc->uc_sigmask, set, sizeof(*set));
	return err;
}

struct sigframe
{
	char __user *pretcode;
	int sig;
	struct sigcontext sc;
	struct _fpstate fpstate;
	unsigned long extramask[_NSIG_WORDS-1];
	char retcode[8];
};

struct rt_sigframe
{
	char __user *pretcode;
	int sig;
	struct siginfo __user *pinfo;
	void __user *puc;
	struct siginfo info;
	struct ucontext uc;
	struct _fpstate fpstate;
	char retcode[8];
};

int setup_signal_stack_sc(unsigned long stack_top, int sig,
			  struct k_sigaction *ka, struct pt_regs *regs,
			  sigset_t *mask)
{
	struct sigframe __user *frame;
	void __user *restorer;
	unsigned long save_sp = PT_REGS_SP(regs);
	int err = 0;

	/* This is the same calculation as i386 - ((sp + 4) & 15) == 0 */
	stack_top = ((stack_top + 4) & -16UL) - 4;
	frame = (struct sigframe __user *) stack_top - 1;
	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		return 1;

	restorer = frame->retcode;
	if (ka->sa.sa_flags & SA_RESTORER)
		restorer = ka->sa.sa_restorer;

	/* Update SP now because the page fault handler refuses to extend
	 * the stack if the faulting address is too far below the current
	 * SP, which frame now certainly is.  If there's an error, the original
	 * value is restored on the way out.
	 * When writing the sigcontext to the stack, we have to write the
	 * original value, so that's passed to copy_sc_to_user, which does
	 * the right thing with it.
	 */
	PT_REGS_SP(regs) = (unsigned long) frame;

	err |= __put_user(restorer, &frame->pretcode);
	err |= __put_user(sig, &frame->sig);
	err |= copy_sc_to_user(&frame->sc, NULL, regs, save_sp);
	err |= __put_user(mask->sig[0], &frame->sc.oldmask);
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
		goto err;

	PT_REGS_SP(regs) = (unsigned long) frame;
	PT_REGS_IP(regs) = (unsigned long) ka->sa.sa_handler;
	PT_REGS_EAX(regs) = (unsigned long) sig;
	PT_REGS_EDX(regs) = (unsigned long) 0;
	PT_REGS_ECX(regs) = (unsigned long) 0;

	if ((current->ptrace & PT_DTRACE) && (current->ptrace & PT_PTRACED))
		ptrace_notify(SIGTRAP);
	return 0;

err:
	PT_REGS_SP(regs) = save_sp;
	return err;
}

int setup_signal_stack_si(unsigned long stack_top, int sig,
			  struct k_sigaction *ka, struct pt_regs *regs,
			  siginfo_t *info, sigset_t *mask)
{
	struct rt_sigframe __user *frame;
	void __user *restorer;
	unsigned long save_sp = PT_REGS_SP(regs);
	int err = 0;

	stack_top &= -8UL;
	frame = (struct rt_sigframe __user *) stack_top - 1;
	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		return 1;

	restorer = frame->retcode;
	if (ka->sa.sa_flags & SA_RESTORER)
		restorer = ka->sa.sa_restorer;

	/* See comment above about why this is here */
	PT_REGS_SP(regs) = (unsigned long) frame;

	err |= __put_user(restorer, &frame->pretcode);
	err |= __put_user(sig, &frame->sig);
	err |= __put_user(&frame->info, &frame->pinfo);
	err |= __put_user(&frame->uc, &frame->puc);
	err |= copy_siginfo_to_user(&frame->info, info);
	err |= copy_ucontext_to_user(&frame->uc, &frame->fpstate, mask,
				     save_sp);

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
		goto err;

	PT_REGS_IP(regs) = (unsigned long) ka->sa.sa_handler;
	PT_REGS_EAX(regs) = (unsigned long) sig;
	PT_REGS_EDX(regs) = (unsigned long) &frame->info;
	PT_REGS_ECX(regs) = (unsigned long) &frame->uc;

	if ((current->ptrace & PT_DTRACE) && (current->ptrace & PT_PTRACED))
		ptrace_notify(SIGTRAP);
	return 0;

err:
	PT_REGS_SP(regs) = save_sp;
	return err;
}

long sys_sigreturn(struct pt_regs regs)
{
	unsigned long sp = PT_REGS_SP(&current->thread.regs);
	struct sigframe __user *frame = (struct sigframe __user *)(sp - 8);
	sigset_t set;
	struct sigcontext __user *sc = &frame->sc;
	unsigned long __user *oldmask = &sc->oldmask;
	unsigned long __user *extramask = frame->extramask;
	int sig_size = (_NSIG_WORDS - 1) * sizeof(unsigned long);

	if (copy_from_user(&set.sig[0], oldmask, sizeof(set.sig[0])) ||
	    copy_from_user(&set.sig[1], extramask, sig_size))
		goto segfault;

	sigdelsetmask(&set, ~_BLOCKABLE);

	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	if (copy_sc_from_user(&current->thread.regs, sc))
		goto segfault;

	/* Avoid ERESTART handling */
	PT_REGS_SYSCALL_NR(&current->thread.regs) = -1;
	return PT_REGS_SYSCALL_RET(&current->thread.regs);

 segfault:
	force_sig(SIGSEGV, current);
	return 0;
}

long sys_rt_sigreturn(struct pt_regs regs)
{
	unsigned long sp = PT_REGS_SP(&current->thread.regs);
	struct rt_sigframe __user *frame =
		(struct rt_sigframe __user *) (sp - 4);
	sigset_t set;
	struct ucontext __user *uc = &frame->uc;
	int sig_size = _NSIG_WORDS * sizeof(unsigned long);

	if (copy_from_user(&set, &uc->uc_sigmask, sig_size))
		goto segfault;

	sigdelsetmask(&set, ~_BLOCKABLE);

	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	if (copy_sc_from_user(&current->thread.regs, &uc->uc_mcontext))
		goto segfault;

	/* Avoid ERESTART handling */
	PT_REGS_SYSCALL_NR(&current->thread.regs) = -1;
	return PT_REGS_SYSCALL_RET(&current->thread.regs);

 segfault:
	force_sig(SIGSEGV, current);
	return 0;
}
