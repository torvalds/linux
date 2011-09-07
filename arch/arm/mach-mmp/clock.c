/*
 *  linux/arch/arm/mach-mmp/clock.c
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/clk.h>
#include <linux/io.h>

#include <mach/regs-apbc.h>
#include "clock.h"

static void apbc_clk_enable(struct clk *clk)
{
	uint32_t clk_rst;

	clk_rst = APBC_APBCLK | APBC_FNCLK | APBC_FNCLKSEL(clk->fnclksel);
	__raw_writel(clk_rst, clk->clk_rst);
}

static void apbc_clk_disable(struct clk *clk)
{
	__raw_writel(0, clk->clk_rst);
}

struct clkops apbc_clk_ops = {
	.enable		= apbc_clk_enable,
	.disable	= apbc_clk_disable,
};

static void apmu_clk_enable(struct clk *clk)
{
	__raw_writel(clk->enable_val, clk->clk_rst);
}

static void apmu_clk_disable(struct clk *clk)
{
	__raw_writel(0, clk->clk_rst);
}

struct clkops apmu_clk_ops = {
	.enable		= apmu_clk_enable,
	.disable	= apmu_clk_disable,
};

static DEFINE_SPINLOCK(clocks_lock);

int clk_enable(struct clk *clk)
{
	unsigned long flags;

	spin_lock_irqsave(&clocks_lock, flags);
	if (clk->enabled++ == 0)
		clk->ops->enable(clk);
	spin_unlock_irqrestore(&clocks_lock, flags);
	return 0;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
	unsigned long flags;

	WARN_ON(clk->enabled == 0);

	spin_lock_irqsave(&clocks_lock, flags);
	if (--clk->enabled == 0)
		clk->ops->disable(clk);
	spin_unlock_irqrestore(&clocks_lock, flags);
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	unsigned long rate;

	if (clk->ops->getrate)
		rate = clk->ops->getrate(clk);
	else
		rate = clk->rate;

	return rate;
}
EXPORT_SYMBOL(clk_get_rate);

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned long flags;
	int ret = -EINVAL;

	if (clk->ops->setrate) {
		spin_lock_irqsave(&clocks_lock, flags);
		ret = clk->ops->setrate(clk, rate);
		spin_unlock_irqrestore(&clocks_lock, flags);
	}

	return ret;
}
EXPORT_SYMBOL(clk_set_rate);
