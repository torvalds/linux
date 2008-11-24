/* arch/arm/mach-msm/clock.c
 *
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2007 QUALCOMM Incorporated
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 */

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/spinlock.h>

#include "clock.h"
#include "proc_comm.h"

static DEFINE_MUTEX(clocks_mutex);
static DEFINE_SPINLOCK(clocks_lock);
static LIST_HEAD(clocks);

/*
 * glue for the proc_comm interface
 */
static inline int pc_clk_enable(unsigned id)
{
	return msm_proc_comm(PCOM_CLKCTL_RPC_ENABLE, &id, NULL);
}

static inline void pc_clk_disable(unsigned id)
{
	msm_proc_comm(PCOM_CLKCTL_RPC_DISABLE, &id, NULL);
}

static inline int pc_clk_set_rate(unsigned id, unsigned rate)
{
	return msm_proc_comm(PCOM_CLKCTL_RPC_SET_RATE, &id, &rate);
}

static inline int pc_clk_set_min_rate(unsigned id, unsigned rate)
{
	return msm_proc_comm(PCOM_CLKCTL_RPC_MIN_RATE, &id, &rate);
}

static inline int pc_clk_set_max_rate(unsigned id, unsigned rate)
{
	return msm_proc_comm(PCOM_CLKCTL_RPC_MAX_RATE, &id, &rate);
}

static inline int pc_clk_set_flags(unsigned id, unsigned flags)
{
	return msm_proc_comm(PCOM_CLKCTL_RPC_SET_FLAGS, &id, &flags);
}

static inline unsigned pc_clk_get_rate(unsigned id)
{
	if (msm_proc_comm(PCOM_CLKCTL_RPC_RATE, &id, NULL))
		return 0;
	else
		return id;
}

static inline unsigned pc_clk_is_enabled(unsigned id)
{
	if (msm_proc_comm(PCOM_CLKCTL_RPC_ENABLED, &id, NULL))
		return 0;
	else
		return id;
}

static inline int pc_pll_request(unsigned id, unsigned on)
{
	on = !!on;
	return msm_proc_comm(PCOM_CLKCTL_RPC_PLL_REQUEST, &id, &on);
}

/*
 * Standard clock functions defined in include/linux/clk.h
 */
struct clk *clk_get(struct device *dev, const char *id)
{
	struct clk *clk;

	mutex_lock(&clocks_mutex);

	list_for_each_entry(clk, &clocks, list)
		if (!strcmp(id, clk->name) && clk->dev == dev)
			goto found_it;

	list_for_each_entry(clk, &clocks, list)
		if (!strcmp(id, clk->name) && clk->dev == NULL)
			goto found_it;

	clk = ERR_PTR(-ENOENT);
found_it:
	mutex_unlock(&clocks_mutex);
	return clk;
}
EXPORT_SYMBOL(clk_get);

void clk_put(struct clk *clk)
{
}
EXPORT_SYMBOL(clk_put);

int clk_enable(struct clk *clk)
{
	unsigned long flags;
	spin_lock_irqsave(&clocks_lock, flags);
	clk->count++;
	if (clk->count == 1)
		pc_clk_enable(clk->id);
	spin_unlock_irqrestore(&clocks_lock, flags);
	return 0;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
	unsigned long flags;
	spin_lock_irqsave(&clocks_lock, flags);
	BUG_ON(clk->count == 0);
	clk->count--;
	if (clk->count == 0)
		pc_clk_disable(clk->id);
	spin_unlock_irqrestore(&clocks_lock, flags);
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	return pc_clk_get_rate(clk->id);
}
EXPORT_SYMBOL(clk_get_rate);

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	int ret;
	if (clk->flags & CLKFLAG_USE_MIN_MAX_TO_SET) {
		ret = pc_clk_set_max_rate(clk->id, rate);
		if (ret)
			return ret;
		return pc_clk_set_min_rate(clk->id, rate);
	}
	return pc_clk_set_rate(clk->id, rate);
}
EXPORT_SYMBOL(clk_set_rate);

int clk_set_parent(struct clk *clk, struct clk *parent)
{
	return -ENOSYS;
}
EXPORT_SYMBOL(clk_set_parent);

struct clk *clk_get_parent(struct clk *clk)
{
	return ERR_PTR(-ENOSYS);
}
EXPORT_SYMBOL(clk_get_parent);

int clk_set_flags(struct clk *clk, unsigned long flags)
{
	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;
	return pc_clk_set_flags(clk->id, flags);
}
EXPORT_SYMBOL(clk_set_flags);


void __init msm_clock_init(void)
{
	unsigned n;

	spin_lock_init(&clocks_lock);
	mutex_lock(&clocks_mutex);
	for (n = 0; n < msm_num_clocks; n++)
		list_add_tail(&msm_clocks[n].list, &clocks);
	mutex_unlock(&clocks_mutex);
}

/* The bootloader and/or AMSS may have left various clocks enabled.
 * Disable any clocks that belong to us (CLKFLAG_AUTO_OFF) but have
 * not been explicitly enabled by a clk_enable() call.
 */
static int __init clock_late_init(void)
{
	unsigned long flags;
	struct clk *clk;
	unsigned count = 0;

	mutex_lock(&clocks_mutex);
	list_for_each_entry(clk, &clocks, list) {
		if (clk->flags & CLKFLAG_AUTO_OFF) {
			spin_lock_irqsave(&clocks_lock, flags);
			if (!clk->count) {
				count++;
				pc_clk_disable(clk->id);
			}
			spin_unlock_irqrestore(&clocks_lock, flags);
		}
	}
	mutex_unlock(&clocks_mutex);
	pr_info("clock_late_init() disabled %d unused clocks\n", count);
	return 0;
}

late_initcall(clock_late_init);
