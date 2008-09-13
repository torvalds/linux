/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  1997-11-28  Modified for POSIX.1b signals by Richard Henderson
 *  2000-06-20  Pentium III FXSR, SSE support by Gareth Hughes
 */
#include <linux/list.h>

#include <linux/personality.h>
#include <linux/binfmts.h>
#include <linux/suspend.h>
#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/signal.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/tracehook.h>
#include <linux/elf.h>
#include <linux/smp.h>
#include <linux/mm.h>

#include <asm/processor.h>
#include <asm/ucontext.h>
#include <asm/uaccess.h>
#include <asm/i387.h>
#include <asm/vdso.h>
#include <asm/syscall.h>
#include <asm/syscalls.h>

#include "sigframe.h"

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

#define __FIX_EFLAGS	(X86_EFLAGS_AC | X86_EFLAGS_OF | \
			 X86_EFLAGS_DF | X86_EFLAGS_TF | X86_EFLAGS_SF | \
			 X86_EFLAGS_ZF | X86_EFLAGS_AF | X86_EFLAGS_PF | \
			 X86_EFLAGS_CF)

#ifdef CONFIG_X86_32
# define FIX_EFLAGS	(__FIX_EFLAGS | X86_EFLAGS_RF)
#else
# define FIX_EFLAGS	__FIX_EFLAGS
#endif

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
	set_restore_sigmask();

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

asmlinkage int sys_sigaltstack(unsigned long bx)
{
	/*
	 * This is needed to make gcc realize it doesn't own the
	 * "struct pt_regs"
	 */
	struct pt_regs *regs = (struct pt_regs *)&bx;
	const stack_t __user *uss = (const stack_t __user *)bx;
	stack_t __user *uoss = (stack_t __user *)regs->cx;

	return do_sigaltstack(uss, uoss, regs->sp);
}


/*
 * Do a signal return; undo the signal stack.
 */
static int
restore_sigcontext(struct pt_regs *regs, struct sigcontext __user *sc,
		   unsigned long *pax)
{
	unsigned int err = 0;

	/* Always make any pending restarted system calls return -EINTR */
	current_thread_info()->restart_block.fn = do_no_restart_syscall;

#define COPY(x)		err |= __get_user(regs->x, &sc->x)

#define COPY_SEG(seg)							\
	{ unsigned short tmp;						\
	  err |= __get_user(tmp, &sc->seg);				\
	  regs->seg = tmp; }

#define COPY_SEG_STRICT(seg)						\
	{ unsigned short tmp;						\
	  err |= __get_user(tmp, &sc->seg);				\
	  regs->seg = tmp|3; }

#define GET_SEG(seg)							\
	{ unsigned short tmp;						\
	  err |= __get_user(tmp, &sc->seg);				\
	  loadsegment(seg, tmp); }

	GET_SEG(gs);
	COPY_SEG(fs);
	COPY_SEG(es);
	COPY_SEG(ds);
	COPY(di); COPY(si); COPY(bp); COPY(sp); COPY(bx);
	COPY(dx); COPY(cx); COPY(ip);
	COPY_SEG_STRICT(cs);
	COPY_SEG_STRICT(ss);

	{
		unsigned int tmpflags;

		err |= __get_user(tmpflags, &sc->flags);
		regs->flags = (regs->flags & ~FIX_EFLAGS) |
						(tmpflags & FIX_EFLAGS);
		regs->orig_ax = -1;		/* disable syscall checks */
	}

	{
		void __user *buf;

		err |= __get_user(buf, &sc->fpstate);
		err |= restore_i387_xstate(buf);
	}

	err |= __get_user(*pax, &sc->ax);
	return err;
}

asmlinkage unsigned long sys_sigreturn(unsigned long __unused)
{
	struct sigframe __user *frame;
	struct pt_regs *regs;
	unsigned long ax;
	sigset_t set;

	regs = (struct pt_regs *) &__unused;
	frame = (struct sigframe __user *)(regs->sp - 8);

	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__get_user(set.sig[0], &frame->sc.oldmask) || (_NSIG_WORDS > 1
		&& __copy_from_user(&set.sig[1], &frame->extramask,
				    sizeof(frame->extramask))))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	if (restore_sigcontext(regs, &frame->sc, &ax))
		goto badframe;
	return ax;

