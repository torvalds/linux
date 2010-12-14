/*
 *
 * Copyright (C) 2010 Google, Inc.
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
#include <linux/list.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/regulator/consumer.h>
#include <asm/clkdev.h>

#include "clock.h"
#include "board.h"
#include "fuse.h"

static LIST_HEAD(clocks);

static DEFINE_SPINLOCK(clock_lock);
static DEFINE_MUTEX(dvfs_lock);

static int clk_is_dvfs(struct clk *c)
{
	return (c->dvfs != NULL);
};

static int dvfs_set_rate(struct dvfs *d, unsigned long rate)
{
	struct dvfs_table *t;

	if (d->table == NULL)
		return -ENODEV;

	for (t = d->table; t->rate != 0; t++) {
		if (rate <= t->rate) {
			if (!d->reg)
				return 0;

			return regulator_set_voltage(d->reg,
				t->millivolts * 1000,
				d->max_millivolts * 1000);
		}
	}

	return -EINVAL;
}

static void dvfs_init(struct clk *c)
{
	int process_id;
	int i;
	struct dvfs_table *table;

	process_id = c->dvfs->cpu ? tegra_core_process_id() :
		tegra_cpu_process_id();

	for (i = 0; i < c->dvfs->process_id_table_length; i++)
		if (process_id == c->dvfs->process_id_table[i].process_id)
			c->dvfs->table = c->dvfs->process_id_table[i].table;

	if (c->dvfs->table == NULL) {
		pr_err("Failed to find dvfs table for clock %s process %d\n",
			c->name, process_id);
		return;
	}

	c->dvfs->max_millivolts = 0;
	for (table = c->dvfs->table; table->rate != 0; table++)
		if (c->dvfs->max_millivolts < table->millivolts)
			c->dvfs->max_millivolts = table->millivolts;

	c->dvfs->reg = regulator_get(NULL, c->dvfs->reg_id);

	if (IS_ERR(c->dvfs->reg)) {
		pr_err("Failed to get regulator %s for clock %s\n",
			c->dvfs->reg_id, c->name);
		c->dvfs->reg = NULL;
		return;
	}

	if (c->refcnt > 0)
		dvfs_set_rate(c->dvfs, c->rate);
}

struct clk *tegra_get_clock_by_name(const char *name)
{
	struct clk *c;
	struct clk *ret = NULL;
	unsigned long flags;
	spin_lock_irqsave(&clock_lock, flags);
	list_for_each_entry(c, &clocks, node) {
		if (strcmp(c->name, name) == 0) {
			ret = c;
			break;
		}
	}
	spin_unlock_irqrestore(&clock_lock, flags);
	return ret;
}

static void clk_recalculate_rate(struct clk *c)
{
	u64 rate;

	if (!c->parent)
		return;

	rate = c->parent->rate;

	if (c->mul != 0 && c->div != 0) {
		rate = rate * c->mul;
		do_div(rate, c->div);
	}

	if (rate > c->max_rate)
		pr_warn("clocks: Set clock %s to rate %llu, max is %lu\n",
			c->name, rate, c->max_rate);

	c->rate = rate;
}

int clk_reparent(struct clk *c, struct clk *parent)
{
	pr_debug("%s: %s\n", __func__, c->name);
	c->parent = parent;
	list_del(&c->sibling);
	list_add_tail(&c->sibling, &parent->children);
	return 0;
}

static void propagate_rate(struct clk *c)
{
	struct clk *clkp;
	pr_debug("%s: %s\n", __func__, c->name);
	list_for_each_entry(clkp, &c->children, sibling) {
		pr_debug("   %s\n", clkp->name);
		clk_recalculate_rate(clkp);
		propagate_rate(clkp);
	}
}

void clk_init(struct clk *c)
{
	unsigned long flags;

	pr_debug("%s: %s\n", __func__, c->name);

	spin_lock_irqsave(&clock_lock, flags);

	INIT_LIST_HEAD(&c->children);
	INIT_LIST_HEAD(&c->sibling);

	if (c->ops && c->ops->init)
		c->ops->init(c);

	clk_recalculate_rate(c);

	list_add(&c->node, &clocks);

	if (c->parent)
		list_add_tail(&c->sibling, &c->parent->children);

	spin_unlock_irqrestore(&clock_lock, flags);
}

int clk_enable_locked(struct clk *c)
{
	int ret;
	pr_debug("%s: %s\n", __func__, c->name);
	if (c->refcnt == 0) {
		if (c->parent) {
			ret = clk_enable_locked(c->parent);
			if (ret)
				return ret;
		}

		if (c->ops && c->ops->enable) {
			ret = c->ops->enable(c);
			if (ret) {
				if (c->parent)
					clk_disable_locked(c->parent);
				return ret;
			}
			c->state = ON;
#ifdef CONFIG_DEBUG_FS
			c->set = 1;
#endif
		}
	}
	c->refcnt++;

	return 0;
}

int clk_enable_cansleep(struct clk *c)
{
	int ret;
	unsigned long flags;

	mutex_lock(&dvfs_lock);

	if (clk_is_dvfs(c) && c->refcnt > 0)
		dvfs_set_rate(c->dvfs, c->rate);

	spin_lock_irqsave(&clock_lock, flags);
	ret = clk_enable_locked(c);
	spin_unlock_irqrestore(&clock_lock, flags);

	mutex_unlock(&dvfs_lock);

	return ret;
}
EXPORT_SYMBOL(clk_enable_cansleep);

int clk_enable(struct clk *c)
{
	int ret;
	unsigned long flags;

	if (clk_is_dvfs(c))
		BUG();

	spin_lock_irqsave(&clock_lock, flags);
	ret = clk_enable_locked(c);
	spin_unlock_irqrestore(&clock_lock, flags);

	return ret;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable_locked(struct clk *c)
{
	pr_debug("%s: %s\n", __func__, c->name);
	if (c->refcnt == 0) {
		WARN(1, "Attempting to disable clock %s with refcnt 0", c->name);
		return;
	}
	if (c->refcnt == 1) {
		if (c->ops && c->ops->disable)
			c->ops->disable(c);

		if (c->parent)
			clk_disable_locked(c->parent);

		c->state = OFF;
	}
	c->refcnt--;
}

void clk_disable_cansleep(struct clk *c)
{
	unsigned long flags;

	mutex_lock(&dvfs_lock);

	spin_lock_irqsave(&clock_lock, flags);
	clk_disable_locked(c);
	spin_unlock_irqrestore(&clock_lock, flags);

	if (clk_is_dvfs(c) && c->refcnt == 0)
		dvfs_set_rate(c->dvfs, c->rate);

	mutex_unlock(&dvfs_lock);
}
EXPORT_SYMBOL(clk_disable_cansleep);

void clk_disable(struct clk *c)
{
	unsigned long flags;

	if (clk_is_dvfs(c))
		BUG();

	spin_lock_irqsave(&clock_lock, flags);
	clk_disable_locked(c);
	spin_unlock_irqrestore(&clock_lock, flags);
}
EXPORT_SYMBOL(clk_disable);

int clk_set_parent_locked(struct clk *c, struct clk *parent)
{
	int ret;

	pr_debug("%s: %s\n", __func__, c->name);

	if (!c->ops || !c->ops->set_parent)
		return -ENOSYS;

	ret = c->ops->set_parent(c, parent);

	if (ret)
		return ret;

	clk_recalculate_rate(c);

	propagate_rate(c);

	return 0;
}

int clk_set_parent(struct clk *c, struct clk *parent)
{
	int ret;
	unsigned long flags;
	spin_lock_irqsave(&clock_lock, flags);
	ret = clk_set_parent_locked(c, parent);
	spin_unlock_irqrestore(&clock_lock, flags);
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
	int ret;

	if (rate > c->max_rate)
		rate = c->max_rate;

	if (!c->ops || !c->ops->set_rate)
		return -ENOSYS;

	ret = c->ops->set_rate(c, rate);

	if (ret)
		return ret;

	clk_recalculate_rate(c);

	propagate_rate(c);

	return 0;
}

int clk_set_rate_cansleep(struct clk *c, unsigned long rate)
{
	int ret = 0;
	unsigned long flags;

	pr_debug("%s: %s\n", __func__, c->name);

	mutex_lock(&dvfs_lock);

	if (rate > c->rate)
		ret = dvfs_set_rate(c->dvfs, rate);
	if (ret)
		goto out;

	spin_lock_irqsave(&clock_lock, flags);
	ret = clk_set_rate_locked(c, rate);
	spin_unlock_irqrestore(&clock_lock, flags);

	if (ret)
		goto out;

	ret = dvfs_set_rate(c->dvfs, rate);

out:
	mutex_unlock(&dvfs_lock);
	return ret;
}
EXPORT_SYMBOL(clk_set_rate_cansleep);

int clk_set_rate(struct clk *c, unsigned long rate)
{
	int ret = 0;
	unsigned long flags;

	pr_debug("%s: %s\n", __func__, c->name);

	if (clk_is_dvfs(c))
		BUG();

	spin_lock_irqsave(&clock_lock, flags);
	ret = clk_set_rate_locked(c, rate);
	spin_unlock_irqrestore(&clock_lock, flags);

	return ret;
}
EXPORT_SYMBOL(clk_set_rate);

unsigned long clk_get_rate(struct clk *c)
{
	unsigned long flags;
	unsigned long ret;

	spin_lock_irqsave(&clock_lock, flags);

	pr_debug("%s: %s\n", __func__, c->name);

	ret = c->rate;

	spin_unlock_irqrestore(&clock_lock, flags);
	return ret;
}
EXPORT_SYMBOL(clk_get_rate);

long clk_round_rate(struct clk *c, unsigned long rate)
{
	pr_debug("%s: %s\n", __func__, c->name);

	if (!c->ops || !c->ops->round_rate)
		return -ENOSYS;

	if (rate > c->max_rate)
		rate = c->max_rate;

	return c->ops->round_rate(c, rate);
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
	tegra2_periph_reset_deassert(c);
}
EXPORT_SYMBOL(tegra_periph_reset_deassert);

void tegra_periph_reset_assert(struct clk *c)
{
	tegra2_periph_reset_assert(c);
}
EXPORT_SYMBOL(tegra_periph_reset_assert);

void __init tegra_init_clock(void)
{
	tegra2_init_clocks();
}

int __init tegra_init_dvfs(void)
{
	struct clk *c, *safe;

	mutex_lock(&dvfs_lock);

	list_for_each_entry_safe(c, safe, &clocks, node)
		if (c->dvfs)
			dvfs_init(c);

	mutex_unlock(&dvfs_lock);

	return 0;
}

late_initcall(tegra_init_dvfs);

#ifdef CONFIG_DEBUG_FS
static struct dentry *clk_debugfs_root;


static void clock_tree_show_one(struct seq_file *s, struct clk *c, int level)
{
	struct clk *child;
	struct clk *safe;
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
		state, c->refcnt, div, c->rate);
	list_for_each_entry_safe(child, safe, &c->children, sibling) {
		clock_tree_show_one(s, child, level + 1);
	}
}

static int clock_tree_show(struct seq_file *s, void *data)
{
	struct clk *c;
	unsigned long flags;
	seq_printf(s, "   clock                          state  ref div      rate\n");
	seq_printf(s, "--------------------------------------------------------------\n");
	spin_lock_irqsave(&clock_lock, flags);
	list_for_each_entry(c, &clocks, node)
		if (c->parent == NULL)
			clock_tree_show_one(s, c, 0);
	spin_unlock_irqrestore(&clock_lock, flags);
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
	struct dentry *d, *child, *child_tmp;

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
	d = c->dent;
	list_for_each_entry_safe(child, child_tmp, &d->d_subdirs, d_u.d_child)
		debugfs_remove(child);
	debugfs_remove(c->dent);
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

static int __init clk_debugfs_init(void)
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

late_initcall(clk_debugfs_init);
#endif
