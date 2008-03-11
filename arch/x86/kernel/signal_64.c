/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2000, 2001, 2002 Andi Kleen SuSE Labs
 *
 *  1997-11-28  Modified for POSIX.1b signals by Richard Henderson
 *  2000-06-20  Pentium III FXSR, SSE support by Gareth Hughes
 *  2000-2002   x86-64 support by Andi Kleen
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
#include <linux/personality.h>
#include <linux/compiler.h>
#include <asm/ucontext.h>
#include <asm/uaccess.h>
#include <asm/i387.h>
#include <asm/proto.h>
#include <asm/ia32_unistd.h>
#include <asm/mce.h>

/* #define DEBUG_SIG 1 */

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

int ia32_setup_rt_frame(int sig, struct k_sigaction *ka, siginfo_t *info,
               sigset_t *set, struct pt_regs * regs); 
int ia32_setup_frame(int sig, struct k_sigaction *ka,
            sigset_t *set, struct pt_regs * regs); 

asmlinkage long
sys_sigaltstack(const stack_t __user *uss, stack_t __user *uoss,
		struct pt_regs *regs)
{
	return do_sigaltstack(uss, uoss, regs->sp);
}


/*
 * Do a signal return; undo the signal stack.
 */

struct rt_sigframe
{
	char __user *pretcode;
	struct ucontext uc;
	struct siginfo info;
};

static int
restore_sigcontext(struct pt_regs *regs, struct sigcontext __user *sc, unsigned long *prax)
{
	unsigned int err = 0;

	/* Always make any pending restarted system calls return -EINTR */
	current_thread_info()->restart_block.fn = do_no_restart_syscall;

#define COPY(x)		err |= __get_user(regs->x, &sc->x)

	COPY(di); COPY(si); COPY(bp); COPY(sp); COPY(bx);
	COPY(dx); COPY(cx); COPY(ip);
	COPY(r8);
	COPY(r9);
	COPY(r10);
	COPY(r11);
	COPY(r12);
	COPY(r13);
	COPY(r14);
	COPY(r15);

	/* Kernel saves and restores only the CS segment register on signals,
	 * which is the bare minimum needed to allow mixed 32/64-bit code.
	 * App's signal handler can save/restore other segments if needed. */
	{
		unsigned cs;
		err |= __get_user(cs, &sc->cs);
		regs->cs = cs | 3;	/* Force into user mode */
	}

	{
		unsigned int tmpflags;
		err |= __get_user(tmpflags, &sc->flags);
		regs->flags = (regs->flags & ~0x40DD5) | (tmpflags & 0x40DD5);
		regs->orig_ax = -1;		/* disable syscall checks */
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

	err |= __get_user(*prax, &sc->ax);
	return err;

badframe:
	return 1;
}

asmlinkage long sys_rt_sigreturn(struct pt_regs *regs)
{
	struct rt_sigframe __user *frame;
	sigset_t set;
	unsigned long ax;

	frame = (struct rt_sigframe __user *)(regs->sp - 8);
	if (!access_ok(VERIFY_READ, frame, sizeof(*frame))) {
		goto badframe;
	} 
	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set))) { 
		goto badframe;
	} 

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
	
	if (restore_sigcontext(regs, &frame->uc.uc_mcontext, &ax))
		goto badframe;

#ifdef DEBUG_SIG
	printk("%d sigreturn ip:%lx sp:%lx frame:%p ax:%lx\n",current->pid,regs->ip,regs->sp,frame,ax);
#endif

	if (do_sigaltstack(&frame->uc.uc_stack, NULL, regs->sp) == -EFAULT)
		goto badframe;

	return ax;

badframe:
	signal_fault(regs,frame,"sigreturn");
	return 0;
}	

/*
 * Set up a signal frame.
 */

static inline int
setup_sigcontext(struct sigcontext __user *sc, struct pt_regs *regs, unsigned long mask, struct task_struct *me)
{
	int err = 0;

	err |= __put_user(regs->cs, &sc->cs);
	err |= __put_user(0, &sc->gs);
	err |= __put_user(0, &sc->fs);

	err |= __put_user(regs->di, &sc->di);
	err |= __put_user(regs->si, &sc->si);
	err |= __put_user(regs->bp, &sc->bp);
	err |= __put_user(regs->sp, &sc->sp);
	err |= __put_user(regs->bx, &sc->bx);
	err |= __put_user(regs->dx, &sc->dx);
	err |= __put_user(regs->cx, &sc->cx);
	err |= __put_user(regs->ax, &sc->ax);
	err |= __put_user(regs->r8, &sc->r8);
	err |= __put_user(regs->r9, &sc->r9);
	err |= __put_user(regs->r10, &sc->r10);
	err |= __put_user(regs->r11, &sc->r11);
	err |= __put_user(regs->r12, &sc->r12);
	err |= __put_user(regs->r13, &sc->r13);
	err |= __put_user(regs->r14, &sc->r14);
	err |= __put_user(regs->r15, &sc->r15);
	err |= __put_user(me->thread.trap_no, &sc->trapno);
	err |= __put_user(me->thread.error_code, &sc->err);
	err |= __put_user(regs->ip, &sc->ip);
	err |= __put_user(regs->flags, &sc->flags);
	err |= __put_user(mask, &sc->oldmask);
	err |= __put_user(me->thread.cr2, &sc->cr2);

	return err;
}