badframe:
	if (show_unhandled_signals && printk_ratelimit()) {
		printk("%s%s[%d] bad frame in sigreturn frame:"
			"%p ip:%lx sp:%lx oeax:%lx",
		    task_pid_nr(current) > 1 ? KERN_INFO : KERN_EMERG,
		    current->comm, task_pid_nr(current), frame, regs->ip,
		    regs->sp, regs->orig_ax);
		print_vma_addr(" in ", regs->ip);
		printk(KERN_CONT "\n");
	}

	force_sig(SIGSEGV, current);

	return 0;
}

asmlinkage int sys_rt_sigreturn(unsigned long __unused)
{
	struct pt_regs *regs = (struct pt_regs *)&__unused;
	struct rt_sigframe __user *frame;
	unsigned long ax;
	sigset_t set;

	frame = (struct rt_sigframe __user *)(regs->sp - sizeof(long));
	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	if (restore_sigcontext(regs, &frame->uc.uc_mcontext, &ax))
		goto badframe;

	if (do_sigaltstack(&frame->uc.uc_stack, NULL, regs->sp) == -EFAULT)
		goto badframe;

	return ax;

badframe:
	signal_fault(regs, frame, "rt sigreturn");
	return 0;
}

/*
 * Set up a signal frame.
 */
static int
setup_sigcontext(struct sigcontext __user *sc, void __user *fpstate,
		 struct pt_regs *regs, unsigned long mask)
{
	int tmp, err = 0;

	err |= __put_user(regs->fs, (unsigned int __user *)&sc->fs);
	savesegment(gs, tmp);
	err |= __put_user(tmp, (unsigned int __user *)&sc->gs);

	err |= __put_user(regs->es, (unsigned int __user *)&sc->es);
	err |= __put_user(regs->ds, (unsigned int __user *)&sc->ds);
	err |= __put_user(regs->di, &sc->di);
	err |= __put_user(regs->si, &sc->si);
	err |= __put_user(regs->bp, &sc->bp);
	err |= __put_user(regs->sp, &sc->sp);
	err |= __put_user(regs->bx, &sc->bx);
	err |= __put_user(regs->dx, &sc->dx);
	err |= __put_user(regs->cx, &sc->cx);
	err |= __put_user(regs->ax, &sc->ax);
	err |= __put_user(current->thread.trap_no, &sc->trapno);
	err |= __put_user(current->thread.error_code, &sc->err);
	err |= __put_user(regs->ip, &sc->ip);
	err |= __put_user(regs->cs, (unsigned int __user *)&sc->cs);
	err |= __put_user(regs->flags, &sc->flags);
	err |= __put_user(regs->sp, &sc->sp_at_signal);
	err |= __put_user(regs->ss, (unsigned int __user *)&sc->ss);

	tmp = save_i387_xstate(fpstate);
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
get_sigframe(struct k_sigaction *ka, struct pt_regs *regs, size_t frame_size,
	     void **fpstate)
{
	unsigned long sp;

	/* Default to using normal stack */
	sp = regs->sp;

	/*
	 * If we are on the alternate signal stack and would overflow it, don't.
	 * Return an always-bogus address instead so we will die with SIGSEGV.
	 */
	if (on_sig_stack(sp) && !likely(on_sig_stack(sp - frame_size)))
		return (void __user *) -1L;

	/* This is the X/Open sanctioned signal stack switching.  */
	if (ka->sa.sa_flags & SA_ONSTACK) {
		if (sas_ss_flags(sp) == 0)
			sp = current->sas_ss_sp + current->sas_ss_size;
	} else {
		/* This is the legacy signal stack switching. */
		if ((regs->ss & 0xffff) != __USER_DS &&
			!(ka->sa.sa_flags & SA_RESTORER) &&
				ka->sa.sa_restorer)
			sp = (unsigned long) ka->sa.sa_restorer;
	}

	if (used_math()) {
		sp = sp - sig_xstate_size;
		*fpstate = (struct _fpstate *) sp;
	}

	sp -= frame_size;
	/*
	 * Align the stack pointer according to the i386 ABI,
	 * i.e. so that on function entry ((sp + 4) & 15) == 0.
	 */
	sp = ((sp + 4) & -16ul) - 4;

	return (void __user *) sp;
}

static int
__setup_frame(int sig, struct k_sigaction *ka, sigset_t *set,
	      struct pt_regs *regs)
{
	struct sigframe __user *frame;
	void __user *restorer;
	int err = 0;
	void __user *fpstate = NULL;

