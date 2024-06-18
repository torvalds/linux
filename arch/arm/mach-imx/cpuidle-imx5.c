// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Freescale Semiconductor, Inc.
 */

#include <linux/cpuidle.h>
#include <linux/module.h>
#include <asm/system_misc.h>
#include "cpuidle.h"

static __cpuidle int imx5_cpuidle_enter(struct cpuidle_device *dev,
					struct cpuidle_driver *drv, int index)
{
	arm_pm_idle();
	return index;
}

static struct cpuidle_driver imx5_cpuidle_driver = {
	.name             = "imx5_cpuidle",
	.owner            = THIS_MODULE,
	.states[0] = {
		.enter            = imx5_cpuidle_enter,
		.exit_latency     = 2,
		.target_residency = 1,
		.name             = "IMX5 SRPG",
		.desc             = "CPU state retained,powered off",
	},
	.state_count = 1,
};

int __init imx5_cpuidle_init(void)
{
	return cpuidle_register(&imx5_cpuidle_driver, NULL);
}
