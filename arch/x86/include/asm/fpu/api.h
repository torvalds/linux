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
 * If you intend to use the FPU in softirq you need to check first with
 * irq_fpu_usable() if it is possible.
 */
extern void kernel_fpu_begin(void);
extern void kernel_fpu_end(void);
extern bool irq_fpu_usable(void);
extern void fpregs_mark_activate(void);

/*
 * Use fpregs_lock() while editing CPU's FPU registers or fpu->state.
 * A context switch will (and softirq might) save CPU's FPU registers to
 * fpu->state and set TIF_NEED_FPU_LOAD leaving CPU's FPU registers in
 * a random state.
 */
static inline void fpregs_lock(void)
{
	preempt_disable();
	local_bh_disable();
}

static inline void fpregs_unlock(void)
{
	local_bh_enable();
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

#ifdef CONFIG_IOMMU_SUPPORT
/* Update current's PASID MSR/state by mm's PASID. */
void update_pasid(void);
#else
static inline void update_pasid(void) { }
#endif
#endif /* _ASM_X86_FPU_API_H */
