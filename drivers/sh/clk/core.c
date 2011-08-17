/*
 * SuperH clock framework
 *
 *  Copyright (C) 2005 - 2010  Paul Mundt
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
#define pr_fmt(fmt) "clock: " fmt

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/syscore_ops.h>
#include <linux/seq_file.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/debugfs.h>
#include <linux/cpufreq.h>
#include <linux/clk.h>
#include <linux/sh_clk.h>

static LIST_HEAD(clock_list);
static DEFINE_SPINLOCK(clock_lock);
static DEFINE_MUTEX(clock_list_sem);

/* clock disable operations are not passed on to hardware during boot */
static int allow_disable;

void clk_rate_table_build(struct clk *clk,
			  struct cpufreq_frequency_table *freq_table,
			  int nr_freqs,
			  struct clk_div_mult_table *src_table,
			  unsigned long *bitmap)
{
	unsigned long mult, div;
	unsigned long freq;
	int i;

	clk->nr_freqs = nr_freqs;

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

struct clk_rate_round_data;

struct clk_rate_round_data {
	unsigned long rate;
	unsigned int min, max;
	long (*func)(unsigned int, struct clk_rate_round_data *);
	void *arg;
};

#define for_each_frequency(pos, r, freq)			\
	for (pos = r->min, freq = r->func(pos, r);		\
	     pos <= r->max; pos++, freq = r->func(pos, r))	\
		if (unlikely(freq == 0))			\
			;					\
		else

static long clk_rate_round_helper(struct clk_rate_round_data *rounder)
{
	unsigned long rate_error, rate_error_prev = ~0UL;
	unsigned long highest, lowest, freq;
	long rate_best_fit = -ENOENT;
	int i;

	highest = 0;
	lowest = ~0UL;

	for_each_frequency(i, rounder, freq) {
		if (freq > highest)
			highest = freq;
		if (freq < lowest)
			lowest = freq;

		rate_error = abs(freq - rounder->rate);
		if (rate_error < rate_error_prev) {
			rate_best_fit = freq;
			rate_error_prev = rate_error;
		}

		if (rate_error == 0)
			break;
	}

	if (rounder->rate >= highest)
		rate_best_fit = highest;
	if (rounder->rate <= lowest)
		rate_best_fit = lowest;

	return rate_best_fit;
}

static long clk_rate_table_iter(unsigned int pos,
				struct clk_rate_round_data *rounder)
{
	struct cpufreq_frequency_table *freq_table = rounder->arg;
	unsigned long freq = freq_table[pos].frequency;

	if (freq == CPUFREQ_ENTRY_INVALID)
		freq = 0;

	return freq;
}

long clk_rate_table_round(struct clk *clk,
			  struct cpufreq_frequency_table *freq_table,
			  unsigned long rate)
{
	struct clk_rate_round_data table_round = {
		.min	= 0,
		.max	= clk->nr_freqs - 1,
		.func	= clk_rate_table_iter,
		.arg	= freq_table,
		.rate	= rate,
	};

	if (clk->nr_freqs < 1)
		return -ENOSYS;

	return clk_rate_round_helper(&table_round);
}

static long clk_rate_div_range_iter(unsigned int pos,
				    struct clk_rate_round_data *rounder)
{
	return clk_get_rate(rounder->arg) / pos;
}

long clk_rate_div_range_round(struct clk *clk, unsigned int div_min,
			      unsigned int div_max, unsigned long rate)
{
	struct clk_rate_round_data div_range_round = {
		.min	= div_min,
		.max	= div_max,
		.func	= clk_rate_div_range_iter,
		.arg	= clk_get_parent(clk),
		.rate	= rate,
	};

	return clk_rate_round_helper(&div_range_round);
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
	if (WARN(!clk->usecount, "Trying to disable clock %p with 0 usecount\n",
		 clk))
		return;

	if (!(--clk->usecount)) {
		if (likely(allow_disable && clk->ops && clk->ops->disable))
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

static struct clk_mapping dummy_mapping;

static struct clk *lookup_root_clock(struct clk *clk)
{
	while (clk->parent)
		clk = clk->parent;

	return clk;
}

static int clk_establish_mapping(struct clk *clk)
{
	struct clk_mapping *mapping = clk->mapping;

	/*
	 * Propagate mappings.
	 */
	if (!mapping) {
		struct clk *clkp;

		/*
		 * dummy mapping for root clocks with no specified ranges
		 */
		if (!clk->parent) {
			clk->mapping = &dummy_mapping;
			return 0;
		}

		/*
		 * If we're on a child clock and it provides no mapping of its
		 * own, inherit the mapping from its root clock.
		 */
		clkp = lookup_root_clock(clk);
		mapping = clkp->mapping;
		BUG_ON(!mapping);
	}

	/*
	 * Establish initial mapping.
	 */
	if (!mapping->base && mapping->phys) {
		kref_init(&mapping->ref);

		mapping->base = ioremap_nocache(mapping->phys, mapping->len);
		if (unlikely(!mapping->base))
			return -ENXIO;
	} else if (mapping->base) {
		/*
		 * Bump the refcount for an existing mapping
		 */
		kref_get(&mapping->ref);
	}

	clk->mapping = mapping;
	return 0;
}

static void clk_destroy_mapping(struct kref *kref)
{
	struct clk_mapping *mapping;

	mapping = container_of(kref, struct clk_mapping, ref);

	iounmap(mapping->base);
}

static void clk_teardown_mapping(struct clk *clk)
{
	struct clk_mapping *mapping = clk->mapping;

	/* Nothing to do */
	if (mapping == &dummy_mapping)
		return;

	kref_put(&mapping->ref, clk_destroy_mapping);
	clk->mapping = NULL;
}

int clk_register(struct clk *clk)
{
	int ret;

	if (IS_ERR_OR_NULL(clk))
		return -EINVAL;

	/*
	 * trap out already registered clocks
	 */
	if (clk->node.next || clk->node.prev)
		return 0;

	mutex_lock(&clock_list_sem);

	INIT_LIST_HEAD(&clk->children);
	clk->usecount = 0;

	ret = clk_establish_mapping(clk);
	if (unlikely(ret))
		goto out_unlock;

	if (clk->parent)
		list_add(&clk->sibling, &clk->parent->children);
	else
		list_add(&clk->sibling, &root_clks);

	list_add(&clk->node, &clock_list);

#ifdef CONFIG_SH_CLK_CPG_LEGACY
	if (clk->ops && clk->ops->init)
		clk->ops->init(clk);
#endif

out_unlock:
	mutex_unlock(&clock_list_sem);

	return ret;
}
EXPORT_SYMBOL_GPL(clk_register);

void clk_unregister(struct clk *clk)
{
	mutex_lock(&clock_list_sem);
	list_del(&clk->sibling);
	list_del(&clk->node);
	clk_teardown_mapping(clk);
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
	int ret = -EOPNOTSUPP;
	unsigned long flags;

	spin_lock_irqsave(&clock_lock, flags);

	if (likely(clk->ops && clk->ops->set_rate)) {
		ret = clk->ops->set_rate(clk, rate);
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
EXPORT_SYMBOL_GPL(clk_set_rate);

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
			if (clk->ops->recalc)
				clk->rate = clk->ops->recalc(clk);
			pr_debug("set parent of %p to %p (new rate %ld)\n",
				 clk, clk->parent, clk->rate);
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

long clk_round_parent(struct clk *clk, unsigned long target,
		      unsigned long *best_freq, unsigned long *parent_freq,
		      unsigned int div_min, unsigned int div_max)
{
	struct cpufreq_frequency_table *freq, *best = NULL;
	unsigned long error = ULONG_MAX, freq_high, freq_low, div;
	struct clk *parent = clk_get_parent(clk);

	if (!parent) {
		*parent_freq = 0;
		*best_freq = clk_round_rate(clk, target);
		return abs(target - *best_freq);
	}

	for (freq = parent->freq_table; freq->frequency != CPUFREQ_TABLE_END;
	     freq++) {
		if (freq->frequency == CPUFREQ_ENTRY_INVALID)
			continue;

		if (unlikely(freq->frequency / target <= div_min - 1)) {
			unsigned long freq_max;

			freq_max = (freq->frequency + div_min / 2) / div_min;
			if (error > target - freq_max) {
				error = target - freq_max;
				best = freq;
				if (best_freq)
					*best_freq = freq_max;
			}

			pr_debug("too low freq %u, error %lu\n", freq->frequency,
				 target - freq_max);

			if (!error)
				break;

			continue;
		}

		if (unlikely(freq->frequency / target >= div_max)) {
			unsigned long freq_min;

			freq_min = (freq->frequency + div_max / 2) / div_max;
			if (error > freq_min - target) {
				error = freq_min - target;
				best = freq;
				if (best_freq)
					*best_freq = freq_min;
			}

			pr_debug("too high freq %u, error %lu\n", freq->frequency,
				 freq_min - target);

			if (!error)
				break;

			continue;
		}

		div = freq->frequency / target;
		freq_high = freq->frequency / div;
		freq_low = freq->frequency / (div + 1);

		if (freq_high - target < error) {
			error = freq_high - target;
			best = freq;
			if (best_freq)
				*best_freq = freq_high;
		}

		if (target - freq_low < error) {
			error = target - freq_low;
			best = freq;
			if (best_freq)
				*best_freq = freq_low;
		}

		pr_debug("%u / %lu = %lu, / %lu = %lu, best %lu, parent %u\n",
			 freq->frequency, div, freq_high, div + 1, freq_low,
			 *best_freq, best->frequency);

		if (!error)
			break;
	}

	if (parent_freq)
		*parent_freq = best->frequency;

	return error;
}
EXPORT_SYMBOL_GPL(clk_round_parent);

#ifdef CONFIG_PM
static void clks_core_resume(void)
{
	struct clk *clkp;

	list_for_each_entry(clkp, &clock_list, node) {
		if (likely(clkp->usecount && clkp->ops)) {
			unsigned long rate = clkp->rate;

			if (likely(clkp->ops->set_parent))
				clkp->ops->set_parent(clkp,
					clkp->parent);
			if (likely(clkp->ops->set_rate))
				clkp->ops->set_rate(clkp, rate);
			else if (likely(clkp->ops->recalc))
				clkp->rate = clkp->ops->recalc(clkp);
		}
	}
}

static struct syscore_ops clks_syscore_ops = {
	.resume = clks_core_resume,
};

static int __init clk_syscore_init(void)
{
	register_syscore_ops(&clks_syscore_ops);

	return 0;
}
subsys_initcall(clk_syscore_init);
#endif

/*
 *	debugfs support to trace clock tree hierarchy and attributes
 */
static struct dentry *clk_debugfs_root;

static int clk_debugfs_register_one(struct clk *c)
{
	int err;
	struct dentry *d;
	struct clk *pa = c->parent;
	char s[255];
	char *p = s;

	p += sprintf(p, "%p", c);
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
	debugfs_remove_recursive(c->dentry);
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

	if (!c->dentry) {
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

static int __init clk_late_init(void)
{
	unsigned long flags;
	struct clk *clk;

	/* disable all clocks with zero use count */
	mutex_lock(&clock_list_sem);
	spin_lock_irqsave(&clock_lock, flags);

	list_for_each_entry(clk, &clock_list, node)
		if (!clk->usecount && clk->ops && clk->ops->disable)
			clk->ops->disable(clk);

	/* from now on allow clock disable operations */
	allow_disable = 1;

	spin_unlock_irqrestore(&clock_lock, flags);
	mutex_unlock(&clock_list_sem);
	return 0;
}
late_initcall(clk_late_init);
