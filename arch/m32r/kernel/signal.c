/*
 *  linux/arch/m32r/kernel/signal.c
 *
 *  Copyright (c) 2003  Hitoshi Yamamoto
 *
 *  Taken from i386 version.
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  1997-11-28  Modified for POSIX.1b signals by Richard Henderson
 *  2000-06-20  Pentium III FXSR, SSE support by Gareth Hughes
 */

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/unistd.h>
#include <linux/stddef.h>
#include <linux/personality.h>
#include <linux/freezer.h>
#include <asm/cacheflush.h>
#include <asm/ucontext.h>
#include <asm/uaccess.h>

#define DEBUG_SIG 0

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

int do_signal(struct pt_regs *, sigset_t *);

asmlinkage int
sys_rt_sigsuspend(sigset_t __user *unewset, size_t sigsetsize,
		  unsigned long r2, unsigned long r3, unsigned long r4,
		  unsigned long r5, unsigned long r6, struct pt_regs *regs)
{
	sigset_t newset;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (copy_from_user(&newset, unewset, sizeof(newset)))
		return -EFAULT;
	sigdelsetmask(&newset, sigmask(SIGKILL)|sigmask(SIGSTOP));

	spin_lock_irq(&current->sighand->siglock);
	current->saved_sigmask = current->blocked;
	current->blocked = newset;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	current->state = TASK_INTERRUPTIBLE;
	schedule();
	set_thread_flag(TIF_RESTORE_SIGMASK);
	return -ERESTARTNOHAND;
}

asmlinkage int
sys_sigaltstack(const stack_t __user *uss, stack_t __user *uoss,
		unsigned long r2, unsigned long r3, unsigned long r4,
		unsigned long r5, unsigned long r6, struct pt_regs *regs)
{
	return do_sigaltstack(uss, uoss, regs->spu);
}


/*
 * Do a signal return; undo the signal stack.
 */

struct rt_sigframe
{
	int sig;
	struct siginfo __user *pinfo;
	void __user *puc;
	struct siginfo info;
	struct ucontext uc;
//	struct _fpstate fpstate;
};

static int
restore_sigcontext(struct pt_regs *regs, struct sigcontext __user *sc,
		   int *r0_p)
{
	unsigned int err = 0;

	/* Always make any pending restarted system calls return -EINTR */
	current_thread_info()->restart_block.fn = do_no_restart_syscall;

#define COPY(x)		err |= __get_user(regs->x, &sc->sc_##x)
	COPY(r4);
	COPY(r5);
	COPY(r6);
	COPY(pt_regs);
	/* COPY(r0); Skip r0 */
	COPY(r1);
	COPY(r2);
	COPY(r3);
	COPY(r7);
	COPY(r8);
	COPY(r9);
	COPY(r10);
	COPY(r11);
	COPY(r12);
	COPY(acc0h);
	COPY(acc0l);
	COPY(acc1h);		/* ISA_DSP_LEVEL2 only */
	COPY(acc1l);		/* ISA_DSP_LEVEL2 only */
	COPY(psw);
	COPY(bpc);
	COPY(bbpsw);
	COPY(bbpc);
	COPY(spu);
	COPY(fp);
	COPY(lr);
	COPY(spi);
#undef COPY

	regs->syscall_nr = -1;	/* disable syscall checks */
	err |= __get_user(*r0_p, &sc->sc_r0);

	return err;
}

