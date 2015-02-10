/*
 * Copyright (C) 2014 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/cpuidle.h>
#include <linux/module.h>
#include <asm/cpuidle.h>
#include <asm/proc-fns.h>

#include "common.h"
#include "cpuidle.h"

static int imx6sl_enter_wait(struct cpuidle_device *dev,
			    struct cpuidle_driver *drv, int index)
{
	imx6q_set_lpm(WAIT_UNCLOCKED);
	/*
	 * Software workaround for ERR005311, see function
	 * description for details.
	 */
	imx6sl_set_wait_clk(true);
	cpu_do_idle();
	imx6sl_set_wait_clk(false);
	imx6q_set_lpm(WAIT_CLOCKED);

	return index;
}

static struct cpuidle_driver imx6sl_cpuidle_driver = {
	.name = "imx6sl_cpuidle",
	.owner = THIS_MODULE,
	.states = {
		/* WFI */
		ARM_CPUIDLE_WFI_STATE,
		/* WAIT */
		{
			.exit_latency = 50,
			.target_residency = 75,
			.flags = CPUIDLE_FLAG_TIMER_STOP,
			.enter = imx6sl_enter_wait,
			.name = "WAIT",
			.desc = "Clock off",
		},
	},
	.state_count = 2,
	.safe_state_index = 0,
};

int __init imx6sl_cpuidle_init(void)
{
	return cpuidle_register(&imx6sl_cpuidle_driver, NULL);
}
