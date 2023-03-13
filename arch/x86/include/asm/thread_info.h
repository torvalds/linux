/* SPDX-License-Identifier: GPL-2.0 */
/* thread_info.h: low-level thread information
 *
 * Copyright (C) 2002  David Howells (dhowells@redhat.com)
 * - Incorporating suggestions made by Linus Torvalds and Dave Miller
 */

#ifndef _ASM_X86_THREAD_INFO_H
#define _ASM_X86_THREAD_INFO_H

#include <linux/compiler.h>
#include <asm/page.h>
#include <asm/percpu.h>
#include <asm/types.h>

/*
 * TOP_OF_KERNEL_STACK_PADDING is a number of unused bytes that we
 * reserve at the top of the kernel stack.  We do it because of a nasty
 * 32-bit corner case.  On x86_32, the hardware stack frame is
 * variable-length.  Except for vm86 mode, struct pt_regs assumes a
 * maximum-length frame.  If we enter from CPL 0, the top 8 bytes of
 * pt_regs don't actually exist.  Ordinarily this doesn't matter, but it
 * does in at least one case:
 *
 * If we take an NMI early enough in SYSENTER, then we can end up with
 * pt_regs that extends above sp0.  On the way out, in the espfix code,
 * we can read the saved SS value, but that value will be above sp0.
 * Without this offset, that can result in a page fault.  (We are
 * careful that, in this case, the value we read doesn't matter.)
 *
 * In vm86 mode, the hardware frame is much longer still, so add 16
 * bytes to make room for the real-mode segments.
 *
 * x86_64 has a fixed-length stack frame.
 */
#ifdef CONFIG_X86_32
# ifdef CONFIG_VM86
#  define TOP_OF_KERNEL_STACK_PADDING 16
# else
#  define TOP_OF_KERNEL_STACK_PADDING 8
# endif
#else
# define TOP_OF_KERNEL_STACK_PADDING 0
#endif

/*
 * low level task data that entry.S needs immediate access to
 * - this struct should fit entirely inside of one cache line
 * - this struct shares the supervisor stack pages
 */
#ifndef __ASSEMBLY__
struct task_struct;
#include <asm/cpufeature.h>
#include <linux/atomic.h>

struct thread_info {
	unsigned long		flags;		/* low level flags */
	u32			status;		/* thread synchronous flags */
};

#define INIT_THREAD_INFO(tsk)			\
{						\
	.flags		= 0,			\
}

#else /* !__ASSEMBLY__ */

#include <asm/asm-offsets.h>

#endif

/*
 * thread information flags
 * - these are process state flags that various assembly files
 *   may need to access
 */
#define TIF_SYSCALL_TRACE	0	/* syscall trace active */
#define TIF_NOTIFY_RESUME	1	/* callback before returning to user */
#define TIF_SIGPENDING		2	/* signal pending */
#define TIF_NEED_RESCHED	3	/* rescheduling necessary */
#define TIF_SINGLESTEP		4	/* reenable singlestep on user return*/
#define TIF_SSBD		5	/* Speculative store bypass disable */
#define TIF_SYSCALL_EMU		6	/* syscall emulation active */
#define TIF_SYSCALL_AUDIT	7	/* syscall auditing active */
#define TIF_SECCOMP		8	/* secure computing */
#define TIF_SPEC_IB		9	/* Indirect branch speculation mitigation */
#define TIF_SPEC_FORCE_UPDATE	10	/* Force speculation MSR update in context switch */
#define TIF_USER_RETURN_NOTIFY	11	/* notify kernel of userspace return */
#define TIF_UPROBE		12	/* breakpointed or singlestepping */
#define TIF_PATCH_PENDING	13	/* pending live patching update */
#define TIF_NEED_FPU_LOAD	14	/* load FPU on return to userspace */
#define TIF_NOCPUID		15	/* CPUID is not accessible in userland */
#define TIF_NOTSC		16	/* TSC is not accessible in userland */
#define TIF_IA32		17	/* IA32 compatibility process */
#define TIF_SLD			18	/* Restore split lock detection on context switch */
#define TIF_NOTIFY_SIGNAL	19	/* signal notifications exist */
#define TIF_MEMDIE		20	/* is terminating due to OOM killer */
#define TIF_POLLING_NRFLAG	21	/* idle is polling for TIF_NEED_RESCHED */
#define TIF_IO_BITMAP		22	/* uses I/O bitmap */
#define TIF_FORCED_TF		24	/* true if TF in eflags artificially */
#define TIF_BLOCKSTEP		25	/* set when we want DEBUGCTLMSR_BTF */
#define TIF_LAZY_MMU_UPDATES	27	/* task is updating the mmu lazily */
#define TIF_SYSCALL_TRACEPOINT	28	/* syscall tracepoint instrumentation */
#define TIF_ADDR32		29	/* 32-bit address space on 64 bits */
#define TIF_X32			30	/* 32-bit native x86-64 binary */

