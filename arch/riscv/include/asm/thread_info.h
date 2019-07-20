/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2009 Chen Liqin <liqin.chen@sunplusct.com>
 * Copyright (C) 2012 Regents of the University of California
 * Copyright (C) 2017 SiFive
 */

#ifndef _ASM_RISCV_THREAD_INFO_H
#define _ASM_RISCV_THREAD_INFO_H

#include <asm/page.h>
#include <linux/const.h>

/* thread information allocation */
#define THREAD_SIZE_ORDER	(1)
#define THREAD_SIZE		(PAGE_SIZE << THREAD_SIZE_ORDER)

#ifndef __ASSEMBLY__

#include <asm/processor.h>
#include <asm/csr.h>

typedef struct {
	unsigned long seg;
} mm_segment_t;

/*
 * low level task data that entry.S needs immediate access to
 * - this struct should fit entirely inside of one cache line
 * - if the members of this struct changes, the assembly constants
 *   in asm-offsets.c must be updated accordingly
 * - thread_info is included in task_struct at an offset of 0.  This means that
 *   tp points to both thread_info and task_struct.
 */
struct thread_info {
	unsigned long		flags;		/* low level flags */
	int                     preempt_count;  /* 0=>preemptible, <0=>BUG */
	mm_segment_t		addr_limit;
	/*
	 * These stack pointers are overwritten on every system call or
	 * exception.  SP is also saved to the stack it can be recovered when
	 * overwritten.
	 */
	long			kernel_sp;	/* Kernel stack pointer */
	long			user_sp;	/* User stack pointer */
	int			cpu;
};

/*
 * macros/functions for gaining access to the thread information structure
 *
 * preempt_count needs to be 1 initially, until the scheduler is functional.
 */
#define INIT_THREAD_INFO(tsk)			\
{						\
	.flags		= 0,			\
	.preempt_count	= INIT_PREEMPT_COUNT,	\
	.addr_limit	= KERNEL_DS,		\
}

#endif /* !__ASSEMBLY__ */

/*
 * thread information flags
 * - these are process state flags that various assembly files may need to
 *   access
 * - pending work-to-be-done flags are in lowest half-word
 * - other flags in upper half-word(s)
 */
#define TIF_SYSCALL_TRACE	0	/* syscall trace active */
#define TIF_NOTIFY_RESUME	1	/* callback before returning to user */
#define TIF_SIGPENDING		2	/* signal pending */
#define TIF_NEED_RESCHED	3	/* rescheduling necessary */
#define TIF_RESTORE_SIGMASK	4	/* restore signal mask in do_signal() */
#define TIF_MEMDIE		5	/* is terminating due to OOM killer */
#define TIF_SYSCALL_TRACEPOINT  6       /* syscall tracepoint instrumentation */
#define TIF_SYSCALL_AUDIT	7	/* syscall auditing */

#define _TIF_SYSCALL_TRACE	(1 << TIF_SYSCALL_TRACE)
#define _TIF_NOTIFY_RESUME	(1 << TIF_NOTIFY_RESUME)
#define _TIF_SIGPENDING		(1 << TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1 << TIF_NEED_RESCHED)
#define _TIF_SYSCALL_TRACEPOINT	(1 << TIF_SYSCALL_TRACEPOINT)
#define _TIF_SYSCALL_AUDIT	(1 << TIF_SYSCALL_AUDIT)

#define _TIF_WORK_MASK \
	(_TIF_NOTIFY_RESUME | _TIF_SIGPENDING | _TIF_NEED_RESCHED)

#define _TIF_SYSCALL_WORK \
	(_TIF_SYSCALL_TRACE | _TIF_SYSCALL_TRACEPOINT | _TIF_SYSCALL_AUDIT)

#endif /* _ASM_RISCV_THREAD_INFO_H */
