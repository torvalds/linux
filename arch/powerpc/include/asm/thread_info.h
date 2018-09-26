/* SPDX-License-Identifier: GPL-2.0 */
/* thread_info.h: PowerPC low-level thread information
 * adapted from the i386 version by Paul Mackerras
 *
 * Copyright (C) 2002  David Howells (dhowells@redhat.com)
 * - Incorporating suggestions made by Linus Torvalds and Dave Miller
 */

#ifndef _ASM_POWERPC_THREAD_INFO_H
#define _ASM_POWERPC_THREAD_INFO_H

#include <asm/asm-const.h>

#ifdef __KERNEL__

#define THREAD_SHIFT		CONFIG_THREAD_SHIFT

#define THREAD_SIZE		(1 << THREAD_SHIFT)

#ifdef CONFIG_PPC64
#define CURRENT_THREAD_INFO(dest, sp)	stringify_in_c(clrrdi dest, sp, THREAD_SHIFT)
#else
#define CURRENT_THREAD_INFO(dest, sp)	stringify_in_c(rlwinm dest, sp, 0, 0, 31-THREAD_SHIFT)
#endif

#ifndef __ASSEMBLY__
#include <linux/cache.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/accounting.h>

/*
 * low level task data.
 */
struct thread_info {
	struct task_struct *task;		/* main task structure */
	int		cpu;			/* cpu we're on */
	int		preempt_count;		/* 0 => preemptable,
						   <0 => BUG */
	unsigned long	local_flags;		/* private flags for thread */
#ifdef CONFIG_LIVEPATCH
	unsigned long *livepatch_sp;
#endif
#if defined(CONFIG_VIRT_CPU_ACCOUNTING_NATIVE) && defined(CONFIG_PPC32)
	struct cpu_accounting_data accounting;
#endif
	/* low level flags - has atomic operations done on it */
	unsigned long	flags ____cacheline_aligned_in_smp;
};

/*
 * macros/functions for gaining access to the thread information structure
 */
#define INIT_THREAD_INFO(tsk)			\
{						\
	.task =		&tsk,			\
	.cpu =		0,			\
	.preempt_count = INIT_PREEMPT_COUNT,	\
	.flags =	0,			\
}

#define THREAD_SIZE_ORDER	(THREAD_SHIFT - PAGE_SHIFT)

/* how to get the thread information struct from C */
static inline struct thread_info *current_thread_info(void)
{
	unsigned long val;

	asm (CURRENT_THREAD_INFO(%0,1) : "=r" (val));

	return (struct thread_info *)val;
}

extern int arch_dup_task_struct(struct task_struct *dst, struct task_struct *src);
#endif /* __ASSEMBLY__ */

/*
 * thread information flag bit numbers
 */
#define TIF_SYSCALL_TRACE	0	/* syscall trace active */
#define TIF_SIGPENDING		1	/* signal pending */
#define TIF_NEED_RESCHED	2	/* rescheduling necessary */
#define TIF_FSCHECK		3	/* Check FS is USER_DS on return */
#define TIF_32BIT		4	/* 32 bit binary */
#define TIF_RESTORE_TM		5	/* need to restore TM FP/VEC/VSX */
#define TIF_PATCH_PENDING	6	/* pending live patching update */
#define TIF_SYSCALL_AUDIT	7	/* syscall auditing active */
#define TIF_SINGLESTEP		8	/* singlestepping active */
#define TIF_NOHZ		9	/* in adaptive nohz mode */
#define TIF_SECCOMP		10	/* secure computing */
#define TIF_RESTOREALL		11	/* Restore all regs (implies NOERROR) */
#define TIF_NOERROR		12	/* Force successful syscall return */
#define TIF_NOTIFY_RESUME	13	/* callback before returning to user */
#define TIF_UPROBE		14	/* breakpointed or single-stepping */
#define TIF_SYSCALL_TRACEPOINT	15	/* syscall tracepoint instrumentation */
#define TIF_EMULATE_STACK_STORE	16	/* Is an instruction emulation
						for stack store? */
