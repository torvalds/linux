/*
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

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

static int kern_do_signal(struct pt_regs *regs)
{
	struct k_sigaction ka_copy;
	siginfo_t info;
	sigset_t *oldset;
	int sig, handled_sig = 0;

	if (test_thread_flag(TIF_RESTORE_SIGMASK))
		oldset = &current->saved_sigmask;
	else
		oldset = &current->blocked;

	while((sig = get_signal_to_deliver(&info, &ka_copy, regs, NULL)) > 0){
		handled_sig = 1;
		/* Whee!  Actually deliver the signal.  */
		if(!handle_signal(regs, sig, &ka_copy, &info, oldset)){
			/* a signal was successfully delivered; the saved
			 * sigmask will have been stored in the signal frame,
			 * and will be restored by sigreturn, so we can simply
			 * clear the TIF_RESTORE_SIGMASK flag */
			if (test_thread_flag(TIF_RESTORE_SIGMASK))
				clear_thread_flag(TIF_RESTORE_SIGMASK);
			break;
		}
	}

	/* Did we come from a system call? */
	if(!handled_sig && (PT_REGS_SYSCALL_NR(regs) >= 0)){
		/* Restart the system call - no handlers present */
		switch(PT_REGS_SYSCALL_RET(regs)){
		case -ERESTARTNOHAND:
		case -ERESTARTSYS:
		case -ERESTARTNOINTR:
			PT_REGS_ORIG_SYSCALL(regs) = PT_REGS_SYSCALL_NR(regs);
			PT_REGS_RESTART_SYSCALL(regs);
			break;
		case -ERESTART_RESTARTBLOCK:
			PT_REGS_ORIG_SYSCALL(regs) = __NR_restart_syscall;
			PT_REGS_RESTART_SYSCALL(regs);
			break;
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

	/* if there's no signal to deliver, we just put the saved sigmask
	 * back */
	if (!handled_sig && test_thread_flag(TIF_RESTORE_SIGMASK)) {
		clear_thread_flag(TIF_RESTORE_SIGMASK);
		sigprocmask(SIG_SETMASK, &current->saved_sigmask, NULL);
	}
	return handled_sig;
}

int do_signal(void)
{
	return kern_do_signal(&current->thread.regs);
}

/*
 * Atomically swap in the new signal mask, and wait for a signal.
 */
long sys_sigsuspend(int history0, int history1, old_sigset_t mask)
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

long sys_sigaltstack(const stack_t __user *uss, stack_t __user *uoss)
{
	return do_sigaltstack(uss, uoss, PT_REGS_SP(&current->thread.regs));
}
