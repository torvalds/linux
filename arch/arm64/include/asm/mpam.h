/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2025 Arm Ltd. */

#ifndef __ASM__MPAM_H
#define __ASM__MPAM_H

#include <linux/jump_label.h>
#include <linux/percpu.h>
#include <linux/sched.h>

#include <asm/sysreg.h>

DECLARE_STATIC_KEY_FALSE(mpam_enabled);
DECLARE_PER_CPU(u64, arm64_mpam_default);
DECLARE_PER_CPU(u64, arm64_mpam_current);

/*
 * The value of the MPAM0_EL1 sysreg when a task is in resctrl's default group.
 * This is used by the context switch code to use the resctrl CPU property
 * instead. The value is modified when CDP is enabled/disabled by mounting
 * the resctrl filesystem.
 */
extern u64 arm64_mpam_global_default;

/*
 * The resctrl filesystem writes to the partid/pmg values for threads and CPUs,
 * which may race with reads in mpam_thread_switch(). Ensure only one of the old
 * or new values are used. Particular care should be taken with the pmg field as
 * mpam_thread_switch() may read a partid and pmg that don't match, causing this
 * value to be stored with cache allocations, despite being considered 'free' by
 * resctrl.
 */
#ifdef CONFIG_ARM64_MPAM
static inline u64 mpam_get_regval(struct task_struct *tsk)
{
	return READ_ONCE(task_thread_info(tsk)->mpam_partid_pmg);
}

static inline void mpam_thread_switch(struct task_struct *tsk)
{
	u64 oldregval;
	int cpu = smp_processor_id();
	u64 regval = mpam_get_regval(tsk);

	if (!static_branch_likely(&mpam_enabled))
		return;

	if (regval == READ_ONCE(arm64_mpam_global_default))
		regval = READ_ONCE(per_cpu(arm64_mpam_default, cpu));

	oldregval = READ_ONCE(per_cpu(arm64_mpam_current, cpu));
	if (oldregval == regval)
		return;

	write_sysreg_s(regval | MPAM1_EL1_MPAMEN, SYS_MPAM1_EL1);
	isb();

	/* Synchronising the EL0 write is left until the ERET to EL0 */
	write_sysreg_s(regval, SYS_MPAM0_EL1);

	WRITE_ONCE(per_cpu(arm64_mpam_current, cpu), regval);
}
#else
static inline void mpam_thread_switch(struct task_struct *tsk) {}
#endif /* CONFIG_ARM64_MPAM */

#endif /* __ASM__MPAM_H */