/*
 * Determine which stack to use..
 */

static void __user *
get_stack(struct k_sigaction *ka, struct pt_regs *regs, unsigned long size)
{
	unsigned long sp;

	/* Default to using normal stack - redzone*/
	sp = regs->sp - 128;

	/* This is the X/Open sanctioned signal stack switching.  */
	if (ka->sa.sa_flags & SA_ONSTACK) {
		if (sas_ss_flags(sp) == 0)
			sp = current->sas_ss_sp + current->sas_ss_size;
	}

	return (void __user *)round_down(sp - size, 16);
}

static int setup_rt_frame(int sig, struct k_sigaction *ka, siginfo_t *info,
			   sigset_t *set, struct pt_regs * regs)
{
	struct rt_sigframe __user *frame;
	struct _fpstate __user *fp = NULL; 
	int err = 0;
	struct task_struct *me = current;

	if (used_math()) {
		fp = get_stack(ka, regs, sizeof(struct _fpstate)); 
		frame = (void __user *)round_down(
			(unsigned long)fp - sizeof(struct rt_sigframe), 16) - 8;

		if (!access_ok(VERIFY_WRITE, fp, sizeof(struct _fpstate)))
			goto give_sigsegv;

		if (save_i387(fp) < 0) 
			err |= -1; 
	} else
		frame = get_stack(ka, regs, sizeof(struct rt_sigframe)) - 8;

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto give_sigsegv;

	if (ka->sa.sa_flags & SA_SIGINFO) { 
		err |= copy_siginfo_to_user(&frame->info, info);
		if (err)
			goto give_sigsegv;
	}
		
	/* Create the ucontext.  */
	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(0, &frame->uc.uc_link);
	err |= __put_user(me->sas_ss_sp, &frame->uc.uc_stack.ss_sp);
	err |= __put_user(sas_ss_flags(regs->sp),
			  &frame->uc.uc_stack.ss_flags);
	err |= __put_user(me->sas_ss_size, &frame->uc.uc_stack.ss_size);
	err |= setup_sigcontext(&frame->uc.uc_mcontext, regs, set->sig[0], me);
	err |= __put_user(fp, &frame->uc.uc_mcontext.fpstate);
	if (sizeof(*set) == 16) { 
		__put_user(set->sig[0], &frame->uc.uc_sigmask.sig[0]);
		__put_user(set->sig[1], &frame->uc.uc_sigmask.sig[1]); 
	} else
		err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	/* x86-64 should always use SA_RESTORER. */
	if (ka->sa.sa_flags & SA_RESTORER) {
		err |= __put_user(ka->sa.sa_restorer, &frame->pretcode);
	} else {
		/* could use a vstub here */
		goto give_sigsegv; 
	}

	if (err)
		goto give_sigsegv;

#ifdef DEBUG_SIG
	printk("%d old ip %lx old sp %lx old ax %lx\n", current->pid,regs->ip,regs->sp,regs->ax);
#endif

	/* Set up registers for signal handler */
	regs->di = sig;
	/* In case the signal handler was declared without prototypes */ 
	regs->ax = 0;

	/* This also works for non SA_SIGINFO handlers because they expect the
	   next argument after the signal number on the stack. */
	regs->si = (unsigned long)&frame->info;
	regs->dx = (unsigned long)&frame->uc;
	regs->ip = (unsigned long) ka->sa.sa_handler;

	regs->sp = (unsigned long)frame;

	/* Set up the CS register to run signal handlers in 64-bit mode,
	   even if the handler happens to be interrupting 32-bit code. */
	regs->cs = __USER_CS;

	/* This, by contrast, has nothing to do with segment registers -
	   see include/asm-x86_64/uaccess.h for details. */
	set_fs(USER_DS);

	regs->flags &= ~(X86_EFLAGS_TF | X86_EFLAGS_DF);
	if (test_thread_flag(TIF_SINGLESTEP))
		ptrace_notify(SIGTRAP);
#ifdef DEBUG_SIG
	printk("SIG deliver (%s:%d): sp=%p pc=%lx ra=%p\n",
		current->comm, current->pid, frame, regs->ip, frame->pretcode);
#endif

	return 0;

give_sigsegv:
	force_sigsegv(sig, current);
	return -EFAULT;
}

/*
 * Return -1L or the syscall number that @regs is executing.
 */
static long current_syscall(struct pt_regs *regs)
{
	/*
	 * We always sign-extend a -1 value being set here,
	 * so this is always either -1L or a syscall number.
	 */
	return regs->orig_ax;
}

/*
 * Return a value that is -EFOO if the system call in @regs->orig_ax
 * returned an error.  This only works for @regs from @current.
 */
static long current_syscall_ret(struct pt_regs *regs)
{
#ifdef CONFIG_IA32_EMULATION
	if (test_thread_flag(TIF_IA32))
		/*
		 * Sign-extend the value so (int)-EFOO becomes (long)-EFOO
		 * and will match correctly in comparisons.
		 */
		return (int) regs->ax;
#endif
	return regs->ax;
}

