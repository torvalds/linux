// SPDX-License-Identifier: GPL-2.0
/*
 * shstk.c - Intel shadow stack support
 *
 * Copyright (c) 2021, Intel Corporation.
 * Yu-cheng Yu <yu-cheng.yu@intel.com>
 */

#include <linux/sched.h>
#include <linux/bitops.h>
#include <asm/prctl.h>

void reset_thread_features(void)
{
	current->thread.features = 0;
	current->thread.features_locked = 0;
}

long shstk_prctl(struct task_struct *task, int option, unsigned long features)
{
	if (option == ARCH_SHSTK_LOCK) {
		task->thread.features_locked |= features;
		return 0;
	}

	/* Don't allow via ptrace */
	if (task != current)
		return -EINVAL;

	/* Do not allow to change locked features */
	if (features & task->thread.features_locked)
		return -EPERM;

	/* Only support enabling/disabling one feature at a time. */
	if (hweight_long(features) > 1)
		return -EINVAL;

	if (option == ARCH_SHSTK_DISABLE) {
		return -EINVAL;
	}

	/* Handle ARCH_SHSTK_ENABLE */
	return -EINVAL;
}
