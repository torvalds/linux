/*
 * Based on arch/arm/plat-omap/clock.c
 *
 * Copyright (C) 2004 - 2005 Nokia corporation
 * Written by Tuukka Tikkanen <tuukka.tikkanen@elektrobit.com>
 * Modified for omap shared clock framework by Tony Lindgren <tony@atomide.com>
 * Copyright 2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Juergen Beisert, kernel@pengutronix.de
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
 */

/* #define DEBUG */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/semaphore.h>
#include <linux/string.h>

#include <mach/clock.h>

static LIST_HEAD(clocks);
static DEFINE_MUTEX(clocks_mutex);

/*-------------------------------------------------------------------------
 * Standard clock functions defined in include/linux/clk.h
 *-------------------------------------------------------------------------*/

/*
 * All the code inside #ifndef CONFIG_COMMON_CLKDEV can be removed once all
 * MXC architectures have switched to using clkdev.
 */
#ifndef CONFIG_COMMON_CLKDEV
/*
 * Retrieve a clock by name.
 *
 * Note that we first try to use device id on the bus
 * and clock name. If this fails, we try to use "<name>.<id>". If this fails,
 * we try to use clock name only.
 * The reference count to the clock's module owner ref count is incremented.
 */
struct clk *clk_get(struct device *dev, const char *id)
{
	struct clk *p, *clk = ERR_PTR(-ENOENT);
	int idno;
	const char *str;

	if (id == NULL)
		return clk;

	if (dev == NULL || dev->bus != &platform_bus_type)
		idno = -1;
	else
		idno = to_platform_device(dev)->id;

	mutex_lock(&clocks_mutex);

	list_for_each_entry(p, &clocks, node) {
		if (p->id == idno &&
		    strcmp(id, p->name) == 0 && try_module_get(p->owner)) {
			clk = p;
			goto found;
		}
	}

	str = strrchr(id, '.');
	if (str) {
		int cnt = str - id;
		str++;
		idno = simple_strtol(str, NULL, 10);
		list_for_each_entry(p, &clocks, node) {
			if (p->id == idno &&
			    strlen(p->name) == cnt &&
			    strncmp(id, p->name, cnt) == 0 &&
			    try_module_get(p->owner)) {
				clk = p;
				goto found;
			}
		}
	}

	list_for_each_entry(p, &clocks, node) {
		if (strcmp(id, p->name) == 0 && try_module_get(p->owner)) {
			clk = p;
			goto found;
		}
	}

	printk(KERN_WARNING "clk: Unable to get requested clock: %s\n", id);

found:
	mutex_unlock(&clocks_mutex);

	return clk;
}
EXPORT_SYMBOL(clk_get);
#endif

static void __clk_disable(struct clk *clk)
{
	if (clk == NULL || IS_ERR(clk))
		return;

	__clk_disable(clk->parent);
	__clk_disable(clk->secondary);

	if (!(--clk->usecount) && clk->disable)
		clk->disable(clk);
}

static int __clk_enable(struct clk *clk)
{
	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	__clk_enable(clk->parent);
	__clk_enable(clk->secondary);

	if (clk->usecount++ == 0 && clk->enable)
		clk->enable(clk);

	return 0;
}

/* This function increments the reference count on the clock and enables the
 * clock if not already enabled. The parent clock tree is recursively enabled
 */
int clk_enable(struct clk *clk)
{
	int ret = 0;

	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	mutex_lock(&clocks_mutex);
	ret = __clk_enable(clk);
	mutex_unlock(&clocks_mutex);

	return ret;
}
EXPORT_SYMBOL(clk_enable);

/* This function decrements the reference count on the clock and disables
 * the clock when reference count is 0. The parent clock tree is
 * recursively disabled
 */
void clk_disable(struct clk *clk)
{
	if (clk == NULL || IS_ERR(clk))
		return;

	mutex_lock(&clocks_mutex);
	__clk_disable(clk);
	mutex_unlock(&clocks_mutex);
}
EXPORT_SYMBOL(clk_disable);

/* Retrieve the *current* clock rate. If the clock itself
 * does not provide a special calculation routine, ask
 * its parent and so on, until one is able to return
 * a valid clock rate
 */
unsigned long clk_get_rate(struct clk *clk)
{
	if (clk == NULL || IS_ERR(clk))
		return 0UL;

	if (clk->get_rate)
		return clk->get_rate(clk);

	return clk_get_rate(clk->parent);
}
EXPORT_SYMBOL(clk_get_rate);

#ifndef CONFIG_COMMON_CLKDEV
/* Decrement the clock's module reference count */
void clk_put(struct clk *clk)
{
	if (clk && !IS_ERR(clk))
		module_put(clk->owner);
}
EXPORT_SYMBOL(clk_put);
#endif

/* Round the requested clock rate to the nearest supported
 * rate that is less than or equal to the requested rate.
 * This is dependent on the clock's current parent.
 */
