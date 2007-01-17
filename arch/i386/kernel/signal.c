/*
 *  linux/arch/i386/kernel/signal.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  1997-11-28  Modified for POSIX.1b signals by Richard Henderson
 *  2000-06-20  Pentium III FXSR, SSE support by Gareth Hughes
 */

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/unistd.h>
#include <linux/stddef.h>
#include <linux/personality.h>
#include <linux/suspend.h>
#include <linux/ptrace.h>
#include <linux/elf.h>
#include <asm/processor.h>
#include <asm/ucontext.h>
#include <asm/uaccess.h>
#include <asm/i387.h>
#include "sigframe.h"

#define DEBUG_SIG 0

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

/*
 * Atomically swap in the new signal mask, and wait for a signal.
 */
asmlinkage int
sys_sigsuspend(int history0, int history1, old_sigset_t mask)
{
	mask &= _BLOCKABLE;
	spin_lock_irq(&current->sighand->siglock);
	current->saved_sigmask = current->blocked;
	siginitset(&current->blocked, mask);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	current->state = TASK_INTERRUPTIBLE;
	schedule();
	set_thread_flag(TIF_RESTORE_SIGMASK);
	return -ERESTARTNOHAND;
}

asmlinkage int 
sys_sigaction(int sig, const struct old_sigaction __user *act,
	      struct old_sigaction __user *oact)
{
	struct k_sigaction new_ka, old_ka;
	int ret;

	if (act) {
		old_sigset_t mask;
		if (!access_ok(VERIFY_READ, act, sizeof(*act)) ||
		    __get_user(new_ka.sa.sa_handler, &act->sa_handler) ||
		    __get_user(new_ka.sa.sa_restorer, &act->sa_restorer))
			return -EFAULT;
		__get_user(new_ka.sa.sa_flags, &act->sa_flags);
		__get_user(mask, &act->sa_mask);
		siginitset(&new_ka.sa.sa_mask, mask);
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		if (!access_ok(VERIFY_WRITE, oact, sizeof(*oact)) ||
		    __put_user(old_ka.sa.sa_handler, &oact->sa_handler) ||
		    __put_user(old_ka.sa.sa_restorer, &oact->sa_restorer))
			return -EFAULT;
		__put_user(old_ka.sa.sa_flags, &oact->sa_flags);
		__put_user(old_ka.sa.sa_mask.sig[0], &oact->sa_mask);
	}

	return ret;
}

asmlinkage int
sys_sigaltstack(unsigned long ebx)
{
	/* This is needed to make gcc realize it doesn't own the "struct pt_regs" */
	struct pt_regs *regs = (struct pt_regs *)&ebx;
	const stack_t __user *uss = (const stack_t __user *)ebx;
	stack_t __user *uoss = (stack_t __user *)regs->ecx;

	return do_sigaltstack(uss, uoss, regs->esp);
}


/*
 * Do a signal return; undo the signal stack.
 */

