/*
 *  linux/arch/arm/plat-versatile/clock.c
 *
 *  Copyright (C) 2004 ARM Limited.
 *  Written by Deep Blue Solutions Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/mutex.h>

#include <asm/hardware/icst.h>

#include <mach/clkdev.h>

int clk_enable(struct clk *clk)
{
	return 0;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	return clk->rate;
}
EXPORT_SYMBOL(clk_get_rate);

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	long ret = -EIO;
	if (clk->ops && clk->ops->round)
		ret = clk->ops->round(clk, rate);
	return ret;
}
EXPORT_SYMBOL(clk_round_rate);

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	int ret = -EIO;
	if (clk->ops && clk->ops->set)
		ret = clk->ops->set(clk, rate);
	return ret;
}
EXPORT_SYMBOL(clk_set_rate);

long icst_clk_round(struct clk *clk, unsigned long rate)
{
	struct icst_vco vco;
	vco = icst_hz_to_vco(clk->params, rate);
	return icst_hz(clk->params, vco);
}
EXPORT_SYMBOL(icst_clk_round);

int icst_clk_set(struct clk *clk, unsigned long rate)
{
	struct icst_vco vco;

	vco = icst_hz_to_vco(clk->params, rate);
	clk->rate = icst_hz(clk->params, vco);
	clk->ops->setvco(clk, vco);

	return 0;
}
EXPORT_SYMBOL(icst_clk_set);
