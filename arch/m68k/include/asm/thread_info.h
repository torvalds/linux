/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_M68K_THREAD_INFO_H
#define _ASM_M68K_THREAD_INFO_H

#include <asm/types.h>
#include <asm/page.h>

/*
 * On machines with 4k pages we default to an 8k thread size, though we
 * allow a 4k with config option. Any other machine page size then
 * the thread size must match the page size (which is 8k and larger here).
 */
#if PAGE_SHIFT < 13
#ifdef CONFIG_4KSTACKS
#define THREAD_SIZE_ORDER	0
#else
#define THREAD_SIZE_ORDER	1
#endif
#else
#define THREAD_SIZE_ORDER	0
#endif

#define THREAD_SIZE	(PAGE_SIZE << THREAD_SIZE_ORDER)

#ifndef __ASSEMBLER__

struct thread_info {
	struct task_struct	*task;		/* main task structure */
	unsigned long		flags;
	int			preempt_count;	/* 0 => preemptable, <0 => BUG */
	__u32			cpu;		/* should always be 0 on m68k */
	unsigned long		tp_value;	/* thread pointer */
};
#endif /* __ASSEMBLER__ */

#define INIT_THREAD_INFO(tsk)			\
{						\
	.task		= &tsk,			\
	.preempt_count	= INIT_PREEMPT_COUNT,	\
}

#ifndef __ASSEMBLER__
/* how to get the thread information struct from C */
static inline struct thread_info *current_thread_info(void)
{
	struct thread_info *ti;
	__asm__(
		"move.l %%sp, %0 \n\t"
		"and.l  %1, %0"
		: "=&d"(ti)
		: "di" (~(THREAD_SIZE-1))
		);
	return ti;
}
#endif

/* entry.S relies on these definitions!
 * bits 0-7 are tested at every exception exit
 * bits 8-15 are also tested at syscall exit
 */
#define TIF_NOTIFY_SIGNAL	4
#define TIF_NOTIFY_RESUME	5	/* callback before returning to user */
#define TIF_SIGPENDING		6	/* signal pending */
#define TIF_NEED_RESCHED	7	/* rescheduling necessary */
#define TIF_SECCOMP		13	/* seccomp syscall filtering active */
#define TIF_DELAYED_TRACE	14	/* single step a syscall */
#define TIF_SYSCALL_TRACE	15	/* syscall trace active */
#define TIF_MEMDIE		16	/* is terminating due to OOM killer */
#define TIF_RESTORE_SIGMASK	18	/* restore signal mask in do_signal */

#define _TIF_NOTIFY_RESUME	(1 << TIF_NOTIFY_RESUME)
#define _TIF_SIGPENDING		(1 << TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1 << TIF_NEED_RESCHED)
#define _TIF_SECCOMP		(1 << TIF_SECCOMP)
#define _TIF_DELAYED_TRACE	(1 << TIF_DELAYED_TRACE)
#define _TIF_SYSCALL_TRACE	(1 << TIF_SYSCALL_TRACE)
#define _TIF_MEMDIE		(1 << TIF_MEMDIE)
#define _TIF_RESTORE_SIGMASK	(1 << TIF_RESTORE_SIGMASK)

#endif	/* _ASM_M68K_THREAD_INFO_H */
