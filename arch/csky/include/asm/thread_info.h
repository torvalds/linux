/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#ifndef _ASM_CSKY_THREAD_INFO_H
#define _ASM_CSKY_THREAD_INFO_H

#ifndef __ASSEMBLY__

#include <linux/version.h>
#include <asm/types.h>
#include <asm/page.h>
#include <asm/processor.h>

struct thread_info {
	struct task_struct	*task;
	void			*dump_exec_domain;
	unsigned long		flags;
	int			preempt_count;
	unsigned long		tp_value;
	mm_segment_t		addr_limit;
	struct restart_block	restart_block;
	struct pt_regs		*regs;
	unsigned int		cpu;
};

#define INIT_THREAD_INFO(tsk)			\
{						\
	.task		= &tsk,			\
	.preempt_count  = INIT_PREEMPT_COUNT,	\
	.addr_limit     = KERNEL_DS,		\
	.cpu		= 0,			\
	.restart_block = {			\
		.fn = do_no_restart_syscall,	\
	},					\
}

#define THREAD_SIZE_ORDER (THREAD_SHIFT - PAGE_SHIFT)

static inline struct thread_info *current_thread_info(void)
{
	unsigned long sp;

	asm volatile("mov %0, sp\n":"=r"(sp));

	return (struct thread_info *)(sp & ~(THREAD_SIZE - 1));
}

#endif /* !__ASSEMBLY__ */

/* entry.S relies on these definitions!
 * bits 0-5 are tested at every exception exit
 */
#define TIF_SIGPENDING		0	/* signal pending */
#define TIF_NOTIFY_RESUME	1       /* callback before returning to user */
#define TIF_NEED_RESCHED	2	/* rescheduling necessary */
#define TIF_SYSCALL_TRACE	5	/* syscall trace active */
#define TIF_DELAYED_TRACE	14	/* single step a syscall */
#define TIF_POLLING_NRFLAG	16	/* poll_idle() is TIF_NEED_RESCHED */
#define TIF_MEMDIE		18      /* is terminating due to OOM killer */
#define TIF_FREEZE		19	/* thread is freezing for suspend */
#define TIF_RESTORE_SIGMASK	20	/* restore signal mask in do_signal() */
#define TIF_SECCOMP		21	/* secure computing */

#define _TIF_SIGPENDING         (1 << TIF_SIGPENDING)
#define _TIF_NOTIFY_RESUME      (1 << TIF_NOTIFY_RESUME)
#define _TIF_NEED_RESCHED       (1 << TIF_NEED_RESCHED)
#define _TIF_SYSCALL_TRACE      (1 << TIF_SYSCALL_TRACE)
#define _TIF_DELAYED_TRACE	(1 << TIF_DELAYED_TRACE)
#define _TIF_POLLING_NRFLAG     (1 << TIF_POLLING_NRFLAG)
#define _TIF_MEMDIE		(1 << TIF_MEMDIE)
#define _TIF_FREEZE             (1 << TIF_FREEZE)
#define _TIF_RESTORE_SIGMASK    (1 << TIF_RESTORE_SIGMASK)
#define _TIF_SECCOMP            (1 << TIF_SECCOMP)

#endif	/* _ASM_CSKY_THREAD_INFO_H */
