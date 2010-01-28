#ifndef __ASM_SH_THREAD_INFO_H
#define __ASM_SH_THREAD_INFO_H

/* SuperH version
 * Copyright (C) 2002  Niibe Yutaka
 *
 * The copyright of original i386 version is:
 *
 *  Copyright (C) 2002  David Howells (dhowells@redhat.com)
 *  - Incorporating suggestions made by Linus Torvalds and Dave Miller
 */
#ifdef __KERNEL__
#include <asm/page.h>

#ifndef __ASSEMBLY__
#include <asm/processor.h>

struct thread_info {
	struct task_struct	*task;		/* main task structure */
	struct exec_domain	*exec_domain;	/* execution domain */
	unsigned long		flags;		/* low level flags */
	__u32			status;		/* thread synchronous flags */
	__u32			cpu;
	int			preempt_count; /* 0 => preemptable, <0 => BUG */
	mm_segment_t		addr_limit;	/* thread address space */
	struct restart_block	restart_block;
	unsigned long		previous_sp;	/* sp of previous stack in case
						   of nested IRQ stacks */
	__u8			supervisor_stack[0];
};

#endif

#define PREEMPT_ACTIVE		0x10000000

#if defined(CONFIG_4KSTACKS)
#define THREAD_SHIFT	12
#else
#define THREAD_SHIFT	13
#endif

#define THREAD_SIZE	(1 << THREAD_SHIFT)
#define STACK_WARN	(THREAD_SIZE >> 3)

/*
 * macros/functions for gaining access to the thread information structure
 */
#ifndef __ASSEMBLY__
#define INIT_THREAD_INFO(tsk)			\
{						\
	.task		= &tsk,			\
	.exec_domain	= &default_exec_domain,	\
	.flags		= 0,			\
	.status		= 0,			\
	.cpu		= 0,			\
	.preempt_count	= INIT_PREEMPT_COUNT,	\
	.addr_limit	= KERNEL_DS,		\
	.restart_block	= {			\
		.fn = do_no_restart_syscall,	\
	},					\
}

#define init_thread_info	(init_thread_union.thread_info)
#define init_stack		(init_thread_union.stack)

/* how to get the current stack pointer from C */
register unsigned long current_stack_pointer asm("r15") __used;

/* how to get the thread information struct from C */
static inline struct thread_info *current_thread_info(void)
{
	struct thread_info *ti;
#if defined(CONFIG_SUPERH64)
	__asm__ __volatile__ ("getcon	cr17, %0" : "=r" (ti));
#elif defined(CONFIG_CPU_HAS_SR_RB)
	__asm__ __volatile__ ("stc	r7_bank, %0" : "=r" (ti));
#else
	unsigned long __dummy;

	__asm__ __volatile__ (
		"mov	r15, %0\n\t"
		"and	%1, %0\n\t"
		: "=&r" (ti), "=r" (__dummy)
		: "1" (~(THREAD_SIZE - 1))
		: "memory");
#endif

	return ti;
}

/* thread information allocation */
#if THREAD_SHIFT >= PAGE_SHIFT

#define THREAD_SIZE_ORDER	(THREAD_SHIFT - PAGE_SHIFT)

#else /* THREAD_SHIFT < PAGE_SHIFT */

#define __HAVE_ARCH_THREAD_INFO_ALLOCATOR

extern struct thread_info *alloc_thread_info(struct task_struct *tsk);
extern void free_thread_info(struct thread_info *ti);

#endif /* THREAD_SHIFT < PAGE_SHIFT */

#endif /* __ASSEMBLY__ */

/*
 * thread information flags
 * - these are process state flags that various assembly files may need to access
 * - pending work-to-be-done flags are in LSW
 * - other flags in MSW
 */
#define TIF_SYSCALL_TRACE	0	/* syscall trace active */
#define TIF_SIGPENDING		1	/* signal pending */
#define TIF_NEED_RESCHED	2	/* rescheduling necessary */
#define TIF_SINGLESTEP		4	/* singlestepping active */
#define TIF_SYSCALL_AUDIT	5	/* syscall auditing active */
#define TIF_SECCOMP		6	/* secure computing */
#define TIF_NOTIFY_RESUME	7	/* callback before returning to user */
#define TIF_SYSCALL_TRACEPOINT	8	/* for ftrace syscall instrumentation */
#define TIF_POLLING_NRFLAG	17	/* true if poll_idle() is polling TIF_NEED_RESCHED */
#define TIF_MEMDIE		18
#define TIF_FREEZE		19	/* Freezing for suspend */

#define _TIF_SYSCALL_TRACE	(1 << TIF_SYSCALL_TRACE)
#define _TIF_SIGPENDING		(1 << TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1 << TIF_NEED_RESCHED)
#define _TIF_SINGLESTEP		(1 << TIF_SINGLESTEP)
#define _TIF_SYSCALL_AUDIT	(1 << TIF_SYSCALL_AUDIT)
#define _TIF_SECCOMP		(1 << TIF_SECCOMP)
#define _TIF_NOTIFY_RESUME	(1 << TIF_NOTIFY_RESUME)
#define _TIF_SYSCALL_TRACEPOINT	(1 << TIF_SYSCALL_TRACEPOINT)
#define _TIF_POLLING_NRFLAG	(1 << TIF_POLLING_NRFLAG)
#define _TIF_FREEZE		(1 << TIF_FREEZE)

/*
 * _TIF_ALLWORK_MASK and _TIF_WORK_MASK need to fit within 2 bytes, or we
 * blow the tst immediate size constraints and need to fix up
 * arch/sh/kernel/entry-common.S.
 */

/* work to do in syscall trace */
#define _TIF_WORK_SYSCALL_MASK	(_TIF_SYSCALL_TRACE | _TIF_SINGLESTEP | \
				 _TIF_SYSCALL_AUDIT | _TIF_SECCOMP    | \
				 _TIF_SYSCALL_TRACEPOINT)

/* work to do on any return to u-space */
#define _TIF_ALLWORK_MASK	(_TIF_SYSCALL_TRACE | _TIF_SIGPENDING      | \
				 _TIF_NEED_RESCHED  | _TIF_SYSCALL_AUDIT   | \
				 _TIF_SINGLESTEP    | _TIF_NOTIFY_RESUME   | \
				 _TIF_SYSCALL_TRACEPOINT)

/* work to do on interrupt/exception return */
#define _TIF_WORK_MASK		(_TIF_ALLWORK_MASK & ~(_TIF_SYSCALL_TRACE | \
				 _TIF_SYSCALL_AUDIT | _TIF_SINGLESTEP))

/*
 * Thread-synchronous status.
 *
 * This is different from the flags in that nobody else
 * ever touches our thread-synchronous status, so we don't
 * have to worry about atomic accesses.
 */
#define TS_RESTORE_SIGMASK	0x0001	/* restore signal mask in do_signal() */
#define TS_USEDFPU		0x0002	/* FPU used by this task this quantum */

#ifndef __ASSEMBLY__
#define HAVE_SET_RESTORE_SIGMASK	1
static inline void set_restore_sigmask(void)
{
	struct thread_info *ti = current_thread_info();
	ti->status |= TS_RESTORE_SIGMASK;
	set_bit(TIF_SIGPENDING, (unsigned long *)&ti->flags);
}
#endif	/* !__ASSEMBLY__ */

#endif /* __KERNEL__ */

#endif /* __ASM_SH_THREAD_INFO_H */
