/*
 * Copyright (C) 2014 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/cpuidle.h>
#include <linux/cpu_pm.h>
#include <linux/module.h>
#include <asm/cacheflush.h>
#include <asm/cpuidle.h>
#include <asm/suspend.h>

#include "common.h"
#include "cpuidle.h"

static int imx6sx_idle_finish(unsigned long val)
{
	/*
	 * for Cortex-A7 which has an internal L2
	 * cache, need to flush it before powering
	 * down ARM platform, since flushing L1 cache
	 * here again has very small overhead, compared
	 * to adding conditional code for L2 cache type,
	 * just call flush_cache_all() is fine.
	 */
	flush_cache_all();
	cpu_do_idle();

	return 0;
}

static int imx6sx_enter_wait(struct cpuidle_device *dev,
			    struct cpuidle_driver *drv, int index)
{
	imx6_set_lpm(WAIT_UNCLOCKED);

	switch (index) {
	case 1:
		cpu_do_idle();
		break;
	case 2:
		imx6_enable_rbc(true);
		imx_gpc_set_arm_power_in_lpm(true);
		imx_set_cpu_jump(0, v7_cpu_resume);
		/* Need to notify there is a cpu pm operation. */
		cpu_pm_enter();
		cpu_cluster_pm_enter();

		cpu_suspend(0, imx6sx_idle_finish);

		cpu_cluster_pm_exit();
		cpu_pm_exit();
		imx_gpc_set_arm_power_in_lpm(false);
		imx6_enable_rbc(false);
		break;
	default:
		break;
	}

	imx6_set_lpm(WAIT_CLOCKED);

	return index;
}

static struct cpuidle_driver imx6sx_cpuidle_driver = {
	.name = "imx6sx_cpuidle",
	.owner = THIS_MODULE,
	.states = {
		/* WFI */
		ARM_CPUIDLE_WFI_STATE,
		/* WAIT */
		{
			.exit_latency = 50,
			.target_residency = 75,
			.flags = CPUIDLE_FLAG_TIMER_STOP,
			.enter = imx6sx_enter_wait,
			.name = "WAIT",
			.desc = "Clock off",
		},
		/* WAIT + ARM power off  */
		{
			/*
			 * ARM gating 31us * 5 + RBC clear 65us
			 * and some margin for SW execution, here set it
			 * to 300us.
			 */
			.exit_latency = 300,
			.target_residency = 500,
			.flags = CPUIDLE_FLAG_TIMER_STOP,
			.enter = imx6sx_enter_wait,
			.name = "LOW-POWER-IDLE",
			.desc = "ARM power off",
		},
	},
	.state_count = 3,
	.safe_state_index = 0,
};

int __init imx6sx_cpuidle_init(void)
{
	imx6_set_int_mem_clk_lpm(true);
	imx6_enable_rbc(false);
	/*
	 * set ARM power up/down timing to the fastest,
	 * sw2iso and sw can be set to one 32K cycle = 31us
	 * except for power up sw2iso which need to be
	 * larger than LDO ramp up time.
	 */
	imx_gpc_set_arm_power_up_timing(2, 1);
	imx_gpc_set_arm_power_down_timing(1, 1);

	return cpuidle_register(&imx6sx_cpuidle_driver, NULL);
}
