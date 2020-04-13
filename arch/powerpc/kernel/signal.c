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
#include <linux/syscalls.h>
#include <asm/hw_breakpoint.h>
#include <linux/uaccess.h>
#include <asm/switch_to.h>
#include <asm/unistd.h>
#include <asm/debug.h>
#include <asm/tm.h>

#include "signal.h"

#ifdef CONFIG_VSX
unsigned long copy_fpr_to_user(void __user *to,
			       struct task_struct *task)
{
	u64 buf[ELF_NFPREG];
	int i;

	/* save FPR copy to local buffer then write to the thread_struct */
	for (i = 0; i < (ELF_NFPREG - 1) ; i++)
		buf[i] = task->thread.TS_FPR(i);
	buf[i] = task->thread.fp_state.fpscr;
	return __copy_to_user(to, buf, ELF_NFPREG * sizeof(double));
}

unsigned long copy_fpr_from_user(struct task_struct *task,
				 void __user *from)
{
	u64 buf[ELF_NFPREG];
	int i;

	if (__copy_from_user(buf, from, ELF_NFPREG * sizeof(double)))
		return 1;
	for (i = 0; i < (ELF_NFPREG - 1) ; i++)
		task->thread.TS_FPR(i) = buf[i];
	task->thread.fp_state.fpscr = buf[i];

	return 0;
}

unsigned long copy_vsx_to_user(void __user *to,
			       struct task_struct *task)
{
	u64 buf[ELF_NVSRHALFREG];
	int i;

	/* save FPR copy to local buffer then write to the thread_struct */
	for (i = 0; i < ELF_NVSRHALFREG; i++)
		buf[i] = task->thread.fp_state.fpr[i][TS_VSRLOWOFFSET];
	return __copy_to_user(to, buf, ELF_NVSRHALFREG * sizeof(double));
}

unsigned long copy_vsx_from_user(struct task_struct *task,
				 void __user *from)
{
	u64 buf[ELF_NVSRHALFREG];
	int i;

	if (__copy_from_user(buf, from, ELF_NVSRHALFREG * sizeof(double)))
		return 1;
	for (i = 0; i < ELF_NVSRHALFREG ; i++)
		task->thread.fp_state.fpr[i][TS_VSRLOWOFFSET] = buf[i];
	return 0;
}

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
unsigned long copy_ckfpr_to_user(void __user *to,
				  struct task_struct *task)
{
	u64 buf[ELF_NFPREG];
	int i;

	/* save FPR copy to local buffer then write to the thread_struct */
	for (i = 0; i < (ELF_NFPREG - 1) ; i++)
		buf[i] = task->thread.TS_CKFPR(i);
	buf[i] = task->thread.ckfp_state.fpscr;
	return __copy_to_user(to, buf, ELF_NFPREG * sizeof(double));
}

unsigned long copy_ckfpr_from_user(struct task_struct *task,
					  void __user *from)
{
	u64 buf[ELF_NFPREG];
	int i;

	if (__copy_from_user(buf, from, ELF_NFPREG * sizeof(double)))
		return 1;
	for (i = 0; i < (ELF_NFPREG - 1) ; i++)
		task->thread.TS_CKFPR(i) = buf[i];
	task->thread.ckfp_state.fpscr = buf[i];

	return 0;
}

unsigned long copy_ckvsx_to_user(void __user *to,
				  struct task_struct *task)
{
	u64 buf[ELF_NVSRHALFREG];
	int i;

	/* save FPR copy to local buffer then write to the thread_struct */
	for (i = 0; i < ELF_NVSRHALFREG; i++)
		buf[i] = task->thread.ckfp_state.fpr[i][TS_VSRLOWOFFSET];
	return __copy_to_user(to, buf, ELF_NVSRHALFREG * sizeof(double));
}

unsigned long copy_ckvsx_from_user(struct task_struct *task,
					  void __user *from)
{
	u64 buf[ELF_NVSRHALFREG];
	int i;

	if (__copy_from_user(buf, from, ELF_NVSRHALFREG * sizeof(double)))
		return 1;
	for (i = 0; i < ELF_NVSRHALFREG ; i++)
		task->thread.ckfp_state.fpr[i][TS_VSRLOWOFFSET] = buf[i];
	return 0;
}
#endif /* CONFIG_PPC_TRANSACTIONAL_MEM */
#else
inline unsigned long copy_fpr_to_user(void __user *to,
				      struct task_struct *task)
{
	return __copy_to_user(to, task->thread.fp_state.fpr,
			      ELF_NFPREG * sizeof(double));
}

inline unsigned long copy_fpr_from_user(struct task_struct *task,
					void __user *from)
{
	return __copy_from_user(task->thread.fp_state.fpr, from,
			      ELF_NFPREG * sizeof(double));
}

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
inline unsigned long copy_ckfpr_to_user(void __user *to,
					 struct task_struct *task)
{
	return __copy_to_user(to, task->thread.ckfp_state.fpr,
			      ELF_NFPREG * sizeof(double));
}

inline unsigned long copy_ckfpr_from_user(struct task_struct *task,
						 void __user *from)
{
	return __copy_from_user(task->thread.ckfp_state.fpr, from,
				ELF_NFPREG * sizeof(double));
}
#endif /* CONFIG_PPC_TRANSACTIONAL_MEM */
#endif

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
	if (!access_ok((void __user *)newsp, oldsp - newsp))
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
	struct ksignal ksig = { .sig = 0 };
	int ret;

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

	rseq_signal_deliver(&ksig, tsk->thread.regs);

	if (is_32bit_task()) {
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

	/* Check valid addr_limit, TIF check is done there */
	addr_limit_user_check();

	if (thread_info_flags & _TIF_UPROBE)
		uprobe_notify_resume(regs);

	if (thread_info_flags & _TIF_PATCH_PENDING)
		klp_update_patch_state(current);

	if (thread_info_flags & _TIF_SIGPENDING) {
		BUG_ON(regs != current->thread.regs);
		do_signal(current);
	}

	if (thread_info_flags & _TIF_NOTIFY_RESUME) {
		clear_thread_flag(TIF_NOTIFY_RESUME);
		tracehook_notify_resume(regs);
		rseq_handle_notify_resume(NULL, regs);
	}

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

	unsigned long ret = tsk->thread.regs->gpr[1];

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	BUG_ON(tsk != current);

	if (MSR_TM_ACTIVE(tsk->thread.regs->msr)) {
		preempt_disable();
		tm_reclaim_current(TM_CAUSE_SIGNAL);
		if (MSR_TM_TRANSACTIONAL(tsk->thread.regs->msr))
			ret = tsk->thread.ckpt_regs.gpr[1];

		/*
		 * If we treclaim, we must clear the current thread's TM bits
		 * before re-enabling preemption. Otherwise we might be
		 * preempted and have the live MSR[TS] changed behind our back
		 * (tm_recheckpoint_new_task() would recheckpoint). Besides, we
		 * enter the signal handler in non-transactional state.
		 */
		tsk->thread.regs->msr &= ~MSR_TS_MASK;
		preempt_enable();
	}
#endif
	return ret;
}
