/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Port on Texas Instruments TMS320C6x architecture
 *
 *  Copyright (C) 2004, 2009, 2010, 2011 Texas Instruments Incorporated
 *  Author: Aurelien Jacquiot (aurelien.jacquiot@jaluna.com)
 *
 *  Updated for 2.6.3x: Mark Salter <msalter@redhat.com>
 */
#ifndef _ASM_C6X_THREAD_INFO_H
#define _ASM_C6X_THREAD_INFO_H

#ifdef __KERNEL__

#include <asm/page.h>

#ifdef CONFIG_4KSTACKS
#define THREAD_SIZE		4096
#define THREAD_SHIFT		12
#define THREAD_SIZE_ORDER	0
#else
#define THREAD_SIZE		8192
#define THREAD_SHIFT		13
#define THREAD_SIZE_ORDER	1
#endif

#define THREAD_START_SP		(THREAD_SIZE - 8)

#ifndef __ASSEMBLY__

typedef struct {
	unsigned long seg;
} mm_segment_t;

/*
 * low level task data.
 */
struct thread_info {
	struct task_struct	*task;		/* main task structure */
	unsigned long		flags;		/* low level flags */
	int			cpu;		/* cpu we're on */
	int			preempt_count;	/* 0 = preemptable, <0 = BUG */
	mm_segment_t		addr_limit;	/* thread address space */
};

/*
 * macros/functions for gaining access to the thread information structure
 *
 * preempt_count needs to be 1 initially, until the scheduler is functional.
 */
#define INIT_THREAD_INFO(tsk)			\
{						\
	.task		= &tsk,			\
	.flags		= 0,			\
	.cpu		= 0,			\
	.preempt_count	= INIT_PREEMPT_COUNT,	\
	.addr_limit	= KERNEL_DS,		\
}

/* get the thread information struct of current task */
static inline __attribute__((const))
struct thread_info *current_thread_info(void)
{
	struct thread_info *ti;
	asm volatile (" clr   .s2 B15,0,%1,%0\n"
		      : "=b" (ti)
		      : "Iu5" (THREAD_SHIFT - 1));
	return ti;
}

#define get_thread_info(ti)	get_task_struct((ti)->task)
#define put_thread_info(ti)	put_task_struct((ti)->task)
#endif /* __ASSEMBLY__ */

/*
 * thread information flag bit numbers
 * - pending work-to-be-done flags are in LSW
 * - other flags in MSW
 */
#define TIF_SYSCALL_TRACE	0	/* syscall trace active */
#define TIF_NOTIFY_RESUME	1	/* resumption notification requested */
#define TIF_SIGPENDING		2	/* signal pending */
#define TIF_NEED_RESCHED	3	/* rescheduling necessary */
#define TIF_RESTORE_SIGMASK	4	/* restore signal mask in do_signal() */

#define TIF_MEMDIE		17	/* OOM killer killed process */

#define TIF_WORK_MASK		0x00007FFE /* work on irq/exception return */
#define TIF_ALLWORK_MASK	0x00007FFF /* work on any return to u-space */

#endif /* __KERNEL__ */

#endif /* _ASM_C6X_THREAD_INFO_H */
