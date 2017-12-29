/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_M32R_THREAD_INFO_H
#define _ASM_M32R_THREAD_INFO_H

/* thread_info.h: m32r low-level thread information
 *
 * Copyright (C) 2002  David Howells (dhowells@redhat.com)
 * - Incorporating suggestions made by Linus Torvalds and Dave Miller
 * Copyright (C) 2004  Hirokazu Takata <takata at linux-m32r.org>
 */

#ifdef __KERNEL__

#ifndef __ASSEMBLY__
#include <asm/processor.h>
#endif

/*
 * low level task data that entry.S needs immediate access to
 * - this struct should fit entirely inside of one cache line
 * - this struct shares the supervisor stack pages
 * - if the contents of this structure are changed, the assembly constants must also be changed
 */
#ifndef __ASSEMBLY__

struct thread_info {
	struct task_struct	*task;		/* main task structure */
	unsigned long		flags;		/* low level flags */
	unsigned long		status;		/* thread-synchronous flags */
	__u32			cpu;		/* current CPU */
	int			preempt_count;	/* 0 => preemptable, <0 => BUG */

	mm_segment_t		addr_limit;	/* thread address space:
					 	   0-0xBFFFFFFF for user-thread
						   0-0xFFFFFFFF for kernel-thread
						*/

	__u8			supervisor_stack[0];
};

#endif /* !__ASSEMBLY__ */

#define THREAD_SIZE		(PAGE_SIZE << 1)
#define THREAD_SIZE_ORDER	1
/*
 * macros/functions for gaining access to the thread information structure
 */
#ifndef __ASSEMBLY__

#define INIT_THREAD_INFO(tsk)			\
{						\
	.task		= &tsk,			\
	.flags		= 0,			\
	.cpu		= 0,			\
	.preempt_count	= INIT_PREEMPT_COUNT,	\
	.addr_limit	= KERNEL_DS,		\
}

#define init_thread_info	(init_thread_union.thread_info)
#define init_stack		(init_thread_union.stack)

/* how to get the thread information struct from C */
static inline struct thread_info *current_thread_info(void)
{
	struct thread_info *ti;

	__asm__ __volatile__ (
		"ldi	%0, #%1			\n\t"
		"and	%0, sp			\n\t"
		: "=r" (ti) : "i" (~(THREAD_SIZE - 1))
	);

	return ti;
}

#define TI_FLAG_FAULT_CODE_SHIFT	28

static inline void set_thread_fault_code(unsigned int val)
{
	struct thread_info *ti = current_thread_info();
	ti->flags = (ti->flags & (~0 >> (32 - TI_FLAG_FAULT_CODE_SHIFT)))
		| (val << TI_FLAG_FAULT_CODE_SHIFT);
}

static inline unsigned int get_thread_fault_code(void)
{
	struct thread_info *ti = current_thread_info();
	return ti->flags >> TI_FLAG_FAULT_CODE_SHIFT;
}

#endif

/*
 * thread information flags
 * - these are process state flags that various assembly files may need to access
 * - pending work-to-be-done flags are in LSW
 * - other flags in MSW
 */
#define TIF_SYSCALL_TRACE	0	/* syscall trace active */
#define TIF_SIGPENDING		1	/* signal pending */
#define TIF_NEED_RESCHED	2	/* rescheduling necessary */
#define TIF_SINGLESTEP		3	/* restore singlestep on return to user mode */
#define TIF_NOTIFY_RESUME	5	/* callback before returning to user */
#define TIF_RESTORE_SIGMASK	8	/* restore signal mask in do_signal() */
#define TIF_USEDFPU		16	/* FPU was used by this task this quantum (SMP) */
#define TIF_MEMDIE		18	/* is terminating due to OOM killer */

#define _TIF_SYSCALL_TRACE	(1<<TIF_SYSCALL_TRACE)
#define _TIF_SIGPENDING		(1<<TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1<<TIF_NEED_RESCHED)
#define _TIF_SINGLESTEP		(1<<TIF_SINGLESTEP)
#define _TIF_NOTIFY_RESUME	(1<<TIF_NOTIFY_RESUME)
#define _TIF_USEDFPU		(1<<TIF_USEDFPU)

#define _TIF_WORK_MASK		(_TIF_SIGPENDING | _TIF_NEED_RESCHED | _TIF_NOTIFY_RESUME)
#define _TIF_ALLWORK_MASK	(_TIF_WORK_MASK | _TIF_SYSCALL_TRACE)

/*
 * Thread-synchronous status.
 *
 * This is different from the flags in that nobody else
 * ever touches our thread-synchronous status, so we don't
 * have to worry about atomic accesses.
 */
#define TS_USEDFPU		0x0001	/* FPU was used by this task this quantum (SMP) */

#endif /* __KERNEL__ */

#endif /* _ASM_M32R_THREAD_INFO_H */