/*
 * OK, we're invoking a handler
 */	

static int
handle_signal(unsigned long sig, siginfo_t *info, struct k_sigaction *ka,
		sigset_t *oldset, struct pt_regs *regs)
{
	int ret;

#ifdef DEBUG_SIG
	printk("handle_signal pid:%d sig:%lu ip:%lx sp:%lx regs=%p\n",
		current->pid, sig,
		regs->ip, regs->sp, regs);
#endif

	/* Are we from a system call? */
	if (current_syscall(regs) >= 0) {
		/* If so, check system call restarting.. */
		switch (current_syscall_ret(regs)) {
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

#ifdef CONFIG_IA32_EMULATION
	if (test_thread_flag(TIF_IA32)) {
		if (ka->sa.sa_flags & SA_SIGINFO)
			ret = ia32_setup_rt_frame(sig, ka, info, oldset, regs);
		else
			ret = ia32_setup_frame(sig, ka, oldset, regs);
	} else 
#endif
	ret = setup_rt_frame(sig, ka, info, oldset, regs);

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
static void do_signal(struct pt_regs *regs)
{
	struct k_sigaction ka;
	siginfo_t info;
	int signr;
	sigset_t *oldset;

	/*
	 * We want the common case to go fast, which
	 * is why we may in certain cases get here from
	 * kernel mode. Just return without doing anything
	 * if so.
	 */
	if (!user_mode(regs))
		return;

	if (test_thread_flag(TIF_RESTORE_SIGMASK))
		oldset = &current->saved_sigmask;
	else
		oldset = &current->blocked;

	signr = get_signal_to_deliver(&info, &ka, regs, NULL);
	if (signr > 0) {
		/* Re-enable any watchpoints before delivering the
		 * signal to user space. The processor register will
		 * have been cleared if the watchpoint triggered
		 * inside the kernel.
		 */
		if (current->thread.debugreg7)
			set_debugreg(current->thread.debugreg7, 7);

		/* Whee!  Actually deliver the signal.  */
		if (handle_signal(signr, &info, &ka, oldset, regs) == 0) {
			/* a signal was successfully delivered; the saved
			 * sigmask will have been stored in the signal frame,
			 * and will be restored by sigreturn, so we can simply
			 * clear the TIF_RESTORE_SIGMASK flag */
			clear_thread_flag(TIF_RESTORE_SIGMASK);
		}
		return;
	}

	/* Did we come from a system call? */
	if (current_syscall(regs) >= 0) {
		/* Restart the system call - no handlers present */
		switch (current_syscall_ret(regs)) {
		case -ERESTARTNOHAND:
		case -ERESTARTSYS:
		case -ERESTARTNOINTR:
			regs->ax = regs->orig_ax;
			regs->ip -= 2;
			break;
		case -ERESTART_RESTARTBLOCK:
			regs->ax = test_thread_flag(TIF_IA32) ?
					__NR_ia32_restart_syscall :
					__NR_restart_syscall;
			regs->ip -= 2;
			break;
		}
	}

	/* if there's no signal to deliver, we just put the saved sigmask
	   back. */
	if (test_thread_flag(TIF_RESTORE_SIGMASK)) {
		clear_thread_flag(TIF_RESTORE_SIGMASK);
		sigprocmask(SIG_SETMASK, &current->saved_sigmask, NULL);
	}
}

void
do_notify_resume(struct pt_regs *regs, void *unused, __u32 thread_info_flags)
{
#ifdef DEBUG_SIG
	printk("do_notify_resume flags:%x ip:%lx sp:%lx caller:%p pending:%x\n",
	       thread_info_flags, regs->ip, regs->sp, __builtin_return_address(0),signal_pending(current));
#endif
	       
	/* Pending single-step? */
	if (thread_info_flags & _TIF_SINGLESTEP) {
		regs->flags |= X86_EFLAGS_TF;
		clear_thread_flag(TIF_SINGLESTEP);
	}

#ifdef CONFIG_X86_MCE
	/* notify userspace of pending MCEs */
	if (thread_info_flags & _TIF_MCE_NOTIFY)
		mce_notify_user();
#endif /* CONFIG_X86_MCE */

	/* deal with pending signal delivery */
	if (thread_info_flags & (_TIF_SIGPENDING|_TIF_RESTORE_SIGMASK))
		do_signal(regs);

	if (thread_info_flags & _TIF_HRTICK_RESCHED)
		hrtick_resched();
}

void signal_fault(struct pt_regs *regs, void __user *frame, char *where)
{ 
	struct task_struct *me = current; 
	if (show_unhandled_signals && printk_ratelimit()) {
		printk("%s[%d] bad frame in %s frame:%p ip:%lx sp:%lx orax:%lx",
	       me->comm,me->pid,where,frame,regs->ip,regs->sp,regs->orig_ax);
		print_vma_addr(" in ", regs->ip);
		printk("\n");
	}

	force_sig(SIGSEGV, me); 
} 
