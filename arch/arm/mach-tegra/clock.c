/*
 *
 * Copyright (C) 2010 Google, Inc.
 * Copyright (c) 2012 NVIDIA CORPORATION.  All rights reserved.
 *
 * Author:
 *	Colin Cross <ccross@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include <mach/clk.h>

#include "board.h"
#include "clock.h"

/*
 * Locking:
 *
 * Each struct clk has a spinlock.
 *
 * To avoid AB-BA locking problems, locks must always be traversed from child
 * clock to parent clock.  For example, when enabling a clock, the clock's lock
 * is taken, and then clk_enable is called on the parent, which take's the
 * parent clock's lock.  There is one exceptions to this ordering: When dumping
 * the clock tree through debugfs.  In this case, clk_lock_all is called,
 * which attemps to iterate through the entire list of clocks and take every
 * clock lock.  If any call to spin_trylock fails, all locked clocks are
 * unlocked, and the process is retried.  When all the locks are held,
 * the only clock operation that can be called is clk_get_rate_all_locked.
 *
 * Within a single clock, no clock operation can call another clock operation
 * on itself, except for clk_get_rate_locked and clk_set_rate_locked.  Any
 * clock operation can call any other clock operation on any of it's possible
 * parents.
 *
 * An additional mutex, clock_list_lock, is used to protect the list of all
 * clocks.
 *
 * The clock operations must lock internally to protect against
 * read-modify-write on registers that are shared by multiple clocks
 */
static DEFINE_MUTEX(clock_list_lock);
static LIST_HEAD(clocks);

#ifndef CONFIG_COMMON_CLK
struct clk *tegra_get_clock_by_name(const char *name)
{
	struct clk *c;
	struct clk *ret = NULL;
	mutex_lock(&clock_list_lock);
	list_for_each_entry(c, &clocks, node) {
		if (strcmp(c->name, name) == 0) {
			ret = c;
			break;
		}
	}
	mutex_unlock(&clock_list_lock);
	return ret;
}

/* Must be called with c->spinlock held */
static unsigned long clk_predict_rate_from_parent(struct clk *c, struct clk *p)
{
	u64 rate;

	rate = clk_get_rate(p);

	if (c->mul != 0 && c->div != 0) {
		rate *= c->mul;
		rate += c->div - 1; /* round up */
		do_div(rate, c->div);
	}

	return rate;
}

/* Must be called with c->spinlock held */
unsigned long clk_get_rate_locked(struct clk *c)
{
	unsigned long rate;

	if (c->parent)
		rate = clk_predict_rate_from_parent(c, c->parent);
	else
		rate = c->rate;

	return rate;
}

unsigned long clk_get_rate(struct clk *c)
{
	unsigned long flags;
	unsigned long rate;

	spin_lock_irqsave(&c->spinlock, flags);

	rate = clk_get_rate_locked(c);

	spin_unlock_irqrestore(&c->spinlock, flags);

	return rate;
}
EXPORT_SYMBOL(clk_get_rate);

int clk_reparent(struct clk *c, struct clk *parent)
{
	c->parent = parent;
	return 0;
}

void clk_init(struct clk *c)
{
	spin_lock_init(&c->spinlock);

	if (c->ops && c->ops->init)
		c->ops->init(c);

	if (!c->ops || !c->ops->enable) {
		c->refcnt++;
		c->set = true;
		if (c->parent)
			c->state = c->parent->state;
		else
			c->state = ON;
	}

	mutex_lock(&clock_list_lock);
	list_add(&c->node, &clocks);
	mutex_unlock(&clock_list_lock);
}

int clk_enable(struct clk *c)
{
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&c->spinlock, flags);

	if (c->refcnt == 0) {
		if (c->parent) {
			ret = clk_enable(c->parent);
			if (ret)
				goto out;
		}

		if (c->ops && c->ops->enable) {
			ret = c->ops->enable(c);
			if (ret) {
				if (c->parent)
					clk_disable(c->parent);
				goto out;
			}
			c->state = ON;
			c->set = true;
		}
	}
	c->refcnt++;
