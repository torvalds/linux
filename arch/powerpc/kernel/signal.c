/*
 * Common signal handling code for both 32 and 64 bits
 *
 *    Copyright (c) 2007 Benjamin Herrenschmidt, IBM Corporation
 *    Extracted from signal_32.c and signal_64.c
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file README.legal in the main directory of
 * this archive for more details.
 */

#include <linux/tracehook.h>
#include <linux/signal.h>
#include <linux/uprobes.h>
#include <linux/key.h>
#include <linux/context_tracking.h>
#include <linux/livepatch.h>
#include <asm/hw_breakpoint.h>
#include <linux/uaccess.h>
#include <asm/unistd.h>
#include <asm/debug.h>
#include <asm/tm.h>

#include "signal.h"

/* Log an error when sending an unhandled signal to a process. Controlled
 * through debug.exception-trace sysctl.
 */

int show_unhandled_signals = 1;

/*
 * Allocate space for the signal frame
 */
void __user *get_sigframe(struct ksignal *ksig, unsigned long sp,
			   size_t frame_size, int is_32)
{
        unsigned long oldsp, newsp;

        /* Default to using normal stack */
        oldsp = get_clean_sp(sp, is_32);
	oldsp = sigsp(oldsp, ksig);
	newsp = (oldsp - frame_size) & ~0xFUL;

	/* Check access */
	if (!access_ok(VERIFY_WRITE, (void __user *)newsp, oldsp - newsp))
		return NULL;

        return (void __user *)newsp;
}

static void check_syscall_restart(struct pt_regs *regs, struct k_sigaction *ka,
				  int has_handler)
{
	unsigned long ret = regs->gpr[3];
	int restart = 1;

	/* syscall ? */
	if (TRAP(regs) != 0x0C00)
		return;

	/* error signalled ? */
	if (!(regs->ccr & 0x10000000))
		return;

	switch (ret) {
	case ERESTART_RESTARTBLOCK:
	case ERESTARTNOHAND:
		/* ERESTARTNOHAND means that the syscall should only be
		 * restarted if there was no handler for the signal, and since
		 * we only get here if there is a handler, we dont restart.
		 */
		restart = !has_handler;
		break;
	case ERESTARTSYS:
		/* ERESTARTSYS means to restart the syscall if there is no
		 * handler or the handler was registered with SA_RESTART
		 */
		restart = !has_handler || (ka->sa.sa_flags & SA_RESTART) != 0;
		break;
	case ERESTARTNOINTR:
		/* ERESTARTNOINTR means that the syscall should be
		 * called again after the signal handler returns.
		 */
		break;
	default:
		return;
	}
	if (restart) {
		if (ret == ERESTART_RESTARTBLOCK)
			regs->gpr[0] = __NR_restart_syscall;
		else
			regs->gpr[3] = regs->orig_gpr3;
		regs->nip -= 4;
		regs->result = 0;
	} else {
		regs->result = -EINTR;
		regs->gpr[3] = EINTR;
		regs->ccr |= 0x10000000;
	}
}

static void do_signal(struct task_struct *tsk)
{
	sigset_t *oldset = sigmask_to_save();
	struct ksignal ksig;
	int ret;
	int is32 = is_32bit_task();

	BUG_ON(tsk != current);

	get_signal(&ksig);

	/* Is there any syscall restart business here ? */
	check_syscall_restart(tsk->thread.regs, &ksig.ka, ksig.sig > 0);

	if (ksig.sig <= 0) {
		/* No signal to deliver -- put the saved sigmask back */
		restore_saved_sigmask();
		tsk->thread.regs->trap = 0;
		return;               /* no signals delivered */
	}

#ifndef CONFIG_PPC_ADV_DEBUG_REGS
        /*
	 * Reenable the DABR before delivering the signal to
	 * user space. The DABR will have been cleared if it
	 * triggered inside the kernel.
	 */
	if (tsk->thread.hw_brk.address && tsk->thread.hw_brk.type)
		__set_breakpoint(&tsk->thread.hw_brk);
#endif
	/* Re-enable the breakpoints for the signal stack */
	thread_change_pc(tsk, tsk->thread.regs);

	if (is32) {
        	if (ksig.ka.sa.sa_flags & SA_SIGINFO)
			ret = handle_rt_signal32(&ksig, oldset, tsk);
		else
			ret = handle_signal32(&ksig, oldset, tsk);
	} else {
		ret = handle_rt_signal64(&ksig, oldset, tsk);
	}

	tsk->thread.regs->trap = 0;
	signal_setup_done(ret, &ksig, test_thread_flag(TIF_SINGLESTEP));
}

void do_notify_resume(struct pt_regs *regs, unsigned long thread_info_flags)
{
	user_exit();

	if (thread_info_flags & _TIF_UPROBE)
		uprobe_notify_resume(regs);

	if (thread_info_flags & _TIF_SIGPENDING) {
		BUG_ON(regs != current->thread.regs);
		do_signal(current);
	}

	if (thread_info_flags & _TIF_NOTIFY_RESUME) {
		clear_thread_flag(TIF_NOTIFY_RESUME);
		tracehook_notify_resume(regs);
	}

	if (thread_info_flags & _TIF_PATCH_PENDING)
		klp_update_patch_state(current);

	user_enter();
}

unsigned long get_tm_stackpointer(struct task_struct *tsk)
{
	/* When in an active transaction that takes a signal, we need to be
	 * careful with the stack.  It's possible that the stack has moved back
	 * up after the tbegin.  The obvious case here is when the tbegin is
	 * called inside a function that returns before a tend.  In this case,
	 * the stack is part of the checkpointed transactional memory state.
	 * If we write over this non transactionally or in suspend, we are in
	 * trouble because if we get a tm abort, the program counter and stack
	 * pointer will be back at the tbegin but our in memory stack won't be
	 * valid anymore.
	 *
	 * To avoid this, when taking a signal in an active transaction, we
	 * need to use the stack pointer from the checkpointed state, rather
	 * than the speculated state.  This ensures that the signal context
	 * (written tm suspended) will be written below the stack required for
	 * the rollback.  The transaction is aborted because of the treclaim,
	 * so any memory written between the tbegin and the signal will be
	 * rolled back anyway.
	 *
	 * For signals taken in non-TM or suspended mode, we use the
	 * normal/non-checkpointed stack pointer.
	 */

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	BUG_ON(tsk != current);

	if (MSR_TM_ACTIVE(tsk->thread.regs->msr)) {
		tm_reclaim_current(TM_CAUSE_SIGNAL);
		if (MSR_TM_TRANSACTIONAL(tsk->thread.regs->msr))
			return tsk->thread.ckpt_regs.gpr[1];
	}
#endif
	return tsk->thread.regs->gpr[1];
}
