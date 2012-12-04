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

#include "cpuidle.h"

static struct cpuidle_driver imx6q_cpuidle_driver = {
	.name = "imx6q_cpuidle",
	.owner = THIS_MODULE,
	.en_core_tk_irqen = 1,
	.states[0] = ARM_CPUIDLE_WFI_STATE,
	.state_count = 1,
};

int __init imx6q_cpuidle_init(void)
{
	return imx_cpuidle_init(&imx6q_cpuidle_driver);
}
