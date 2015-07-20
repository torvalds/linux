/*
 * Copyright (C) 2002  David Howells (dhowells@redhat.com)
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _ASM_TILE_THREAD_INFO_H
#define _ASM_TILE_THREAD_INFO_H

#include <asm/processor.h>
#include <asm/page.h>
#ifndef __ASSEMBLY__

/*
 * Low level task data that assembly code needs immediate access to.
 * The structure is placed at the bottom of the supervisor stack.
 */
struct thread_info {
	struct task_struct	*task;		/* main task structure */
	unsigned long		flags;		/* low level flags */
	unsigned long		status;		/* thread-synchronous flags */
	__u32			homecache_cpu;	/* CPU we are homecached on */
	__u32			cpu;		/* current CPU */
	int			preempt_count;	/* 0 => preemptable,
						   <0 => BUG */

	mm_segment_t		addr_limit;	/* thread address space
						   (KERNEL_DS or USER_DS) */
	struct single_step_state *step_state;	/* single step state
						   (if non-zero) */
	int			align_ctl;	/* controls unaligned access */
#ifdef __tilegx__
	unsigned long		unalign_jit_tmp[4]; /* temp r0..r3 storage */
	void __user		*unalign_jit_base; /* unalign fixup JIT base */
#endif
	bool in_backtrace;			/* currently doing backtrace? */
};

/*
 * macros/functions for gaining access to the thread information structure.
 */
#define INIT_THREAD_INFO(tsk)			\
{						\
	.task		= &tsk,			\
	.flags		= 0,			\
	.cpu		= 0,			\
	.preempt_count	= INIT_PREEMPT_COUNT,	\
	.addr_limit	= KERNEL_DS,		\
	.step_state	= NULL,			\
	.align_ctl	= 0,			\
}

#define init_thread_info	(init_thread_union.thread_info)
#define init_stack		(init_thread_union.stack)

#endif /* !__ASSEMBLY__ */

#if PAGE_SIZE < 8192
#define THREAD_SIZE_ORDER (13 - PAGE_SHIFT)
#else
#define THREAD_SIZE_ORDER (0)
#endif
#define THREAD_SIZE_PAGES (1 << THREAD_SIZE_ORDER)

#define THREAD_SIZE (PAGE_SIZE << THREAD_SIZE_ORDER)
#define LOG2_THREAD_SIZE (PAGE_SHIFT + THREAD_SIZE_ORDER)

#define STACK_WARN             (THREAD_SIZE/8)

#ifndef __ASSEMBLY__

void arch_release_thread_info(struct thread_info *info);

/* How to get the thread information struct from C. */
register unsigned long stack_pointer __asm__("sp");

#define current_thread_info() \
  ((struct thread_info *)(stack_pointer & -THREAD_SIZE))

/* Sit on a nap instruction until interrupted. */
extern void smp_nap(void);

/* Enable interrupts racelessly and nap forever: helper for arch_cpu_idle(). */
extern void _cpu_idle(void);

#else /* __ASSEMBLY__ */

/*
 * How to get the thread information struct from assembly.
 * Note that we use different macros since different architectures
 * have different semantics in their "mm" instruction and we would
 * like to guarantee that the macro expands to exactly one instruction.
 */
#ifdef __tilegx__
#define EXTRACT_THREAD_INFO(reg) mm reg, zero, LOG2_THREAD_SIZE, 63
#else
#define GET_THREAD_INFO(reg) mm reg, sp, zero, LOG2_THREAD_SIZE, 31
#endif

#endif /* !__ASSEMBLY__ */

/*
 * Thread information flags that various assembly files may need to access.
 * Keep flags accessed frequently in low bits, particular since it makes
 * it easier to build constants in assembly.
 */
#define TIF_SIGPENDING		0	/* signal pending */
#define TIF_NEED_RESCHED	1	/* rescheduling necessary */
#define TIF_SINGLESTEP		2	/* restore singlestep on return to
					   user mode */
