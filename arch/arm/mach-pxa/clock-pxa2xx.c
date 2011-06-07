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
#include <linux/syscore_ops.h>

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

static int pxa2xx_clock_suspend(void)
{
	saved_cken = CKEN;
	return 0;
}

static void pxa2xx_clock_resume(void)
{
	CKEN = saved_cken;
}
#else
#define pxa2xx_clock_suspend	NULL
#define pxa2xx_clock_resume	NULL
#endif

struct syscore_ops pxa2xx_clock_syscore_ops = {
	.suspend	= pxa2xx_clock_suspend,
	.resume		= pxa2xx_clock_resume,
};
