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
