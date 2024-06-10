/* SPDX-License-Identifier: GPL-2.0 */
/*
 * thread_info.h: LoongArch low-level thread information
 *
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#ifndef _ASM_THREAD_INFO_H
#define _ASM_THREAD_INFO_H

#ifdef __KERNEL__

#ifndef __ASSEMBLY__

#include <asm/processor.h>

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
	unsigned long		tp_value;	/* thread pointer */
	__u32			cpu;		/* current CPU */
	int			preempt_count;	/* 0 => preemptible, <0 => BUG */
	struct pt_regs		*regs;
	unsigned long		syscall;	/* syscall number */
	unsigned long		syscall_work;	/* SYSCALL_WORK_ flags */
};

/*
 * macros/functions for gaining access to the thread information structure
 */
#define INIT_THREAD_INFO(tsk)			\
{						\
	.task		= &tsk,			\
	.flags		= _TIF_FIXADE,		\
	.cpu		= 0,			\
	.preempt_count	= INIT_PREEMPT_COUNT,	\
}

/* How to get the thread information struct from C. */
register struct thread_info *__current_thread_info __asm__("$tp");

static inline struct thread_info *current_thread_info(void)
{
	return __current_thread_info;
}

register unsigned long current_stack_pointer __asm__("$sp");

#endif /* !__ASSEMBLY__ */

/* thread information allocation */
#define THREAD_SIZE		SZ_16K
#define THREAD_MASK		(THREAD_SIZE - 1UL)
#define THREAD_SIZE_ORDER	ilog2(THREAD_SIZE / PAGE_SIZE)
/*
 * thread information flags
 * - these are process state flags that various assembly files may need to
 *   access
 * - pending work-to-be-done flags are in LSW
 * - other flags in MSW
 */
#define TIF_SIGPENDING		1	/* signal pending */
#define TIF_NEED_RESCHED	2	/* rescheduling necessary */
#define TIF_NOTIFY_RESUME	3	/* callback before returning to user */
#define TIF_NOTIFY_SIGNAL	4	/* signal notifications exist */
#define TIF_RESTORE_SIGMASK	5	/* restore signal mask in do_signal() */
#define TIF_NOHZ		6	/* in adaptive nohz mode */
#define TIF_UPROBE		7	/* breakpointed or singlestepping */
#define TIF_USEDFPU		8	/* FPU was used by this task this quantum (SMP) */
#define TIF_USEDSIMD		9	/* SIMD has been used this quantum */
#define TIF_MEMDIE		10	/* is terminating due to OOM killer */
#define TIF_FIXADE		11	/* Fix address errors in software */
#define TIF_LOGADE		12	/* Log address errors to syslog */
#define TIF_32BIT_REGS		13	/* 32-bit general purpose registers */
#define TIF_32BIT_ADDR		14	/* 32-bit address space */
#define TIF_LOAD_WATCH		15	/* If set, load watch registers */
#define TIF_SINGLESTEP		16	/* Single Step */
#define TIF_LSX_CTX_LIVE	17	/* LSX context must be preserved */
#define TIF_LASX_CTX_LIVE	18	/* LASX context must be preserved */
#define TIF_USEDLBT		19	/* LBT was used by this task this quantum (SMP) */
#define TIF_LBT_CTX_LIVE	20	/* LBT context must be preserved */
#define TIF_PATCH_PENDING	21	/* pending live patching update */

#define _TIF_SIGPENDING		(1<<TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1<<TIF_NEED_RESCHED)
#define _TIF_NOTIFY_RESUME	(1<<TIF_NOTIFY_RESUME)
#define _TIF_NOTIFY_SIGNAL	(1<<TIF_NOTIFY_SIGNAL)
#define _TIF_NOHZ		(1<<TIF_NOHZ)
#define _TIF_UPROBE		(1<<TIF_UPROBE)
#define _TIF_USEDFPU		(1<<TIF_USEDFPU)
#define _TIF_USEDSIMD		(1<<TIF_USEDSIMD)
#define _TIF_FIXADE		(1<<TIF_FIXADE)
#define _TIF_LOGADE		(1<<TIF_LOGADE)
#define _TIF_32BIT_REGS		(1<<TIF_32BIT_REGS)
#define _TIF_32BIT_ADDR		(1<<TIF_32BIT_ADDR)
#define _TIF_LOAD_WATCH		(1<<TIF_LOAD_WATCH)
#define _TIF_SINGLESTEP		(1<<TIF_SINGLESTEP)
#define _TIF_LSX_CTX_LIVE	(1<<TIF_LSX_CTX_LIVE)
#define _TIF_LASX_CTX_LIVE	(1<<TIF_LASX_CTX_LIVE)
#define _TIF_USEDLBT		(1<<TIF_USEDLBT)
#define _TIF_LBT_CTX_LIVE	(1<<TIF_LBT_CTX_LIVE)
#define _TIF_PATCH_PENDING	(1<<TIF_PATCH_PENDING)

#endif /* __KERNEL__ */
#endif /* _ASM_THREAD_INFO_H */
