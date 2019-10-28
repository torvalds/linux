// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 ARM Ltd, All Rights Reserved.
 */

#include <linux/compat.h>
#include <linux/errno.h>
#include <linux/prctl.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/thread_info.h>

#include <asm/cpufeature.h>

static void ssbd_ssbs_enable(struct task_struct *task)
{
	u64 val = is_compat_thread(task_thread_info(task)) ?
		  PSR_AA32_SSBS_BIT : PSR_SSBS_BIT;

	task_pt_regs(task)->pstate |= val;
}

static void ssbd_ssbs_disable(struct task_struct *task)
{
	u64 val = is_compat_thread(task_thread_info(task)) ?
		  PSR_AA32_SSBS_BIT : PSR_SSBS_BIT;

	task_pt_regs(task)->pstate &= ~val;
}

/*
 * prctl interface for SSBD
 * FIXME: Drop the below ifdefery once merged in 4.18.
 */
#ifdef PR_SPEC_STORE_BYPASS
static int ssbd_prctl_set(struct task_struct *task, unsigned long ctrl)
{
	int state = arm64_get_ssbd_state();

	/* Unsupported */
	if (state == ARM64_SSBD_UNKNOWN)
		return -EINVAL;

	/* Treat the unaffected/mitigated state separately */
	if (state == ARM64_SSBD_MITIGATED) {
		switch (ctrl) {
		case PR_SPEC_ENABLE:
			return -EPERM;
		case PR_SPEC_DISABLE:
		case PR_SPEC_FORCE_DISABLE:
			return 0;
		}
	}

	/*
	 * Things are a bit backward here: the arm64 internal API
	 * *enables the mitigation* when the userspace API *disables
	 * speculation*. So much fun.
	 */
	switch (ctrl) {
	case PR_SPEC_ENABLE:
		/* If speculation is force disabled, enable is not allowed */
		if (state == ARM64_SSBD_FORCE_ENABLE ||
		    task_spec_ssb_force_disable(task))
			return -EPERM;
		task_clear_spec_ssb_disable(task);
		clear_tsk_thread_flag(task, TIF_SSBD);
		ssbd_ssbs_enable(task);
		break;
	case PR_SPEC_DISABLE:
		if (state == ARM64_SSBD_FORCE_DISABLE)
			return -EPERM;
		task_set_spec_ssb_disable(task);
		set_tsk_thread_flag(task, TIF_SSBD);
		ssbd_ssbs_disable(task);
		break;
	case PR_SPEC_FORCE_DISABLE:
		if (state == ARM64_SSBD_FORCE_DISABLE)
			return -EPERM;
		task_set_spec_ssb_disable(task);
		task_set_spec_ssb_force_disable(task);
		set_tsk_thread_flag(task, TIF_SSBD);
		ssbd_ssbs_disable(task);
		break;
	default:
		return -ERANGE;
	}

	return 0;
}

int arch_prctl_spec_ctrl_set(struct task_struct *task, unsigned long which,
			     unsigned long ctrl)
{
	switch (which) {
	case PR_SPEC_STORE_BYPASS:
		return ssbd_prctl_set(task, ctrl);
	default:
		return -ENODEV;
	}
}

static int ssbd_prctl_get(struct task_struct *task)
{
	switch (arm64_get_ssbd_state()) {
	case ARM64_SSBD_UNKNOWN:
		return -EINVAL;
	case ARM64_SSBD_FORCE_ENABLE:
		return PR_SPEC_DISABLE;
	case ARM64_SSBD_KERNEL:
		if (task_spec_ssb_force_disable(task))
			return PR_SPEC_PRCTL | PR_SPEC_FORCE_DISABLE;
		if (task_spec_ssb_disable(task))
			return PR_SPEC_PRCTL | PR_SPEC_DISABLE;
		return PR_SPEC_PRCTL | PR_SPEC_ENABLE;
	case ARM64_SSBD_FORCE_DISABLE:
		return PR_SPEC_ENABLE;
	default:
		return PR_SPEC_NOT_AFFECTED;
	}
}

int arch_prctl_spec_ctrl_get(struct task_struct *task, unsigned long which)
{
	switch (which) {
	case PR_SPEC_STORE_BYPASS:
		return ssbd_prctl_get(task);
	default:
		return -ENODEV;
	}
}
#endif	/* PR_SPEC_STORE_BYPASS */
