/*
 * linux/arch/arm/mach-w90x900/clock.c
 *
 * Copyright (c) 2008 Nuvoton technology corporation
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/clk.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include <mach/hardware.h>

#include "clock.h"

#define SUBCLK 0x24

static DEFINE_SPINLOCK(clocks_lock);

int clk_enable(struct clk *clk)
{
	unsigned long flags;

	spin_lock_irqsave(&clocks_lock, flags);
	if (clk->enabled++ == 0)
		(clk->enable)(clk, 1);
	spin_unlock_irqrestore(&clocks_lock, flags);

	return 0;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
	unsigned long flags;

	if (!clk)
		return;

	WARN_ON(clk->enabled == 0);

	spin_lock_irqsave(&clocks_lock, flags);
	if (--clk->enabled == 0)
		(clk->enable)(clk, 0);
	spin_unlock_irqrestore(&clocks_lock, flags);
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	return 15000000;
}
EXPORT_SYMBOL(clk_get_rate);

void nuc900_clk_enable(struct clk *clk, int enable)
{
	unsigned int clocks = clk->cken;
	unsigned long clken;

	clken = __raw_readl(W90X900_VA_CLKPWR);

	if (enable)
		clken |= clocks;
	else
		clken &= ~clocks;

	__raw_writel(clken, W90X900_VA_CLKPWR);
}

void nuc900_subclk_enable(struct clk *clk, int enable)
{
	unsigned int clocks = clk->cken;
	unsigned long clken;

	clken = __raw_readl(W90X900_VA_CLKPWR + SUBCLK);

	if (enable)
		clken |= clocks;
	else
		clken &= ~clocks;

	__raw_writel(clken, W90X900_VA_CLKPWR + SUBCLK);
}
