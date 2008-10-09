/*
 *  linux/arch/arm/plat-omap/clock.c
 *
 *  Copyright (C) 2004 - 2008 Nokia corporation
 *  Written by Tuukka Tikkanen <tuukka.tikkanen@elektrobit.com>
 *
 *  Modified for omap shared clock framework by Tony Lindgren <tony@atomide.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/cpufreq.h>
#include <linux/debugfs.h>
#include <linux/io.h>

#include <mach/clock.h>

static LIST_HEAD(clocks);
static DEFINE_MUTEX(clocks_mutex);
static DEFINE_SPINLOCK(clockfw_lock);

static struct clk_functions *arch_clock;

/*-------------------------------------------------------------------------
 * Standard clock functions defined in include/linux/clk.h
 *-------------------------------------------------------------------------*/

/*
 * Returns a clock. Note that we first try to use device id on the bus
 * and clock name. If this fails, we try to use clock name only.
 */
struct clk * clk_get(struct device *dev, const char *id)
{
	struct clk *p, *clk = ERR_PTR(-ENOENT);
	int idno;

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

	list_for_each_entry(p, &clocks, node) {
		if (strcmp(id, p->name) == 0 && try_module_get(p->owner)) {
			clk = p;
			break;
		}
	}

found:
	mutex_unlock(&clocks_mutex);

	return clk;
}
EXPORT_SYMBOL(clk_get);

int clk_enable(struct clk *clk)
{
	unsigned long flags;
	int ret = 0;

	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	spin_lock_irqsave(&clockfw_lock, flags);
	if (arch_clock->clk_enable)
		ret = arch_clock->clk_enable(clk);
	spin_unlock_irqrestore(&clockfw_lock, flags);

	return ret;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
	unsigned long flags;

	if (clk == NULL || IS_ERR(clk))
		return;

	spin_lock_irqsave(&clockfw_lock, flags);
	if (clk->usecount == 0) {
		printk(KERN_ERR "Trying disable clock %s with 0 usecount\n",
		       clk->name);
		WARN_ON(1);
		goto out;
	}

	if (arch_clock->clk_disable)
		arch_clock->clk_disable(clk);

out:
	spin_unlock_irqrestore(&clockfw_lock, flags);
}
EXPORT_SYMBOL(clk_disable);

int clk_get_usecount(struct clk *clk)
{
	unsigned long flags;
	int ret = 0;

	if (clk == NULL || IS_ERR(clk))
		return 0;

	spin_lock_irqsave(&clockfw_lock, flags);
	ret = clk->usecount;
	spin_unlock_irqrestore(&clockfw_lock, flags);

	return ret;
}
EXPORT_SYMBOL(clk_get_usecount);

unsigned long clk_get_rate(struct clk *clk)
{
	unsigned long flags;
	unsigned long ret = 0;

	if (clk == NULL || IS_ERR(clk))
		return 0;

	spin_lock_irqsave(&clockfw_lock, flags);
	ret = clk->rate;
	spin_unlock_irqrestore(&clockfw_lock, flags);

	return ret;
}
EXPORT_SYMBOL(clk_get_rate);

void clk_put(struct clk *clk)
{
	if (clk && !IS_ERR(clk))
		module_put(clk->owner);
}
EXPORT_SYMBOL(clk_put);

/*-------------------------------------------------------------------------
 * Optional clock functions defined in include/linux/clk.h
 *-------------------------------------------------------------------------*/

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	unsigned long flags;
	long ret = 0;

	if (clk == NULL || IS_ERR(clk))
		return ret;

	spin_lock_irqsave(&clockfw_lock, flags);
	if (arch_clock->clk_round_rate)
		ret = arch_clock->clk_round_rate(clk, rate);
	spin_unlock_irqrestore(&clockfw_lock, flags);

	return ret;
}
EXPORT_SYMBOL(clk_round_rate);

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned long flags;
	int ret = -EINVAL;

	if (clk == NULL || IS_ERR(clk))
		return ret;

	spin_lock_irqsave(&clockfw_lock, flags);
	if (arch_clock->clk_set_rate)
		ret = arch_clock->clk_set_rate(clk, rate);
	spin_unlock_irqrestore(&clockfw_lock, flags);

	return ret;
}
EXPORT_SYMBOL(clk_set_rate);