out:
	spin_unlock_irqrestore(&c->spinlock, flags);
	return ret;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *c)
{
	unsigned long flags;

	spin_lock_irqsave(&c->spinlock, flags);

	if (c->refcnt == 0) {
		WARN(1, "Attempting to disable clock %s with refcnt 0", c->name);
		spin_unlock_irqrestore(&c->spinlock, flags);
		return;
	}
	if (c->refcnt == 1) {
		if (c->ops && c->ops->disable)
			c->ops->disable(c);

		if (c->parent)
			clk_disable(c->parent);

		c->state = OFF;
	}
	c->refcnt--;

	spin_unlock_irqrestore(&c->spinlock, flags);
}
EXPORT_SYMBOL(clk_disable);

int clk_set_parent(struct clk *c, struct clk *parent)
{
	int ret;
	unsigned long flags;
	unsigned long new_rate;
	unsigned long old_rate;

	spin_lock_irqsave(&c->spinlock, flags);

	if (!c->ops || !c->ops->set_parent) {
		ret = -ENOSYS;
		goto out;
	}

	new_rate = clk_predict_rate_from_parent(c, parent);
	old_rate = clk_get_rate_locked(c);

	ret = c->ops->set_parent(c, parent);
	if (ret)
		goto out;

out:
	spin_unlock_irqrestore(&c->spinlock, flags);
	return ret;
}
EXPORT_SYMBOL(clk_set_parent);

struct clk *clk_get_parent(struct clk *c)
{
	return c->parent;
}
EXPORT_SYMBOL(clk_get_parent);

int clk_set_rate_locked(struct clk *c, unsigned long rate)
{
	long new_rate;

	if (!c->ops || !c->ops->set_rate)
		return -ENOSYS;

	if (rate > c->max_rate)
		rate = c->max_rate;

	if (c->ops && c->ops->round_rate) {
		new_rate = c->ops->round_rate(c, rate);

		if (new_rate < 0)
			return new_rate;

		rate = new_rate;
	}

	return c->ops->set_rate(c, rate);
}

int clk_set_rate(struct clk *c, unsigned long rate)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&c->spinlock, flags);

	ret = clk_set_rate_locked(c, rate);

	spin_unlock_irqrestore(&c->spinlock, flags);

	return ret;
}
EXPORT_SYMBOL(clk_set_rate);


/* Must be called with clocks lock and all indvidual clock locks held */
unsigned long clk_get_rate_all_locked(struct clk *c)
{
	u64 rate;
	int mul = 1;
	int div = 1;
	struct clk *p = c;

	while (p) {
		c = p;
		if (c->mul != 0 && c->div != 0) {
			mul *= c->mul;
			div *= c->div;
		}
		p = c->parent;
	}

	rate = c->rate;
	rate *= mul;
	do_div(rate, div);

	return rate;
}

long clk_round_rate(struct clk *c, unsigned long rate)
{
	unsigned long flags;
	long ret;

	spin_lock_irqsave(&c->spinlock, flags);

	if (!c->ops || !c->ops->round_rate) {
		ret = -ENOSYS;
		goto out;
	}

	if (rate > c->max_rate)
		rate = c->max_rate;

	ret = c->ops->round_rate(c, rate);

out:
	spin_unlock_irqrestore(&c->spinlock, flags);
	return ret;
}
EXPORT_SYMBOL(clk_round_rate);

