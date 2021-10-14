/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 1994 Linus Torvalds
 *
 * Pentium III FXSR, SSE support
 * General FPU state handling cleanups
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 * x86-64 work by Andi Kleen 2002
 */

#ifndef _ASM_X86_FPU_API_H
#define _ASM_X86_FPU_API_H
#include <linux/bottom_half.h>

/*
 * Use kernel_fpu_begin/end() if you intend to use FPU in kernel context. It
 * disables preemption so be careful if you intend to use it for long periods
 * of time.
 * If you intend to use the FPU in irq/softirq you need to check first with
 * irq_fpu_usable() if it is possible.
 */

/* Kernel FPU states to initialize in kernel_fpu_begin_mask() */
#define KFPU_387	_BITUL(0)	/* 387 state will be initialized */
#define KFPU_MXCSR	_BITUL(1)	/* MXCSR will be initialized */

extern void kernel_fpu_begin_mask(unsigned int kfpu_mask);
extern void kernel_fpu_end(void);
extern bool irq_fpu_usable(void);
extern void fpregs_mark_activate(void);

/* Code that is unaware of kernel_fpu_begin_mask() can use this */
static inline void kernel_fpu_begin(void)
{
#ifdef CONFIG_X86_64
	/*
	 * Any 64-bit code that uses 387 instructions must explicitly request
	 * KFPU_387.
	 */
	kernel_fpu_begin_mask(KFPU_MXCSR);
#else
	/*
	 * 32-bit kernel code may use 387 operations as well as SSE2, etc,
	 * as long as it checks that the CPU has the required capability.
	 */
	kernel_fpu_begin_mask(KFPU_387 | KFPU_MXCSR);
#endif
}

/*
 * Use fpregs_lock() while editing CPU's FPU registers or fpu->state.
 * A context switch will (and softirq might) save CPU's FPU registers to
 * fpu->state and set TIF_NEED_FPU_LOAD leaving CPU's FPU registers in
 * a random state.
 *
 * local_bh_disable() protects against both preemption and soft interrupts
 * on !RT kernels.
 *
 * On RT kernels local_bh_disable() is not sufficient because it only
 * serializes soft interrupt related sections via a local lock, but stays
 * preemptible. Disabling preemption is the right choice here as bottom
 * half processing is always in thread context on RT kernels so it
 * implicitly prevents bottom half processing as well.
 *
 * Disabling preemption also serializes against kernel_fpu_begin().
 */
static inline void fpregs_lock(void)
{
	if (!IS_ENABLED(CONFIG_PREEMPT_RT))
		local_bh_disable();
	else
		preempt_disable();
}

static inline void fpregs_unlock(void)
{
	if (!IS_ENABLED(CONFIG_PREEMPT_RT))
		local_bh_enable();
	else
		preempt_enable();
}

#ifdef CONFIG_X86_DEBUG_FPU
extern void fpregs_assert_state_consistent(void);
#else
static inline void fpregs_assert_state_consistent(void) { }
#endif

/*
 * Load the task FPU state before returning to userspace.
 */
extern void switch_fpu_return(void);

/*
 * Query the presence of one or more xfeatures. Works on any legacy CPU as well.
 *
 * If 'feature_name' is set then put a human-readable description of
 * the feature there as well - this can be used to print error (or success)
 * messages.
 */
extern int cpu_has_xfeatures(u64 xfeatures_mask, const char **feature_name);

/*
 * Tasks that are not using SVA have mm->pasid set to zero to note that they
 * will not have the valid bit set in MSR_IA32_PASID while they are running.
 */
#define PASID_DISABLED	0

#endif /* _ASM_X86_FPU_API_H */
