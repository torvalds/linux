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
#define THREAD_SIZE	4096
#else
#define THREAD_SIZE	8192
#endif
#else
#define THREAD_SIZE	PAGE_SIZE
#endif
#define THREAD_SIZE_ORDER	((THREAD_SIZE / PAGE_SIZE) - 1)

#ifndef __ASSEMBLY__

struct thread_info {
	struct task_struct	*task;		/* main task structure */
	unsigned long		flags;
	struct exec_domain	*exec_domain;	/* execution domain */
	int			preempt_count;	/* 0 => preemptable, <0 => BUG */
	__u32			cpu;		/* should always be 0 on m68k */
	unsigned long		tp_value;	/* thread pointer */
	struct restart_block    restart_block;
};
#endif /* __ASSEMBLY__ */

#define PREEMPT_ACTIVE		0x4000000

#define INIT_THREAD_INFO(tsk)			\
{						\
	.task		= &tsk,			\
	.exec_domain	= &default_exec_domain,	\
	.preempt_count	= INIT_PREEMPT_COUNT,	\
	.restart_block = {			\
		.fn = do_no_restart_syscall,	\
	},					\
}

#define init_stack		(init_thread_union.stack)

#ifdef CONFIG_MMU

#ifndef __ASSEMBLY__
#include <asm/current.h>
#endif

#ifdef ASM_OFFSETS_C
#define task_thread_info(tsk)	((struct thread_info *) NULL)
#else
#include <asm/asm-offsets.h>
#define task_thread_info(tsk)	((struct thread_info *)((char *)tsk+TASK_TINFO))
#endif

#define init_thread_info	(init_task.thread.info)
#define task_stack_page(tsk)	((tsk)->stack)
#define current_thread_info()	task_thread_info(current)

#define __HAVE_THREAD_FUNCTIONS

#define setup_thread_stack(p, org) ({			\
	*(struct task_struct **)(p)->stack = (p);	\
	task_thread_info(p)->task = (p);		\
})

#define end_of_stack(p)		((unsigned long *)(p)->stack + 1)

#else /* !CONFIG_MMU */

#ifndef __ASSEMBLY__
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

#define init_thread_info	(init_thread_union.thread_info)

#endif /* CONFIG_MMU */

/* entry.S relies on these definitions!
 * bits 0-7 are tested at every exception exit
 * bits 8-15 are also tested at syscall exit
 */
#define TIF_SIGPENDING		6	/* signal pending */
#define TIF_NEED_RESCHED	7	/* rescheduling necessary */
#define TIF_DELAYED_TRACE	14	/* single step a syscall */
#define TIF_SYSCALL_TRACE	15	/* syscall trace active */
#define TIF_MEMDIE		16	/* is terminating due to OOM killer */
#define TIF_RESTORE_SIGMASK	18	/* restore signal mask in do_signal */

#endif	/* _ASM_M68K_THREAD_INFO_H */
