/*
 * Copyright (C) 2012 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/cpuidle.h>
#include <linux/module.h>
#include <asm/system_misc.h>
#include "cpuidle.h"

static int imx5_cpuidle_enter(struct cpuidle_device *dev,
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
