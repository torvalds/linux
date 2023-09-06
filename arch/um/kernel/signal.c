// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#include <linux/module.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/ftrace.h>
#include <asm/siginfo.h>
#include <asm/signal.h>
#include <asm/unistd.h>
#include <frame_kern.h>
#include <kern_util.h>
#include <os.h>

EXPORT_SYMBOL(block_signals);
EXPORT_SYMBOL(unblock_signals);

void block_signals_trace(void)
{
	block_signals();
	if (current_thread_info())
		trace_hardirqs_off();
}

void unblock_signals_trace(void)
{
	if (current_thread_info())
		trace_hardirqs_on();
	unblock_signals();
}

void um_trace_signals_on(void)
{
	if (current_thread_info())
		trace_hardirqs_on();
}

void um_trace_signals_off(void)
{
	if (current_thread_info())
		trace_hardirqs_off();
}

/*
 * OK, we're invoking a handler
 */
static void handle_signal(struct ksignal *ksig, struct pt_regs *regs)
{
	sigset_t *oldset = sigmask_to_save();
	int singlestep = 0;
	unsigned long sp;
	int err;

	if (test_thread_flag(TIF_SINGLESTEP) && (current->ptrace & PT_PTRACED))
		singlestep = 1;

	/* Did we come from a system call? */
	if (PT_REGS_SYSCALL_NR(regs) >= 0) {
		/* If so, check system call restarting.. */
		switch (PT_REGS_SYSCALL_RET(regs)) {
		case -ERESTART_RESTARTBLOCK:
		case -ERESTARTNOHAND:
			PT_REGS_SYSCALL_RET(regs) = -EINTR;
			break;

		case -ERESTARTSYS:
			if (!(ksig->ka.sa.sa_flags & SA_RESTART)) {
				PT_REGS_SYSCALL_RET(regs) = -EINTR;
				break;
			}
			fallthrough;
		case -ERESTARTNOINTR:
			PT_REGS_RESTART_SYSCALL(regs);
			PT_REGS_ORIG_SYSCALL(regs) = PT_REGS_SYSCALL_NR(regs);
			break;
		}
	}

	sp = PT_REGS_SP(regs);
	if ((ksig->ka.sa.sa_flags & SA_ONSTACK) && (sas_ss_flags(sp) == 0))
		sp = current->sas_ss_sp + current->sas_ss_size;

#ifdef CONFIG_ARCH_HAS_SC_SIGNALS
	if (!(ksig->ka.sa.sa_flags & SA_SIGINFO))
		err = setup_signal_stack_sc(sp, ksig, regs, oldset);
	else
#endif
		err = setup_signal_stack_si(sp, ksig, regs, oldset);

	signal_setup_done(err, ksig, singlestep);
}

void do_signal(struct pt_regs *regs)
{
	struct ksignal ksig;
	int handled_sig = 0;

	while (get_signal(&ksig)) {
		handled_sig = 1;
		/* Whee!  Actually deliver the signal.  */
		handle_signal(&ksig, regs);
	}

	/* Did we come from a system call? */
	if (!handled_sig && (PT_REGS_SYSCALL_NR(regs) >= 0)) {
		/* Restart the system call - no handlers present */
		switch (PT_REGS_SYSCALL_RET(regs)) {
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

	/*
	 * This closes a way to execute a system call on the host.  If
	 * you set a breakpoint on a system call instruction and singlestep
	 * from it, the tracing thread used to PTRACE_SINGLESTEP the process
	 * rather than PTRACE_SYSCALL it, allowing the system call to execute
	 * on the host.  The tracing thread will check this flag and
	 * PTRACE_SYSCALL if necessary.
	 */
	if (test_thread_flag(TIF_SINGLESTEP))
		current->thread.singlestep_syscall =
			is_syscall(PT_REGS_IP(&current->thread.regs));

	/*
	 * if there's no signal to deliver, we just put the saved sigmask
	 * back
	 */
	if (!handled_sig)
		restore_saved_sigmask();
}
