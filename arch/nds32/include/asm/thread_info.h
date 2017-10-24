// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#ifndef __ASM_NDS32_THREAD_INFO_H
#define __ASM_NDS32_THREAD_INFO_H

#ifdef __KERNEL__

#define THREAD_SIZE_ORDER 	(1)
#define THREAD_SIZE		(PAGE_SIZE << THREAD_SIZE_ORDER)

#ifndef __ASSEMBLY__

struct task_struct;

#include <asm/ptrace.h>
#include <asm/types.h>

typedef unsigned long mm_segment_t;

/*
 * low level task data that entry.S needs immediate access to.
 * __switch_to() assumes cpu_context follows immediately after cpu_domain.
 */
struct thread_info {
	unsigned long flags;	/* low level flags */
	__s32 preempt_count;	/* 0 => preemptable, <0 => bug */
	mm_segment_t addr_limit;	/* address limit */
};
#define INIT_THREAD_INFO(tsk)						\
{									\
	.preempt_count	= INIT_PREEMPT_COUNT,				\
	.addr_limit	= KERNEL_DS,					\
}
#define thread_saved_pc(tsk) ((unsigned long)(tsk->thread.cpu_context.pc))
#define thread_saved_fp(tsk) ((unsigned long)(tsk->thread.cpu_context.fp))
#endif

/*
 * thread information flags:
 *  TIF_SYSCALL_TRACE	- syscall trace active
 *  TIF_SIGPENDING	- signal pending
 *  TIF_NEED_RESCHED	- rescheduling necessary
 *  TIF_NOTIFY_RESUME	- callback before returning to user
 *  TIF_USEDFPU		- FPU was used by this task this quantum (SMP)
 *  TIF_POLLING_NRFLAG	- true if poll_idle() is polling TIF_NEED_RESCHED
 */
#define TIF_SIGPENDING		1
#define TIF_NEED_RESCHED	2
#define TIF_SINGLESTEP		3
#define TIF_NOTIFY_RESUME	4	/* callback before returning to user */
#define TIF_SYSCALL_TRACE	8
#define TIF_USEDFPU             16
#define TIF_POLLING_NRFLAG	17
#define TIF_MEMDIE		18
#define TIF_FREEZE		19
#define TIF_RESTORE_SIGMASK	20

#define _TIF_SIGPENDING		(1 << TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1 << TIF_NEED_RESCHED)
#define _TIF_NOTIFY_RESUME	(1 << TIF_NOTIFY_RESUME)
#define _TIF_SINGLESTEP		(1 << TIF_SINGLESTEP)
#define _TIF_SYSCALL_TRACE	(1 << TIF_SYSCALL_TRACE)
#define _TIF_POLLING_NRFLAG	(1 << TIF_POLLING_NRFLAG)
#define _TIF_FREEZE		(1 << TIF_FREEZE)
#define _TIF_RESTORE_SIGMASK	(1 << TIF_RESTORE_SIGMASK)

/*
 * Change these and you break ASM code in entry-common.S
 */
#define _TIF_WORK_MASK		0x000000ff
#define _TIF_WORK_SYSCALL_ENTRY (_TIF_SYSCALL_TRACE | _TIF_SINGLESTEP)
#define _TIF_WORK_SYSCALL_LEAVE (_TIF_SYSCALL_TRACE | _TIF_SINGLESTEP)

#endif /* __KERNEL__ */
#endif /* __ASM_NDS32_THREAD_INFO_H */
