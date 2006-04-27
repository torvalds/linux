/*
 * Copyright (C) 2004 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include "linux/signal.h"
#include "linux/ptrace.h"
#include "asm/current.h"
#include "asm/ucontext.h"
#include "asm/uaccess.h"
#include "asm/unistd.h"
#include "frame_kern.h"
#include "sigcontext.h"
#include "registers.h"
#include "mode.h"

#ifdef CONFIG_MODE_SKAS

#include "skas.h"

static int copy_sc_from_user_skas(struct pt_regs *regs,
				  struct sigcontext __user *from)
{
  	struct sigcontext sc;
	unsigned long fpregs[HOST_FP_SIZE];
	int err;

	err = copy_from_user(&sc, from, sizeof(sc));
	err |= copy_from_user(fpregs, sc.fpstate, sizeof(fpregs));
	if(err)
		return(err);

	REGS_GS(regs->regs.skas.regs) = sc.gs;
	REGS_FS(regs->regs.skas.regs) = sc.fs;
	REGS_ES(regs->regs.skas.regs) = sc.es;
	REGS_DS(regs->regs.skas.regs) = sc.ds;
	REGS_EDI(regs->regs.skas.regs) = sc.edi;
	REGS_ESI(regs->regs.skas.regs) = sc.esi;
	REGS_EBP(regs->regs.skas.regs) = sc.ebp;
	REGS_SP(regs->regs.skas.regs) = sc.esp;
	REGS_EBX(regs->regs.skas.regs) = sc.ebx;
	REGS_EDX(regs->regs.skas.regs) = sc.edx;
	REGS_ECX(regs->regs.skas.regs) = sc.ecx;
	REGS_EAX(regs->regs.skas.regs) = sc.eax;
	REGS_IP(regs->regs.skas.regs) = sc.eip;
	REGS_CS(regs->regs.skas.regs) = sc.cs;
	REGS_EFLAGS(regs->regs.skas.regs) = sc.eflags;
	REGS_SS(regs->regs.skas.regs) = sc.ss;

	err = restore_fp_registers(userspace_pid[0], fpregs);
	if(err < 0){
	  	printk("copy_sc_from_user_skas - PTRACE_SETFPREGS failed, "
		       "errno = %d\n", err);
		return(1);
	}

	return(0);
}

int copy_sc_to_user_skas(struct sigcontext __user *to, struct _fpstate __user *to_fp,
                         struct pt_regs *regs, unsigned long sp)
{
  	struct sigcontext sc;
	unsigned long fpregs[HOST_FP_SIZE];
	struct faultinfo * fi = &current->thread.arch.faultinfo;
	int err;

	sc.gs = REGS_GS(regs->regs.skas.regs);
	sc.fs = REGS_FS(regs->regs.skas.regs);
	sc.es = REGS_ES(regs->regs.skas.regs);
	sc.ds = REGS_DS(regs->regs.skas.regs);
	sc.edi = REGS_EDI(regs->regs.skas.regs);
	sc.esi = REGS_ESI(regs->regs.skas.regs);
	sc.ebp = REGS_EBP(regs->regs.skas.regs);
	sc.esp = sp;
	sc.ebx = REGS_EBX(regs->regs.skas.regs);
	sc.edx = REGS_EDX(regs->regs.skas.regs);
	sc.ecx = REGS_ECX(regs->regs.skas.regs);
	sc.eax = REGS_EAX(regs->regs.skas.regs);
	sc.eip = REGS_IP(regs->regs.skas.regs);
	sc.cs = REGS_CS(regs->regs.skas.regs);
	sc.eflags = REGS_EFLAGS(regs->regs.skas.regs);
	sc.esp_at_signal = regs->regs.skas.regs[UESP];
	sc.ss = regs->regs.skas.regs[SS];
        sc.cr2 = fi->cr2;
        sc.err = fi->error_code;
        sc.trapno = fi->trap_no;

	err = save_fp_registers(userspace_pid[0], fpregs);
	if(err < 0){
	  	printk("copy_sc_to_user_skas - PTRACE_GETFPREGS failed, "
		       "errno = %d\n", err);
		return(1);
	}
	to_fp = (to_fp ? to_fp : (struct _fpstate __user *) (to + 1));
	sc.fpstate = to_fp;

	if(err)
	  	return(err);

	return(copy_to_user(to, &sc, sizeof(sc)) ||
	       copy_to_user(to_fp, fpregs, sizeof(fpregs)));
}
#endif

#ifdef CONFIG_MODE_TT

/* These copy a sigcontext to/from userspace.  They copy the fpstate pointer,
 * blowing away the old, good one.  So, that value is saved, and then restored
 * after the sigcontext copy.  In copy_from, the variable holding the saved
 * fpstate pointer, and the sigcontext that it should be restored to are both
 * in the kernel, so we can just restore using an assignment.  In copy_to, the
 * saved pointer is in the kernel, but the sigcontext is in userspace, so we
 * copy_to_user it.
 */