static int tegra_clk_init_one_from_table(struct tegra_clk_init_table *table)
{
	struct clk *c;
	struct clk *p;

	int ret = 0;

	c = tegra_get_clock_by_name(table->name);

	if (!c) {
		pr_warning("Unable to initialize clock %s\n",
			table->name);
		return -ENODEV;
	}

	if (table->parent) {
		p = tegra_get_clock_by_name(table->parent);
		if (!p) {
			pr_warning("Unable to find parent %s of clock %s\n",
				table->parent, table->name);
			return -ENODEV;
		}

		if (c->parent != p) {
			ret = clk_set_parent(c, p);
			if (ret) {
				pr_warning("Unable to set parent %s of clock %s: %d\n",
					table->parent, table->name, ret);
				return -EINVAL;
			}
		}
	}

	if (table->rate && table->rate != clk_get_rate(c)) {
		ret = clk_set_rate(c, table->rate);
		if (ret) {
			pr_warning("Unable to set clock %s to rate %lu: %d\n",
				table->name, table->rate, ret);
			return -EINVAL;
		}
	}

	if (table->enabled) {
		ret = clk_enable(c);
		if (ret) {
			pr_warning("Unable to enable clock %s: %d\n",
				table->name, ret);
			return -EINVAL;
		}
	}

	return 0;
}

void tegra_clk_init_from_table(struct tegra_clk_init_table *table)
{
	for (; table->name; table++)
		tegra_clk_init_one_from_table(table);
}
EXPORT_SYMBOL(tegra_clk_init_from_table);

void tegra_periph_reset_deassert(struct clk *c)
{
	BUG_ON(!c->ops->reset);
	c->ops->reset(c, false);
}
EXPORT_SYMBOL(tegra_periph_reset_deassert);

void tegra_periph_reset_assert(struct clk *c)
{
	BUG_ON(!c->ops->reset);
	c->ops->reset(c, true);
}
EXPORT_SYMBOL(tegra_periph_reset_assert);

/* Several extended clock configuration bits (e.g., clock routing, clock
 * phase control) are included in PLL and peripheral clock source
 * registers. */
int tegra_clk_cfg_ex(struct clk *c, enum tegra_clk_ex_param p, u32 setting)
{
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&c->spinlock, flags);

	if (!c->ops || !c->ops->clk_cfg_ex) {
		ret = -ENOSYS;
		goto out;
	}
	ret = c->ops->clk_cfg_ex(c, p, setting);

out:
	spin_unlock_irqrestore(&c->spinlock, flags);

	return ret;
}

#ifdef CONFIG_DEBUG_FS

static int __clk_lock_all_spinlocks(void)
{
	struct clk *c;

	list_for_each_entry(c, &clocks, node)
		if (!spin_trylock(&c->spinlock))
			goto unlock_spinlocks;

	return 0;

unlock_spinlocks:
	list_for_each_entry_continue_reverse(c, &clocks, node)
		spin_unlock(&c->spinlock);

	return -EAGAIN;
}

static void __clk_unlock_all_spinlocks(void)
{
	struct clk *c;

	list_for_each_entry_reverse(c, &clocks, node)
		spin_unlock(&c->spinlock);
}

/*
 * This function retries until it can take all locks, and may take
 * an arbitrarily long time to complete.
 * Must be called with irqs enabled, returns with irqs disabled
 * Must be called with clock_list_lock held
 */
static void clk_lock_all(void)
{
	int ret;
retry:
	local_irq_disable();

	ret = __clk_lock_all_spinlocks();
	if (ret)
		goto failed_spinlocks;

	/* All locks taken successfully, return */
	return;

failed_spinlocks:
	local_irq_enable();
	yield();
	goto retry;
}

/*
 * Unlocks all clocks after a clk_lock_all
 * Must be called with irqs disabled, returns with irqs enabled
 * Must be called with clock_list_lock held
 */
static void clk_unlock_all(void)
{
	__clk_unlock_all_spinlocks();

	local_irq_enable();
}

static struct dentry *clk_debugfs_root;


