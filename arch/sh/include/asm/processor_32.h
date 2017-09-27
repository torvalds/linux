/*
 * include/asm-sh/processor.h
 *
 * Copyright (C) 1999, 2000  Niibe Yutaka
 * Copyright (C) 2002, 2003  Paul Mundt
 */

#ifndef __ASM_SH_PROCESSOR_32_H
#define __ASM_SH_PROCESSOR_32_H
#ifdef __KERNEL__

#include <linux/compiler.h>
#include <linux/linkage.h>
#include <asm/page.h>
#include <asm/types.h>
#include <asm/hw_breakpoint.h>

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr() ({ void *pc; __asm__("mova	1f, %0\n.align 2\n1:":"=z" (pc)); pc; })

/* Core Processor Version Register */
#define CCN_PVR		0xff000030
#define CCN_CVR		0xff000040
#define CCN_PRR		0xff000044

/*
 * User space process size: 2GB.
 *
 * Since SH7709 and SH7750 have "area 7", we can't use 0x7c000000--0x7fffffff
 */
#define TASK_SIZE	0x7c000000UL

#define STACK_TOP	TASK_SIZE
#define STACK_TOP_MAX	STACK_TOP

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE	PAGE_ALIGN(TASK_SIZE / 3)

/*
 * Bit of SR register
 *
 * FD-bit:
 *     When it's set, it means the processor doesn't have right to use FPU,
 *     and it results exception when the floating operation is executed.
 *
 * IMASK-bit:
 *     Interrupt level mask
 */
#define SR_DSP		0x00001000
#define SR_IMASK	0x000000f0
#define SR_FD		0x00008000
#define SR_MD		0x40000000

/*
 * DSP structure and data
 */
struct sh_dsp_struct {
	unsigned long dsp_regs[14];
	long status;
};

/*
 * FPU structure and data
 */

struct sh_fpu_hard_struct {
	unsigned long fp_regs[16];
	unsigned long xfp_regs[16];
	unsigned long fpscr;
	unsigned long fpul;

	long status; /* software status information */
};

/* Dummy fpu emulator  */
struct sh_fpu_soft_struct {
	unsigned long fp_regs[16];
	unsigned long xfp_regs[16];
	unsigned long fpscr;
	unsigned long fpul;

	unsigned char lookahead;
	unsigned long entry_pc;
};

union thread_xstate {
	struct sh_fpu_hard_struct hardfpu;
	struct sh_fpu_soft_struct softfpu;
};

struct thread_struct {
	/* Saved registers when thread is descheduled */
	unsigned long sp;
	unsigned long pc;

	/* Various thread flags, see SH_THREAD_xxx */
	unsigned long flags;

	/* Save middle states of ptrace breakpoints */
	struct perf_event *ptrace_bps[HBP_NUM];

#ifdef CONFIG_SH_DSP
	/* Dsp status information */
	struct sh_dsp_struct dsp_status;
#endif

	/* Extended processor state */
	union thread_xstate *xstate;

	/*
	 * fpu_counter contains the number of consecutive context switches
	 * that the FPU is used. If this is over a threshold, the lazy fpu
	 * saving becomes unlazy to save the trap. This is an unsigned char
	 * so that after 256 times the counter wraps and the behavior turns
	 * lazy again; this to deal with bursty apps that only use FPU for
	 * a short time
	 */
	unsigned char fpu_counter;
};

#define INIT_THREAD  {						\
	.sp = sizeof(init_stack) + (long) &init_stack,		\
	.flags = 0,						\
}

/* Forward declaration, a strange C thing */
struct task_struct;

extern void start_thread(struct pt_regs *regs, unsigned long new_pc, unsigned long new_sp);

/* Free all resources held by a thread. */
extern void release_thread(struct task_struct *);

/*
 * FPU lazy state save handling.
 */

static __inline__ void disable_fpu(void)
{
	unsigned long __dummy;

	/* Set FD flag in SR */
	__asm__ __volatile__("stc	sr, %0\n\t"
			     "or	%1, %0\n\t"
			     "ldc	%0, sr"
			     : "=&r" (__dummy)
			     : "r" (SR_FD));
}

static __inline__ void enable_fpu(void)
{
	unsigned long __dummy;

	/* Clear out FD flag in SR */
	__asm__ __volatile__("stc	sr, %0\n\t"
			     "and	%1, %0\n\t"
			     "ldc	%0, sr"
			     : "=&r" (__dummy)
			     : "r" (~SR_FD));
}

/* Double presision, NANS as NANS, rounding to nearest, no exceptions */
#define FPSCR_INIT  0x00080000

#define	FPSCR_CAUSE_MASK	0x0001f000	/* Cause bits */
#define	FPSCR_FLAG_MASK		0x0000007c	/* Flag bits */

/*
 * Return saved PC of a blocked thread.
 */
#define thread_saved_pc(tsk)	(tsk->thread.pc)

void show_trace(struct task_struct *tsk, unsigned long *sp,
		struct pt_regs *regs);

#ifdef CONFIG_DUMP_CODE
void show_code(struct pt_regs *regs);
#else
static inline void show_code(struct pt_regs *regs)
{
}
#endif

extern unsigned long get_wchan(struct task_struct *p);

#define KSTK_EIP(tsk)  (task_pt_regs(tsk)->pc)
#define KSTK_ESP(tsk)  (task_pt_regs(tsk)->regs[15])

#if defined(CONFIG_CPU_SH2A) || defined(CONFIG_CPU_SH4)

#define PREFETCH_STRIDE		L1_CACHE_BYTES
#define ARCH_HAS_PREFETCH
#define ARCH_HAS_PREFETCHW

static inline void prefetch(const void *x)
{
	__builtin_prefetch(x, 0, 3);
}

static inline void prefetchw(const void *x)
{
	__builtin_prefetch(x, 1, 3);
}
#endif

#endif /* __KERNEL__ */
#endif /* __ASM_SH_PROCESSOR_32_H */