int clk_set_parent(struct clk *clk, struct clk *parent)
{
	unsigned long flags;
	int ret = -EINVAL;

	if (clk == NULL || IS_ERR(clk) || parent == NULL || IS_ERR(parent))
		return ret;

	spin_lock_irqsave(&clockfw_lock, flags);
	if (arch_clock->clk_set_parent)
		ret =  arch_clock->clk_set_parent(clk, parent);
	spin_unlock_irqrestore(&clockfw_lock, flags);

	return ret;
}
EXPORT_SYMBOL(clk_set_parent);

struct clk *clk_get_parent(struct clk *clk)
{
	unsigned long flags;
	struct clk * ret = NULL;

	if (clk == NULL || IS_ERR(clk))
		return ret;

	spin_lock_irqsave(&clockfw_lock, flags);
	if (arch_clock->clk_get_parent)
		ret = arch_clock->clk_get_parent(clk);
	spin_unlock_irqrestore(&clockfw_lock, flags);

	return ret;
}
EXPORT_SYMBOL(clk_get_parent);

/*-------------------------------------------------------------------------
 * OMAP specific clock functions shared between omap1 and omap2
 *-------------------------------------------------------------------------*/

unsigned int __initdata mpurate;

/*
 * By default we use the rate set by the bootloader.
 * You can override this with mpurate= cmdline option.
 */
static int __init omap_clk_setup(char *str)
{
	get_option(&str, &mpurate);

	if (!mpurate)
		return 1;

	if (mpurate < 1000)
		mpurate *= 1000000;

	return 1;
}
__setup("mpurate=", omap_clk_setup);

/* Used for clocks that always have same value as the parent clock */
void followparent_recalc(struct clk *clk)
{
	if (clk == NULL || IS_ERR(clk))
		return;

	clk->rate = clk->parent->rate;
	if (unlikely(clk->flags & RATE_PROPAGATES))
		propagate_rate(clk);
}

/* Propagate rate to children */
void propagate_rate(struct clk * tclk)
{
	struct clk *clkp;

	if (tclk == NULL || IS_ERR(tclk))
		return;

	list_for_each_entry(clkp, &clocks, node) {
		if (likely(clkp->parent != tclk))
			continue;
		if (likely((u32)clkp->recalc))
			clkp->recalc(clkp);
	}
}

/**
 * recalculate_root_clocks - recalculate and propagate all root clocks
 *
 * Recalculates all root clocks (clocks with no parent), which if the
 * clock's .recalc is set correctly, should also propagate their rates.
 * Called at init.
 */
void recalculate_root_clocks(void)
{
	struct clk *clkp;

	list_for_each_entry(clkp, &clocks, node) {
		if (unlikely(!clkp->parent) && likely((u32)clkp->recalc))
			clkp->recalc(clkp);
	}
}

int clk_register(struct clk *clk)
{
	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	mutex_lock(&clocks_mutex);
	list_add(&clk->node, &clocks);
	if (clk->init)
		clk->init(clk);
	mutex_unlock(&clocks_mutex);

	return 0;
}
EXPORT_SYMBOL(clk_register);

void clk_unregister(struct clk *clk)
{
	if (clk == NULL || IS_ERR(clk))
		return;

	mutex_lock(&clocks_mutex);
	list_del(&clk->node);
	mutex_unlock(&clocks_mutex);
}
EXPORT_SYMBOL(clk_unregister);

void clk_deny_idle(struct clk *clk)
{
	unsigned long flags;

	if (clk == NULL || IS_ERR(clk))
		return;

	spin_lock_irqsave(&clockfw_lock, flags);
	if (arch_clock->clk_deny_idle)
		arch_clock->clk_deny_idle(clk);
	spin_unlock_irqrestore(&clockfw_lock, flags);
}
EXPORT_SYMBOL(clk_deny_idle);

void clk_allow_idle(struct clk *clk)
{
	unsigned long flags;

	if (clk == NULL || IS_ERR(clk))
		return;

	spin_lock_irqsave(&clockfw_lock, flags);
	if (arch_clock->clk_allow_idle)
		arch_clock->clk_allow_idle(clk);
	spin_unlock_irqrestore(&clockfw_lock, flags);
}
EXPORT_SYMBOL(clk_allow_idle);

