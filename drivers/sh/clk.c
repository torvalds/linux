/*
 * drivers/sh/clk.c - SuperH clock framework
 *
 *  Copyright (C) 2005 - 2009  Paul Mundt
 *
 * This clock framework is derived from the OMAP version by:
 *
 *	Copyright (C) 2004 - 2008 Nokia Corporation
 *	Written by Tuukka Tikkanen <tuukka.tikkanen@elektrobit.com>
 *
 *  Modified for omap shared clock framework by Tony Lindgren <tony@atomide.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/kobject.h>
#include <linux/sysdev.h>
#include <linux/seq_file.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/debugfs.h>
#include <linux/cpufreq.h>
#include <linux/clk.h>
#include <linux/sh_clk.h>

static LIST_HEAD(clock_list);
static DEFINE_SPINLOCK(clock_lock);
static DEFINE_MUTEX(clock_list_sem);

void clk_rate_table_build(struct clk *clk,
			  struct cpufreq_frequency_table *freq_table,
			  int nr_freqs,
			  struct clk_div_mult_table *src_table,
			  unsigned long *bitmap)
{
	unsigned long mult, div;
	unsigned long freq;
	int i;

	for (i = 0; i < nr_freqs; i++) {
		div = 1;
		mult = 1;

		if (src_table->divisors && i < src_table->nr_divisors)
			div = src_table->divisors[i];

		if (src_table->multipliers && i < src_table->nr_multipliers)
			mult = src_table->multipliers[i];

		if (!div || !mult || (bitmap && !test_bit(i, bitmap)))
			freq = CPUFREQ_ENTRY_INVALID;
		else
			freq = clk->parent->rate * mult / div;

		freq_table[i].index = i;
		freq_table[i].frequency = freq;
	}

	/* Termination entry */
	freq_table[i].index = i;
	freq_table[i].frequency = CPUFREQ_TABLE_END;
}

long clk_rate_table_round(struct clk *clk,
			  struct cpufreq_frequency_table *freq_table,
			  unsigned long rate)
{
	unsigned long rate_error, rate_error_prev = ~0UL;
	unsigned long rate_best_fit = rate;
	unsigned long highest, lowest;
	int i;

	highest = lowest = 0;

	for (i = 0; freq_table[i].frequency != CPUFREQ_TABLE_END; i++) {
		unsigned long freq = freq_table[i].frequency;

		if (freq == CPUFREQ_ENTRY_INVALID)
			continue;

		if (freq > highest)
			highest = freq;
		if (freq < lowest)
			lowest = freq;

		rate_error = abs(freq - rate);
		if (rate_error < rate_error_prev) {
			rate_best_fit = freq;
			rate_error_prev = rate_error;
		}

		if (rate_error == 0)
			break;
	}

	if (rate >= highest)
		rate_best_fit = highest;
	if (rate <= lowest)
		rate_best_fit = lowest;

	return rate_best_fit;
}

int clk_rate_table_find(struct clk *clk,
			struct cpufreq_frequency_table *freq_table,
			unsigned long rate)
{
	int i;

	for (i = 0; freq_table[i].frequency != CPUFREQ_TABLE_END; i++) {
		unsigned long freq = freq_table[i].frequency;

		if (freq == CPUFREQ_ENTRY_INVALID)
			continue;

		if (freq == rate)
			return i;
	}

	return -ENOENT;
}

/* Used for clocks that always have same value as the parent clock */
unsigned long followparent_recalc(struct clk *clk)
{
	return clk->parent ? clk->parent->rate : 0;
}

int clk_reparent(struct clk *child, struct clk *parent)
{
	list_del_init(&child->sibling);
	if (parent)
		list_add(&child->sibling, &parent->children);
	child->parent = parent;

	/* now do the debugfs renaming to reattach the child
	   to the proper parent */

	return 0;
}

/* Propagate rate to children */
void propagate_rate(struct clk *tclk)
{
	struct clk *clkp;

	list_for_each_entry(clkp, &tclk->children, sibling) {
		if (clkp->ops && clkp->ops->recalc)
			clkp->rate = clkp->ops->recalc(clkp);

		propagate_rate(clkp);
	}
}

static void __clk_disable(struct clk *clk)
{
	if (WARN(!clk->usecount, "Trying to disable clock %s with 0 usecount\n",
		 clk->name))
		return;

	if (!(--clk->usecount)) {
		if (likely(clk->ops && clk->ops->disable))
			clk->ops->disable(clk);
		if (likely(clk->parent))
			__clk_disable(clk->parent);
	}
}