	frame = get_sigframe(ka, regs, sizeof(*frame), &fpstate);

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		return -EFAULT;

	err = __put_user(sig, &frame->sig);
	if (err)
		return -EFAULT;

	err = setup_sigcontext(&frame->sc, fpstate, regs, set->sig[0]);
	if (err)
		return -EFAULT;

	if (_NSIG_WORDS > 1) {
		err = __copy_to_user(&frame->extramask, &set->sig[1],
				      sizeof(frame->extramask));
		if (err)
			return -EFAULT;
	}

	if (current->mm->context.vdso)
		restorer = VDSO32_SYMBOL(current->mm->context.vdso, sigreturn);
	else
		restorer = &frame->retcode;
	if (ka->sa.sa_flags & SA_RESTORER)
		restorer = ka->sa.sa_restorer;

	/* Set up to return from userspace.  */
	err |= __put_user(restorer, &frame->pretcode);

	/*
	 * This is popl %eax ; movl $__NR_sigreturn, %eax ; int $0x80
	 *
	 * WE DO NOT USE IT ANY MORE! It's only left here for historical
	 * reasons and because gdb uses it as a signature to notice
	 * signal handler stack frames.
	 */
	err |= __put_user(0xb858, (short __user *)(frame->retcode+0));
	err |= __put_user(__NR_sigreturn, (int __user *)(frame->retcode+2));
	err |= __put_user(0x80cd, (short __user *)(frame->retcode+6));

	if (err)
		return -EFAULT;

	/* Set up registers for signal handler */
	regs->sp = (unsigned long)frame;
	regs->ip = (unsigned long)ka->sa.sa_handler;
	regs->ax = (unsigned long)sig;
	regs->dx = 0;
	regs->cx = 0;

	regs->ds = __USER_DS;
	regs->es = __USER_DS;
	regs->ss = __USER_DS;
	regs->cs = __USER_CS;

