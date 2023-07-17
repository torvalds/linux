/*
 * NiosII low-level thread information
 *
 * Copyright (C) 2011 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2004 Microtronix Datacom Ltd.
 *
 * Based on asm/thread_info_no.h from m68k which is:
 *
 * Copyright (C) 2002 David Howells <dhowells@redhat.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_NIOS2_THREAD_INFO_H
#define _ASM_NIOS2_THREAD_INFO_H

#ifdef __KERNEL__

/*
 * Size of the kernel stack for each process.
 */
#define THREAD_SIZE_ORDER	1
#define THREAD_SIZE		8192 /* 2 * PAGE_SIZE */

#ifndef __ASSEMBLY__

/*
 * low level task data that entry.S needs immediate access to
 * - this struct should fit entirely inside of one cache line
 * - this struct shares the supervisor stack pages
 * - if the contents of this structure are changed, the assembly constants
 *   must also be changed
 */
struct thread_info {
	struct task_struct	*task;		/* main task structure */
	unsigned long		flags;		/* low level flags */
	__u32			cpu;		/* current CPU */
	int			preempt_count;	/* 0 => preemptable,<0 => BUG */
	struct pt_regs		*regs;
};

/*
 * macros/functions for gaining access to the thread information structure
 *
 * preempt_count needs to be 1 initially, until the scheduler is functional.
 */
#define INIT_THREAD_INFO(tsk)			\
{						\
	.task		= &tsk,			\
	.flags		= 0,			\
	.cpu		= 0,			\
	.preempt_count	= INIT_PREEMPT_COUNT,	\
}

/* how to get the thread information struct from C */
static inline struct thread_info *current_thread_info(void)
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
#define TIF_SYSCALL_TRACE	0	/* syscall trace active */
#define TIF_NOTIFY_RESUME	1	/* resumption notification requested */
#define TIF_SIGPENDING		2	/* signal pending */
#define TIF_NEED_RESCHED	3	/* rescheduling necessary */
#define TIF_MEMDIE		4	/* is terminating due to OOM killer */
#define TIF_SECCOMP		5	/* secure computing */
#define TIF_SYSCALL_AUDIT	6	/* syscall auditing active */
#define TIF_NOTIFY_SIGNAL	7	/* signal notifications exist */
#define TIF_RESTORE_SIGMASK	9	/* restore signal mask in do_signal() */

#define TIF_POLLING_NRFLAG	16	/* true if poll_idle() is polling
					   TIF_NEED_RESCHED */

#define _TIF_SYSCALL_TRACE	(1 << TIF_SYSCALL_TRACE)
#define _TIF_NOTIFY_RESUME	(1 << TIF_NOTIFY_RESUME)
#define _TIF_SIGPENDING		(1 << TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1 << TIF_NEED_RESCHED)
#define _TIF_SECCOMP		(1 << TIF_SECCOMP)
#define _TIF_SYSCALL_AUDIT	(1 << TIF_SYSCALL_AUDIT)
#define _TIF_NOTIFY_SIGNAL	(1 << TIF_NOTIFY_SIGNAL)
#define _TIF_RESTORE_SIGMASK	(1 << TIF_RESTORE_SIGMASK)
#define _TIF_POLLING_NRFLAG	(1 << TIF_POLLING_NRFLAG)

/* work to do on interrupt/exception return */
#define _TIF_WORK_MASK		0x0000FFFE

#endif /* __KERNEL__ */

#endif /* _ASM_NIOS2_THREAD_INFO_H */
