/*
 * Copyright 2004-2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _ASM_THREAD_INFO_H
#define _ASM_THREAD_INFO_H

#include <asm/page.h>
#include <asm/entry.h>
#include <asm/l1layout.h>
#include <linux/compiler.h>

#ifdef __KERNEL__

/* Thread Align Mask to reach to the top of the stack
 * for any process
 */
#define ALIGN_PAGE_MASK		0xffffe000

/*
 * Size of kernel stack for each process. This must be a power of 2...
 */
#define THREAD_SIZE_ORDER	1
#define THREAD_SIZE		8192	/* 2 pages */
#define STACK_WARN		(THREAD_SIZE/8)

#ifndef __ASSEMBLY__

typedef unsigned long mm_segment_t;

/*
 * low level task data.
 * If you change this, change the TI_* offsets below to match.
 */

struct thread_info {
	struct task_struct *task;	/* main task structure */
	struct exec_domain *exec_domain;	/* execution domain */
	unsigned long flags;	/* low level flags */
	int cpu;		/* cpu we're on */
	int preempt_count;	/* 0 => preemptable, <0 => BUG */
	mm_segment_t addr_limit;	/* address limit */
	struct restart_block restart_block;
#ifndef CONFIG_SMP
	struct l1_scratch_task_info l1_task_info;
#endif
};

/*
 * macros/functions for gaining access to the thread information structure
 */
#define INIT_THREAD_INFO(tsk)			\
{						\
	.task		= &tsk,			\
	.exec_domain	= &default_exec_domain,	\
	.flags		= 0,			\
	.cpu		= 0,			\
	.preempt_count	= INIT_PREEMPT_COUNT,	\
	.restart_block	= {			\
		.fn = do_no_restart_syscall,	\
	},					\
}
#define init_thread_info	(init_thread_union.thread_info)
#define init_stack		(init_thread_union.stack)

/* Given a task stack pointer, you can find its corresponding
 * thread_info structure just by masking it to the THREAD_SIZE
 * boundary (currently 8K as you can see above).
 */
__attribute_const__
static inline struct thread_info *current_thread_info(void)
{
	struct thread_info *ti;
	__asm__("%0 = sp;" : "=da"(ti));
	return (struct thread_info *)((long)ti & ~((long)THREAD_SIZE-1));
}

#endif				/* __ASSEMBLY__ */

/*
 * Offsets in thread_info structure, used in assembly code
 */
#define TI_TASK		0
#define TI_EXECDOMAIN	4
#define TI_FLAGS	8
#define TI_CPU		12
#define TI_PREEMPT	16

/*
 * thread information flag bit numbers
 */
#define TIF_SYSCALL_TRACE	0	/* syscall trace active */
#define TIF_SIGPENDING		1	/* signal pending */
#define TIF_NEED_RESCHED	2	/* rescheduling necessary */
#define TIF_MEMDIE		4	/* is terminating due to OOM killer */
#define TIF_RESTORE_SIGMASK	5	/* restore signal mask in do_signal() */
#define TIF_IRQ_SYNC		7	/* sync pipeline stage */
#define TIF_NOTIFY_RESUME	8	/* callback before returning to user */
#define TIF_SINGLESTEP		9

/* as above, but as bit values */
#define _TIF_SYSCALL_TRACE	(1<<TIF_SYSCALL_TRACE)
#define _TIF_SIGPENDING		(1<<TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1<<TIF_NEED_RESCHED)
#define _TIF_IRQ_SYNC		(1<<TIF_IRQ_SYNC)
#define _TIF_NOTIFY_RESUME	(1<<TIF_NOTIFY_RESUME)
#define _TIF_SINGLESTEP		(1<<TIF_SINGLESTEP)

#define _TIF_WORK_MASK		0x0000FFFE	/* work to do on interrupt/exception return */

#endif				/* __KERNEL__ */

#endif				/* _ASM_THREAD_INFO_H */