	return 0;
}

static int __setup_rt_frame(int sig, struct k_sigaction *ka, siginfo_t *info,
			    sigset_t *set, struct pt_regs *regs)
{
	struct rt_sigframe __user *frame;
	void __user *restorer;
	int err = 0;
	void __user *fpstate = NULL;

	frame = get_sigframe(ka, regs, sizeof(*frame), &fpstate);

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		return -EFAULT;

	err |= __put_user(sig, &frame->sig);
	err |= __put_user(&frame->info, &frame->pinfo);
	err |= __put_user(&frame->uc, &frame->puc);
	err |= copy_siginfo_to_user(&frame->info, info);
	if (err)
		return -EFAULT;

	/* Create the ucontext.  */
	if (cpu_has_xsave)
		err |= __put_user(UC_FP_XSTATE, &frame->uc.uc_flags);
	else
		err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(0, &frame->uc.uc_link);
	err |= __put_user(current->sas_ss_sp, &frame->uc.uc_stack.ss_sp);
	err |= __put_user(sas_ss_flags(regs->sp),
			  &frame->uc.uc_stack.ss_flags);
	err |= __put_user(current->sas_ss_size, &frame->uc.uc_stack.ss_size);
	err |= setup_sigcontext(&frame->uc.uc_mcontext, fpstate,
				regs, set->sig[0]);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));
	if (err)
		return -EFAULT;

	/* Set up to return from userspace.  */
	restorer = VDSO32_SYMBOL(current->mm->context.vdso, rt_sigreturn);
	if (ka->sa.sa_flags & SA_RESTORER)
		restorer = ka->sa.sa_restorer;
	err |= __put_user(restorer, &frame->pretcode);

	/*
	 * This is movl $__NR_rt_sigreturn, %ax ; int $0x80
	 *
	 * WE DO NOT USE IT ANY MORE! It's only left here for historical
	 * reasons and because gdb uses it as a signature to notice
	 * signal handler stack frames.
	 */
	err |= __put_user(0xb8, (char __user *)(frame->retcode+0));
	err |= __put_user(__NR_rt_sigreturn, (int __user *)(frame->retcode+1));
	err |= __put_user(0x80cd, (short __user *)(frame->retcode+5));

	if (err)
		return -EFAULT;

	/* Set up registers for signal handler */
	regs->sp = (unsigned long)frame;
	regs->ip = (unsigned long)ka->sa.sa_handler;
	regs->ax = (unsigned long)sig;
	regs->dx = (unsigned long)&frame->info;
	regs->cx = (unsigned long)&frame->uc;

	regs->ds = __USER_DS;
	regs->es = __USER_DS;
	regs->ss = __USER_DS;
	regs->cs = __USER_CS;

	return 0;
}

/*
 * OK, we're invoking a handler:
 */
static int
setup_rt_frame(int sig, struct k_sigaction *ka, siginfo_t *info,
	       sigset_t *set, struct pt_regs *regs)
{
	int ret;
	int usig;

	usig = current_thread_info()->exec_domain
		&& current_thread_info()->exec_domain->signal_invmap
		&& sig < 32
		? current_thread_info()->exec_domain->signal_invmap[sig]
		: sig;

	/* Set up the stack frame */
	if (ka->sa.sa_flags & SA_SIGINFO)
		ret = __setup_rt_frame(usig, ka, info, set, regs);
	else
		ret = __setup_frame(usig, ka, set, regs);

	if (ret) {
		force_sigsegv(sig, current);
		return -EFAULT;
	}

	return ret;
}

static int
handle_signal(unsigned long sig, siginfo_t *info, struct k_sigaction *ka,
	      sigset_t *oldset, struct pt_regs *regs)
{
	int ret;

	/* Are we from a system call? */
	if (syscall_get_nr(current, regs) >= 0) {
		/* If so, check system call restarting.. */
		switch (syscall_get_error(current, regs)) {
		case -ERESTART_RESTARTBLOCK:
		case -ERESTARTNOHAND:
			regs->ax = -EINTR;
			break;

		case -ERESTARTSYS:
			if (!(ka->sa.sa_flags & SA_RESTART)) {
				regs->ax = -EINTR;
				break;
			}
		/* fallthrough */
		case -ERESTARTNOINTR:
			regs->ax = regs->orig_ax;
			regs->ip -= 2;
			break;
		}
	}

	/*
	 * If TF is set due to a debugger (TIF_FORCED_TF), clear the TF
	 * flag so that register information in the sigcontext is correct.
	 */
	if (unlikely(regs->flags & X86_EFLAGS_TF) &&
	    likely(test_and_clear_thread_flag(TIF_FORCED_TF)))
		regs->flags &= ~X86_EFLAGS_TF;

	ret = setup_rt_frame(sig, ka, info, oldset, regs);

	if (ret)
		return ret;

	/*
	 * Clear the direction flag as per the ABI for function entry.
	 */
	regs->flags &= ~X86_EFLAGS_DF;

	/*
	 * Clear TF when entering the signal handler, but
	 * notify any tracer that was single-stepping it.
	 * The tracer may want to single-step inside the
	 * handler too.
	 */
	regs->flags &= ~X86_EFLAGS_TF;

	spin_lock_irq(&current->sighand->siglock);
	sigorsets(&current->blocked, &current->blocked, &ka->sa.sa_mask);
	if (!(ka->sa.sa_flags & SA_NODEFER))
		sigaddset(&current->blocked, sig);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	tracehook_signal_handler(sig, info, ka, regs,
				 test_thread_flag(TIF_SINGLESTEP));

	return 0;
}

