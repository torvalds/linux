/*
 * Based on arch/arm/include/asm/thread_info.h
 *
 * Copyright (C) 2002 Russell King.
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_THREAD_INFO_H
#define __ASM_THREAD_INFO_H

#ifdef __KERNEL__

#include <linux/compiler.h>

#ifndef CONFIG_ARM64_64K_PAGES
#define THREAD_SIZE_ORDER	2
#endif

#define THREAD_SIZE		16384
#define THREAD_START_SP		(THREAD_SIZE - 16)

#ifndef __ASSEMBLY__

struct task_struct;
struct exec_domain;

#include <asm/types.h>

typedef unsigned long mm_segment_t;

/*
 * low level task data that entry.S needs immediate access to.
 * __switch_to() assumes cpu_context follows immediately after cpu_domain.
 */
struct thread_info {
	unsigned long		flags;		/* low level flags */
	mm_segment_t		addr_limit;	/* address limit */
	struct task_struct	*task;		/* main task structure */
	struct exec_domain	*exec_domain;	/* execution domain */
	struct restart_block	restart_block;
	int			preempt_count;	/* 0 => preemptable, <0 => bug */
	int			cpu;		/* cpu */
};

#define INIT_THREAD_INFO(tsk)						\
{									\
	.task		= &tsk,						\
	.exec_domain	= &default_exec_domain,				\
	.flags		= 0,						\
	.preempt_count	= INIT_PREEMPT_COUNT,				\
	.addr_limit	= KERNEL_DS,					\
	.restart_block	= {						\
		.fn	= do_no_restart_syscall,			\
	},								\
}

#define init_thread_info	(init_thread_union.thread_info)
#define init_stack		(init_thread_union.stack)

/*
 * how to get the thread information struct from C
 */
static inline struct thread_info *current_thread_info(void) __attribute_const__;

static inline struct thread_info *current_thread_info(void)
{
	register unsigned long sp asm ("sp");
	return (struct thread_info *)(sp & ~(THREAD_SIZE - 1));
}

#define thread_saved_pc(tsk)	\
	((unsigned long)(tsk->thread.cpu_context.pc))
#define thread_saved_sp(tsk)	\
	((unsigned long)(tsk->thread.cpu_context.sp))
#define thread_saved_fp(tsk)	\
	((unsigned long)(tsk->thread.cpu_context.fp))

#endif

/*
 * We use bit 30 of the preempt_count to indicate that kernel
 * preemption is occurring.  See <asm/hardirq.h>.
 */
#define PREEMPT_ACTIVE	0x40000000

/*
 * thread information flags:
 *  TIF_SYSCALL_TRACE	- syscall trace active
 *  TIF_SYSCALL_TRACEPOINT - syscall tracepoint for ftrace
 *  TIF_SYSCALL_AUDIT	- syscall auditing
 *  TIF_SECOMP		- syscall secure computing
 *  TIF_SIGPENDING	- signal pending
 *  TIF_NEED_RESCHED	- rescheduling necessary
 *  TIF_NOTIFY_RESUME	- callback before returning to user
 *  TIF_USEDFPU		- FPU was used by this task this quantum (SMP)
 *  TIF_POLLING_NRFLAG	- true if poll_idle() is polling TIF_NEED_RESCHED
 */
#define TIF_SIGPENDING		0
#define TIF_NEED_RESCHED	1
#define TIF_NOTIFY_RESUME	2	/* callback before returning to user */
#define TIF_SYSCALL_TRACE	8
#define TIF_SYSCALL_AUDIT	9
#define TIF_SYSCALL_TRACEPOINT	10
#define TIF_SECCOMP		11
#define TIF_POLLING_NRFLAG	16
#define TIF_MEMDIE		18	/* is terminating due to OOM killer */
#define TIF_FREEZE		19
#define TIF_RESTORE_SIGMASK	20
#define TIF_SINGLESTEP		21
#define TIF_32BIT		22	/* 32bit process */
#define TIF_SWITCH_MM		23	/* deferred switch_mm */

#define _TIF_SIGPENDING		(1 << TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1 << TIF_NEED_RESCHED)
#define _TIF_NOTIFY_RESUME	(1 << TIF_NOTIFY_RESUME)
#define _TIF_SYSCALL_TRACE	(1 << TIF_SYSCALL_TRACE)
#define _TIF_SYSCALL_AUDIT	(1 << TIF_SYSCALL_AUDIT)
#define _TIF_SYSCALL_TRACEPOINT	(1 << TIF_SYSCALL_TRACEPOINT)
#define _TIF_SECCOMP		(1 << TIF_SECCOMP)
#define _TIF_32BIT		(1 << TIF_32BIT)

#define _TIF_WORK_MASK		(_TIF_NEED_RESCHED | _TIF_SIGPENDING | \
				 _TIF_NOTIFY_RESUME)

#define _TIF_SYSCALL_WORK	(_TIF_SYSCALL_TRACE | _TIF_SYSCALL_AUDIT | \
				 _TIF_SYSCALL_TRACEPOINT | _TIF_SECCOMP)

#endif /* __KERNEL__ */
#endif /* __ASM_THREAD_INFO_H */