asmlinkage int
sys_rt_sigreturn(unsigned long r0, unsigned long r1,
		 unsigned long r2, unsigned long r3, unsigned long r4,
		 unsigned long r5, unsigned long r6, struct pt_regs *regs)
{
	struct rt_sigframe __user *frame = (struct rt_sigframe __user *)regs->spu;
	sigset_t set;
	int result;

	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	if (restore_sigcontext(regs, &frame->uc.uc_mcontext, &result))
		goto badframe;

	if (do_sigaltstack(&frame->uc.uc_stack, NULL, regs->spu) == -EFAULT)
		goto badframe;

	return result;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

/*
 * Set up a signal frame.
 */

static int
setup_sigcontext(struct sigcontext __user *sc, struct pt_regs *regs,
	         unsigned long mask)
{
	int err = 0;

#define COPY(x)	err |= __put_user(regs->x, &sc->sc_##x)
	COPY(r4);
	COPY(r5);
	COPY(r6);
	COPY(pt_regs);
	COPY(r0);
	COPY(r1);
	COPY(r2);
	COPY(r3);
	COPY(r7);
	COPY(r8);
	COPY(r9);
	COPY(r10);
	COPY(r11);
	COPY(r12);
	COPY(acc0h);
	COPY(acc0l);
	COPY(acc1h);		/* ISA_DSP_LEVEL2 only */
	COPY(acc1l);		/* ISA_DSP_LEVEL2 only */
	COPY(psw);
	COPY(bpc);
	COPY(bbpsw);
	COPY(bbpc);
	COPY(spu);
	COPY(fp);
	COPY(lr);
	COPY(spi);
#undef COPY

	err |= __put_user(mask, &sc->oldmask);

	return err;
}

/*
 * Determine which stack to use..
 */
static inline void __user *
get_sigframe(struct k_sigaction *ka, unsigned long sp, size_t frame_size)
{
	/* This is the X/Open sanctioned signal stack switching.  */
	if (ka->sa.sa_flags & SA_ONSTACK) {
		if (sas_ss_flags(sp) == 0)
			sp = current->sas_ss_sp + current->sas_ss_size;
	}

	return (void __user *)((sp - frame_size) & -8ul);
}

static void setup_rt_frame(int sig, struct k_sigaction *ka, siginfo_t *info,
			   sigset_t *set, struct pt_regs *regs)
{
	struct rt_sigframe __user *frame;
	int err = 0;
	int signal;

	frame = get_sigframe(ka, regs->spu, sizeof(*frame));

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto give_sigsegv;

	signal = current_thread_info()->exec_domain
		&& current_thread_info()->exec_domain->signal_invmap
		&& sig < 32
		? current_thread_info()->exec_domain->signal_invmap[sig]
		: sig;

	err |= __put_user(signal, &frame->sig);
	if (err)
		goto give_sigsegv;

	err |= __put_user(&frame->info, &frame->pinfo);
	err |= __put_user(&frame->uc, &frame->puc);
	err |= copy_siginfo_to_user(&frame->info, info);
	if (err)
		goto give_sigsegv;

	/* Create the ucontext.  */
	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(0, &frame->uc.uc_link);
	err |= __put_user(current->sas_ss_sp, &frame->uc.uc_stack.ss_sp);
	err |= __put_user(sas_ss_flags(regs->spu),
			  &frame->uc.uc_stack.ss_flags);
	err |= __put_user(current->sas_ss_size, &frame->uc.uc_stack.ss_size);
	err |= setup_sigcontext(&frame->uc.uc_mcontext, regs, set->sig[0]);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));
	if (err)
		goto give_sigsegv;

	/* Set up to return from userspace.  */
	regs->lr = (unsigned long)ka->sa.sa_restorer;

	/* Set up registers for signal handler */
	regs->spu = (unsigned long)frame;
	regs->r0 = signal;	/* Arg for signal handler */
	regs->r1 = (unsigned long)&frame->info;
	regs->r2 = (unsigned long)&frame->uc;
	regs->bpc = (unsigned long)ka->sa.sa_handler;

	set_fs(USER_DS);

#if DEBUG_SIG
	printk("SIG deliver (%s:%d): sp=%p pc=%p\n",
		current->comm, current->pid, frame, regs->pc);
#endif

	return;

give_sigsegv:
	force_sigsegv(sig, current);
}