static void clock_tree_show_one(struct seq_file *s, struct clk *c, int level)
{
	struct clk *child;
	const char *state = "uninit";
	char div[8] = {0};

	if (c->state == ON)
		state = "on";
	else if (c->state == OFF)
		state = "off";

	if (c->mul != 0 && c->div != 0) {
		if (c->mul > c->div) {
			int mul = c->mul / c->div;
			int mul2 = (c->mul * 10 / c->div) % 10;
			int mul3 = (c->mul * 10) % c->div;
			if (mul2 == 0 && mul3 == 0)
				snprintf(div, sizeof(div), "x%d", mul);
			else if (mul3 == 0)
				snprintf(div, sizeof(div), "x%d.%d", mul, mul2);
			else
				snprintf(div, sizeof(div), "x%d.%d..", mul, mul2);
		} else {
			snprintf(div, sizeof(div), "%d%s", c->div / c->mul,
				(c->div % c->mul) ? ".5" : "");
		}
	}

	seq_printf(s, "%*s%c%c%-*s %-6s %-3d %-8s %-10lu\n",
		level * 3 + 1, "",
		c->rate > c->max_rate ? '!' : ' ',
		!c->set ? '*' : ' ',
		30 - level * 3, c->name,
		state, c->refcnt, div, clk_get_rate_all_locked(c));

	list_for_each_entry(child, &clocks, node) {
		if (child->parent != c)
			continue;

		clock_tree_show_one(s, child, level + 1);
	}
}

static int clock_tree_show(struct seq_file *s, void *data)
{
	struct clk *c;
	seq_printf(s, "   clock                          state  ref div      rate\n");
	seq_printf(s, "--------------------------------------------------------------\n");

	mutex_lock(&clock_list_lock);

	clk_lock_all();

	list_for_each_entry(c, &clocks, node)
		if (c->parent == NULL)
			clock_tree_show_one(s, c, 0);

	clk_unlock_all();

	mutex_unlock(&clock_list_lock);
	return 0;
}

static int clock_tree_open(struct inode *inode, struct file *file)
{
	return single_open(file, clock_tree_show, inode->i_private);
}