#define TIF_MEMDIE		17	/* is terminating due to OOM killer */
#if defined(CONFIG_PPC64)
#define TIF_ELF2ABI		18	/* function descriptors must die! */
#endif
#define TIF_POLLING_NRFLAG	19	/* true if poll_idle() is polling TIF_NEED_RESCHED */

/* as above, but as bit values */
#define _TIF_SYSCALL_TRACE	(1<<TIF_SYSCALL_TRACE)
#define _TIF_SIGPENDING		(1<<TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1<<TIF_NEED_RESCHED)
#define _TIF_POLLING_NRFLAG	(1<<TIF_POLLING_NRFLAG)
#define _TIF_32BIT		(1<<TIF_32BIT)
#define _TIF_RESTORE_TM		(1<<TIF_RESTORE_TM)
#define _TIF_PATCH_PENDING	(1<<TIF_PATCH_PENDING)
#define _TIF_SYSCALL_AUDIT	(1<<TIF_SYSCALL_AUDIT)
#define _TIF_SINGLESTEP		(1<<TIF_SINGLESTEP)
#define _TIF_SECCOMP		(1<<TIF_SECCOMP)
#define _TIF_RESTOREALL		(1<<TIF_RESTOREALL)
#define _TIF_NOERROR		(1<<TIF_NOERROR)
#define _TIF_NOTIFY_RESUME	(1<<TIF_NOTIFY_RESUME)
#define _TIF_UPROBE		(1<<TIF_UPROBE)
#define _TIF_SYSCALL_TRACEPOINT	(1<<TIF_SYSCALL_TRACEPOINT)
#define _TIF_EMULATE_STACK_STORE	(1<<TIF_EMULATE_STACK_STORE)
#define _TIF_NOHZ		(1<<TIF_NOHZ)
#define _TIF_FSCHECK		(1<<TIF_FSCHECK)
#define _TIF_SYSCALL_DOTRACE	(_TIF_SYSCALL_TRACE | _TIF_SYSCALL_AUDIT | \
				 _TIF_SECCOMP | _TIF_SYSCALL_TRACEPOINT | \
				 _TIF_NOHZ)

#define _TIF_USER_WORK_MASK	(_TIF_SIGPENDING | _TIF_NEED_RESCHED | \
				 _TIF_NOTIFY_RESUME | _TIF_UPROBE | \
				 _TIF_RESTORE_TM | _TIF_PATCH_PENDING | \
				 _TIF_FSCHECK)
#define _TIF_PERSYSCALL_MASK	(_TIF_RESTOREALL|_TIF_NOERROR)

/* Bits in local_flags */
/* Don't move TLF_NAPPING without adjusting the code in entry_32.S */
#define TLF_NAPPING		0	/* idle thread enabled NAP mode */
#define TLF_SLEEPING		1	/* suspend code enabled SLEEP mode */
#define TLF_LAZY_MMU		3	/* tlb_batch is active */
#define TLF_RUNLATCH		4	/* Is the runlatch enabled? */

#define _TLF_NAPPING		(1 << TLF_NAPPING)
#define _TLF_SLEEPING		(1 << TLF_SLEEPING)
#define _TLF_LAZY_MMU		(1 << TLF_LAZY_MMU)
#define _TLF_RUNLATCH		(1 << TLF_RUNLATCH)

#ifndef __ASSEMBLY__

static inline bool test_thread_local_flags(unsigned int flags)
{
	struct thread_info *ti = current_thread_info();
	return (ti->local_flags & flags) != 0;
}

#ifdef CONFIG_PPC64
#define is_32bit_task()	(test_thread_flag(TIF_32BIT))
#else
#define is_32bit_task()	(1)
#endif

#if defined(CONFIG_PPC64)
#define is_elf2_task() (test_thread_flag(TIF_ELF2ABI))
#else
#define is_elf2_task() (0)
#endif

#endif	/* !__ASSEMBLY__ */

#endif /* __KERNEL__ */

#endif /* _ASM_POWERPC_THREAD_INFO_H */
