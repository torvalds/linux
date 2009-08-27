#ifndef _ALPHA_THREAD_INFO_H
#define _ALPHA_THREAD_INFO_H

#ifdef __KERNEL__

#ifndef __ASSEMBLY__
#include <asm/processor.h>
#include <asm/types.h>
#include <asm/hwrpb.h>
#endif

#ifndef __ASSEMBLY__
struct thread_info {
	struct pcb_struct	pcb;		/* palcode state */

	struct task_struct	*task;		/* main task structure */
	unsigned int		flags;		/* low level flags */
	unsigned int		ieee_state;	/* see fpu.h */

	struct exec_domain	*exec_domain;	/* execution domain */
	mm_segment_t		addr_limit;	/* thread address space */
	unsigned		cpu;		/* current CPU */
	int			preempt_count; /* 0 => preemptable, <0 => BUG */

	int bpt_nsaved;
	unsigned long bpt_addr[2];		/* breakpoint handling  */
	unsigned int bpt_insn[2];

	struct restart_block	restart_block;
};

/*
 * Macros/functions for gaining access to the thread information structure.
 */
#define INIT_THREAD_INFO(tsk)			\
{						\
	.task		= &tsk,			\
	.exec_domain	= &default_exec_domain,	\
	.addr_limit	= KERNEL_DS,		\
	.preempt_count	= INIT_PREEMPT_COUNT,	\
	.restart_block = {			\
		.fn = do_no_restart_syscall,	\
	},					\
}

#define init_thread_info	(init_thread_union.thread_info)
#define init_stack		(init_thread_union.stack)

/* How to get the thread information struct from C.  */
register struct thread_info *__current_thread_info __asm__("$8");
#define current_thread_info()  __current_thread_info

/* Thread information allocation.  */
#define THREAD_SIZE_ORDER 1
#define THREAD_SIZE (2*PAGE_SIZE)

#endif /* __ASSEMBLY__ */

#define PREEMPT_ACTIVE		0x40000000

/*
 * Thread information flags:
 * - these are process state flags and used from assembly
 * - pending work-to-be-done flags come first to fit in and immediate operand.
 *
 * TIF_SYSCALL_TRACE is known to be 0 via blbs.
 */
#define TIF_SYSCALL_TRACE	0	/* syscall trace active */
#define TIF_SIGPENDING		1	/* signal pending */
#define TIF_NEED_RESCHED	2	/* rescheduling necessary */
#define TIF_POLLING_NRFLAG	3	/* poll_idle is polling NEED_RESCHED */
#define TIF_DIE_IF_KERNEL	4	/* dik recursion lock */
#define TIF_UAC_NOPRINT		5	/* see sysinfo.h */
#define TIF_UAC_NOFIX		6
#define TIF_UAC_SIGBUS		7
#define TIF_MEMDIE		8
#define TIF_RESTORE_SIGMASK	9	/* restore signal mask in do_signal */
#define TIF_FREEZE		16	/* is freezing for suspend */

#define _TIF_SYSCALL_TRACE	(1<<TIF_SYSCALL_TRACE)
#define _TIF_SIGPENDING		(1<<TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1<<TIF_NEED_RESCHED)
#define _TIF_POLLING_NRFLAG	(1<<TIF_POLLING_NRFLAG)
#define _TIF_RESTORE_SIGMASK	(1<<TIF_RESTORE_SIGMASK)
#define _TIF_FREEZE		(1<<TIF_FREEZE)

/* Work to do on interrupt/exception return.  */
#define _TIF_WORK_MASK		(_TIF_SIGPENDING | _TIF_NEED_RESCHED)

/* Work to do on any return to userspace.  */
#define _TIF_ALLWORK_MASK	(_TIF_WORK_MASK		\
				 | _TIF_SYSCALL_TRACE)

#define ALPHA_UAC_SHIFT		6
#define ALPHA_UAC_MASK		(1 << TIF_UAC_NOPRINT | 1 << TIF_UAC_NOFIX | \
				 1 << TIF_UAC_SIGBUS)

#define SET_UNALIGN_CTL(task,value)	({				     \
	task_thread_info(task)->flags = ((task_thread_info(task)->flags &    \
		~ALPHA_UAC_MASK)					     \
		| (((value) << ALPHA_UAC_SHIFT)       & (1<<TIF_UAC_NOPRINT))\
		| (((value) << (ALPHA_UAC_SHIFT + 1)) & (1<<TIF_UAC_SIGBUS)) \
		| (((value) << (ALPHA_UAC_SHIFT - 1)) & (1<<TIF_UAC_NOFIX)));\
	0; })

#define GET_UNALIGN_CTL(task,value)	({				\
	put_user((task_thread_info(task)->flags & (1 << TIF_UAC_NOPRINT))\
		  >> ALPHA_UAC_SHIFT					\
		 | (task_thread_info(task)->flags & (1 << TIF_UAC_SIGBUS))\
		 >> (ALPHA_UAC_SHIFT + 1)				\
		 | (task_thread_info(task)->flags & (1 << TIF_UAC_NOFIX))\
		 >> (ALPHA_UAC_SHIFT - 1),				\
		 (int __user *)(value));				\
	})

#endif /* __KERNEL__ */
#endif /* _ALPHA_THREAD_INFO_H */
