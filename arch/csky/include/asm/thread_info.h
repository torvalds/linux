/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_CSKY_THREAD_INFO_H
#define _ASM_CSKY_THREAD_INFO_H

#ifndef __ASSEMBLY__

#include <asm/types.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <abi/switch_context.h>

struct thread_info {
	struct task_struct	*task;
	void			*dump_exec_domain;
	unsigned long		flags;
	int			preempt_count;
	unsigned long		tp_value;
	mm_segment_t		addr_limit;
	struct restart_block	restart_block;
	struct pt_regs		*regs;
	unsigned int		cpu;
};

#define INIT_THREAD_INFO(tsk)			\
{						\
	.task		= &tsk,			\
	.preempt_count  = INIT_PREEMPT_COUNT,	\
	.addr_limit     = KERNEL_DS,		\
	.cpu		= 0,			\
	.restart_block = {			\
		.fn = do_no_restart_syscall,	\
	},					\
}

#define THREAD_SIZE_ORDER (THREAD_SHIFT - PAGE_SHIFT)

#define thread_saved_fp(tsk) \
	((unsigned long)(((struct switch_stack *)(tsk->thread.sp))->r8))

#define thread_saved_sp(tsk) \
	((unsigned long)(tsk->thread.sp))

#define thread_saved_lr(tsk) \
	((unsigned long)(((struct switch_stack *)(tsk->thread.sp))->r15))

static inline struct thread_info *current_thread_info(void)
{
	unsigned long sp;

	asm volatile("mov %0, sp\n":"=r"(sp));

	return (struct thread_info *)(sp & ~(THREAD_SIZE - 1));
}

#endif /* !__ASSEMBLY__ */

#define TIF_SIGPENDING		0	/* signal pending */
#define TIF_NOTIFY_RESUME	1       /* callback before returning to user */
#define TIF_NEED_RESCHED	2	/* rescheduling necessary */
#define TIF_UPROBE		3	/* uprobe breakpoint or singlestep */
#define TIF_SYSCALL_TRACE	4	/* syscall trace active */
#define TIF_SYSCALL_TRACEPOINT	5       /* syscall tracepoint instrumentation */
#define TIF_SYSCALL_AUDIT	6	/* syscall auditing */
#define TIF_NOTIFY_SIGNAL	7	/* signal notifications exist */
#define TIF_POLLING_NRFLAG	16	/* poll_idle() is TIF_NEED_RESCHED */
#define TIF_MEMDIE		18      /* is terminating due to OOM killer */
#define TIF_RESTORE_SIGMASK	20	/* restore signal mask in do_signal() */
#define TIF_SECCOMP		21	/* secure computing */

#define _TIF_SIGPENDING		(1 << TIF_SIGPENDING)
#define _TIF_NOTIFY_RESUME	(1 << TIF_NOTIFY_RESUME)
#define _TIF_NEED_RESCHED	(1 << TIF_NEED_RESCHED)
#define _TIF_SYSCALL_TRACE	(1 << TIF_SYSCALL_TRACE)
#define _TIF_SYSCALL_TRACEPOINT	(1 << TIF_SYSCALL_TRACEPOINT)
#define _TIF_SYSCALL_AUDIT	(1 << TIF_SYSCALL_AUDIT)
#define _TIF_NOTIFY_SIGNAL	(1 << TIF_NOTIFY_SIGNAL)
#define _TIF_UPROBE		(1 << TIF_UPROBE)
#define _TIF_POLLING_NRFLAG	(1 << TIF_POLLING_NRFLAG)
#define _TIF_MEMDIE		(1 << TIF_MEMDIE)
#define _TIF_RESTORE_SIGMASK	(1 << TIF_RESTORE_SIGMASK)
#define _TIF_SECCOMP		(1 << TIF_SECCOMP)

#define _TIF_WORK_MASK		(_TIF_NEED_RESCHED | _TIF_SIGPENDING | \
				 _TIF_NOTIFY_RESUME | _TIF_UPROBE | \
				 _TIF_NOTIFY_SIGNAL)

#define _TIF_SYSCALL_WORK	(_TIF_SYSCALL_TRACE | _TIF_SYSCALL_AUDIT | \
				 _TIF_SYSCALL_TRACEPOINT | _TIF_SECCOMP)

#endif	/* _ASM_CSKY_THREAD_INFO_H */
