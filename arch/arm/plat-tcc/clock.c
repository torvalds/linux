/*
 * Clock framework for Telechips SoCs
 * Based on arch/arm/plat-mxc/clock.c
 *
 * Copyright (C) 2004 - 2005 Nokia corporation
 * Written by Tuukka Tikkanen <tuukka.tikkanen@elektrobit.com>
 * Modified for omap shared clock framework by Tony Lindgren <tony@atomide.com>
 * Copyright 2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Juergen Beisert, kernel@pengutronix.de
 * Copyright 2010 Hans J. Koch, hjk@linutronix.de
 *
 * Licensed under the terms of the GPL v2.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/string.h>

#include <mach/clock.h>
#include <mach/hardware.h>

static DEFINE_MUTEX(clocks_mutex);

/*-------------------------------------------------------------------------
 * Standard clock functions defined in include/linux/clk.h
 *-------------------------------------------------------------------------*/

static void __clk_disable(struct clk *clk)
{
	BUG_ON(clk->refcount == 0);

	if (!(--clk->refcount) && clk->disable) {
		/* Unconditionally disable the clock in hardware */
		clk->disable(clk);
		/* recursively disable parents */
		if (clk->parent)
			__clk_disable(clk->parent);
	}
}

static int __clk_enable(struct clk *clk)
{
	int ret = 0;

	if (clk->refcount++ == 0 && clk->enable) {
		if (clk->parent)
			ret = __clk_enable(clk->parent);
		if (ret)
			return ret;
		else
			return clk->enable(clk);
	}

	return 0;
}

/* This function increments the reference count on the clock and enables the
 * clock if not already enabled. The parent clock tree is recursively enabled
 */
int clk_enable(struct clk *clk)
{
	int ret = 0;

	if (!clk)
		return -EINVAL;

	mutex_lock(&clocks_mutex);
	ret = __clk_enable(clk);
	mutex_unlock(&clocks_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(clk_enable);

/* This function decrements the reference count on the clock and disables
 * the clock when reference count is 0. The parent clock tree is
 * recursively disabled
 */
void clk_disable(struct clk *clk)
{
	if (!clk)
		return;

	mutex_lock(&clocks_mutex);
	__clk_disable(clk);
	mutex_unlock(&clocks_mutex);
}
EXPORT_SYMBOL_GPL(clk_disable);

/* Retrieve the *current* clock rate. If the clock itself
 * does not provide a special calculation routine, ask
 * its parent and so on, until one is able to return
 * a valid clock rate
 */
unsigned long clk_get_rate(struct clk *clk)
{
	if (!clk)
		return 0UL;

	if (clk->get_rate)
		return clk->get_rate(clk);

	return clk_get_rate(clk->parent);
}
EXPORT_SYMBOL_GPL(clk_get_rate);

/* Round the requested clock rate to the nearest supported
 * rate that is less than or equal to the requested rate.
 * This is dependent on the clock's current parent.
 */
long clk_round_rate(struct clk *clk, unsigned long rate)
{
	if (!clk)
		return 0;
	if (!clk->round_rate)
		return 0;

	return clk->round_rate(clk, rate);
}
EXPORT_SYMBOL_GPL(clk_round_rate);

/* Set the clock to the requested clock rate. The rate must
 * match a supported rate exactly based on what clk_round_rate returns
 */
int clk_set_rate(struct clk *clk, unsigned long rate)
{
	int ret = -EINVAL;

	if (!clk)
		return ret;
	if (!clk->set_rate || !rate)
		return ret;

	mutex_lock(&clocks_mutex);
	ret = clk->set_rate(clk, rate);
	mutex_unlock(&clocks_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(clk_set_rate);

/* Set the clock's parent to another clock source */
int clk_set_parent(struct clk *clk, struct clk *parent)
{
	struct clk *old;
	int ret = -EINVAL;

	if (!clk)
		return ret;
	if (!clk->set_parent || !parent)
		return ret;

	mutex_lock(&clocks_mutex);
	old = clk->parent;
	if (clk->refcount)
		__clk_enable(parent);
	ret = clk->set_parent(clk, parent);
	if (ret)
		old = parent;
	if (clk->refcount)
		__clk_disable(old);
	mutex_unlock(&clocks_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(clk_set_parent);

/* Retrieve the clock's parent clock source */
struct clk *clk_get_parent(struct clk *clk)
{
	if (!clk)
		return NULL;

	return clk->parent;
}
EXPORT_SYMBOL_GPL(clk_get_parent);
