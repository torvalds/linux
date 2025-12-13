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
 * x86-64 has a fixed-length stack frame, but it depends on whether
 * or not FRED is enabled. Future versions of FRED might make this
 * dynamic, but for now it is always 2 words longer.
 */
#ifdef CONFIG_X86_32
# ifdef CONFIG_VM86
#  define TOP_OF_KERNEL_STACK_PADDING 16
# else
#  define TOP_OF_KERNEL_STACK_PADDING 8
# endif
#else /* x86-64 */
# ifdef CONFIG_X86_FRED
#  define TOP_OF_KERNEL_STACK_PADDING (2 * 8)
# else
#  define TOP_OF_KERNEL_STACK_PADDING 0
# endif
#endif

/*
 * low level task data that entry.S needs immediate access to
 * - this struct should fit entirely inside of one cache line
 * - this struct shares the supervisor stack pages
 */
#ifndef __ASSEMBLER__
struct task_struct;
#include <asm/cpufeature.h>
#include <linux/atomic.h>

struct thread_info {
	unsigned long		flags;		/* low level flags */
	unsigned long		syscall_work;	/* SYSCALL_WORK_ flags */
	u32			status;		/* thread synchronous flags */
#ifdef CONFIG_SMP
	u32			cpu;		/* current CPU */
#endif
};

#define INIT_THREAD_INFO(tsk)			\
{						\
	.flags		= 0,			\
}

#else /* !__ASSEMBLER__ */

#include <asm/asm-offsets.h>

#endif

/*
 * Tell the generic TIF infrastructure which bits x86 supports
 */
#define HAVE_TIF_NEED_RESCHED_LAZY
#define HAVE_TIF_POLLING_NRFLAG
#define HAVE_TIF_SINGLESTEP

#include <asm-generic/thread_info_tif.h>

/* Architecture specific TIF space starts at 16 */
#define TIF_SSBD		16	/* Speculative store bypass disable */
#define TIF_SPEC_IB		17	/* Indirect branch speculation mitigation */
#define TIF_SPEC_L1D_FLUSH	18	/* Flush L1D on mm switches (processes) */
#define TIF_NEED_FPU_LOAD	19	/* load FPU on return to userspace */
#define TIF_NOCPUID		20	/* CPUID is not accessible in userland */
#define TIF_NOTSC		21	/* TSC is not accessible in userland */
#define TIF_IO_BITMAP		22	/* uses I/O bitmap */
#define TIF_SPEC_FORCE_UPDATE	23	/* Force speculation MSR update in context switch */
#define TIF_FORCED_TF		24	/* true if TF in eflags artificially */
#define TIF_SINGLESTEP		25	/* reenable singlestep on user return*/
#define TIF_BLOCKSTEP		26	/* set when we want DEBUGCTLMSR_BTF */
#define TIF_LAZY_MMU_UPDATES	27	/* task is updating the mmu lazily */
#define TIF_ADDR32		28	/* 32-bit address space on 64 bits */

#define _TIF_SSBD		BIT(TIF_SSBD)
#define _TIF_SPEC_IB		BIT(TIF_SPEC_IB)
#define _TIF_SPEC_L1D_FLUSH	BIT(TIF_SPEC_L1D_FLUSH)
#define _TIF_NEED_FPU_LOAD	BIT(TIF_NEED_FPU_LOAD)
#define _TIF_NOCPUID		BIT(TIF_NOCPUID)
#define _TIF_NOTSC		BIT(TIF_NOTSC)
#define _TIF_IO_BITMAP		BIT(TIF_IO_BITMAP)
#define _TIF_SPEC_FORCE_UPDATE	BIT(TIF_SPEC_FORCE_UPDATE)
#define _TIF_FORCED_TF		BIT(TIF_FORCED_TF)
#define _TIF_BLOCKSTEP		BIT(TIF_BLOCKSTEP)
#define _TIF_SINGLESTEP		BIT(TIF_SINGLESTEP)
#define _TIF_LAZY_MMU_UPDATES	BIT(TIF_LAZY_MMU_UPDATES)
#define _TIF_ADDR32		BIT(TIF_ADDR32)

/* flags to check in __switch_to() */
#define _TIF_WORK_CTXSW_BASE					\
	(_TIF_NOCPUID | _TIF_NOTSC | _TIF_BLOCKSTEP |		\
	 _TIF_SSBD | _TIF_SPEC_FORCE_UPDATE)

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
#ifndef __ASSEMBLER__

/*
 * Walks up the stack frames to make sure that the specified object is
 * entirely contained by a single stack frame.
 *
 * Returns:
 *	GOOD_FRAME	if within a frame
 *	BAD_STACK	if placed across a frame boundary (or outside stack)
 *	NOT_STACK	unable to determine (no frame pointers, etc)
 *
 * This function reads pointers from the stack and dereferences them. The
 * pointers may not have their KMSAN shadow set up properly, which may result
 * in false positive reports. Disable instrumentation to avoid those.
 */
__no_kmsan_checks
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

#endif  /* !__ASSEMBLER__ */

/*
 * Thread-synchronous status.
 *
 * This is different from the flags in that nobody else
 * ever touches our thread-synchronous status, so we don't
 * have to worry about atomic accesses.
 */
#define TS_COMPAT		0x0002	/* 32bit syscall active (64BIT)*/

#ifndef __ASSEMBLER__
#ifdef CONFIG_COMPAT
#define TS_I386_REGS_POKED	0x0004	/* regs poked by 32-bit ptracer */

#define arch_set_restart_data(restart)	\
	do { restart->arch_data = current_thread_info()->status; } while (0)

#endif

#ifdef CONFIG_X86_32
#define in_ia32_syscall() true
#else
#define in_ia32_syscall() (IS_ENABLED(CONFIG_IA32_EMULATION) && \
			   current_thread_info()->status & TS_COMPAT)
#endif

extern void arch_setup_new_exec(void);
#define arch_setup_new_exec arch_setup_new_exec
#endif	/* !__ASSEMBLER__ */

#endif /* _ASM_X86_THREAD_INFO_H */
