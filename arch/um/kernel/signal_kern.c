/* 
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/config.h"
#include "linux/stddef.h"
#include "linux/sys.h"
#include "linux/sched.h"
#include "linux/wait.h"
#include "linux/kernel.h"
#include "linux/smp_lock.h"
#include "linux/module.h"
#include "linux/slab.h"
#include "linux/tty.h"
#include "linux/binfmts.h"
#include "linux/ptrace.h"
#include "asm/signal.h"
#include "asm/uaccess.h"
#include "asm/unistd.h"
#include "user_util.h"
#include "asm/ucontext.h"
#include "kern_util.h"
#include "signal_kern.h"
#include "signal_user.h"
#include "kern.h"
#include "frame_kern.h"
#include "sigcontext.h"
#include "mode.h"

EXPORT_SYMBOL(block_signals);
EXPORT_SYMBOL(unblock_signals);

#define _S(nr) (1<<((nr)-1))

#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

/*
 * OK, we're invoking a handler
 */	
static int handle_signal(struct pt_regs *regs, unsigned long signr,
			 struct k_sigaction *ka, siginfo_t *info,
			 sigset_t *oldset)
{
	unsigned long sp;
	int err;

	/* Always make any pending restarted system calls return -EINTR */
	current_thread_info()->restart_block.fn = do_no_restart_syscall;

	/* Did we come from a system call? */
	if(PT_REGS_SYSCALL_NR(regs) >= 0){
		/* If so, check system call restarting.. */
		switch(PT_REGS_SYSCALL_RET(regs)){
		case -ERESTART_RESTARTBLOCK:
		case -ERESTARTNOHAND:
			PT_REGS_SYSCALL_RET(regs) = -EINTR;
			break;

		case -ERESTARTSYS:
			if (!(ka->sa.sa_flags & SA_RESTART)) {
				PT_REGS_SYSCALL_RET(regs) = -EINTR;
				break;
			}
		/* fallthrough */
		case -ERESTARTNOINTR:
			PT_REGS_RESTART_SYSCALL(regs);
			PT_REGS_ORIG_SYSCALL(regs) = PT_REGS_SYSCALL_NR(regs);
			break;
		}
	}

	sp = PT_REGS_SP(regs);
	if((ka->sa.sa_flags & SA_ONSTACK) && (sas_ss_flags(sp) == 0))
		sp = current->sas_ss_sp + current->sas_ss_size;

#ifdef CONFIG_ARCH_HAS_SC_SIGNALS
	if(!(ka->sa.sa_flags & SA_SIGINFO))
		err = setup_signal_stack_sc(sp, signr, ka, regs, oldset);
	else
#endif
		err = setup_signal_stack_si(sp, signr, ka, regs, info, oldset);

	if(err){
		spin_lock_irq(&current->sighand->siglock);
		current->blocked = *oldset;
		recalc_sigpending();
		spin_unlock_irq(&current->sighand->siglock);
		force_sigsegv(signr, current);
	} else {
		spin_lock_irq(&current->sighand->siglock);
		sigorsets(&current->blocked, &current->blocked, 
			  &ka->sa.sa_mask);
		 if(!(ka->sa.sa_flags & SA_NODEFER))
			sigaddset(&current->blocked, signr);
		recalc_sigpending();
		spin_unlock_irq(&current->sighand->siglock);
	}

	return err;
}

static int kern_do_signal(struct pt_regs *regs, sigset_t *oldset)
{
	struct k_sigaction ka_copy;
	siginfo_t info;
	int sig, handled_sig = 0;

	while((sig = get_signal_to_deliver(&info, &ka_copy, regs, NULL)) > 0){
		handled_sig = 1;
		/* Whee!  Actually deliver the signal.  */
		if(!handle_signal(regs, sig, &ka_copy, &info, oldset))
			break;
	}

	/* Did we come from a system call? */
	if(!handled_sig && (PT_REGS_SYSCALL_NR(regs) >= 0)){
		/* Restart the system call - no handlers present */
		if(PT_REGS_SYSCALL_RET(regs) == -ERESTARTNOHAND ||
		   PT_REGS_SYSCALL_RET(regs) == -ERESTARTSYS ||
		   PT_REGS_SYSCALL_RET(regs) == -ERESTARTNOINTR){
			PT_REGS_ORIG_SYSCALL(regs) = PT_REGS_SYSCALL_NR(regs);
			PT_REGS_RESTART_SYSCALL(regs);
		}
		else if(PT_REGS_SYSCALL_RET(regs) == -ERESTART_RESTARTBLOCK){
			PT_REGS_SYSCALL_RET(regs) = __NR_restart_syscall;
			PT_REGS_RESTART_SYSCALL(regs);
 		}
	}

	/* This closes a way to execute a system call on the host.  If
	 * you set a breakpoint on a system call instruction and singlestep
	 * from it, the tracing thread used to PTRACE_SINGLESTEP the process
	 * rather than PTRACE_SYSCALL it, allowing the system call to execute
	 * on the host.  The tracing thread will check this flag and 
	 * PTRACE_SYSCALL if necessary.
	 */
	if(current->ptrace & PT_DTRACE)
		current->thread.singlestep_syscall =
			is_syscall(PT_REGS_IP(&current->thread.regs));
	return(handled_sig);
}

int do_signal(void)
{
	return(kern_do_signal(&current->thread.regs, &current->blocked));
}

/*
 * Atomically swap in the new signal mask, and wait for a signal.
 */
long sys_sigsuspend(int history0, int history1, old_sigset_t mask)
{
	sigset_t saveset;

	mask &= _BLOCKABLE;
	spin_lock_irq(&current->sighand->siglock);
	saveset = current->blocked;
	siginitset(&current->blocked, mask);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	PT_REGS_SYSCALL_RET(&current->thread.regs) = -EINTR;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if(kern_do_signal(&current->thread.regs, &saveset))
			return(-EINTR);
	}
}

long sys_rt_sigsuspend(sigset_t __user *unewset, size_t sigsetsize)
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

	PT_REGS_SYSCALL_RET(&current->thread.regs) = -EINTR;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (kern_do_signal(&current->thread.regs, &saveset))
			return(-EINTR);
	}
}

long sys_sigaltstack(const stack_t __user *uss, stack_t __user *uoss)
{
	return(do_sigaltstack(uss, uoss, PT_REGS_SP(&current->thread.regs)));
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