long clk_round_rate(struct clk *clk, unsigned long rate)
{
	if (clk == NULL || IS_ERR(clk) || !clk->round_rate)
		return 0;

	return clk->round_rate(clk, rate);
}
EXPORT_SYMBOL(clk_round_rate);

/* Set the clock to the requested clock rate. The rate must
 * match a supported rate exactly based on what clk_round_rate returns
 */
int clk_set_rate(struct clk *clk, unsigned long rate)
{
	int ret = -EINVAL;

	if (clk == NULL || IS_ERR(clk) || clk->set_rate == NULL || rate == 0)
		return ret;

	mutex_lock(&clocks_mutex);
	ret = clk->set_rate(clk, rate);
	mutex_unlock(&clocks_mutex);

	return ret;
}
EXPORT_SYMBOL(clk_set_rate);

/* Set the clock's parent to another clock source */
int clk_set_parent(struct clk *clk, struct clk *parent)
{
	int ret = -EINVAL;

	if (clk == NULL || IS_ERR(clk) || parent == NULL ||
	    IS_ERR(parent) || clk->set_parent == NULL)
		return ret;

	mutex_lock(&clocks_mutex);
	ret = clk->set_parent(clk, parent);
	if (ret == 0)
		clk->parent = parent;
	mutex_unlock(&clocks_mutex);

	return ret;
}
EXPORT_SYMBOL(clk_set_parent);

/* Retrieve the clock's parent clock source */
struct clk *clk_get_parent(struct clk *clk)
{
	struct clk *ret = NULL;

	if (clk == NULL || IS_ERR(clk))
		return ret;

	return clk->parent;
}
EXPORT_SYMBOL(clk_get_parent);

#ifndef CONFIG_COMMON_CLKDEV
/*
 * Add a new clock to the clock tree.
 */
int clk_register(struct clk *clk)
{
	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	mutex_lock(&clocks_mutex);
	list_add(&clk->node, &clocks);
	mutex_unlock(&clocks_mutex);

	return 0;
}
EXPORT_SYMBOL(clk_register);

/* Remove a clock from the clock tree */
void clk_unregister(struct clk *clk)
{
	if (clk == NULL || IS_ERR(clk))
		return;

	mutex_lock(&clocks_mutex);
	list_del(&clk->node);
	mutex_unlock(&clocks_mutex);
}
EXPORT_SYMBOL(clk_unregister);

#ifdef CONFIG_PROC_FS
static int mxc_clock_read_proc(char *page, char **start, off_t off,
				int count, int *eof, void *data)
{
	struct clk *clkp;
	char *p = page;
	int len;

	list_for_each_entry(clkp, &clocks, node) {
		p += sprintf(p, "%s-%d:\t\t%lu, %d", clkp->name, clkp->id,
				clk_get_rate(clkp), clkp->usecount);
		if (clkp->parent)
			p += sprintf(p, ", %s-%d\n", clkp->parent->name,
				     clkp->parent->id);
		else
			p += sprintf(p, "\n");
	}

	len = (p - page) - off;
	if (len < 0)
		len = 0;

	*eof = (len <= count) ? 1 : 0;
	*start = page + off;

	return len;
}

static int __init mxc_setup_proc_entry(void)
{
	struct proc_dir_entry *res;

	res = create_proc_read_entry("cpu/clocks", 0, NULL,
				     mxc_clock_read_proc, NULL);
	if (!res) {
		printk(KERN_ERR "Failed to create proc/cpu/clocks\n");
		return -ENOMEM;
	}
	return 0;
}

late_initcall(mxc_setup_proc_entry);
#endif /* CONFIG_PROC_FS */
#endif

/*
 * Get the resulting clock rate from a PLL register value and the input
 * frequency. PLLs with this register layout can at least be found on
 * MX1, MX21, MX27 and MX31
 *
 *                  mfi + mfn / (mfd + 1)
 *  f = 2 * f_ref * --------------------
 *                        pd + 1
 */
unsigned long mxc_decode_pll(unsigned int reg_val, u32 freq)
{
	long long ll;
	int mfn_abs;
	unsigned int mfi, mfn, mfd, pd;

	mfi = (reg_val >> 10) & 0xf;
	mfn = reg_val & 0x3ff;
	mfd = (reg_val >> 16) & 0x3ff;
	pd =  (reg_val >> 26) & 0xf;

	mfi = mfi <= 5 ? 5 : mfi;

	mfn_abs = mfn;

#if !defined CONFIG_ARCH_MX1 && !defined CONFIG_ARCH_MX21
	if (mfn >= 0x200) {
		mfn |= 0xFFFFFE00;
		mfn_abs = -mfn;
	}
#endif

	freq *= 2;
	freq /= pd + 1;

	ll = (unsigned long long)freq * mfn_abs;

	do_div(ll, mfd + 1);
	if (mfn < 0)
		ll = -ll;
	ll = (freq * mfi) + ll;

	return ll;
}