/*
 * OK, we're invoking a handler
 */

static void
handle_signal(unsigned long sig, struct k_sigaction *ka, siginfo_t *info,
	      sigset_t *oldset, struct pt_regs *regs)
{
	unsigned short inst;

	/* Are we from a system call? */
	if (regs->syscall_nr >= 0) {
		/* If so, check system call restarting.. */
		switch (regs->r0) {
		        case -ERESTART_RESTARTBLOCK:
			case -ERESTARTNOHAND:
				regs->r0 = -EINTR;
				break;

			case -ERESTARTSYS:
				if (!(ka->sa.sa_flags & SA_RESTART)) {
					regs->r0 = -EINTR;
					break;
				}
			/* fallthrough */
			case -ERESTARTNOINTR:
				regs->r0 = regs->orig_r0;
				inst = *(unsigned short *)(regs->bpc - 2);
				if ((inst & 0xfff0) == 0x10f0)	/* trap ? */
					regs->bpc -= 2;
				else
					regs->bpc -= 4;
		}
	}

	/* Set up the stack frame */
	setup_rt_frame(sig, ka, info, oldset, regs);

	spin_lock_irq(&current->sighand->siglock);
	sigorsets(&current->blocked,&current->blocked,&ka->sa.sa_mask);
	if (!(ka->sa.sa_flags & SA_NODEFER))
		sigaddset(&current->blocked,sig);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
}

/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 */
int do_signal(struct pt_regs *regs, sigset_t *oldset)
{
	siginfo_t info;
	int signr;
	struct k_sigaction ka;
	unsigned short inst;

	/*
	 * We want the common case to go fast, which
	 * is why we may in certain cases get here from
	 * kernel mode. Just return without doing anything
	 * if so.
	 */
	if (!user_mode(regs))
		return 1;

	if (try_to_freeze()) 
		goto no_signal;

	if (!oldset)
		oldset = &current->blocked;

	signr = get_signal_to_deliver(&info, &ka, regs, NULL);
	if (signr > 0) {
		/* Re-enable any watchpoints before delivering the
		 * signal to user space. The processor register will
		 * have been cleared if the watchpoint triggered
		 * inside the kernel.
		 */

		/* Whee!  Actually deliver the signal.  */
		handle_signal(signr, &ka, &info, oldset, regs);
		return 1;
	}

 no_signal:
	/* Did we come from a system call? */
	if (regs->syscall_nr >= 0) {
		/* Restart the system call - no handlers present */
		if (regs->r0 == -ERESTARTNOHAND ||
		    regs->r0 == -ERESTARTSYS ||
		    regs->r0 == -ERESTARTNOINTR) {
			regs->r0 = regs->orig_r0;
			inst = *(unsigned short *)(regs->bpc - 2);
			if ((inst & 0xfff0) == 0x10f0)	/* trap ? */
				regs->bpc -= 2;
			else
				regs->bpc -= 4;
		}
		if (regs->r0 == -ERESTART_RESTARTBLOCK){
			regs->r0 = regs->orig_r0;
			regs->r7 = __NR_restart_syscall;
			inst = *(unsigned short *)(regs->bpc - 2);
			if ((inst & 0xfff0) == 0x10f0)	/* trap ? */
				regs->bpc -= 2;
			else
				regs->bpc -= 4;
		}
	}
	return 0;
}

/*
 * notification of userspace execution resumption
 * - triggered by current->work.notify_resume
 */
void do_notify_resume(struct pt_regs *regs, sigset_t *oldset,
		      __u32 thread_info_flags)
{
	/* Pending single-step? */
	if (thread_info_flags & _TIF_SINGLESTEP)
		clear_thread_flag(TIF_SINGLESTEP);

	/* deal with pending signal delivery */
	if (thread_info_flags & _TIF_SIGPENDING)
		do_signal(regs,oldset);

	clear_thread_flag(TIF_IRET);
}