static int
restore_sigcontext(struct pt_regs *regs, struct sigcontext __user *sc, int *peax)
{
	unsigned int err = 0;

	/* Always make any pending restarted system calls return -EINTR */
	current_thread_info()->restart_block.fn = do_no_restart_syscall;

#define COPY(x)		err |= __get_user(regs->x, &sc->x)

#define COPY_SEG(seg)							\
	{ unsigned short tmp;						\
	  err |= __get_user(tmp, &sc->seg);				\
	  regs->x##seg = tmp; }

#define COPY_SEG_STRICT(seg)						\
	{ unsigned short tmp;						\
	  err |= __get_user(tmp, &sc->seg);				\
	  regs->x##seg = tmp|3; }

#define GET_SEG(seg)							\
	{ unsigned short tmp;						\
	  err |= __get_user(tmp, &sc->seg);				\
	  loadsegment(seg,tmp); }

#define	FIX_EFLAGS	(X86_EFLAGS_AC | X86_EFLAGS_RF |		 \
			 X86_EFLAGS_OF | X86_EFLAGS_DF |		 \
			 X86_EFLAGS_TF | X86_EFLAGS_SF | X86_EFLAGS_ZF | \
			 X86_EFLAGS_AF | X86_EFLAGS_PF | X86_EFLAGS_CF)

	COPY_SEG(gs);
	GET_SEG(fs);
	COPY_SEG(es);
	COPY_SEG(ds);
	COPY(edi);
	COPY(esi);
	COPY(ebp);
	COPY(esp);
	COPY(ebx);
	COPY(edx);
	COPY(ecx);
	COPY(eip);
	COPY_SEG_STRICT(cs);
	COPY_SEG_STRICT(ss);
	
	{
		unsigned int tmpflags;
		err |= __get_user(tmpflags, &sc->eflags);
		regs->eflags = (regs->eflags & ~FIX_EFLAGS) | (tmpflags & FIX_EFLAGS);
		regs->orig_eax = -1;		/* disable syscall checks */
	}

	{
		struct _fpstate __user * buf;
		err |= __get_user(buf, &sc->fpstate);
		if (buf) {
			if (!access_ok(VERIFY_READ, buf, sizeof(*buf)))
				goto badframe;
			err |= restore_i387(buf);
		} else {
			struct task_struct *me = current;
			if (used_math()) {
				clear_fpu(me);
				clear_used_math();
			}
		}
	}

	err |= __get_user(*peax, &sc->eax);
	return err;

badframe:
	return 1;
}

asmlinkage int sys_sigreturn(unsigned long __unused)
{
	struct pt_regs *regs = (struct pt_regs *) &__unused;
	struct sigframe __user *frame = (struct sigframe __user *)(regs->esp - 8);
	sigset_t set;
	int eax;

	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__get_user(set.sig[0], &frame->sc.oldmask)
	    || (_NSIG_WORDS > 1
		&& __copy_from_user(&set.sig[1], &frame->extramask,
				    sizeof(frame->extramask))))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
	
	if (restore_sigcontext(regs, &frame->sc, &eax))
		goto badframe;
	return eax;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}	

asmlinkage int sys_rt_sigreturn(unsigned long __unused)
{
	struct pt_regs *regs = (struct pt_regs *) &__unused;
	struct rt_sigframe __user *frame = (struct rt_sigframe __user *)(regs->esp - 4);
	sigset_t set;
	int eax;

	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
	
	if (restore_sigcontext(regs, &frame->uc.uc_mcontext, &eax))
		goto badframe;

	if (do_sigaltstack(&frame->uc.uc_stack, NULL, regs->esp) == -EFAULT)
		goto badframe;

	return eax;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}	

/*
 * Set up a signal frame.
 */

static int
setup_sigcontext(struct sigcontext __user *sc, struct _fpstate __user *fpstate,
		 struct pt_regs *regs, unsigned long mask)
{
	int tmp, err = 0;

	err |= __put_user(regs->xgs, (unsigned int __user *)&sc->gs);
	savesegment(fs, tmp);
	err |= __put_user(tmp, (unsigned int __user *)&sc->fs);

	err |= __put_user(regs->xes, (unsigned int __user *)&sc->es);
	err |= __put_user(regs->xds, (unsigned int __user *)&sc->ds);
	err |= __put_user(regs->edi, &sc->edi);
	err |= __put_user(regs->esi, &sc->esi);
	err |= __put_user(regs->ebp, &sc->ebp);
	err |= __put_user(regs->esp, &sc->esp);
	err |= __put_user(regs->ebx, &sc->ebx);
	err |= __put_user(regs->edx, &sc->edx);
	err |= __put_user(regs->ecx, &sc->ecx);
	err |= __put_user(regs->eax, &sc->eax);
	err |= __put_user(current->thread.trap_no, &sc->trapno);
	err |= __put_user(current->thread.error_code, &sc->err);
	err |= __put_user(regs->eip, &sc->eip);
	err |= __put_user(regs->xcs, (unsigned int __user *)&sc->cs);
	err |= __put_user(regs->eflags, &sc->eflags);
	err |= __put_user(regs->esp, &sc->esp_at_signal);
	err |= __put_user(regs->xss, (unsigned int __user *)&sc->ss);