void clk_disable(struct clk *clk)
{
	unsigned long flags;

	if (!clk)
		return;

	spin_lock_irqsave(&clock_lock, flags);
	__clk_disable(clk);
	spin_unlock_irqrestore(&clock_lock, flags);
}
EXPORT_SYMBOL_GPL(clk_disable);

static int __clk_enable(struct clk *clk)
{
	int ret = 0;

	if (clk->usecount++ == 0) {
		if (clk->parent) {
			ret = __clk_enable(clk->parent);
			if (unlikely(ret))
				goto err;
		}

		if (clk->ops && clk->ops->enable) {
			ret = clk->ops->enable(clk);
			if (ret) {
				if (clk->parent)
					__clk_disable(clk->parent);
				goto err;
			}
		}
	}

	return ret;
err:
	clk->usecount--;
	return ret;
}

int clk_enable(struct clk *clk)
{
	unsigned long flags;
	int ret;

	if (!clk)
		return -EINVAL;

	spin_lock_irqsave(&clock_lock, flags);
	ret = __clk_enable(clk);
	spin_unlock_irqrestore(&clock_lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(clk_enable);

static LIST_HEAD(root_clks);

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

	list_for_each_entry(clkp, &root_clks, sibling) {
		if (clkp->ops && clkp->ops->recalc)
			clkp->rate = clkp->ops->recalc(clkp);
		propagate_rate(clkp);
	}
}

int clk_register(struct clk *clk)
{
	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	/*
	 * trap out already registered clocks
	 */
	if (clk->node.next || clk->node.prev)
		return 0;

	mutex_lock(&clock_list_sem);

	INIT_LIST_HEAD(&clk->children);
	clk->usecount = 0;

	if (clk->parent)
		list_add(&clk->sibling, &clk->parent->children);
	else
		list_add(&clk->sibling, &root_clks);

	list_add(&clk->node, &clock_list);
	if (clk->ops && clk->ops->init)
		clk->ops->init(clk);
	mutex_unlock(&clock_list_sem);

	return 0;
}
EXPORT_SYMBOL_GPL(clk_register);

void clk_unregister(struct clk *clk)
{
	mutex_lock(&clock_list_sem);
	list_del(&clk->sibling);
	list_del(&clk->node);
	mutex_unlock(&clock_list_sem);
}
EXPORT_SYMBOL_GPL(clk_unregister);

void clk_enable_init_clocks(void)
{
	struct clk *clkp;

	list_for_each_entry(clkp, &clock_list, node)
		if (clkp->flags & CLK_ENABLE_ON_INIT)
			clk_enable(clkp);
}

unsigned long clk_get_rate(struct clk *clk)
{
	return clk->rate;
}
EXPORT_SYMBOL_GPL(clk_get_rate);

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	return clk_set_rate_ex(clk, rate, 0);
}
EXPORT_SYMBOL_GPL(clk_set_rate);

