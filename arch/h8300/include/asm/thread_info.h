/* SPDX-License-Identifier: GPL-2.0 */
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

/*
 * Size of kernel stack for each process. This must be a power of 2...
 */
#define THREAD_SIZE_ORDER	1
#define THREAD_SIZE		8192	/* 2 pages */

#ifndef __ASSEMBLY__

/*
 * low level task data.
 * If you change this, change the TI_* offsets below to match.
 */
struct thread_info {
	struct task_struct *task;		/* main task structure */
	unsigned long	   flags;		/* low level flags */
	int		   cpu;			/* cpu we're on */
	int		   preempt_count;	/* 0 => preemptable, <0 => BUG */
};

/*
 * macros/functions for gaining access to the thread information structure
 */
#define INIT_THREAD_INFO(tsk)			\
{						\
	.task =		&tsk,			\
	.flags =	0,			\
	.cpu =		0,			\
	.preempt_count = INIT_PREEMPT_COUNT,	\
}

/* how to get the thread information struct from C */
static inline struct thread_info *current_thread_info(void)
{
	struct thread_info *ti;

	__asm__("mov.l	sp, %0\n\t"
		"and.w	%1, %T0"
		: "=&r"(ti)
		: "i" (~(THREAD_SIZE-1) & 0xffff));
	return ti;
}

#endif /* __ASSEMBLY__ */

/*
 * thread information flag bit numbers
 */
#define TIF_SYSCALL_TRACE	0	/* syscall trace active */
#define TIF_SIGPENDING		1	/* signal pending */
#define TIF_NEED_RESCHED	2	/* rescheduling necessary */
#define TIF_SINGLESTEP		3	/* singlestepping active */
#define TIF_MEMDIE		4	/* is terminating due to OOM killer */
#define TIF_RESTORE_SIGMASK	5	/* restore signal mask in do_signal() */
#define TIF_NOTIFY_RESUME	6	/* callback before returning to user */
#define TIF_SYSCALL_AUDIT	7	/* syscall auditing active */
#define TIF_SYSCALL_TRACEPOINT	8	/* for ftrace syscall instrumentation */
#define TIF_POLLING_NRFLAG	9	/* true if poll_idle() is polling TIF_NEED_RESCHED */
#define TIF_NOTIFY_SIGNAL	10	/* signal notifications exist */

/* as above, but as bit values */
#define _TIF_SYSCALL_TRACE	(1 << TIF_SYSCALL_TRACE)
#define _TIF_SIGPENDING		(1 << TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1 << TIF_NEED_RESCHED)
#define _TIF_NOTIFY_RESUME	(1 << TIF_NOTIFY_RESUME)
#define _TIF_SINGLESTEP		(1 << TIF_SINGLESTEP)
#define _TIF_SYSCALL_AUDIT	(1 << TIF_SYSCALL_AUDIT)
#define _TIF_SYSCALL_TRACEPOINT	(1 << TIF_SYSCALL_TRACEPOINT)
#define _TIF_POLLING_NRFLAG	(1 << TIF_POLLING_NRFLAG)
#define _TIF_NOTIFY_SIGNAL	(1 << TIF_NOTIFY_SIGNAL)

/* work to do in syscall trace */
#define _TIF_WORK_SYSCALL_MASK	(_TIF_SYSCALL_TRACE | _TIF_SINGLESTEP | \
				 _TIF_SYSCALL_AUDIT | _TIF_SYSCALL_TRACEPOINT)

/* work to do on any return to u-space */
#define _TIF_ALLWORK_MASK	(_TIF_SYSCALL_TRACE | _TIF_SIGPENDING      | \
				 _TIF_NEED_RESCHED  | _TIF_SYSCALL_AUDIT   | \
				 _TIF_SINGLESTEP    | _TIF_NOTIFY_RESUME   | \
				 _TIF_SYSCALL_TRACEPOINT | _TIF_NOTIFY_SIGNAL)

/* work to do on interrupt/exception return */
#define _TIF_WORK_MASK		(_TIF_ALLWORK_MASK & ~(_TIF_SYSCALL_TRACE | \
				 _TIF_SYSCALL_AUDIT | _TIF_SINGLESTEP))

#endif /* __KERNEL__ */

#endif /* _ASM_THREAD_INFO_H */