	tmp = save_i387(fpstate);
	if (tmp < 0)
	  err = 1;
	else
	  err |= __put_user(tmp ? fpstate : NULL, &sc->fpstate);

	/* non-iBCS2 extensions.. */
	err |= __put_user(mask, &sc->oldmask);
	err |= __put_user(current->thread.cr2, &sc->cr2);

	return err;
}

/*
 * Determine which stack to use..
 */
static inline void __user *
get_sigframe(struct k_sigaction *ka, struct pt_regs * regs, size_t frame_size)
{
	unsigned long esp;

	/* Default to using normal stack */
	esp = regs->esp;

	/* This is the X/Open sanctioned signal stack switching.  */
	if (ka->sa.sa_flags & SA_ONSTACK) {
		if (sas_ss_flags(esp) == 0)
			esp = current->sas_ss_sp + current->sas_ss_size;
	}

	/* This is the legacy signal stack switching. */
	else if ((regs->xss & 0xffff) != __USER_DS &&
		 !(ka->sa.sa_flags & SA_RESTORER) &&
		 ka->sa.sa_restorer) {
		esp = (unsigned long) ka->sa.sa_restorer;
	}

	esp -= frame_size;
	/* Align the stack pointer according to the i386 ABI,
	 * i.e. so that on function entry ((sp + 4) & 15) == 0. */
	esp = ((esp + 4) & -16ul) - 4;
	return (void __user *) esp;
}

/* These symbols are defined with the addresses in the vsyscall page.
   See vsyscall-sigreturn.S.  */
extern void __user __kernel_sigreturn;
extern void __user __kernel_rt_sigreturn;

static int setup_frame(int sig, struct k_sigaction *ka,
		       sigset_t *set, struct pt_regs * regs)
{
	void __user *restorer;
	struct sigframe __user *frame;
	int err = 0;
	int usig;

	frame = get_sigframe(ka, regs, sizeof(*frame));

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto give_sigsegv;

	usig = current_thread_info()->exec_domain
		&& current_thread_info()->exec_domain->signal_invmap
		&& sig < 32
		? current_thread_info()->exec_domain->signal_invmap[sig]
		: sig;

	err = __put_user(usig, &frame->sig);
	if (err)
		goto give_sigsegv;

	err = setup_sigcontext(&frame->sc, &frame->fpstate, regs, set->sig[0]);
	if (err)
		goto give_sigsegv;

	if (_NSIG_WORDS > 1) {
		err = __copy_to_user(&frame->extramask, &set->sig[1],
				      sizeof(frame->extramask));
		if (err)
			goto give_sigsegv;
	}

	restorer = (void *)VDSO_SYM(&__kernel_sigreturn);
	if (ka->sa.sa_flags & SA_RESTORER)
		restorer = ka->sa.sa_restorer;

	/* Set up to return from userspace.  */
	err |= __put_user(restorer, &frame->pretcode);
	 
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
		goto give_sigsegv;

	/* Set up registers for signal handler */
	regs->esp = (unsigned long) frame;
	regs->eip = (unsigned long) ka->sa.sa_handler;
	regs->eax = (unsigned long) sig;
	regs->edx = (unsigned long) 0;
	regs->ecx = (unsigned long) 0;

	set_fs(USER_DS);
	regs->xds = __USER_DS;
	regs->xes = __USER_DS;
	regs->xss = __USER_DS;
	regs->xcs = __USER_CS;

	/*
	 * Clear TF when entering the signal handler, but
	 * notify any tracer that was single-stepping it.
	 * The tracer may want to single-step inside the
	 * handler too.
	 */
	regs->eflags &= ~TF_MASK;
	if (test_thread_flag(TIF_SINGLESTEP))
		ptrace_notify(SIGTRAP);

#if DEBUG_SIG
	printk("SIG deliver (%s:%d): sp=%p pc=%p ra=%p\n",
		current->comm, current->pid, frame, regs->eip, frame->pretcode);
#endif

	return 0;

give_sigsegv:
	force_sigsegv(sig, current);
	return -EFAULT;
}

