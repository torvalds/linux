// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 * Copyright 2017-2018 NXP
 *   Anson Huang <Anson.Huang@nxp.com>
 */

#include <linux/cpuidle.h>
#include <linux/module.h>
#include <asm/cpuidle.h>

#include "common.h"
#include "cpuidle.h"

static int imx7ulp_enter_wait(struct cpuidle_device *dev,
			    struct cpuidle_driver *drv, int index)
{
	if (index == 1)
		imx7ulp_set_lpm(ULP_PM_WAIT);
	else
		imx7ulp_set_lpm(ULP_PM_STOP);

	cpu_do_idle();

	imx7ulp_set_lpm(ULP_PM_RUN);

	return index;
}

static struct cpuidle_driver imx7ulp_cpuidle_driver = {
	.name = "imx7ulp_cpuidle",
	.owner = THIS_MODULE,
	.states = {
		/* WFI */
		ARM_CPUIDLE_WFI_STATE,
		/* WAIT */
		{
			.exit_latency = 50,
			.target_residency = 75,
			.enter = imx7ulp_enter_wait,
			.name = "WAIT",
			.desc = "PSTOP2",
		},
		/* STOP */
		{
			.exit_latency = 100,
			.target_residency = 150,
			.enter = imx7ulp_enter_wait,
			.name = "STOP",
			.desc = "PSTOP1",
		},
	},
	.state_count = 3,
	.safe_state_index = 0,
};

int __init imx7ulp_cpuidle_init(void)
{
	return cpuidle_register(&imx7ulp_cpuidle_driver, NULL);
}
