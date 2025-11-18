/* SPDX-License-Identifier: GPL-2.0 */
/*
 * thread_info.h: LoongArch low-level thread information
 *
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#ifndef _ASM_THREAD_INFO_H
#define _ASM_THREAD_INFO_H

#ifdef __KERNEL__

#ifndef __ASSEMBLER__

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

#endif /* !__ASSEMBLER__ */

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
 *
 * Tell the generic TIF infrastructure which special bits loongarch supports
 */
#define HAVE_TIF_NEED_RESCHED_LAZY
#define HAVE_TIF_RESTORE_SIGMASK

#include <asm-generic/thread_info_tif.h>

/* Architecture specific bits */
#define TIF_NOHZ		16	/* in adaptive nohz mode */
#define TIF_USEDFPU		17	/* FPU was used by this task this quantum (SMP) */
#define TIF_USEDSIMD		18	/* SIMD has been used this quantum */
#define TIF_FIXADE		19	/* Fix address errors in software */
#define TIF_LOGADE		20	/* Log address errors to syslog */
#define TIF_32BIT_REGS		21	/* 32-bit general purpose registers */
#define TIF_32BIT_ADDR		22	/* 32-bit address space */
#define TIF_LOAD_WATCH		23	/* If set, load watch registers */
#define TIF_SINGLESTEP		24	/* Single Step */
#define TIF_LSX_CTX_LIVE	25	/* LSX context must be preserved */
#define TIF_LASX_CTX_LIVE	26	/* LASX context must be preserved */
#define TIF_USEDLBT		27	/* LBT was used by this task this quantum (SMP) */
#define TIF_LBT_CTX_LIVE	28	/* LBT context must be preserved */

#define _TIF_NOHZ		BIT(TIF_NOHZ)
#define _TIF_USEDFPU		BIT(TIF_USEDFPU)
#define _TIF_USEDSIMD		BIT(TIF_USEDSIMD)
#define _TIF_FIXADE		BIT(TIF_FIXADE)
#define _TIF_LOGADE		BIT(TIF_LOGADE)
#define _TIF_32BIT_REGS		BIT(TIF_32BIT_REGS)
#define _TIF_32BIT_ADDR		BIT(TIF_32BIT_ADDR)
#define _TIF_LOAD_WATCH		BIT(TIF_LOAD_WATCH)
#define _TIF_SINGLESTEP		BIT(TIF_SINGLESTEP)
#define _TIF_LSX_CTX_LIVE	BIT(TIF_LSX_CTX_LIVE)
#define _TIF_LASX_CTX_LIVE	BIT(TIF_LASX_CTX_LIVE)
#define _TIF_USEDLBT		BIT(TIF_USEDLBT)
#define _TIF_LBT_CTX_LIVE	BIT(TIF_LBT_CTX_LIVE)

#endif /* __KERNEL__ */
#endif /* _ASM_THREAD_INFO_H */
