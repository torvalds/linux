#ifndef _ASM_SCORE_THREAD_INFO_H
#define _ASM_SCORE_THREAD_INFO_H

#ifdef __KERNEL__

#define KU_MASK	0x08
#define KU_USER	0x08
#define KU_KERN	0x00

#include <asm/page.h>
#include <linux/const.h>

/* thread information allocation */
#define THREAD_SIZE_ORDER	(1)
#define THREAD_SIZE		(PAGE_SIZE << THREAD_SIZE_ORDER)
#define THREAD_MASK		(THREAD_SIZE - _AC(1,UL))

#ifndef __ASSEMBLY__

#include <asm/processor.h>

/*
 * low level task data that entry.S needs immediate access to
 * - this struct should fit entirely inside of one cache line
 * - this struct shares the supervisor stack pages
 * - if the contents of this structure are changed, the assembly constants
 *   must also be changed
 */
struct thread_info {
	struct task_struct	*task;		/* main task structure */
	struct exec_domain	*exec_domain;	/* execution domain */
	unsigned long		flags;		/* low level flags */
	unsigned long		tp_value;	/* thread pointer */
	__u32			cpu;		/* current CPU */

	/* 0 => preemptable, < 0 => BUG */
	int			preempt_count;

	/*
	 * thread address space:
	 * 0-0xBFFFFFFF for user-thead
	 * 0-0xFFFFFFFF for kernel-thread
	 */
	mm_segment_t		addr_limit;
	struct restart_block	restart_block;
	struct pt_regs		*regs;
};

/*
 * macros/functions for gaining access to the thread information structure
 *
 * preempt_count needs to be 1 initially, until the scheduler is functional.
 */
#define INIT_THREAD_INFO(tsk)			\
{						\
	.task		= &tsk,			\
	.exec_domain	= &default_exec_domain,	\
	.cpu		= 0,			\
	.preempt_count	= 1,			\
	.addr_limit	= KERNEL_DS,		\
	.restart_block	= {			\
		.fn = do_no_restart_syscall,	\
	},					\
}

#define init_thread_info	(init_thread_union.thread_info)
#define init_stack		(init_thread_union.stack)

/* How to get the thread information struct from C. */
register struct thread_info *__current_thread_info __asm__("r28");
#define current_thread_info()	__current_thread_info

#endif /* !__ASSEMBLY__ */

#define PREEMPT_ACTIVE		0x10000000

/*
 * thread information flags
 * - these are process state flags that various assembly files may need to
 *   access
 * - pending work-to-be-done flags are in LSW
 * - other flags in MSW
 */
#define TIF_SYSCALL_TRACE	0	/* syscall trace active */
#define TIF_SIGPENDING		1	/* signal pending */
#define TIF_NEED_RESCHED	2	/* rescheduling necessary */
#define TIF_NOTIFY_RESUME	5	/* callback before returning to user */
#define TIF_RESTORE_SIGMASK	9	/* restore signal mask in do_signal() */
#define TIF_POLLING_NRFLAG	17	/* true if poll_idle() is polling
						 TIF_NEED_RESCHED */
#define TIF_MEMDIE		18	/* is terminating due to OOM killer */

#define _TIF_SYSCALL_TRACE	(1<<TIF_SYSCALL_TRACE)
#define _TIF_SIGPENDING		(1<<TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1<<TIF_NEED_RESCHED)
#define _TIF_NOTIFY_RESUME	(1<<TIF_NOTIFY_RESUME)
#define _TIF_RESTORE_SIGMASK	(1<<TIF_RESTORE_SIGMASK)
#define _TIF_POLLING_NRFLAG	(1<<TIF_POLLING_NRFLAG)

#define _TIF_WORK_MASK		(0x0000ffff)

#endif /* __KERNEL__ */

#endif /* _ASM_SCORE_THREAD_INFO_H */
