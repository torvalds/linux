/*
 * DaVinci Power Management Routines
 *
 * Copyright (C) 2009 Texas Instruments, Inc. http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/pm.h>
#include <linux/suspend.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/spinlock.h>

#include <asm/cacheflush.h>
#include <asm/delay.h>
#include <asm/io.h>

#include <mach/da8xx.h>
#include <mach/sram.h>
#include <mach/pm.h>

#include "clock.h"

#define DEEPSLEEP_SLEEPCOUNT_MASK	0xFFFF

static void (*davinci_sram_suspend) (struct davinci_pm_config *);
static struct davinci_pm_config *pdata;

static void davinci_sram_push(void *dest, void *src, unsigned int size)
{
	memcpy(dest, src, size);
	flush_icache_range((unsigned long)dest, (unsigned long)(dest + size));
}

static void davinci_pm_suspend(void)
{
	unsigned val;

	if (pdata->cpupll_reg_base != pdata->ddrpll_reg_base) {

		/* Switch CPU PLL to bypass mode */
		val = __raw_readl(pdata->cpupll_reg_base + PLLCTL);
		val &= ~(PLLCTL_PLLENSRC | PLLCTL_PLLEN);
		__raw_writel(val, pdata->cpupll_reg_base + PLLCTL);

		udelay(PLL_BYPASS_TIME);

		/* Powerdown CPU PLL */
		val = __raw_readl(pdata->cpupll_reg_base + PLLCTL);
		val |= PLLCTL_PLLPWRDN;
		__raw_writel(val, pdata->cpupll_reg_base + PLLCTL);
	}

	/* Configure sleep count in deep sleep register */
	val = __raw_readl(pdata->deepsleep_reg);
	val &= ~DEEPSLEEP_SLEEPCOUNT_MASK,
	val |= pdata->sleepcount;
	__raw_writel(val, pdata->deepsleep_reg);

	/* System goes to sleep in this call */
	davinci_sram_suspend(pdata);

	if (pdata->cpupll_reg_base != pdata->ddrpll_reg_base) {

		/* put CPU PLL in reset */
		val = __raw_readl(pdata->cpupll_reg_base + PLLCTL);
		val &= ~PLLCTL_PLLRST;
		__raw_writel(val, pdata->cpupll_reg_base + PLLCTL);

		/* put CPU PLL in power down */
		val = __raw_readl(pdata->cpupll_reg_base + PLLCTL);
		val &= ~PLLCTL_PLLPWRDN;
		__raw_writel(val, pdata->cpupll_reg_base + PLLCTL);

		/* wait for CPU PLL reset */
		udelay(PLL_RESET_TIME);

		/* bring CPU PLL out of reset */
		val = __raw_readl(pdata->cpupll_reg_base + PLLCTL);
		val |= PLLCTL_PLLRST;
		__raw_writel(val, pdata->cpupll_reg_base + PLLCTL);

		/* Wait for CPU PLL to lock */
		udelay(PLL_LOCK_TIME);

		/* Remove CPU PLL from bypass mode */
		val = __raw_readl(pdata->cpupll_reg_base + PLLCTL);
		val &= ~PLLCTL_PLLENSRC;
		val |= PLLCTL_PLLEN;
		__raw_writel(val, pdata->cpupll_reg_base + PLLCTL);
	}
}

static int davinci_pm_enter(suspend_state_t state)
{
	int ret = 0;

	switch (state) {
	case PM_SUSPEND_STANDBY:
	case PM_SUSPEND_MEM:
		davinci_pm_suspend();
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct platform_suspend_ops davinci_pm_ops = {
	.enter		= davinci_pm_enter,
	.valid		= suspend_valid_only_mem,
};

static int __init davinci_pm_probe(struct platform_device *pdev)
{
	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "cannot get platform data\n");
		return -ENOENT;
	}

	davinci_sram_suspend = sram_alloc(davinci_cpu_suspend_sz, NULL);
	if (!davinci_sram_suspend) {
		dev_err(&pdev->dev, "cannot allocate SRAM memory\n");
		return -ENOMEM;
	}

	davinci_sram_push(davinci_sram_suspend, davinci_cpu_suspend,
						davinci_cpu_suspend_sz);

	suspend_set_ops(&davinci_pm_ops);

	return 0;
}

static int __exit davinci_pm_remove(struct platform_device *pdev)
{
	sram_free(davinci_sram_suspend, davinci_cpu_suspend_sz);
	return 0;
}

static struct platform_driver davinci_pm_driver = {
	.driver = {
		.name	 = "pm-davinci",
		.owner	 = THIS_MODULE,
	},
	.remove = __exit_p(davinci_pm_remove),
};

int __init davinci_pm_init(void)
{
	return platform_driver_probe(&davinci_pm_driver, davinci_pm_probe);
}