#define _TIF_SYSCALL_TRACE	(1 << TIF_SYSCALL_TRACE)
#define _TIF_NOTIFY_RESUME	(1 << TIF_NOTIFY_RESUME)
#define _TIF_SIGPENDING		(1 << TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1 << TIF_NEED_RESCHED)
#define _TIF_SINGLESTEP		(1 << TIF_SINGLESTEP)
#define _TIF_SSBD		(1 << TIF_SSBD)
#define _TIF_SYSCALL_EMU	(1 << TIF_SYSCALL_EMU)
#define _TIF_SYSCALL_AUDIT	(1 << TIF_SYSCALL_AUDIT)
#define _TIF_SECCOMP		(1 << TIF_SECCOMP)
#define _TIF_SPEC_IB		(1 << TIF_SPEC_IB)
#define _TIF_SPEC_FORCE_UPDATE	(1 << TIF_SPEC_FORCE_UPDATE)
#define _TIF_USER_RETURN_NOTIFY	(1 << TIF_USER_RETURN_NOTIFY)
#define _TIF_UPROBE		(1 << TIF_UPROBE)
#define _TIF_PATCH_PENDING	(1 << TIF_PATCH_PENDING)
#define _TIF_NEED_FPU_LOAD	(1 << TIF_NEED_FPU_LOAD)
#define _TIF_NOCPUID		(1 << TIF_NOCPUID)
#define _TIF_NOTSC		(1 << TIF_NOTSC)
#define _TIF_IA32		(1 << TIF_IA32)
#define _TIF_NOTIFY_SIGNAL	(1 << TIF_NOTIFY_SIGNAL)
#define _TIF_SLD		(1 << TIF_SLD)
#define _TIF_POLLING_NRFLAG	(1 << TIF_POLLING_NRFLAG)
#define _TIF_IO_BITMAP		(1 << TIF_IO_BITMAP)
#define _TIF_FORCED_TF		(1 << TIF_FORCED_TF)
#define _TIF_BLOCKSTEP		(1 << TIF_BLOCKSTEP)
#define _TIF_LAZY_MMU_UPDATES	(1 << TIF_LAZY_MMU_UPDATES)
#define _TIF_SYSCALL_TRACEPOINT	(1 << TIF_SYSCALL_TRACEPOINT)
#define _TIF_ADDR32		(1 << TIF_ADDR32)
#define _TIF_X32		(1 << TIF_X32)

/* flags to check in __switch_to() */
#define _TIF_WORK_CTXSW_BASE					\
	(_TIF_NOCPUID | _TIF_NOTSC | _TIF_BLOCKSTEP |		\
	 _TIF_SSBD | _TIF_SPEC_FORCE_UPDATE | _TIF_SLD)

/*
 * Avoid calls to __switch_to_xtra() on UP as STIBP is not evaluated.
 */
#ifdef CONFIG_SMP
# define _TIF_WORK_CTXSW	(_TIF_WORK_CTXSW_BASE | _TIF_SPEC_IB)
#else
# define _TIF_WORK_CTXSW	(_TIF_WORK_CTXSW_BASE)
#endif

