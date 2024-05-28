/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * Vineetg: Oct 2009
 *  No need for ARC specific thread_info allocator (kmalloc/free). This is
 *  anyways one page allocation, thus slab alloc can be short-circuited and
 *  the generic version (get_free_page) would be loads better.
 *
 * Sameer Dhavale: Codito Technologies 2004
 */

#ifndef _ASM_THREAD_INFO_H
#define _ASM_THREAD_INFO_H

#include <asm/page.h>

#ifdef CONFIG_16KSTACKS
#define THREAD_SIZE_ORDER 1
#else
#define THREAD_SIZE_ORDER 0
#endif

#define THREAD_SIZE     (PAGE_SIZE << THREAD_SIZE_ORDER)
#define THREAD_SHIFT	(PAGE_SHIFT << THREAD_SIZE_ORDER)

#ifndef __ASSEMBLY__

#include <linux/thread_info.h>

/*
 * low level task data that entry.S needs immediate access to
 * - this struct should fit entirely inside of one cache line
 * - this struct shares the supervisor stack pages
 * - if the contents of this structure are changed, the assembly constants
 *   must also be changed
 */
struct thread_info {
	unsigned long flags;		/* low level flags */
	unsigned long ksp;		/* kernel mode stack top in __switch_to */
	int preempt_count;		/* 0 => preemptible, <0 => BUG */
	int cpu;			/* current CPU */
	unsigned long thr_ptr;		/* TLS ptr */
	struct task_struct *task;	/* main task structure */
};

/*
 * initilaize thread_info for any @tsk
 *  - this is not related to init_task per se
 */
#define INIT_THREAD_INFO(tsk)			\
{						\
	.task       = &tsk,			\
	.flags      = 0,			\
	.cpu        = 0,			\
	.preempt_count  = INIT_PREEMPT_COUNT,	\
}

static inline __attribute_const__ struct thread_info *current_thread_info(void)
{
	register unsigned long sp asm("sp");
	return (struct thread_info *)(sp & ~(THREAD_SIZE - 1));
}

#endif /* !__ASSEMBLY__ */

/*
 * thread information flags
 * - these are process state flags that various assembly files may need to
 *   access
 * - pending work-to-be-done flags are in LSW
 * - other flags in MSW
 */
#define TIF_RESTORE_SIGMASK	0	/* restore sig mask in do_signal() */
#define TIF_NOTIFY_RESUME	1	/* resumption notification requested */
#define TIF_SIGPENDING		2	/* signal pending */
#define TIF_NEED_RESCHED	3	/* rescheduling necessary */
#define TIF_SYSCALL_AUDIT	4	/* syscall auditing active */
#define TIF_NOTIFY_SIGNAL	5	/* signal notifications exist */
#define TIF_SYSCALL_TRACE	15	/* syscall trace active */
/* true if poll_idle() is polling TIF_NEED_RESCHED */
#define TIF_MEMDIE		16
#define TIF_SYSCALL_TRACEPOINT	17	/* syscall tracepoint instrumentation */

#define _TIF_SYSCALL_TRACE	(1<<TIF_SYSCALL_TRACE)
#define _TIF_NOTIFY_RESUME	(1<<TIF_NOTIFY_RESUME)
#define _TIF_SIGPENDING		(1<<TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1<<TIF_NEED_RESCHED)
#define _TIF_SYSCALL_AUDIT	(1<<TIF_SYSCALL_AUDIT)
#define _TIF_NOTIFY_SIGNAL	(1<<TIF_NOTIFY_SIGNAL)
#define _TIF_MEMDIE		(1<<TIF_MEMDIE)
#define _TIF_SYSCALL_TRACEPOINT	(1<<TIF_SYSCALL_TRACEPOINT)

/* work to do on interrupt/exception return */
#define _TIF_WORK_MASK		(_TIF_NEED_RESCHED | _TIF_SIGPENDING | \
				 _TIF_NOTIFY_RESUME | _TIF_NOTIFY_SIGNAL)

#define _TIF_SYSCALL_WORK	(_TIF_SYSCALL_TRACE | _TIF_SYSCALL_TRACEPOINT)

/*
 * _TIF_ALLWORK_MASK includes SYSCALL_TRACE, but we don't need it.
 * SYSCALL_TRACE is anyway separately/unconditionally tested right after a
 * syscall, so all that remains to be tested is _TIF_WORK_MASK
 */

#endif /* _ASM_THREAD_INFO_H */