static const struct file_operations clock_tree_fops = {
	.open		= clock_tree_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int possible_parents_show(struct seq_file *s, void *data)
{
	struct clk *c = s->private;
	int i;

	for (i = 0; c->inputs[i].input; i++) {
		char *first = (i == 0) ? "" : " ";
		seq_printf(s, "%s%s", first, c->inputs[i].input->name);
	}
	seq_printf(s, "\n");
	return 0;
}

static int possible_parents_open(struct inode *inode, struct file *file)
{
	return single_open(file, possible_parents_show, inode->i_private);
}

static const struct file_operations possible_parents_fops = {
	.open		= possible_parents_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int clk_debugfs_register_one(struct clk *c)
{
	struct dentry *d;

	d = debugfs_create_dir(c->name, clk_debugfs_root);
	if (!d)
		return -ENOMEM;
	c->dent = d;

	d = debugfs_create_u8("refcnt", S_IRUGO, c->dent, (u8 *)&c->refcnt);
	if (!d)
		goto err_out;

	d = debugfs_create_u32("rate", S_IRUGO, c->dent, (u32 *)&c->rate);
	if (!d)
		goto err_out;

	d = debugfs_create_x32("flags", S_IRUGO, c->dent, (u32 *)&c->flags);
	if (!d)
		goto err_out;

	if (c->inputs) {
		d = debugfs_create_file("possible_parents", S_IRUGO, c->dent,
			c, &possible_parents_fops);
		if (!d)
			goto err_out;
	}

	return 0;

err_out:
	debugfs_remove_recursive(c->dent);
	return -ENOMEM;
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

int __init tegra_clk_debugfs_init(void)
{
	struct clk *c;
	struct dentry *d;
	int err = -ENOMEM;

	d = debugfs_create_dir("clock", NULL);
	if (!d)
		return -ENOMEM;
	clk_debugfs_root = d;

	d = debugfs_create_file("clock_tree", S_IRUGO, clk_debugfs_root, NULL,
		&clock_tree_fops);
	if (!d)
		goto err_out;

	list_for_each_entry(c, &clocks, node) {
		err = clk_debugfs_register(c);
		if (err)
			goto err_out;
	}
	return 0;
err_out:
	debugfs_remove_recursive(clk_debugfs_root);
	return err;
}
#endif
#else

void tegra_clk_add(struct clk *clk)
{
	struct clk_tegra *c = to_clk_tegra(__clk_get_hw(clk));

	mutex_lock(&clock_list_lock);
	list_add(&c->node, &clocks);
	mutex_unlock(&clock_list_lock);
}

struct clk *tegra_get_clock_by_name(const char *name)
{
	struct clk_tegra *c;
	struct clk *ret = NULL;
	mutex_lock(&clock_list_lock);
	list_for_each_entry(c, &clocks, node) {
		if (strcmp(__clk_get_name(c->hw.clk), name) == 0) {
			ret = c->hw.clk;
			break;
		}
	}
	mutex_unlock(&clock_list_lock);
	return ret;
}

static int tegra_clk_init_one_from_table(struct tegra_clk_init_table *table)
{
	struct clk *c;
	struct clk *p;
	struct clk *parent;

	int ret = 0;

	c = tegra_get_clock_by_name(table->name);

	if (!c) {
		pr_warn("Unable to initialize clock %s\n",
			table->name);
		return -ENODEV;
	}

	parent = clk_get_parent(c);

	if (table->parent) {
		p = tegra_get_clock_by_name(table->parent);
		if (!p) {
			pr_warn("Unable to find parent %s of clock %s\n",
				table->parent, table->name);
			return -ENODEV;
		}

		if (parent != p) {
			ret = clk_set_parent(c, p);
			if (ret) {
				pr_warn("Unable to set parent %s of clock %s: %d\n",
					table->parent, table->name, ret);
				return -EINVAL;
			}
		}
	}

	if (table->rate && table->rate != clk_get_rate(c)) {
		ret = clk_set_rate(c, table->rate);
		if (ret) {
			pr_warn("Unable to set clock %s to rate %lu: %d\n",
				table->name, table->rate, ret);
			return -EINVAL;
		}
	}

	if (table->enabled) {
		ret = clk_prepare_enable(c);
		if (ret) {
			pr_warn("Unable to enable clock %s: %d\n",
				table->name, ret);
			return -EINVAL;
		}
	}

	return 0;
}

void tegra_clk_init_from_table(struct tegra_clk_init_table *table)
{
	for (; table->name; table++)
		tegra_clk_init_one_from_table(table);
}

void tegra_periph_reset_deassert(struct clk *c)
{
	struct clk_tegra *clk = to_clk_tegra(__clk_get_hw(c));
	BUG_ON(!clk->reset);
	clk->reset(__clk_get_hw(c), false);
}
EXPORT_SYMBOL(tegra_periph_reset_deassert);

void tegra_periph_reset_assert(struct clk *c)
{
	struct clk_tegra *clk = to_clk_tegra(__clk_get_hw(c));
	BUG_ON(!clk->reset);
	clk->reset(__clk_get_hw(c), true);
}
EXPORT_SYMBOL(tegra_periph_reset_assert);

/* Several extended clock configuration bits (e.g., clock routing, clock
 * phase control) are included in PLL and peripheral clock source
 * registers. */
int tegra_clk_cfg_ex(struct clk *c, enum tegra_clk_ex_param p, u32 setting)
{
	int ret = 0;
	struct clk_tegra *clk = to_clk_tegra(__clk_get_hw(c));

	if (!clk->clk_cfg_ex) {
		ret = -ENOSYS;
		goto out;
	}
	ret = clk->clk_cfg_ex(__clk_get_hw(c), p, setting);

out:
	return ret;
}
#endif /* !CONFIG_COMMON_CLK */
