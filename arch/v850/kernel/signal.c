/*
 * arch/v850/kernel/signal.c -- Signal handling
 *
 *  Copyright (C) 2001,02,03  NEC Electronics Corporation
 *  Copyright (C) 2001,02,03  Miles Bader <miles@gnu.org>
 *  Copyright (C) 1999,2000,2002  Niibe Yutaka & Kaz Kojima
 *  Copyright (C) 1991,1992  Linus Torvalds
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * 1997-11-28  Modified for POSIX.1b signals by Richard Henderson
 *
 * This file was derived from the sh version, arch/sh/kernel/signal.c
 */

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
#include <linux/tty.h>

#include <asm/ucontext.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/thread_info.h>
#include <asm/cacheflush.h>

#define DEBUG_SIG 0

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

asmlinkage int do_signal(struct pt_regs *regs, sigset_t *oldset);

/*
 * Atomically swap in the new signal mask, and wait for a signal.
 */
asmlinkage int
sys_sigsuspend(old_sigset_t mask, struct pt_regs *regs)
{
	sigset_t saveset;

	mask &= _BLOCKABLE;
	spin_lock_irq(&current->sighand->siglock);
	saveset = current->blocked;
	siginitset(&current->blocked, mask);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	regs->gpr[GPR_RVAL] = -EINTR;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(regs, &saveset))
			return -EINTR;
	}
}

asmlinkage int
sys_rt_sigsuspend(sigset_t *unewset, size_t sigsetsize,
		  struct pt_regs *regs)
{
	sigset_t saveset, newset;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (copy_from_user(&newset, unewset, sizeof(newset)))
		return -EFAULT;
	sigdelsetmask(&newset, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	saveset = current->blocked;
	current->blocked = newset;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	regs->gpr[GPR_RVAL] = -EINTR;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(regs, &saveset))
			return -EINTR;
	}
}

asmlinkage int 
sys_sigaction(int sig, const struct old_sigaction *act,
	      struct old_sigaction *oact)
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
sys_sigaltstack(const stack_t *uss, stack_t *uoss,
		struct pt_regs *regs)
{
	return do_sigaltstack(uss, uoss, regs->gpr[GPR_SP]);
}


/*
 * Do a signal return; undo the signal stack.
 */

struct sigframe
{
	struct sigcontext sc;
	unsigned long extramask[_NSIG_WORDS-1];
	unsigned long tramp[2];	/* signal trampoline */
};

struct rt_sigframe
{
	struct siginfo info;
	struct ucontext uc;
	unsigned long tramp[2];	/* signal trampoline */
};

static int
restore_sigcontext(struct pt_regs *regs, struct sigcontext *sc, int *rval_p)
{
	unsigned int err = 0;

#define COPY(x)		err |= __get_user(regs->x, &sc->regs.x)
	COPY(gpr[0]);	COPY(gpr[1]);	COPY(gpr[2]);	COPY(gpr[3]);
	COPY(gpr[4]);	COPY(gpr[5]);	COPY(gpr[6]);	COPY(gpr[7]);
	COPY(gpr[8]);	COPY(gpr[9]);	COPY(gpr[10]);	COPY(gpr[11]);
	COPY(gpr[12]);	COPY(gpr[13]);	COPY(gpr[14]);	COPY(gpr[15]);
	COPY(gpr[16]);	COPY(gpr[17]);	COPY(gpr[18]);	COPY(gpr[19]);
	COPY(gpr[20]);	COPY(gpr[21]);	COPY(gpr[22]);	COPY(gpr[23]);
	COPY(gpr[24]);	COPY(gpr[25]);	COPY(gpr[26]);	COPY(gpr[27]);
	COPY(gpr[28]);	COPY(gpr[29]);	COPY(gpr[30]);	COPY(gpr[31]);
	COPY(pc);	COPY(psw);
	COPY(ctpc);	COPY(ctpsw);	COPY(ctbp);
#undef COPY

	return err;
}