#define NR_restart_syscall	__NR_restart_syscall
/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 */
static void do_signal(struct pt_regs *regs)
{
	struct k_sigaction ka;
	siginfo_t info;
	int signr;
	sigset_t *oldset;

	/*
	 * We want the common case to go fast, which is why we may in certain
	 * cases get here from kernel mode. Just return without doing anything
	 * if so.
	 * X86_32: vm86 regs switched out by assembly code before reaching
	 * here, so testing against kernel CS suffices.
	 */
	if (!user_mode(regs))
		return;

	if (current_thread_info()->status & TS_RESTORE_SIGMASK)
		oldset = &current->saved_sigmask;
	else
		oldset = &current->blocked;

	signr = get_signal_to_deliver(&info, &ka, regs, NULL);
	if (signr > 0) {
		/*
		 * Re-enable any watchpoints before delivering the
		 * signal to user space. The processor register will
		 * have been cleared if the watchpoint triggered
		 * inside the kernel.
		 */
		if (current->thread.debugreg7)
			set_debugreg(current->thread.debugreg7, 7);

		/* Whee! Actually deliver the signal.  */
		if (handle_signal(signr, &info, &ka, oldset, regs) == 0) {
			/*
			 * A signal was successfully delivered; the saved
			 * sigmask will have been stored in the signal frame,
			 * and will be restored by sigreturn, so we can simply
			 * clear the TS_RESTORE_SIGMASK flag.
			 */
			current_thread_info()->status &= ~TS_RESTORE_SIGMASK;
		}
		return;
	}

	/* Did we come from a system call? */
	if (syscall_get_nr(current, regs) >= 0) {
		/* Restart the system call - no handlers present */
		switch (syscall_get_error(current, regs)) {
		case -ERESTARTNOHAND:
		case -ERESTARTSYS:
		case -ERESTARTNOINTR:
			regs->ax = regs->orig_ax;
			regs->ip -= 2;
			break;

		case -ERESTART_RESTARTBLOCK:
			regs->ax = NR_restart_syscall;
			regs->ip -= 2;
			break;
		}
	}

	/*
	 * If there's no signal to deliver, we just put the saved sigmask
	 * back.
	 */
	if (current_thread_info()->status & TS_RESTORE_SIGMASK) {
		current_thread_info()->status &= ~TS_RESTORE_SIGMASK;
		sigprocmask(SIG_SETMASK, &current->saved_sigmask, NULL);
	}
}

/*
 * notification of userspace execution resumption
 * - triggered by the TIF_WORK_MASK flags
 */
void
do_notify_resume(struct pt_regs *regs, void *unused, __u32 thread_info_flags)
{
	/* deal with pending signal delivery */
	if (thread_info_flags & _TIF_SIGPENDING)
		do_signal(regs);

	if (thread_info_flags & _TIF_NOTIFY_RESUME) {
		clear_thread_flag(TIF_NOTIFY_RESUME);
		tracehook_notify_resume(regs);
	}

	clear_thread_flag(TIF_IRET);
}

void signal_fault(struct pt_regs *regs, void __user *frame, char *where)
{
	struct task_struct *me = current;

	if (show_unhandled_signals && printk_ratelimit()) {
		printk(KERN_INFO
		       "%s[%d] bad frame in %s frame:%p ip:%lx sp:%lx orax:%lx",
		       me->comm, me->pid, where, frame,
		       regs->ip, regs->sp, regs->orig_ax);
		print_vma_addr(" in ", regs->ip);
		printk(KERN_CONT "\n");
	}

	force_sig(SIGSEGV, me);
}
