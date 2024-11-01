// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2010 Freescale Semiconductor, Inc.
 */

#include <linux/kernel.h>
#include <linux/suspend.h>
#include <linux/io.h>
#include "pm.h"

static int mxs_suspend_enter(suspend_state_t state)
{
	switch (state) {
	case PM_SUSPEND_MEM:
		cpu_do_idle();
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

static const struct platform_suspend_ops mxs_suspend_ops = {
	.enter = mxs_suspend_enter,
	.valid = suspend_valid_only_mem,
};

void __init mxs_pm_init(void)
{
	suspend_set_ops(&mxs_suspend_ops);
}
