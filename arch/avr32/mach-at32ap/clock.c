/*
 * Clock management for AT32AP CPUs
 *
 * Copyright (C) 2006 Atmel Corporation
 *
 * Based on arch/arm/mach-at91/clock.c
 *   Copyright (C) 2005 David Brownell
 *   Copyright (C) 2005 Ivan Kokshaysky
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/string.h>

#include "clock.h"

static DEFINE_SPINLOCK(clk_lock);

struct clk *clk_get(struct device *dev, const char *id)
{
	int i;

	for (i = 0; i < at32_nr_clocks; i++) {
		struct clk *clk = at32_clock_list[i];

		if (clk->dev == dev && strcmp(id, clk->name) == 0)
			return clk;
	}

	return ERR_PTR(-ENOENT);
}
EXPORT_SYMBOL(clk_get);

void clk_put(struct clk *clk)
{
	/* clocks are static for now, we can't free them */
}
EXPORT_SYMBOL(clk_put);

static void __clk_enable(struct clk *clk)
{
	if (clk->parent)
		__clk_enable(clk->parent);
	if (clk->users++ == 0 && clk->mode)
		clk->mode(clk, 1);
}

int clk_enable(struct clk *clk)
{
	unsigned long flags;

	spin_lock_irqsave(&clk_lock, flags);
	__clk_enable(clk);
	spin_unlock_irqrestore(&clk_lock, flags);

	return 0;
}
EXPORT_SYMBOL(clk_enable);

static void __clk_disable(struct clk *clk)
{
	if (clk->users == 0) {
		printk(KERN_ERR "%s: mismatched disable\n", clk->name);
		WARN_ON(1);
		return;
	}

	if (--clk->users == 0 && clk->mode)
		clk->mode(clk, 0);
	if (clk->parent)
		__clk_disable(clk->parent);
}

void clk_disable(struct clk *clk)
{
	unsigned long flags;

	spin_lock_irqsave(&clk_lock, flags);
	__clk_disable(clk);
	spin_unlock_irqrestore(&clk_lock, flags);
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	unsigned long flags;
	unsigned long rate;

	spin_lock_irqsave(&clk_lock, flags);
	rate = clk->get_rate(clk);
	spin_unlock_irqrestore(&clk_lock, flags);

	return rate;
}
EXPORT_SYMBOL(clk_get_rate);

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	unsigned long flags, actual_rate;

	if (!clk->set_rate)
		return -ENOSYS;

	spin_lock_irqsave(&clk_lock, flags);
	actual_rate = clk->set_rate(clk, rate, 0);
	spin_unlock_irqrestore(&clk_lock, flags);

	return actual_rate;
}
EXPORT_SYMBOL(clk_round_rate);

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned long flags;
	long ret;

	if (!clk->set_rate)
		return -ENOSYS;

	spin_lock_irqsave(&clk_lock, flags);
	ret = clk->set_rate(clk, rate, 1);
	spin_unlock_irqrestore(&clk_lock, flags);

	return (ret < 0) ? ret : 0;
}
EXPORT_SYMBOL(clk_set_rate);

int clk_set_parent(struct clk *clk, struct clk *parent)
{
	unsigned long flags;
	int ret;

	if (!clk->set_parent)
		return -ENOSYS;

	spin_lock_irqsave(&clk_lock, flags);
	ret = clk->set_parent(clk, parent);
	spin_unlock_irqrestore(&clk_lock, flags);

	return ret;
}
EXPORT_SYMBOL(clk_set_parent);

struct clk *clk_get_parent(struct clk *clk)
{
	return clk->parent;
}
EXPORT_SYMBOL(clk_get_parent);
