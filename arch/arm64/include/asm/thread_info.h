/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Based on arch/arm/include/asm/thread_info.h
 *
 * Copyright (C) 2002 Russell King.
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_THREAD_INFO_H
#define __ASM_THREAD_INFO_H

#include <linux/compiler.h>

#ifndef __ASSEMBLY__

struct task_struct;

#include <asm/memory.h>
#include <asm/stack_pointer.h>
#include <asm/types.h>

/*
 * low level task data that entry.S needs immediate access to.
 */
struct thread_info {
	unsigned long		flags;		/* low level flags */
#ifdef CONFIG_ARM64_SW_TTBR0_PAN
	u64			ttbr0;		/* saved TTBR0_EL1 */
#endif
	union {
		u64		preempt_count;	/* 0 => preemptible, <0 => bug */
		struct {
#ifdef CONFIG_CPU_BIG_ENDIAN
			u32	need_resched;
			u32	count;
#else
			u32	count;
			u32	need_resched;
#endif
		} preempt;
	};
#ifdef CONFIG_SHADOW_CALL_STACK
	void			*scs_base;
	void			*scs_sp;
#endif
};

#define thread_saved_pc(tsk)	\
	((unsigned long)(tsk->thread.cpu_context.pc))
#define thread_saved_sp(tsk)	\
	((unsigned long)(tsk->thread.cpu_context.sp))
#define thread_saved_fp(tsk)	\
	((unsigned long)(tsk->thread.cpu_context.fp))

void arch_setup_new_exec(void);
#define arch_setup_new_exec     arch_setup_new_exec

void arch_release_task_struct(struct task_struct *tsk);
int arch_dup_task_struct(struct task_struct *dst,
				struct task_struct *src);

#endif

#define TIF_SIGPENDING		0	/* signal pending */
#define TIF_NEED_RESCHED	1	/* rescheduling necessary */
#define TIF_NOTIFY_RESUME	2	/* callback before returning to user */
#define TIF_FOREIGN_FPSTATE	3	/* CPU's FP state is not current's */
#define TIF_UPROBE		4	/* uprobe breakpoint or singlestep */
#define TIF_MTE_ASYNC_FAULT	5	/* MTE Asynchronous Tag Check Fault */
#define TIF_NOTIFY_SIGNAL	6	/* signal notifications exist */
#define TIF_SYSCALL_TRACE	8	/* syscall trace active */
#define TIF_SYSCALL_AUDIT	9	/* syscall auditing */
#define TIF_SYSCALL_TRACEPOINT	10	/* syscall tracepoint for ftrace */
#define TIF_SECCOMP		11	/* syscall secure computing */
#define TIF_SYSCALL_EMU		12	/* syscall emulation active */
#define TIF_MEMDIE		18	/* is terminating due to OOM killer */
#define TIF_FREEZE		19
#define TIF_RESTORE_SIGMASK	20
#define TIF_SINGLESTEP		21
#define TIF_32BIT		22	/* 32bit process */
#define TIF_SVE			23	/* Scalable Vector Extension in use */
#define TIF_SVE_VL_INHERIT	24	/* Inherit sve_vl_onexec across exec */
#define TIF_SSBD		25	/* Wants SSB mitigation */
#define TIF_TAGGED_ADDR		26	/* Allow tagged user addresses */

#define _TIF_SIGPENDING		(1 << TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1 << TIF_NEED_RESCHED)
#define _TIF_NOTIFY_RESUME	(1 << TIF_NOTIFY_RESUME)
#define _TIF_FOREIGN_FPSTATE	(1 << TIF_FOREIGN_FPSTATE)
#define _TIF_SYSCALL_TRACE	(1 << TIF_SYSCALL_TRACE)
#define _TIF_SYSCALL_AUDIT	(1 << TIF_SYSCALL_AUDIT)
#define _TIF_SYSCALL_TRACEPOINT	(1 << TIF_SYSCALL_TRACEPOINT)
#define _TIF_SECCOMP		(1 << TIF_SECCOMP)
#define _TIF_SYSCALL_EMU	(1 << TIF_SYSCALL_EMU)
#define _TIF_UPROBE		(1 << TIF_UPROBE)
#define _TIF_SINGLESTEP		(1 << TIF_SINGLESTEP)
#define _TIF_32BIT		(1 << TIF_32BIT)
#define _TIF_SVE		(1 << TIF_SVE)
#define _TIF_MTE_ASYNC_FAULT	(1 << TIF_MTE_ASYNC_FAULT)
#define _TIF_NOTIFY_SIGNAL	(1 << TIF_NOTIFY_SIGNAL)

#define _TIF_WORK_MASK		(_TIF_NEED_RESCHED | _TIF_SIGPENDING | \
				 _TIF_NOTIFY_RESUME | _TIF_FOREIGN_FPSTATE | \
				 _TIF_UPROBE | _TIF_MTE_ASYNC_FAULT | \
				 _TIF_NOTIFY_SIGNAL)

#define _TIF_SYSCALL_WORK	(_TIF_SYSCALL_TRACE | _TIF_SYSCALL_AUDIT | \
				 _TIF_SYSCALL_TRACEPOINT | _TIF_SECCOMP | \
				 _TIF_SYSCALL_EMU)

#ifdef CONFIG_SHADOW_CALL_STACK
#define INIT_SCS							\
	.scs_base	= init_shadow_call_stack,			\
	.scs_sp		= init_shadow_call_stack,
#else
#define INIT_SCS
#endif

#define INIT_THREAD_INFO(tsk)						\
{									\
	.flags		= _TIF_FOREIGN_FPSTATE,				\
	.preempt_count	= INIT_PREEMPT_COUNT,				\
	INIT_SCS							\
}

#endif /* __ASM_THREAD_INFO_H */
