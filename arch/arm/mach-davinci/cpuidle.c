/*
 * CPU idle for DaVinci SoCs
 *
 * Copyright (C) 2009 Texas Instruments Incorporated. http://www.ti.com/
 *
 * Derived from Marvell Kirkwood CPU idle code
 * (arch/arm/mach-kirkwood/cpuidle.c)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/cpuidle.h>
#include <linux/io.h>
#include <linux/export.h>
#include <asm/proc-fns.h>
#include <asm/cpuidle.h>

#include <mach/cpuidle.h>
#include <mach/ddr2.h>

#define DAVINCI_CPUIDLE_MAX_STATES	2

static void __iomem *ddr2_reg_base;
static bool ddr2_pdown;

static void davinci_save_ddr_power(int enter, bool pdown)
{
	u32 val;

	val = __raw_readl(ddr2_reg_base + DDR2_SDRCR_OFFSET);

	if (enter) {
		if (pdown)
			val |= DDR2_SRPD_BIT;
		else
			val &= ~DDR2_SRPD_BIT;
		val |= DDR2_LPMODEN_BIT;
	} else {
		val &= ~(DDR2_SRPD_BIT | DDR2_LPMODEN_BIT);
	}

	__raw_writel(val, ddr2_reg_base + DDR2_SDRCR_OFFSET);
}

/* Actual code that puts the SoC in different idle states */
static int davinci_enter_idle(struct cpuidle_device *dev,
			      struct cpuidle_driver *drv, int index)
{
	davinci_save_ddr_power(1, ddr2_pdown);
	cpu_do_idle();
	davinci_save_ddr_power(0, ddr2_pdown);

	return index;
}

static struct cpuidle_driver davinci_idle_driver = {
	.name			= "cpuidle-davinci",
	.owner			= THIS_MODULE,
	.states[0]		= ARM_CPUIDLE_WFI_STATE,
	.states[1]		= {
		.enter			= davinci_enter_idle,
		.exit_latency		= 10,
		.target_residency	= 10000,
		.name			= "DDR SR",
		.desc			= "WFI and DDR Self Refresh",
	},
	.state_count = DAVINCI_CPUIDLE_MAX_STATES,
};

static int __init davinci_cpuidle_probe(struct platform_device *pdev)
{
	struct davinci_cpuidle_config *pdata = pdev->dev.platform_data;

	if (!pdata) {
		dev_err(&pdev->dev, "cannot get platform data\n");
		return -ENOENT;
	}

	ddr2_reg_base = pdata->ddr2_ctlr_base;

	ddr2_pdown = pdata->ddr2_pdown;

	return cpuidle_register(&davinci_idle_driver, NULL);
}

static struct platform_driver davinci_cpuidle_driver = {
	.driver = {
		.name	= "cpuidle-davinci",
	},
};

static int __init davinci_cpuidle_init(void)
{
	return platform_driver_probe(&davinci_cpuidle_driver,
						davinci_cpuidle_probe);
}
device_initcall(davinci_cpuidle_init);

