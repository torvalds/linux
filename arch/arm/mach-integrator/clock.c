/*
 *  linux/arch/arm/mach-integrator/clock.c
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
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/clk.h>
#include <linux/mutex.h>

#include <asm/hardware/icst525.h>

#include "clock.h"

static LIST_HEAD(clocks);
static DEFINE_MUTEX(clocks_mutex);

struct clk *clk_get(struct device *dev, const char *id)
{
	struct clk *p, *clk = ERR_PTR(-ENOENT);

	mutex_lock(&clocks_mutex);
	list_for_each_entry(p, &clocks, node) {
		if (strcmp(id, p->name) == 0 && try_module_get(p->owner)) {
			clk = p;
			break;
		}
	}
	mutex_unlock(&clocks_mutex);

	return clk;
}
EXPORT_SYMBOL(clk_get);

void clk_put(struct clk *clk)
{
	module_put(clk->owner);
}
EXPORT_SYMBOL(clk_put);

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
	struct icst525_vco vco;

	vco = icst525_khz_to_vco(clk->params, rate / 1000);
	return icst525_khz(clk->params, vco) * 1000;
}
EXPORT_SYMBOL(clk_round_rate);

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	int ret = -EIO;
	if (clk->setvco) {
		struct icst525_vco vco;

		vco = icst525_khz_to_vco(clk->params, rate / 1000);
		clk->rate = icst525_khz(clk->params, vco) * 1000;

		printk("Clock %s: setting VCO reg params: S=%d R=%d V=%d\n",
			clk->name, vco.s, vco.r, vco.v);

		clk->setvco(clk, vco);
		ret = 0;
	}
	return 0;
}
EXPORT_SYMBOL(clk_set_rate);

/*
 * These are fixed clocks.
 */
static struct clk kmi_clk = {
	.name	= "KMIREFCLK",
	.rate	= 24000000,
};

static struct clk uart_clk = {
	.name	= "UARTCLK",
	.rate	= 14745600,
};

int clk_register(struct clk *clk)
{
	mutex_lock(&clocks_mutex);
	list_add(&clk->node, &clocks);
	mutex_unlock(&clocks_mutex);
	return 0;
}
EXPORT_SYMBOL(clk_register);

void clk_unregister(struct clk *clk)
{
	mutex_lock(&clocks_mutex);
	list_del(&clk->node);
	mutex_unlock(&clocks_mutex);
}
EXPORT_SYMBOL(clk_unregister);

static int __init clk_init(void)
{
	clk_register(&kmi_clk);
	clk_register(&uart_clk);
	return 0;
}
arch_initcall(clk_init);