static int setup_rt_frame(int sig, struct k_sigaction *ka, siginfo_t *info,
			   sigset_t *set, struct pt_regs * regs)
{
	void __user *restorer;
	struct rt_sigframe __user *frame;
	int err = 0;
	int usig;

	frame = get_sigframe(ka, regs, sizeof(*frame));

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto give_sigsegv;

	usig = current_thread_info()->exec_domain
		&& current_thread_info()->exec_domain->signal_invmap
		&& sig < 32
		? current_thread_info()->exec_domain->signal_invmap[sig]
		: sig;

	err |= __put_user(usig, &frame->sig);
	err |= __put_user(&frame->info, &frame->pinfo);
	err |= __put_user(&frame->uc, &frame->puc);
	err |= copy_siginfo_to_user(&frame->info, info);
	if (err)
		goto give_sigsegv;

	/* Create the ucontext.  */
	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(0, &frame->uc.uc_link);
	err |= __put_user(current->sas_ss_sp, &frame->uc.uc_stack.ss_sp);
	err |= __put_user(sas_ss_flags(regs->esp),
			  &frame->uc.uc_stack.ss_flags);
	err |= __put_user(current->sas_ss_size, &frame->uc.uc_stack.ss_size);
	err |= setup_sigcontext(&frame->uc.uc_mcontext, &frame->fpstate,
			        regs, set->sig[0]);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));
	if (err)
		goto give_sigsegv;

	/* Set up to return from userspace.  */
	restorer = (void *)VDSO_SYM(&__kernel_rt_sigreturn);
	if (ka->sa.sa_flags & SA_RESTORER)
		restorer = ka->sa.sa_restorer;
	err |= __put_user(restorer, &frame->pretcode);
	 
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
		goto give_sigsegv;

	/* Set up registers for signal handler */
	regs->esp = (unsigned long) frame;
	regs->eip = (unsigned long) ka->sa.sa_handler;
	regs->eax = (unsigned long) usig;
	regs->edx = (unsigned long) &frame->info;
	regs->ecx = (unsigned long) &frame->uc;

	set_fs(USER_DS);
	regs->xds = __USER_DS;
	regs->xes = __USER_DS;
	regs->xss = __USER_DS;
	regs->xcs = __USER_CS;

	/*
	 * Clear TF when entering the signal handler, but
	 * notify any tracer that was single-stepping it.
	 * The tracer may want to single-step inside the
	 * handler too.
	 */
	regs->eflags &= ~TF_MASK;
	if (test_thread_flag(TIF_SINGLESTEP))
		ptrace_notify(SIGTRAP);

#if DEBUG_SIG
	printk("SIG deliver (%s:%d): sp=%p pc=%p ra=%p\n",
		current->comm, current->pid, frame, regs->eip, frame->pretcode);
#endif

	return 0;

give_sigsegv:
	force_sigsegv(sig, current);
	return -EFAULT;
}

/*
 * OK, we're invoking a handler
 */	