#ifdef CONFIG_X86_IOPL_IOPERM
# define _TIF_WORK_CTXSW_PREV	(_TIF_WORK_CTXSW| _TIF_USER_RETURN_NOTIFY | \
				 _TIF_IO_BITMAP)
#else
# define _TIF_WORK_CTXSW_PREV	(_TIF_WORK_CTXSW| _TIF_USER_RETURN_NOTIFY)
#endif

#define _TIF_WORK_CTXSW_NEXT	(_TIF_WORK_CTXSW)

#define STACK_WARN		(THREAD_SIZE/8)

/*
 * macros/functions for gaining access to the thread information structure
 *
 * preempt_count needs to be 1 initially, until the scheduler is functional.
 */
#ifndef __ASSEMBLY__

/*
 * Walks up the stack frames to make sure that the specified object is
 * entirely contained by a single stack frame.
 *
 * Returns:
 *	GOOD_FRAME	if within a frame
 *	BAD_STACK	if placed across a frame boundary (or outside stack)
 *	NOT_STACK	unable to determine (no frame pointers, etc)
 */
static inline int arch_within_stack_frames(const void * const stack,
					   const void * const stackend,
					   const void *obj, unsigned long len)
{
#if defined(CONFIG_FRAME_POINTER)
	const void *frame = NULL;
	const void *oldframe;

	oldframe = __builtin_frame_address(1);
	if (oldframe)
		frame = __builtin_frame_address(2);
	/*
	 * low ----------------------------------------------> high
	 * [saved bp][saved ip][args][local vars][saved bp][saved ip]
	 *                     ^----------------^
	 *               allow copies only within here
	 */
	while (stack <= frame && frame < stackend) {
		/*
		 * If obj + len extends past the last frame, this
		 * check won't pass and the next frame will be 0,
		 * causing us to bail out and correctly report
		 * the copy as invalid.
		 */
		if (obj + len <= frame)
			return obj >= oldframe + 2 * sizeof(void *) ?
				GOOD_FRAME : BAD_STACK;
		oldframe = frame;
		frame = *(const void * const *)frame;
	}
	return BAD_STACK;
#else
	return NOT_STACK;
#endif
}

#else /* !__ASSEMBLY__ */

#ifdef CONFIG_X86_64
# define cpu_current_top_of_stack (cpu_tss_rw + TSS_sp1)
#endif

#endif

/*
 * Thread-synchronous status.
 *
 * This is different from the flags in that nobody else
 * ever touches our thread-synchronous status, so we don't
 * have to worry about atomic accesses.
 */
#define TS_COMPAT		0x0002	/* 32bit syscall active (64BIT)*/

#ifndef __ASSEMBLY__
#ifdef CONFIG_COMPAT
#define TS_I386_REGS_POKED	0x0004	/* regs poked by 32-bit ptracer */
#define TS_COMPAT_RESTART	0x0008

#define arch_set_restart_data	arch_set_restart_data

static inline void arch_set_restart_data(struct restart_block *restart)
{
	struct thread_info *ti = current_thread_info();
	if (ti->status & TS_COMPAT)
		ti->status |= TS_COMPAT_RESTART;
	else
		ti->status &= ~TS_COMPAT_RESTART;
}
#endif

#ifdef CONFIG_X86_32
#define in_ia32_syscall() true
#else
#define in_ia32_syscall() (IS_ENABLED(CONFIG_IA32_EMULATION) && \
			   current_thread_info()->status & TS_COMPAT)
#endif

extern void arch_task_cache_init(void);
extern int arch_dup_task_struct(struct task_struct *dst, struct task_struct *src);
extern void arch_release_task_struct(struct task_struct *tsk);
extern void arch_setup_new_exec(void);
#define arch_setup_new_exec arch_setup_new_exec
#endif	/* !__ASSEMBLY__ */

#endif /* _ASM_X86_THREAD_INFO_H */
