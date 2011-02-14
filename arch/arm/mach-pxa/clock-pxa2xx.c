/*
 * linux/arch/arm/mach-pxa/clock-pxa2xx.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sysdev.h>

#include <mach/pxa2xx-regs.h>

#include "clock.h"

void clk_pxa2xx_cken_enable(struct clk *clk)
{
	CKEN |= 1 << clk->cken;
}

void clk_pxa2xx_cken_disable(struct clk *clk)
{
	CKEN &= ~(1 << clk->cken);
}

const struct clkops clk_pxa2xx_cken_ops = {
	.enable		= clk_pxa2xx_cken_enable,
	.disable	= clk_pxa2xx_cken_disable,
};

#ifdef CONFIG_PM
static uint32_t saved_cken;

static int pxa2xx_clock_suspend(struct sys_device *d, pm_message_t state)
{
	saved_cken = CKEN;
	return 0;
}

static int pxa2xx_clock_resume(struct sys_device *d)
{
	CKEN = saved_cken;
	return 0;
}
#else
#define pxa2xx_clock_suspend	NULL
#define pxa2xx_clock_resume	NULL
#endif

struct sysdev_class pxa2xx_clock_sysclass = {
	.name		= "pxa2xx-clock",
	.suspend	= pxa2xx_clock_suspend,
	.resume		= pxa2xx_clock_resume,
};

static int __init pxa2xx_clock_init(void)
{
	if (cpu_is_pxa2xx())
		return sysdev_class_register(&pxa2xx_clock_sysclass);
	return 0;
}
postcore_initcall(pxa2xx_clock_init);