int copy_sc_from_user_tt(struct sigcontext *to, struct sigcontext __user *from,
			 int fpsize)
{
	struct _fpstate *to_fp;
	struct _fpstate __user *from_fp;
	unsigned long sigs;
	int err;

	to_fp = to->fpstate;
	sigs = to->oldmask;
	err = copy_from_user(to, from, sizeof(*to));
	from_fp = to->fpstate;
	to->oldmask = sigs;
	to->fpstate = to_fp;
	if(to_fp != NULL)
		err |= copy_from_user(to_fp, from_fp, fpsize);
	return(err);
}

int copy_sc_to_user_tt(struct sigcontext __user *to, struct _fpstate __user *fp,
		       struct sigcontext *from, int fpsize, unsigned long sp)
{
	struct _fpstate __user *to_fp;
	struct _fpstate *from_fp;
	int err;

	to_fp =	(fp ? fp : (struct _fpstate __user *) (to + 1));
	from_fp = from->fpstate;
	err = copy_to_user(to, from, sizeof(*to));

	/* The SP in the sigcontext is the updated one for the signal
	 * delivery.  The sp passed in is the original, and this needs
	 * to be restored, so we stick it in separately.
	 */
	err |= copy_to_user(&SC_SP(to), &sp, sizeof(sp));

	if(from_fp != NULL){
		err |= copy_to_user(&to->fpstate, &to_fp, sizeof(to->fpstate));
		err |= copy_to_user(to_fp, from_fp, fpsize);
	}
	return err;
}
#endif

static int copy_sc_from_user(struct pt_regs *to, void __user *from)
{
	int ret;

	ret = CHOOSE_MODE(copy_sc_from_user_tt(UPT_SC(&to->regs), from,
					       sizeof(struct _fpstate)),
			  copy_sc_from_user_skas(to, from));
	return(ret);
}

static int copy_sc_to_user(struct sigcontext __user *to, struct _fpstate __user *fp,
			   struct pt_regs *from, unsigned long sp)
{
	return(CHOOSE_MODE(copy_sc_to_user_tt(to, fp, UPT_SC(&from->regs),
					      sizeof(*fp), sp),
                           copy_sc_to_user_skas(to, fp, from, sp)));
}

static int copy_ucontext_to_user(struct ucontext __user *uc, struct _fpstate __user *fp,
				 sigset_t *set, unsigned long sp)
{
	int err = 0;

	err |= put_user(current->sas_ss_sp, &uc->uc_stack.ss_sp);
	err |= put_user(sas_ss_flags(sp), &uc->uc_stack.ss_flags);
	err |= put_user(current->sas_ss_size, &uc->uc_stack.ss_size);
	err |= copy_sc_to_user(&uc->uc_mcontext, fp, &current->thread.regs, sp);
	err |= copy_to_user(&uc->uc_sigmask, set, sizeof(*set));
	return(err);
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

	stack_top &= -8UL;
	frame = (struct sigframe __user *) stack_top - 1;
	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		return 1;

	restorer = frame->retcode;
	if(ka->sa.sa_flags & SA_RESTORER)
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

	if(err)
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
	if(ka->sa.sa_flags & SA_RESTORER)
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

	if(err)
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

	if(copy_from_user(&set.sig[0], oldmask, sizeof(set.sig[0])) ||
	   copy_from_user(&set.sig[1], extramask, sig_size))
		goto segfault;

	sigdelsetmask(&set, ~_BLOCKABLE);

	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	if(copy_sc_from_user(&current->thread.regs, sc))
		goto segfault;

	/* Avoid ERESTART handling */
	PT_REGS_SYSCALL_NR(&current->thread.regs) = -1;
	return(PT_REGS_SYSCALL_RET(&current->thread.regs));

 segfault:
	force_sig(SIGSEGV, current);
	return 0;
}

long sys_rt_sigreturn(struct pt_regs regs)
{
	unsigned long sp = PT_REGS_SP(&current->thread.regs);
	struct rt_sigframe __user *frame = (struct rt_sigframe __user *) (sp - 4);
	sigset_t set;
	struct ucontext __user *uc = &frame->uc;
	int sig_size = _NSIG_WORDS * sizeof(unsigned long);

	if(copy_from_user(&set, &uc->uc_sigmask, sig_size))
		goto segfault;

	sigdelsetmask(&set, ~_BLOCKABLE);

	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	if(copy_sc_from_user(&current->thread.regs, &uc->uc_mcontext))
		goto segfault;

	/* Avoid ERESTART handling */
	PT_REGS_SYSCALL_NR(&current->thread.regs) = -1;
	return(PT_REGS_SYSCALL_RET(&current->thread.regs));

 segfault:
	force_sig(SIGSEGV, current);
	return 0;
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