static int
handle_signal(unsigned long sig, siginfo_t *info, struct k_sigaction *ka,
	      sigset_t *oldset,	struct pt_regs * regs)
{
	int ret;

	/* Are we from a system call? */
	if (regs->orig_eax >= 0) {
		/* If so, check system call restarting.. */
		switch (regs->eax) {
		        case -ERESTART_RESTARTBLOCK:
			case -ERESTARTNOHAND:
				regs->eax = -EINTR;
				break;

			case -ERESTARTSYS:
				if (!(ka->sa.sa_flags & SA_RESTART)) {
					regs->eax = -EINTR;
					break;
				}
			/* fallthrough */
			case -ERESTARTNOINTR:
				regs->eax = regs->orig_eax;
				regs->eip -= 2;
		}
	}

	/*
	 * If TF is set due to a debugger (PT_DTRACE), clear the TF flag so
	 * that register information in the sigcontext is correct.
	 */
	if (unlikely(regs->eflags & TF_MASK)
	    && likely(current->ptrace & PT_DTRACE)) {
		current->ptrace &= ~PT_DTRACE;
		regs->eflags &= ~TF_MASK;
	}

	/* Set up the stack frame */
	if (ka->sa.sa_flags & SA_SIGINFO)
		ret = setup_rt_frame(sig, ka, info, oldset, regs);
	else
		ret = setup_frame(sig, ka, oldset, regs);

	if (ret == 0) {
		spin_lock_irq(&current->sighand->siglock);
		sigorsets(&current->blocked,&current->blocked,&ka->sa.sa_mask);
		if (!(ka->sa.sa_flags & SA_NODEFER))
			sigaddset(&current->blocked,sig);
		recalc_sigpending();
		spin_unlock_irq(&current->sighand->siglock);
	}

	return ret;
}

/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 */
static void fastcall do_signal(struct pt_regs *regs)
{
	siginfo_t info;
	int signr;
	struct k_sigaction ka;
	sigset_t *oldset;

	/*
	 * We want the common case to go fast, which
	 * is why we may in certain cases get here from
	 * kernel mode. Just return without doing anything
 	 * if so.  vm86 regs switched out by assembly code
 	 * before reaching here, so testing against kernel
 	 * CS suffices.
	 */
	if (!user_mode(regs))
		return;

	if (test_thread_flag(TIF_RESTORE_SIGMASK))
		oldset = &current->saved_sigmask;
	else
		oldset = &current->blocked;

	signr = get_signal_to_deliver(&info, &ka, regs, NULL);
	if (signr > 0) {
		/* Reenable any watchpoints before delivering the
		 * signal to user space. The processor register will
		 * have been cleared if the watchpoint triggered
		 * inside the kernel.
		 */
		if (unlikely(current->thread.debugreg[7]))
			set_debugreg(current->thread.debugreg[7], 7);

		/* Whee!  Actually deliver the signal.  */
		if (handle_signal(signr, &info, &ka, oldset, regs) == 0) {
			/* a signal was successfully delivered; the saved
			 * sigmask will have been stored in the signal frame,
			 * and will be restored by sigreturn, so we can simply
			 * clear the TIF_RESTORE_SIGMASK flag */
			if (test_thread_flag(TIF_RESTORE_SIGMASK))
				clear_thread_flag(TIF_RESTORE_SIGMASK);
		}

		return;
	}

	/* Did we come from a system call? */
	if (regs->orig_eax >= 0) {
		/* Restart the system call - no handlers present */
		switch (regs->eax) {
		case -ERESTARTNOHAND:
		case -ERESTARTSYS:
		case -ERESTARTNOINTR:
			regs->eax = regs->orig_eax;
			regs->eip -= 2;
			break;

		case -ERESTART_RESTARTBLOCK:
			regs->eax = __NR_restart_syscall;
			regs->eip -= 2;
			break;
		}
	}

	/* if there's no signal to deliver, we just put the saved sigmask
	 * back */
	if (test_thread_flag(TIF_RESTORE_SIGMASK)) {
		clear_thread_flag(TIF_RESTORE_SIGMASK);
		sigprocmask(SIG_SETMASK, &current->saved_sigmask, NULL);
	}
}

/*
 * notification of userspace execution resumption
 * - triggered by the TIF_WORK_MASK flags
 */
__attribute__((regparm(3)))
void do_notify_resume(struct pt_regs *regs, void *_unused,
		      __u32 thread_info_flags)
{
	/* Pending single-step? */
	if (thread_info_flags & _TIF_SINGLESTEP) {
		regs->eflags |= TF_MASK;
		clear_thread_flag(TIF_SINGLESTEP);
	}

	/* deal with pending signal delivery */
	if (thread_info_flags & (_TIF_SIGPENDING | _TIF_RESTORE_SIGMASK))
		do_signal(regs);
	
	clear_thread_flag(TIF_IRET);
}
