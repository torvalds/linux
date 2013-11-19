/* thread_info.h: Meta low-level thread information
 *
 * Copyright (C) 2002  David Howells (dhowells@redhat.com)
 * - Incorporating suggestions made by Linus Torvalds and Dave Miller
 *
 * Meta port by Imagination Technologies
 */

#ifndef _ASM_THREAD_INFO_H
#define _ASM_THREAD_INFO_H

#include <linux/compiler.h>
#include <asm/page.h>

#ifndef __ASSEMBLY__
#include <asm/processor.h>
#endif

/*
 * low level task data that entry.S needs immediate access to
 * - this struct should fit entirely inside of one cache line
 * - this struct shares the supervisor stack pages
 * - if the contents of this structure are changed, the assembly constants must
 *   also be changed
 */
#ifndef __ASSEMBLY__

/* This must be 8 byte aligned so we can ensure stack alignment. */
struct thread_info {
	struct task_struct *task;	/* main task structure */
	struct exec_domain *exec_domain;	/* execution domain */
	unsigned long flags;	/* low level flags */
	unsigned long status;	/* thread-synchronous flags */
	u32 cpu;		/* current CPU */
	int preempt_count;	/* 0 => preemptable, <0 => BUG */

	mm_segment_t addr_limit;	/* thread address space */
	struct restart_block restart_block;

	u8 supervisor_stack[0];
};

#else /* !__ASSEMBLY__ */

#include <generated/asm-offsets.h>

#endif

#ifdef CONFIG_4KSTACKS
#define THREAD_SHIFT		12
#else
#define THREAD_SHIFT		13
#endif

#if THREAD_SHIFT >= PAGE_SHIFT
#define THREAD_SIZE_ORDER	(THREAD_SHIFT - PAGE_SHIFT)
#else
#define THREAD_SIZE_ORDER	0
#endif

#define THREAD_SIZE		(PAGE_SIZE << THREAD_SIZE_ORDER)

#define STACK_WARN		(THREAD_SIZE/8)
/*
 * macros/functions for gaining access to the thread information structure
 */
#ifndef __ASSEMBLY__

#define INIT_THREAD_INFO(tsk)			\
{						\
	.task		= &tsk,			\
	.exec_domain	= &default_exec_domain,	\
	.flags		= 0,			\
	.cpu		= 0,			\
	.preempt_count	= INIT_PREEMPT_COUNT,	\
	.addr_limit	= KERNEL_DS,		\
	.restart_block = {			\
		.fn = do_no_restart_syscall,	\
	},					\
}

#define init_thread_info	(init_thread_union.thread_info)
#define init_stack		(init_thread_union.stack)

/* how to get the current stack pointer from C */
register unsigned long current_stack_pointer asm("A0StP") __used;

/* how to get the thread information struct from C */
static inline struct thread_info *current_thread_info(void)
{
	return (struct thread_info *)(current_stack_pointer &
				      ~(THREAD_SIZE - 1));
}

#define __HAVE_ARCH_KSTACK_END
static inline int kstack_end(void *addr)
{
	return addr == (void *) (((unsigned long) addr & ~(THREAD_SIZE - 1))
				 + sizeof(struct thread_info));
}

#endif

/*
 * thread information flags
 * - these are process state flags that various assembly files may need to
 *   access
 * - pending work-to-be-done flags are in LSW
 * - other flags in MSW
 */
#define TIF_SYSCALL_TRACE	0	/* syscall trace active */
#define TIF_SIGPENDING		1	/* signal pending */
#define TIF_NEED_RESCHED	2	/* rescheduling necessary */
#define TIF_SINGLESTEP		3	/* restore singlestep on return to user
					   mode */
#define TIF_SYSCALL_AUDIT	4	/* syscall auditing active */
#define TIF_SECCOMP		5	/* secure computing */
#define TIF_RESTORE_SIGMASK	6	/* restore signal mask in do_signal() */
#define TIF_NOTIFY_RESUME	7	/* callback before returning to user */
#define TIF_POLLING_NRFLAG      8	/* true if poll_idle() is polling
					   TIF_NEED_RESCHED */
#define TIF_MEMDIE		9	/* is terminating due to OOM killer */
#define TIF_SYSCALL_TRACEPOINT  10	/* syscall tracepoint instrumentation */


#define _TIF_SYSCALL_TRACE	(1<<TIF_SYSCALL_TRACE)
#define _TIF_SIGPENDING		(1<<TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1<<TIF_NEED_RESCHED)
#define _TIF_SINGLESTEP		(1<<TIF_SINGLESTEP)
#define _TIF_SYSCALL_AUDIT	(1<<TIF_SYSCALL_AUDIT)
#define _TIF_SECCOMP		(1<<TIF_SECCOMP)
#define _TIF_NOTIFY_RESUME	(1<<TIF_NOTIFY_RESUME)
#define _TIF_RESTORE_SIGMASK	(1<<TIF_RESTORE_SIGMASK)
#define _TIF_SYSCALL_TRACEPOINT	(1<<TIF_SYSCALL_TRACEPOINT)

/* work to do in syscall trace */
#define _TIF_WORK_SYSCALL_MASK	(_TIF_SYSCALL_TRACE | _TIF_SINGLESTEP | \
				 _TIF_SYSCALL_AUDIT | _TIF_SECCOMP | \
				 _TIF_SYSCALL_TRACEPOINT)

/* work to do on any return to u-space */
#define _TIF_ALLWORK_MASK	(_TIF_SYSCALL_TRACE | _TIF_SIGPENDING      | \
				 _TIF_NEED_RESCHED  | _TIF_SYSCALL_AUDIT   | \
				 _TIF_SINGLESTEP    | _TIF_RESTORE_SIGMASK | \
				 _TIF_NOTIFY_RESUME)

/* work to do on interrupt/exception return */
#define _TIF_WORK_MASK		(_TIF_ALLWORK_MASK & ~(_TIF_SYSCALL_TRACE | \
				 _TIF_SYSCALL_AUDIT | _TIF_SINGLESTEP))

#endif /* _ASM_THREAD_INFO_H */