void clk_enable_init_clocks(void)
{
	struct clk *clkp;

	list_for_each_entry(clkp, &clocks, node) {
		if (clkp->flags & ENABLE_ON_INIT)
			clk_enable(clkp);
	}
}
EXPORT_SYMBOL(clk_enable_init_clocks);

#ifdef CONFIG_CPU_FREQ
void clk_init_cpufreq_table(struct cpufreq_frequency_table **table)
{
	unsigned long flags;

	spin_lock_irqsave(&clockfw_lock, flags);
	if (arch_clock->clk_init_cpufreq_table)
		arch_clock->clk_init_cpufreq_table(table);
	spin_unlock_irqrestore(&clockfw_lock, flags);
}
EXPORT_SYMBOL(clk_init_cpufreq_table);
#endif

/*-------------------------------------------------------------------------*/

#ifdef CONFIG_OMAP_RESET_CLOCKS
/*
 * Disable any unused clocks left on by the bootloader
 */
static int __init clk_disable_unused(void)
{
	struct clk *ck;
	unsigned long flags;

	list_for_each_entry(ck, &clocks, node) {
		if (ck->usecount > 0 || (ck->flags & ALWAYS_ENABLED) ||
			ck->enable_reg == 0)
			continue;

		spin_lock_irqsave(&clockfw_lock, flags);
		if (arch_clock->clk_disable_unused)
			arch_clock->clk_disable_unused(ck);
		spin_unlock_irqrestore(&clockfw_lock, flags);
	}

	return 0;
}
late_initcall(clk_disable_unused);
#endif

int __init clk_init(struct clk_functions * custom_clocks)
{
	if (!custom_clocks) {
		printk(KERN_ERR "No custom clock functions registered\n");
		BUG();
	}

	arch_clock = custom_clocks;

	return 0;
}

#if defined(CONFIG_PM_DEBUG) && defined(CONFIG_DEBUG_FS)
/*
 *	debugfs support to trace clock tree hierarchy and attributes
 */
static struct dentry *clk_debugfs_root;

static int clk_debugfs_register_one(struct clk *c)
{
	int err;
	struct dentry *d, *child;
	struct clk *pa = c->parent;
	char s[255];
	char *p = s;

	p += sprintf(p, "%s", c->name);
	if (c->id != 0)
		sprintf(p, ":%d", c->id);
	d = debugfs_create_dir(s, pa ? pa->dent : clk_debugfs_root);
	if (IS_ERR(d))
		return PTR_ERR(d);
	c->dent = d;

	d = debugfs_create_u8("usecount", S_IRUGO, c->dent, (u8 *)&c->usecount);
	if (IS_ERR(d)) {
		err = PTR_ERR(d);
		goto err_out;
	}
	d = debugfs_create_u32("rate", S_IRUGO, c->dent, (u32 *)&c->rate);
	if (IS_ERR(d)) {
		err = PTR_ERR(d);
		goto err_out;
	}
	d = debugfs_create_x32("flags", S_IRUGO, c->dent, (u32 *)&c->flags);
	if (IS_ERR(d)) {
		err = PTR_ERR(d);
		goto err_out;
	}
	return 0;

err_out:
	d = c->dent;
	list_for_each_entry(child, &d->d_subdirs, d_u.d_child)
		debugfs_remove(child);
	debugfs_remove(c->dent);
	return err;
}

static int clk_debugfs_register(struct clk *c)
{
	int err;
	struct clk *pa = c->parent;

	if (pa && !pa->dent) {
		err = clk_debugfs_register(pa);
		if (err)
			return err;
	}

	if (!c->dent) {
		err = clk_debugfs_register_one(c);
		if (err)
			return err;
	}
	return 0;
}

static int __init clk_debugfs_init(void)
{
	struct clk *c;
	struct dentry *d;
	int err;

	d = debugfs_create_dir("clock", NULL);
	if (IS_ERR(d))
		return PTR_ERR(d);
	clk_debugfs_root = d;

	list_for_each_entry(c, &clocks, node) {
		err = clk_debugfs_register(c);
		if (err)
			goto err_out;
	}
	return 0;
err_out:
	debugfs_remove(clk_debugfs_root); /* REVISIT: Cleanup correctly */
	return err;
}
late_initcall(clk_debugfs_init);

#endif /* defined(CONFIG_PM_DEBUG) && defined(CONFIG_DEBUG_FS) */