#define TIF_ASYNC_TLB		3	/* got an async TLB fault in kernel */
#define TIF_SYSCALL_TRACE	4	/* syscall trace active */
#define TIF_SYSCALL_AUDIT	5	/* syscall auditing active */
#define TIF_SECCOMP		6	/* secure computing */
#define TIF_MEMDIE		7	/* OOM killer at work */
#define TIF_NOTIFY_RESUME	8	/* callback before returning to user */
#define TIF_SYSCALL_TRACEPOINT	9	/* syscall tracepoint instrumentation */
#define TIF_POLLING_NRFLAG	10	/* idle is polling for TIF_NEED_RESCHED */
#define TIF_NOHZ		11	/* in adaptive nohz mode */

#define _TIF_SIGPENDING		(1<<TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1<<TIF_NEED_RESCHED)
#define _TIF_SINGLESTEP		(1<<TIF_SINGLESTEP)
#define _TIF_ASYNC_TLB		(1<<TIF_ASYNC_TLB)
#define _TIF_SYSCALL_TRACE	(1<<TIF_SYSCALL_TRACE)
#define _TIF_SYSCALL_AUDIT	(1<<TIF_SYSCALL_AUDIT)
#define _TIF_SECCOMP		(1<<TIF_SECCOMP)
#define _TIF_MEMDIE		(1<<TIF_MEMDIE)
#define _TIF_NOTIFY_RESUME	(1<<TIF_NOTIFY_RESUME)
#define _TIF_SYSCALL_TRACEPOINT	(1<<TIF_SYSCALL_TRACEPOINT)
#define _TIF_POLLING_NRFLAG	(1<<TIF_POLLING_NRFLAG)
#define _TIF_NOHZ		(1<<TIF_NOHZ)

/* Work to do on any return to user space. */
#define _TIF_ALLWORK_MASK \
	(_TIF_SIGPENDING | _TIF_NEED_RESCHED | _TIF_SINGLESTEP | \
	 _TIF_ASYNC_TLB | _TIF_NOTIFY_RESUME | _TIF_NOHZ)

/* Work to do at syscall entry. */
#define _TIF_SYSCALL_ENTRY_WORK \
	(_TIF_SYSCALL_TRACE | _TIF_SYSCALL_TRACEPOINT | _TIF_NOHZ)

/* Work to do at syscall exit. */
#define _TIF_SYSCALL_EXIT_WORK (_TIF_SYSCALL_TRACE | _TIF_SYSCALL_TRACEPOINT)

/*
 * Thread-synchronous status.
 *
 * This is different from the flags in that nobody else
 * ever touches our thread-synchronous status, so we don't
 * have to worry about atomic accesses.
 */
#ifdef __tilegx__
#define TS_COMPAT		0x0001	/* 32-bit compatibility mode */
#endif
#define TS_RESTORE_SIGMASK	0x0008	/* restore signal mask in do_signal */

#ifndef __ASSEMBLY__
#define HAVE_SET_RESTORE_SIGMASK	1
static inline void set_restore_sigmask(void)
{
	struct thread_info *ti = current_thread_info();
	ti->status |= TS_RESTORE_SIGMASK;
	WARN_ON(!test_bit(TIF_SIGPENDING, &ti->flags));
}
static inline void clear_restore_sigmask(void)
{
	current_thread_info()->status &= ~TS_RESTORE_SIGMASK;
}
static inline bool test_restore_sigmask(void)
{
	return current_thread_info()->status & TS_RESTORE_SIGMASK;
}
static inline bool test_and_clear_restore_sigmask(void)
{
	struct thread_info *ti = current_thread_info();
	if (!(ti->status & TS_RESTORE_SIGMASK))
		return false;
	ti->status &= ~TS_RESTORE_SIGMASK;
	return true;
}
#endif	/* !__ASSEMBLY__ */

#endif /* _ASM_TILE_THREAD_INFO_H */