int clk_set_rate_ex(struct clk *clk, unsigned long rate, int algo_id)
{
	int ret = -EOPNOTSUPP;
	unsigned long flags;

	spin_lock_irqsave(&clock_lock, flags);

	if (likely(clk->ops && clk->ops->set_rate)) {
		ret = clk->ops->set_rate(clk, rate, algo_id);
		if (ret != 0)
			goto out_unlock;
	} else {
		clk->rate = rate;
		ret = 0;
	}

	if (clk->ops && clk->ops->recalc)
		clk->rate = clk->ops->recalc(clk);

	propagate_rate(clk);

out_unlock:
	spin_unlock_irqrestore(&clock_lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(clk_set_rate_ex);

int clk_set_parent(struct clk *clk, struct clk *parent)
{
	unsigned long flags;
	int ret = -EINVAL;

	if (!parent || !clk)
		return ret;
	if (clk->parent == parent)
		return 0;

	spin_lock_irqsave(&clock_lock, flags);
	if (clk->usecount == 0) {
		if (clk->ops->set_parent)
			ret = clk->ops->set_parent(clk, parent);
		else
			ret = clk_reparent(clk, parent);

		if (ret == 0) {
			pr_debug("clock: set parent of %s to %s (new rate %ld)\n",
				 clk->name, clk->parent->name, clk->rate);
			if (clk->ops->recalc)
				clk->rate = clk->ops->recalc(clk);
			propagate_rate(clk);
		}
	} else
		ret = -EBUSY;
	spin_unlock_irqrestore(&clock_lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(clk_set_parent);

struct clk *clk_get_parent(struct clk *clk)
{
	return clk->parent;
}
EXPORT_SYMBOL_GPL(clk_get_parent);

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	if (likely(clk->ops && clk->ops->round_rate)) {
		unsigned long flags, rounded;

		spin_lock_irqsave(&clock_lock, flags);
		rounded = clk->ops->round_rate(clk, rate);
		spin_unlock_irqrestore(&clock_lock, flags);

		return rounded;
	}

	return clk_get_rate(clk);
}
EXPORT_SYMBOL_GPL(clk_round_rate);

#ifdef CONFIG_PM
static int clks_sysdev_suspend(struct sys_device *dev, pm_message_t state)
{
	static pm_message_t prev_state;
	struct clk *clkp;

	switch (state.event) {
	case PM_EVENT_ON:
		/* Resumeing from hibernation */
		if (prev_state.event != PM_EVENT_FREEZE)
			break;

		list_for_each_entry(clkp, &clock_list, node) {
			if (likely(clkp->ops)) {
				unsigned long rate = clkp->rate;

				if (likely(clkp->ops->set_parent))
					clkp->ops->set_parent(clkp,
						clkp->parent);
				if (likely(clkp->ops->set_rate))
					clkp->ops->set_rate(clkp,
						rate, NO_CHANGE);
				else if (likely(clkp->ops->recalc))
					clkp->rate = clkp->ops->recalc(clkp);
			}
		}
		break;
	case PM_EVENT_FREEZE:
		break;
	case PM_EVENT_SUSPEND:
		break;
	}

	prev_state = state;
	return 0;
}

static int clks_sysdev_resume(struct sys_device *dev)
{
	return clks_sysdev_suspend(dev, PMSG_ON);
}

static struct sysdev_class clks_sysdev_class = {
	.name = "clks",
};

static struct sysdev_driver clks_sysdev_driver = {
	.suspend = clks_sysdev_suspend,
	.resume = clks_sysdev_resume,
};

static struct sys_device clks_sysdev_dev = {
	.cls = &clks_sysdev_class,
};

static int __init clk_sysdev_init(void)
{
	sysdev_class_register(&clks_sysdev_class);
	sysdev_driver_register(&clks_sysdev_class, &clks_sysdev_driver);
	sysdev_register(&clks_sysdev_dev);

	return 0;
}
subsys_initcall(clk_sysdev_init);
#endif

/*
 *	debugfs support to trace clock tree hierarchy and attributes
 */
static struct dentry *clk_debugfs_root;

static int clk_debugfs_register_one(struct clk *c)
{
	int err;
	struct dentry *d, *child, *child_tmp;
	struct clk *pa = c->parent;
	char s[255];
	char *p = s;

	p += sprintf(p, "%s", c->name);
	if (c->id >= 0)
		sprintf(p, ":%d", c->id);
	d = debugfs_create_dir(s, pa ? pa->dentry : clk_debugfs_root);
	if (!d)
		return -ENOMEM;
	c->dentry = d;

	d = debugfs_create_u8("usecount", S_IRUGO, c->dentry, (u8 *)&c->usecount);
	if (!d) {
		err = -ENOMEM;
		goto err_out;
	}
	d = debugfs_create_u32("rate", S_IRUGO, c->dentry, (u32 *)&c->rate);
	if (!d) {
		err = -ENOMEM;
		goto err_out;
	}
	d = debugfs_create_x32("flags", S_IRUGO, c->dentry, (u32 *)&c->flags);
	if (!d) {
		err = -ENOMEM;
		goto err_out;
	}
	return 0;

err_out:
	d = c->dentry;
	list_for_each_entry_safe(child, child_tmp, &d->d_subdirs, d_u.d_child)
		debugfs_remove(child);
	debugfs_remove(c->dentry);
	return err;
}

static int clk_debugfs_register(struct clk *c)
{
	int err;
	struct clk *pa = c->parent;

	if (pa && !pa->dentry) {
		err = clk_debugfs_register(pa);
		if (err)
			return err;
	}

	if (!c->dentry && c->name) {
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
	if (!d)
		return -ENOMEM;
	clk_debugfs_root = d;

	list_for_each_entry(c, &clock_list, node) {
		err = clk_debugfs_register(c);
		if (err)
			goto err_out;
	}
	return 0;
err_out:
	debugfs_remove_recursive(clk_debugfs_root);
	return err;
}
late_initcall(clk_debugfs_init);
