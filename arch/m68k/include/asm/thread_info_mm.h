#ifndef _ASM_M68K_THREAD_INFO_H
#define _ASM_M68K_THREAD_INFO_H

#ifndef ASM_OFFSETS_C
#include <asm/asm-offsets.h>
#endif
#include <asm/types.h>
#include <asm/page.h>

#ifndef __ASSEMBLY__
#include <asm/current.h>

struct thread_info {
	struct task_struct	*task;		/* main task structure */
	unsigned long		flags;
	struct exec_domain	*exec_domain;	/* execution domain */
	int			preempt_count;	/* 0 => preemptable, <0 => BUG */
	__u32 cpu; /* should always be 0 on m68k */
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

/* THREAD_SIZE should be 8k, so handle differently for 4k and 8k machines */
#define THREAD_SIZE_ORDER (13 - PAGE_SHIFT)

#define init_thread_info	(init_task.thread.info)
#define init_stack		(init_thread_union.stack)

#ifdef ASM_OFFSETS_C
#define task_thread_info(tsk)	((struct thread_info *) NULL)
#else
#define task_thread_info(tsk)	((struct thread_info *)((char *)tsk+TASK_TINFO))
#endif

#define task_stack_page(tsk)	((tsk)->stack)
#define current_thread_info()	task_thread_info(current)

#define __HAVE_THREAD_FUNCTIONS

#define setup_thread_stack(p, org) ({			\
	*(struct task_struct **)(p)->stack = (p);	\
	task_thread_info(p)->task = (p);		\
})

#define end_of_stack(p) ((unsigned long *)(p)->stack + 1)

/* entry.S relies on these definitions!
 * bits 0-7 are tested at every exception exit
 * bits 8-15 are also tested at syscall exit
 */
#define TIF_SIGPENDING		6	/* signal pending */
#define TIF_NEED_RESCHED	7	/* rescheduling necessary */
#define TIF_DELAYED_TRACE	14	/* single step a syscall */
#define TIF_SYSCALL_TRACE	15	/* syscall trace active */
#define TIF_MEMDIE		16	/* is terminating due to OOM killer */
#define TIF_FREEZE		17	/* thread is freezing for suspend */

#endif	/* _ASM_M68K_THREAD_INFO_H */
