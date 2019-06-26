/*
 * Copyright (C) 2012 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/cpuidle.h>
#include <linux/module.h>
#include <asm/cpuidle.h>

#include <soc/imx/cpuidle.h>

#include "common.h"
#include "cpuidle.h"
#include "hardware.h"

static int num_idle_cpus = 0;
static DEFINE_SPINLOCK(cpuidle_lock);

static int imx6q_enter_wait(struct cpuidle_device *dev,
			    struct cpuidle_driver *drv, int index)
{
	spin_lock(&cpuidle_lock);
	if (++num_idle_cpus == num_online_cpus())
		imx6_set_lpm(WAIT_UNCLOCKED);
	spin_unlock(&cpuidle_lock);

	cpu_do_idle();

	spin_lock(&cpuidle_lock);
	if (num_idle_cpus-- == num_online_cpus())
		imx6_set_lpm(WAIT_CLOCKED);
	spin_unlock(&cpuidle_lock);

	return index;
}

static struct cpuidle_driver imx6q_cpuidle_driver = {
	.name = "imx6q_cpuidle",
	.owner = THIS_MODULE,
	.states = {
		/* WFI */
		ARM_CPUIDLE_WFI_STATE,
		/* WAIT */
		{
			.exit_latency = 50,
			.target_residency = 75,
			.flags = CPUIDLE_FLAG_TIMER_STOP,
			.enter = imx6q_enter_wait,
			.name = "WAIT",
			.desc = "Clock off",
		},
	},
	.state_count = 2,
	.safe_state_index = 0,
};

/*
 * i.MX6 Q/DL has an erratum (ERR006687) that prevents the FEC from waking the
 * CPUs when they are in wait(unclocked) state. As the hardware workaround isn't
 * applicable to all boards, disable the deeper idle state when the workaround
 * isn't present and the FEC is in use.
 */
void imx6q_cpuidle_fec_irqs_used(void)
{
	imx6q_cpuidle_driver.states[1].disabled = true;
}
EXPORT_SYMBOL_GPL(imx6q_cpuidle_fec_irqs_used);

void imx6q_cpuidle_fec_irqs_unused(void)
{
	imx6q_cpuidle_driver.states[1].disabled = false;
}
EXPORT_SYMBOL_GPL(imx6q_cpuidle_fec_irqs_unused);

int __init imx6q_cpuidle_init(void)
{
	/* Set INT_MEM_CLK_LPM bit to get a reliable WAIT mode support */
	imx6_set_int_mem_clk_lpm(true);

	return cpuidle_register(&imx6q_cpuidle_driver, NULL);
}
