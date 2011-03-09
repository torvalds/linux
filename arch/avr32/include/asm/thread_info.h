/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_AVR32_THREAD_INFO_H
#define __ASM_AVR32_THREAD_INFO_H

#include <asm/page.h>

#define THREAD_SIZE_ORDER	1
#define THREAD_SIZE		(PAGE_SIZE << THREAD_SIZE_ORDER)

#ifndef __ASSEMBLY__
#include <asm/types.h>

struct task_struct;
struct exec_domain;

struct thread_info {
	struct task_struct	*task;		/* main task structure */
	struct exec_domain	*exec_domain;	/* execution domain */
	unsigned long		flags;		/* low level flags */
	__u32			cpu;
	__s32			preempt_count;	/* 0 => preemptable, <0 => BUG */
	__u32			rar_saved;	/* return address... */
	__u32			rsr_saved;	/* ...and status register
						   saved by debug handler
						   when setting up
						   trampoline */
	struct restart_block	restart_block;
	__u8			supervisor_stack[0];
};

#define INIT_THREAD_INFO(tsk)						\
{									\
	.task		= &tsk,						\
	.exec_domain	= &default_exec_domain,				\
	.flags		= 0,						\
	.cpu		= 0,						\
	.preempt_count	= INIT_PREEMPT_COUNT,				\
	.restart_block	= {						\
		.fn	= do_no_restart_syscall				\
	}								\
}

#define init_thread_info	(init_thread_union.thread_info)
#define init_stack		(init_thread_union.stack)

/*
 * Get the thread information struct from C.
 * We do the usual trick and use the lower end of the stack for this
 */
static inline struct thread_info *current_thread_info(void)
{
	unsigned long addr = ~(THREAD_SIZE - 1);

	asm("and %0, sp" : "=r"(addr) : "0"(addr));
	return (struct thread_info *)addr;
}

#define get_thread_info(ti) get_task_struct((ti)->task)
#define put_thread_info(ti) put_task_struct((ti)->task)

#endif /* !__ASSEMBLY__ */

#define PREEMPT_ACTIVE		0x40000000

/*
 * Thread information flags
 * - these are process state flags that various assembly files may need to access
 * - pending work-to-be-done flags are in LSW
 * - other flags in MSW
 */
#define TIF_SYSCALL_TRACE       0       /* syscall trace active */
#define TIF_SIGPENDING          1       /* signal pending */
#define TIF_NEED_RESCHED        2       /* rescheduling necessary */
#define TIF_POLLING_NRFLAG      3       /* true if poll_idle() is polling
					   TIF_NEED_RESCHED */
#define TIF_BREAKPOINT		4	/* enter monitor mode on return */
#define TIF_SINGLE_STEP		5	/* single step in progress */
#define TIF_MEMDIE		6	/* is terminating due to OOM killer */
#define TIF_RESTORE_SIGMASK	7	/* restore signal mask in do_signal */
#define TIF_CPU_GOING_TO_SLEEP	8	/* CPU is entering sleep 0 mode */
#define TIF_NOTIFY_RESUME	9	/* callback before returning to user */
#define TIF_FREEZE		29
#define TIF_DEBUG		30	/* debugging enabled */
#define TIF_USERSPACE		31      /* true if FS sets userspace */

#define _TIF_SYSCALL_TRACE	(1 << TIF_SYSCALL_TRACE)
#define _TIF_SIGPENDING		(1 << TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1 << TIF_NEED_RESCHED)
#define _TIF_POLLING_NRFLAG	(1 << TIF_POLLING_NRFLAG)
#define _TIF_SINGLE_STEP	(1 << TIF_SINGLE_STEP)
#define _TIF_MEMDIE		(1 << TIF_MEMDIE)
#define _TIF_RESTORE_SIGMASK	(1 << TIF_RESTORE_SIGMASK)
#define _TIF_CPU_GOING_TO_SLEEP (1 << TIF_CPU_GOING_TO_SLEEP)
#define _TIF_NOTIFY_RESUME	(1 << TIF_NOTIFY_RESUME)
#define _TIF_FREEZE		(1 << TIF_FREEZE)

/* Note: The masks below must never span more than 16 bits! */

/* work to do on interrupt/exception return */
#define _TIF_WORK_MASK				\
	((1 << TIF_SIGPENDING)			\
	 | _TIF_NOTIFY_RESUME			\
	 | (1 << TIF_NEED_RESCHED)		\
	 | (1 << TIF_POLLING_NRFLAG)		\
	 | (1 << TIF_BREAKPOINT)		\
	 | (1 << TIF_RESTORE_SIGMASK))

/* work to do on any return to userspace */
#define _TIF_ALLWORK_MASK	(_TIF_WORK_MASK | (1 << TIF_SYSCALL_TRACE) | \
				 _TIF_NOTIFY_RESUME)
/* work to do on return from debug mode */
#define _TIF_DBGWORK_MASK	(_TIF_WORK_MASK & ~(1 << TIF_BREAKPOINT))

#endif /* __ASM_AVR32_THREAD_INFO_H */
