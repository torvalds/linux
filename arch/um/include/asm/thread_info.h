/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#ifndef __UM_THREAD_INFO_H
#define __UM_THREAD_INFO_H

#define THREAD_SIZE_ORDER CONFIG_KERNEL_STACK_ORDER
#define THREAD_SIZE ((1 << CONFIG_KERNEL_STACK_ORDER) * PAGE_SIZE)

#ifndef __ASSEMBLER__

#include <asm/types.h>
#include <asm/page.h>
#include <asm/segment.h>
#include <sysdep/ptrace_user.h>

struct thread_info {
	unsigned long		flags;		/* low level flags */
	__u32			cpu;		/* current CPU */
	int			preempt_count;  /* 0 => preemptable,
						   <0 => BUG */
};

#define INIT_THREAD_INFO(tsk)			\
{						\
	.flags =		0,		\
	.cpu =		0,			\
	.preempt_count = INIT_PREEMPT_COUNT,	\
}

#endif

#define TIF_SYSCALL_TRACE	0	/* syscall trace active */
#define TIF_SIGPENDING		1	/* signal pending */
#define TIF_NEED_RESCHED	2	/* rescheduling necessary */
#define TIF_NOTIFY_SIGNAL	3	/* signal notifications exist */
#define TIF_RESTART_BLOCK	4
#define TIF_MEMDIE		5	/* is terminating due to OOM killer */
#define TIF_SYSCALL_AUDIT	6
#define TIF_RESTORE_SIGMASK	7
#define TIF_NOTIFY_RESUME	8
#define TIF_SECCOMP		9	/* secure computing */
#define TIF_SINGLESTEP		10	/* single stepping userspace */
#define TIF_SYSCALL_TRACEPOINT	11	/* syscall tracepoint instrumentation */


#define _TIF_SYSCALL_TRACE	(1 << TIF_SYSCALL_TRACE)
#define _TIF_SIGPENDING		(1 << TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1 << TIF_NEED_RESCHED)
#define _TIF_NOTIFY_SIGNAL	(1 << TIF_NOTIFY_SIGNAL)
#define _TIF_MEMDIE		(1 << TIF_MEMDIE)
#define _TIF_SYSCALL_AUDIT	(1 << TIF_SYSCALL_AUDIT)
#define _TIF_NOTIFY_RESUME	(1 << TIF_NOTIFY_RESUME)
#define _TIF_SECCOMP		(1 << TIF_SECCOMP)
#define _TIF_SINGLESTEP		(1 << TIF_SINGLESTEP)

#define _TIF_WORK_MASK		(_TIF_NEED_RESCHED | _TIF_SIGPENDING | _TIF_NOTIFY_SIGNAL | \
				 _TIF_NOTIFY_RESUME)

#endif
