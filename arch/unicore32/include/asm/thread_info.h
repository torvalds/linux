/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/arch/unicore32/include/asm/thread_info.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 */
#ifndef __UNICORE_THREAD_INFO_H__
#define __UNICORE_THREAD_INFO_H__

#ifdef __KERNEL__

#include <linux/compiler.h>
#include <asm/fpstate.h>

#define THREAD_SIZE_ORDER	1
#define THREAD_SIZE		8192
#define THREAD_START_SP		(THREAD_SIZE - 8)

#ifndef __ASSEMBLY__

struct task_struct;

#include <asm/types.h>

typedef struct {
	unsigned long seg;
} mm_segment_t;

struct cpu_context_save {
	__u32	r4;
	__u32	r5;
	__u32	r6;
	__u32	r7;
	__u32	r8;
	__u32	r9;
	__u32	r10;
	__u32	r11;
	__u32	r12;
	__u32	r13;
	__u32	r14;
	__u32	r15;
	__u32	r16;
	__u32	r17;
	__u32	r18;
	__u32	r19;
	__u32	r20;
	__u32	r21;
	__u32	r22;
	__u32	r23;
	__u32	r24;
	__u32	r25;
	__u32	r26;
	__u32	fp;
	__u32	sp;
	__u32	pc;
};

/*
 * low level task data that entry.S needs immediate access to.
 * __switch_to() assumes cpu_context follows immediately after cpu_domain.
 */
struct thread_info {
	unsigned long		flags;		/* low level flags */
	int			preempt_count;	/* 0 => preemptable */
						/* <0 => bug */
	mm_segment_t		addr_limit;	/* address limit */
	struct task_struct	*task;		/* main task structure */
	__u32			cpu;		/* cpu */
	struct cpu_context_save	cpu_context;	/* cpu context */
	__u32			syscall;	/* syscall number */
	__u8			used_cp[16];	/* thread used copro */
#ifdef CONFIG_UNICORE_FPU_F64
	struct fp_state		fpstate __attribute__((aligned(8)));
#endif
};

#define INIT_THREAD_INFO(tsk)						\
{									\
	.task		= &tsk,						\
	.flags		= 0,						\
	.preempt_count	= INIT_PREEMPT_COUNT,				\
	.addr_limit	= KERNEL_DS,					\
}

/*
 * how to get the thread information struct from C
 */
static inline struct thread_info *current_thread_info(void) __attribute_const__;

static inline struct thread_info *current_thread_info(void)
{
	register unsigned long sp asm ("sp");
	return (struct thread_info *)(sp & ~(THREAD_SIZE - 1));
}

#define thread_saved_pc(tsk)	\
	((unsigned long)(task_thread_info(tsk)->cpu_context.pc))
#define thread_saved_sp(tsk)	\
	((unsigned long)(task_thread_info(tsk)->cpu_context.sp))
#define thread_saved_fp(tsk)	\
	((unsigned long)(task_thread_info(tsk)->cpu_context.fp))

#endif

/*
 * thread information flags:
 *  TIF_SYSCALL_TRACE	- syscall trace active
 *  TIF_SIGPENDING	- signal pending
 *  TIF_NEED_RESCHED	- rescheduling necessary
 *  TIF_NOTIFY_RESUME	- callback before returning to user
 */
#define TIF_SIGPENDING		0
#define TIF_NEED_RESCHED	1
#define TIF_NOTIFY_RESUME	2	/* callback before returning to user */
#define TIF_SYSCALL_TRACE	8
#define TIF_MEMDIE		18
#define TIF_RESTORE_SIGMASK	20

#define _TIF_SIGPENDING		(1 << TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1 << TIF_NEED_RESCHED)
#define _TIF_NOTIFY_RESUME	(1 << TIF_NOTIFY_RESUME)
#define _TIF_SYSCALL_TRACE	(1 << TIF_SYSCALL_TRACE)

/*
 * Change these and you break ASM code in entry-common.S
 */
#define _TIF_WORK_MASK \
	(_TIF_SIGPENDING | _TIF_NEED_RESCHED | _TIF_NOTIFY_RESUME)

#endif /* __KERNEL__ */
#endif /* __UNICORE_THREAD_INFO_H__ */