asmlinkage int sys_sigreturn(struct pt_regs *regs)
{
	struct sigframe *frame = (struct sigframe *)regs->gpr[GPR_SP];
	sigset_t set;
	int rval;

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

	if (restore_sigcontext(regs, &frame->sc, &rval))
		goto badframe;
	return rval;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

asmlinkage int sys_rt_sigreturn(struct pt_regs *regs)
{
	struct rt_sigframe *frame = (struct rt_sigframe *)regs->gpr[GPR_SP];
	sigset_t set;
	stack_t st;
	int rval;

	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;

	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	if (restore_sigcontext(regs, &frame->uc.uc_mcontext, &rval))
		goto badframe;

	if (__copy_from_user(&st, &frame->uc.uc_stack, sizeof(st)))
		goto badframe;
	/* It is more difficult to avoid calling this function than to
	   call it and ignore errors.  */
	do_sigaltstack(&st, NULL, regs->gpr[GPR_SP]);

	return rval;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}	

/*
 * Set up a signal frame.
 */

static int
setup_sigcontext(struct sigcontext *sc, struct pt_regs *regs,
		 unsigned long mask)
{
	int err = 0;

#define COPY(x)		err |= __put_user(regs->x, &sc->regs.x)
	COPY(gpr[0]);	COPY(gpr[1]);	COPY(gpr[2]);	COPY(gpr[3]);
	COPY(gpr[4]);	COPY(gpr[5]);	COPY(gpr[6]);	COPY(gpr[7]);
	COPY(gpr[8]);	COPY(gpr[9]);	COPY(gpr[10]);	COPY(gpr[11]);
	COPY(gpr[12]);	COPY(gpr[13]);	COPY(gpr[14]);	COPY(gpr[15]);
	COPY(gpr[16]);	COPY(gpr[17]);	COPY(gpr[18]);	COPY(gpr[19]);
	COPY(gpr[20]);	COPY(gpr[21]);	COPY(gpr[22]);	COPY(gpr[23]);
	COPY(gpr[24]);	COPY(gpr[25]);	COPY(gpr[26]);	COPY(gpr[27]);
	COPY(gpr[28]);	COPY(gpr[29]);	COPY(gpr[30]);	COPY(gpr[31]);
	COPY(pc);	COPY(psw);
	COPY(ctpc);	COPY(ctpsw);	COPY(ctbp);
#undef COPY

	err |= __put_user(mask, &sc->oldmask);

	return err;
}

/*
 * Determine which stack to use..
 */
static inline void *
get_sigframe(struct k_sigaction *ka, struct pt_regs *regs, size_t frame_size)
{
	/* Default to using normal stack */
	unsigned long sp = regs->gpr[GPR_SP];

	if ((ka->sa.sa_flags & SA_ONSTACK) != 0 && ! sas_ss_flags(sp))
		sp = current->sas_ss_sp + current->sas_ss_size;

	return (void *)((sp - frame_size) & -8UL);
}

static void setup_frame(int sig, struct k_sigaction *ka,
			sigset_t *set, struct pt_regs *regs)
{
	struct sigframe *frame;
	int err = 0;
	int signal;

	frame = get_sigframe(ka, regs, sizeof(*frame));

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto give_sigsegv;

	signal = current_thread_info()->exec_domain
		&& current_thread_info()->exec_domain->signal_invmap
		&& sig < 32
		? current_thread_info()->exec_domain->signal_invmap[sig]
		: sig;

	err |= setup_sigcontext(&frame->sc, regs, set->sig[0]);

	if (_NSIG_WORDS > 1) {
		err |= __copy_to_user(frame->extramask, &set->sig[1],
				      sizeof(frame->extramask));
	}

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	if (ka->sa.sa_flags & SA_RESTORER) {
		regs->gpr[GPR_LP] = (unsigned long) ka->sa.sa_restorer;
	} else {
		/* Note, these encodings are _little endian_!  */

		/* addi  __NR_sigreturn, r0, r12  */
		err |= __put_user(0x6600 | (__NR_sigreturn << 16),
				  frame->tramp + 0);
		/* trap 0 */
		err |= __put_user(0x010007e0,
				  frame->tramp + 1);

		regs->gpr[GPR_LP] = (unsigned long)frame->tramp;

		flush_cache_sigtramp (regs->gpr[GPR_LP]);
	}

	if (err)
		goto give_sigsegv;

	/* Set up registers for signal handler.  */
	regs->pc = (v850_reg_t) ka->sa.sa_handler;
	regs->gpr[GPR_SP] = (v850_reg_t)frame;
	/* Signal handler args:  */
	regs->gpr[GPR_ARG0] = signal; /* arg 0: signum */
	regs->gpr[GPR_ARG1] = (v850_reg_t)&frame->sc;/* arg 1: sigcontext */

	set_fs(USER_DS);

#if DEBUG_SIG
	printk("SIG deliver (%s:%d): sp=%p pc=%08lx ra=%08lx\n",
		current->comm, current->pid, frame, regs->pc, );
#endif

	return;

give_sigsegv:
	force_sigsegv(sig, current);
}

static void setup_rt_frame(int sig, struct k_sigaction *ka, siginfo_t *info,
			   sigset_t *set, struct pt_regs *regs)
{
	struct rt_sigframe *frame;
	int err = 0;
	int signal;

	frame = get_sigframe(ka, regs, sizeof(*frame));

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto give_sigsegv;

	signal = current_thread_info()->exec_domain
		&& current_thread_info()->exec_domain->signal_invmap
		&& sig < 32
		? current_thread_info()->exec_domain->signal_invmap[sig]
		: sig;

	err |= copy_siginfo_to_user(&frame->info, info);

	/* Create the ucontext.  */
	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(0, &frame->uc.uc_link);
	err |= __put_user((void *)current->sas_ss_sp,
			  &frame->uc.uc_stack.ss_sp);
	err |= __put_user(sas_ss_flags(regs->gpr[GPR_SP]),
			  &frame->uc.uc_stack.ss_flags);
	err |= __put_user(current->sas_ss_size, &frame->uc.uc_stack.ss_size);
	err |= setup_sigcontext(&frame->uc.uc_mcontext,
			        regs, set->sig[0]);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	if (ka->sa.sa_flags & SA_RESTORER) {
		regs->gpr[GPR_LP] = (unsigned long) ka->sa.sa_restorer;
	} else {
		/* Note, these encodings are _little endian_!  */

		/* addi  __NR_sigreturn, r0, r12  */
		err |= __put_user(0x6600 | (__NR_sigreturn << 16),
				  frame->tramp + 0);
		/* trap 0 */
		err |= __put_user(0x010007e0,
				  frame->tramp + 1);

		regs->gpr[GPR_LP] = (unsigned long)frame->tramp;

		flush_cache_sigtramp (regs->gpr[GPR_LP]);
	}

	if (err)
		goto give_sigsegv;

	/* Set up registers for signal handler.  */
	regs->pc = (v850_reg_t) ka->sa.sa_handler;
	regs->gpr[GPR_SP] = (v850_reg_t)frame;
	/* Signal handler args:  */
	regs->gpr[GPR_ARG0] = signal; /* arg 0: signum */
	regs->gpr[GPR_ARG1] = (v850_reg_t)&frame->info; /* arg 1: siginfo */
	regs->gpr[GPR_ARG2] = (v850_reg_t)&frame->uc; /* arg 2: ucontext */

	set_fs(USER_DS);

#if DEBUG_SIG
	printk("SIG deliver (%s:%d): sp=%p pc=%08lx pr=%08lx\n",
		current->comm, current->pid, frame, regs->pc, regs->pr);
#endif

	return;

give_sigsegv:
	force_sigsegv(sig, current);
}

/*
 * OK, we're invoking a handler
 */	

static void
handle_signal(unsigned long sig, siginfo_t *info, struct k_sigaction *ka,
	      sigset_t *oldset,	struct pt_regs * regs)
{
	/* Are we from a system call? */
	if (PT_REGS_SYSCALL (regs)) {
		/* If so, check system call restarting.. */
		switch (regs->gpr[GPR_RVAL]) {
		case -ERESTART_RESTARTBLOCK:
			current_thread_info()->restart_block.fn =
				do_no_restart_syscall;
			/* fall through */
		case -ERESTARTNOHAND:
			regs->gpr[GPR_RVAL] = -EINTR;
			break;

		case -ERESTARTSYS:
			if (!(ka->sa.sa_flags & SA_RESTART)) {
				regs->gpr[GPR_RVAL] = -EINTR;
				break;
			}
			/* fallthrough */
		case -ERESTARTNOINTR:
			regs->gpr[12] = PT_REGS_SYSCALL (regs);
			regs->pc -= 4; /* Size of `trap 0' insn.  */
		}

		PT_REGS_SET_SYSCALL (regs, 0);
	}

	/* Set up the stack frame */
	if (ka->sa.sa_flags & SA_SIGINFO)
		setup_rt_frame(sig, ka, info, oldset, regs);
	else
		setup_frame(sig, ka, oldset, regs);

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
 *
 * Note that we go through the signals twice: once to check the signals that
 * the kernel can handle, and then we build all the user-level signal handling
 * stack-frames in one go after that.
 */
int do_signal(struct pt_regs *regs, sigset_t *oldset)
{
	siginfo_t info;
	int signr;
	struct k_sigaction ka;

	/*
	 * We want the common case to go fast, which
	 * is why we may in certain cases get here from
	 * kernel mode. Just return without doing anything
	 * if so.
	 */
	if (!user_mode(regs))
		return 1;

	if (!oldset)
		oldset = &current->blocked;

	signr = get_signal_to_deliver(&info, &ka, regs, NULL);
	if (signr > 0) {
		/* Whee!  Actually deliver the signal.  */
		handle_signal(signr, &info, &ka, oldset, regs);
		return 1;
	}

	/* Did we come from a system call? */
	if (PT_REGS_SYSCALL (regs)) {
		int rval = (int)regs->gpr[GPR_RVAL];
		/* Restart the system call - no handlers present */
		if (rval == -ERESTARTNOHAND
		    || rval == -ERESTARTSYS
		    || rval == -ERESTARTNOINTR)
		{
			regs->gpr[12] = PT_REGS_SYSCALL (regs);
			regs->pc -= 4; /* Size of `trap 0' insn.  */
		}
		else if (rval == -ERESTART_RESTARTBLOCK) {
			regs->gpr[12] = __NR_restart_syscall;
			regs->pc -= 4; /* Size of `trap 0' insn.  */
		}
	}
	return 0;
}
