/* thread_info.h: h8300 low-level thread information
 * adapted from the i386 and PPC versions by Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 * Copyright (C) 2002  David Howells (dhowells@redhat.com)
 * - Incorporating suggestions made by Linus Torvalds and Dave Miller
 */

#ifndef _ASM_THREAD_INFO_H
#define _ASM_THREAD_INFO_H

#include <asm/page.h>

#ifdef __KERNEL__

#ifndef __ASSEMBLY__

/*
 * low level task data.
 * If you change this, change the TI_* offsets below to match.
 */
struct thread_info {
	struct task_struct *task;		/* main task structure */
	struct exec_domain *exec_domain;	/* execution domain */
	unsigned long	   flags;		/* low level flags */
	int		   cpu;			/* cpu we're on */
	int		   preempt_count;	/* 0 => preemptable, <0 => BUG */
	struct restart_block restart_block;
};

/*
 * macros/functions for gaining access to the thread information structure
 */
#define INIT_THREAD_INFO(tsk)			\
{						\
	.task =		&tsk,			\
	.exec_domain =	&default_exec_domain,	\
	.flags =	0,			\
	.cpu =		0,			\
	.preempt_count = INIT_PREEMPT_COUNT,	\
	.restart_block	= {			\
		.fn = do_no_restart_syscall,	\
	},					\
}

#define init_thread_info	(init_thread_union.thread_info)
#define init_stack		(init_thread_union.stack)


/*
 * Size of kernel stack for each process. This must be a power of 2...
 */
#define THREAD_SIZE_ORDER	1
#define THREAD_SIZE		8192	/* 2 pages */


/* how to get the thread information struct from C */
static inline struct thread_info *current_thread_info(void)
{
	struct thread_info *ti;
	__asm__(
		"mov.l	sp, %0 \n\t"
		"and.l	%1, %0"
		: "=&r"(ti)
		: "i" (~(THREAD_SIZE-1))
		);
	return ti;
}

#endif /* __ASSEMBLY__ */

/*
 * Offsets in thread_info structure, used in assembly code
 */
#define TI_TASK		0
#define TI_EXECDOMAIN	4
#define TI_FLAGS	8
#define TI_CPU		12
#define TI_PRE_COUNT	16

#define	PREEMPT_ACTIVE	0x4000000

/*
 * thread information flag bit numbers
 */
#define TIF_SYSCALL_TRACE	0	/* syscall trace active */
#define TIF_SIGPENDING		1	/* signal pending */
#define TIF_NEED_RESCHED	2	/* rescheduling necessary */
#define TIF_MEMDIE		4	/* is terminating due to OOM killer */
#define TIF_RESTORE_SIGMASK	5	/* restore signal mask in do_signal() */
#define TIF_NOTIFY_RESUME	6	/* callback before returning to user */

/* as above, but as bit values */
#define _TIF_SYSCALL_TRACE	(1<<TIF_SYSCALL_TRACE)
#define _TIF_SIGPENDING		(1<<TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1<<TIF_NEED_RESCHED)
#define _TIF_NOTIFY_RESUME	(1 << TIF_NOTIFY_RESUME)

#define _TIF_WORK_MASK		(_TIF_SIGPENDING | _TIF_NEED_RESCHED | \
				 _TIF_NOTIFY_RESUME)

#endif /* __KERNEL__ */

#endif /* _ASM_THREAD_INFO_H */
