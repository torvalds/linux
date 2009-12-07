#ifndef _ASM_M32R_THREAD_INFO_H
#define _ASM_M32R_THREAD_INFO_H

/* thread_info.h: m32r low-level thread information
 *
 * Copyright (C) 2002  David Howells (dhowells@redhat.com)
 * - Incorporating suggestions made by Linus Torvalds and Dave Miller
 * Copyright (C) 2004  Hirokazu Takata <takata at linux-m32r.org>
 */

#ifdef __KERNEL__

#ifndef __ASSEMBLY__
#include <asm/processor.h>
#endif

/*
 * low level task data that entry.S needs immediate access to
 * - this struct should fit entirely inside of one cache line
 * - this struct shares the supervisor stack pages
 * - if the contents of this structure are changed, the assembly constants must also be changed
 */
#ifndef __ASSEMBLY__

struct thread_info {
	struct task_struct	*task;		/* main task structure */
	struct exec_domain	*exec_domain;	/* execution domain */
	unsigned long		flags;		/* low level flags */
	unsigned long		status;		/* thread-synchronous flags */
	__u32			cpu;		/* current CPU */
	int			preempt_count;	/* 0 => preemptable, <0 => BUG */

	mm_segment_t		addr_limit;	/* thread address space:
					 	   0-0xBFFFFFFF for user-thread
						   0-0xFFFFFFFF for kernel-thread
						*/
	struct restart_block    restart_block;

	__u8			supervisor_stack[0];
};

#else /* !__ASSEMBLY__ */

/* offsets into the thread_info struct for assembly code access */
#define TI_TASK		0x00000000
#define TI_EXEC_DOMAIN	0x00000004
#define TI_FLAGS	0x00000008
#define TI_STATUS	0x0000000C
#define TI_CPU		0x00000010
#define TI_PRE_COUNT	0x00000014
#define TI_ADDR_LIMIT	0x00000018
#define TI_RESTART_BLOCK 0x000001C

#endif

#define PREEMPT_ACTIVE		0x10000000

#define THREAD_SIZE (PAGE_SIZE << 1)

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

/* how to get the thread information struct from C */
static inline struct thread_info *current_thread_info(void)
{
	struct thread_info *ti;

	__asm__ __volatile__ (
		"ldi	%0, #%1			\n\t"
		"and	%0, sp			\n\t"
		: "=r" (ti) : "i" (~(THREAD_SIZE - 1))
	);

	return ti;
}

#define __HAVE_ARCH_THREAD_INFO_ALLOCATOR

/* thread information allocation */
#ifdef CONFIG_DEBUG_STACK_USAGE
#define alloc_thread_info(tsk)					\
	({							\
		struct thread_info *ret;			\
	 							\
	 	ret = kzalloc(THREAD_SIZE, GFP_KERNEL);		\
								\
	 	ret;						\
	 })
#else
#define alloc_thread_info(tsk) kmalloc(THREAD_SIZE, GFP_KERNEL)
#endif

#define free_thread_info(info) kfree(info)

#define TI_FLAG_FAULT_CODE_SHIFT	28

static inline void set_thread_fault_code(unsigned int val)
{
	struct thread_info *ti = current_thread_info();
	ti->flags = (ti->flags & (~0 >> (32 - TI_FLAG_FAULT_CODE_SHIFT)))
		| (val << TI_FLAG_FAULT_CODE_SHIFT);
}

static inline unsigned int get_thread_fault_code(void)
{
	struct thread_info *ti = current_thread_info();
	return ti->flags >> TI_FLAG_FAULT_CODE_SHIFT;
}

#endif

/*
 * thread information flags
 * - these are process state flags that various assembly files may need to access
 * - pending work-to-be-done flags are in LSW
 * - other flags in MSW
 */
#define TIF_SYSCALL_TRACE	0	/* syscall trace active */
#define TIF_SIGPENDING		1	/* signal pending */
#define TIF_NEED_RESCHED	2	/* rescheduling necessary */
#define TIF_SINGLESTEP		3	/* restore singlestep on return to user mode */
#define TIF_IRET		4	/* return with iret */
#define TIF_NOTIFY_RESUME	5	/* callback before returning to user */
#define TIF_RESTORE_SIGMASK	8	/* restore signal mask in do_signal() */
#define TIF_USEDFPU		16	/* FPU was used by this task this quantum (SMP) */
#define TIF_POLLING_NRFLAG	17	/* true if poll_idle() is polling TIF_NEED_RESCHED */
#define TIF_MEMDIE		18	/* OOM killer killed process */
#define TIF_FREEZE		19	/* is freezing for suspend */

#define _TIF_SYSCALL_TRACE	(1<<TIF_SYSCALL_TRACE)
#define _TIF_SIGPENDING		(1<<TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1<<TIF_NEED_RESCHED)
#define _TIF_SINGLESTEP		(1<<TIF_SINGLESTEP)
#define _TIF_IRET		(1<<TIF_IRET)
#define _TIF_NOTIFY_RESUME	(1<<TIF_NOTIFY_RESUME)
#define _TIF_RESTORE_SIGMASK	(1<<TIF_RESTORE_SIGMASK)
#define _TIF_USEDFPU		(1<<TIF_USEDFPU)
#define _TIF_POLLING_NRFLAG	(1<<TIF_POLLING_NRFLAG)
#define _TIF_FREEZE		(1<<TIF_FREEZE)

#define _TIF_WORK_MASK		0x0000FFFE	/* work to do on interrupt/exception return */
#define _TIF_ALLWORK_MASK	0x0000FFFF	/* work to do on any return to u-space */

/*
 * Thread-synchronous status.
 *
 * This is different from the flags in that nobody else
 * ever touches our thread-synchronous status, so we don't
 * have to worry about atomic accesses.
 */
#define TS_USEDFPU		0x0001	/* FPU was used by this task this quantum (SMP) */

#endif /* __KERNEL__ */

#endif /* _ASM_M32R_THREAD_INFO_H */
