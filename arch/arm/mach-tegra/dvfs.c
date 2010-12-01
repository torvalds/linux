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
#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/list_sort.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <linux/delay.h>

#include <asm/clkdev.h>

#include <mach/clk.h>

#include "board.h"
#include "clock.h"
#include "dvfs.h"

static LIST_HEAD(dvfs_rail_list);
static DEFINE_MUTEX(dvfs_lock);

static int dvfs_rail_update(struct dvfs_rail *rail);

void tegra_dvfs_add_relationships(struct dvfs_relationship *rels, int n)
{
	int i;
	struct dvfs_relationship *rel;

	mutex_lock(&dvfs_lock);

	for (i = 0; i < n; i++) {
		rel = &rels[i];
		list_add_tail(&rel->from_node, &rel->to->relationships_from);
		list_add_tail(&rel->to_node, &rel->from->relationships_to);
	}

	mutex_unlock(&dvfs_lock);
}

int tegra_dvfs_init_rails(struct dvfs_rail *rails[], int n)
{
	int i;

	mutex_lock(&dvfs_lock);

	for (i = 0; i < n; i++) {
		INIT_LIST_HEAD(&rails[i]->dvfs);
		INIT_LIST_HEAD(&rails[i]->relationships_from);
		INIT_LIST_HEAD(&rails[i]->relationships_to);
		rails[i]->millivolts = rails[i]->nominal_millivolts;
		rails[i]->new_millivolts = rails[i]->nominal_millivolts;
		if (!rails[i]->step)
			rails[i]->step = rails[i]->max_millivolts;

		list_add_tail(&rails[i]->node, &dvfs_rail_list);
	}

	mutex_unlock(&dvfs_lock);

	return 0;
};

static int dvfs_solve_relationship(struct dvfs_relationship *rel)
{
	return rel->solve(rel->from, rel->to);
}

/* Sets the voltage on a dvfs rail to a specific value, and updates any
 * rails that depend on this rail. */
static int dvfs_rail_set_voltage(struct dvfs_rail *rail, int millivolts)
{
	int ret = 0;
	struct dvfs_relationship *rel;
	int step = (millivolts > rail->millivolts) ? rail->step : -rail->step;
	int i;
	int steps;

	if (!rail->reg) {
		if (millivolts == rail->millivolts)
			return 0;
		else
			return -EINVAL;
	}

	if (rail->disabled)
		return 0;

	steps = DIV_ROUND_UP(abs(millivolts - rail->millivolts), rail->step);

	for (i = 0; i < steps; i++) {
		if (abs(millivolts - rail->millivolts) > rail->step)
			rail->new_millivolts = rail->millivolts + step;
		else
			rail->new_millivolts = millivolts;

		/* Before changing the voltage, tell each rail that depends
		 * on this rail that the voltage will change.
		 * This rail will be the "from" rail in the relationship,
		 * the rail that depends on this rail will be the "to" rail.
		 * from->millivolts will be the old voltage
		 * from->new_millivolts will be the new voltage */
		list_for_each_entry(rel, &rail->relationships_to, to_node) {
			ret = dvfs_rail_update(rel->to);
			if (ret)
				return ret;
		}

		if (!rail->disabled) {
			ret = regulator_set_voltage(rail->reg,
				rail->new_millivolts * 1000,
				rail->max_millivolts * 1000);
		}
		if (ret) {
			pr_err("Failed to set dvfs regulator %s\n", rail->reg_id);
			return ret;
		}

		rail->millivolts = rail->new_millivolts;

		/* After changing the voltage, tell each rail that depends
		 * on this rail that the voltage has changed.
		 * from->millivolts and from->new_millivolts will be the
		 * new voltage */
		list_for_each_entry(rel, &rail->relationships_to, to_node) {
			ret = dvfs_rail_update(rel->to);
			if (ret)
				return ret;
		}
	}

	if (unlikely(rail->millivolts != millivolts)) {
		pr_err("%s: rail didn't reach target %d in %d steps (%d)\n",
			__func__, millivolts, steps, rail->millivolts);
		return -EINVAL;
	}

	return ret;
}

/* Determine the minimum valid voltage for a rail, taking into account
 * the dvfs clocks and any rails that this rail depends on.  Calls
 * dvfs_rail_set_voltage with the new voltage, which will call
 * dvfs_rail_update on any rails that depend on this rail. */
static int dvfs_rail_update(struct dvfs_rail *rail)
{
	int millivolts = 0;
	struct dvfs *d;
	struct dvfs_relationship *rel;
	int ret = 0;

	/* if dvfs is suspended, return and handle it during resume */
	if (rail->suspended)
		return 0;

	/* if regulators are not connected yet, return and handle it later */
	if (!rail->reg)
		return 0;

	/* Find the maximum voltage requested by any clock */
	list_for_each_entry(d, &rail->dvfs, reg_node)
		millivolts = max(d->cur_millivolts, millivolts);

	rail->new_millivolts = millivolts;

	/* Check any rails that this rail depends on */
	list_for_each_entry(rel, &rail->relationships_from, from_node)
		rail->new_millivolts = dvfs_solve_relationship(rel);

	if (rail->new_millivolts != rail->millivolts)
		ret = dvfs_rail_set_voltage(rail, rail->new_millivolts);

	return ret;
}

static int dvfs_rail_connect_to_regulator(struct dvfs_rail *rail)
{
	struct regulator *reg;

	if (!rail->reg) {
		reg = regulator_get(NULL, rail->reg_id);
		if (IS_ERR(reg))
			return -EINVAL;
	}

	rail->reg = reg;

	return 0;
}

static int
__tegra_dvfs_set_rate(struct dvfs *d, unsigned long rate)
{
	int i = 0;
	int ret;

	if (d->freqs == NULL || d->millivolts == NULL)
		return -ENODEV;

	if (rate > d->freqs[d->num_freqs - 1]) {
		pr_warn("tegra_dvfs: rate %lu too high for dvfs on %s\n", rate,
			d->clk_name);
		return -EINVAL;
	}

	if (rate == 0) {
		d->cur_millivolts = 0;
	} else {
		while (i < d->num_freqs && rate > d->freqs[i])
			i++;

		d->cur_millivolts = d->millivolts[i];
	}

	d->cur_rate = rate;

	ret = dvfs_rail_update(d->dvfs_rail);
	if (ret)
		pr_err("Failed to set regulator %s for clock %s to %d mV\n",
			d->dvfs_rail->reg_id, d->clk_name, d->cur_millivolts);

	return ret;
}

int tegra_dvfs_set_rate(struct clk *c, unsigned long rate)
{
	int ret;

	if (!c->dvfs)
		return -EINVAL;

	mutex_lock(&dvfs_lock);
	ret = __tegra_dvfs_set_rate(c->dvfs, rate);
	mutex_unlock(&dvfs_lock);

	return ret;
}
EXPORT_SYMBOL(tegra_dvfs_set_rate);

/* May only be called during clock init, does not take any locks on clock c. */
int __init tegra_enable_dvfs_on_clk(struct clk *c, struct dvfs *d)
{
	int i;

	if (c->dvfs) {
		pr_err("Error when enabling dvfs on %s for clock %s:\n",
			d->dvfs_rail->reg_id, c->name);
		pr_err("DVFS already enabled for %s\n",
			c->dvfs->dvfs_rail->reg_id);
		return -EINVAL;
	}

	for (i = 0; i < MAX_DVFS_FREQS; i++) {
		if (d->millivolts[i] == 0)
			break;

		d->freqs[i] *= d->freqs_mult;

		/* If final frequencies are 0, pad with previous frequency */
		if (d->freqs[i] == 0 && i > 1)
			d->freqs[i] = d->freqs[i - 1];
	}
	d->num_freqs = i;

	if (d->auto_dvfs) {
		c->auto_dvfs = true;
		clk_set_cansleep(c);
	}

	c->dvfs = d;

	mutex_lock(&dvfs_lock);
	list_add_tail(&d->reg_node, &d->dvfs_rail->dvfs);
	mutex_unlock(&dvfs_lock);

	return 0;
}

static bool tegra_dvfs_all_rails_suspended(void)
{
	struct dvfs_rail *rail;
	bool all_suspended = true;

	list_for_each_entry(rail, &dvfs_rail_list, node)
		if (!rail->suspended && !rail->disabled)
			all_suspended = false;

	return all_suspended;
}

static bool tegra_dvfs_from_rails_suspended(struct dvfs_rail *to)
{
	struct dvfs_relationship *rel;
	bool all_suspended = true;

	list_for_each_entry(rel, &to->relationships_from, from_node)
		if (!rel->from->suspended && !rel->from->disabled)
			all_suspended = false;

	return all_suspended;
}

static int tegra_dvfs_suspend_one(void)
{
	struct dvfs_rail *rail;
	int ret;

	list_for_each_entry(rail, &dvfs_rail_list, node) {
		if (!rail->suspended && !rail->disabled &&
		    tegra_dvfs_from_rails_suspended(rail)) {
			ret = dvfs_rail_set_voltage(rail,
				rail->nominal_millivolts);
			if (ret)
				return ret;
			rail->suspended = true;
			return 0;
		}
	}

	return -EINVAL;
}

static void tegra_dvfs_resume(void)
{
	struct dvfs_rail *rail;

	mutex_lock(&dvfs_lock);

	list_for_each_entry(rail, &dvfs_rail_list, node)
		rail->suspended = false;

	list_for_each_entry(rail, &dvfs_rail_list, node)
		dvfs_rail_update(rail);

	mutex_unlock(&dvfs_lock);
}

static int tegra_dvfs_suspend(void)
{
	int ret = 0;

	mutex_lock(&dvfs_lock);

	while (!tegra_dvfs_all_rails_suspended()) {
		ret = tegra_dvfs_suspend_one();
		if (ret)
			break;
	}

	mutex_unlock(&dvfs_lock);

	if (ret)
		tegra_dvfs_resume();

	return ret;
}

static int tegra_dvfs_pm_notify(struct notifier_block *nb,
				unsigned long event, void *data)
{
	switch (event) {
	case PM_SUSPEND_PREPARE:
		if (tegra_dvfs_suspend())
			return NOTIFY_STOP;
		break;
	case PM_POST_SUSPEND:
		tegra_dvfs_resume();
		break;
	}

	return NOTIFY_OK;
};

static struct notifier_block tegra_dvfs_nb = {
	.notifier_call = tegra_dvfs_pm_notify,
};

/* must be called with dvfs lock held */
static void __tegra_dvfs_rail_disable(struct dvfs_rail *rail)
{
	int ret;

	if (!rail->disabled) {
		ret = dvfs_rail_set_voltage(rail, rail->nominal_millivolts);
		if (ret)
			pr_info("dvfs: failed to set regulator %s to disable "
				"voltage %d\n", rail->reg_id,
				rail->nominal_millivolts);
		rail->disabled = true;
	}
}

/* must be called with dvfs lock held */
static void __tegra_dvfs_rail_enable(struct dvfs_rail *rail)
{
	if (rail->disabled) {
		rail->disabled = false;
		dvfs_rail_update(rail);
	}
}

void tegra_dvfs_rail_enable(struct dvfs_rail *rail)
{
	mutex_lock(&dvfs_lock);
	__tegra_dvfs_rail_enable(rail);
	mutex_unlock(&dvfs_lock);
}

void tegra_dvfs_rail_disable(struct dvfs_rail *rail)
{
	mutex_lock(&dvfs_lock);
	__tegra_dvfs_rail_disable(rail);
	mutex_unlock(&dvfs_lock);
}

int tegra_dvfs_rail_disable_by_name(const char *reg_id)
{
	struct dvfs_rail *rail;
	int ret = 0;

	mutex_lock(&dvfs_lock);
	list_for_each_entry(rail, &dvfs_rail_list, node) {
		if (!strcmp(reg_id, rail->reg_id)) {
			__tegra_dvfs_rail_disable(rail);
			goto out;
		}
	}

	ret = -EINVAL;

out:
	mutex_unlock(&dvfs_lock);
	return ret;
}

/*
 * Iterate through all the dvfs regulators, finding the regulator exported
 * by the regulator api for each one.  Must be called in late init, after
 * all the regulator api's regulators are initialized.
 */
int __init tegra_dvfs_late_init(void)
{
	struct dvfs_rail *rail;

	mutex_lock(&dvfs_lock);

	list_for_each_entry(rail, &dvfs_rail_list, node)
		dvfs_rail_connect_to_regulator(rail);

	list_for_each_entry(rail, &dvfs_rail_list, node)
		dvfs_rail_update(rail);

	mutex_unlock(&dvfs_lock);

	register_pm_notifier(&tegra_dvfs_nb);

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static int dvfs_tree_sort_cmp(void *p, struct list_head *a, struct list_head *b)
{
	struct dvfs *da = list_entry(a, struct dvfs, reg_node);
	struct dvfs *db = list_entry(b, struct dvfs, reg_node);
	int ret;

	ret = strcmp(da->dvfs_rail->reg_id, db->dvfs_rail->reg_id);
	if (ret != 0)
		return ret;

	if (da->cur_millivolts < db->cur_millivolts)
		return 1;
	if (da->cur_millivolts > db->cur_millivolts)
		return -1;

	return strcmp(da->clk_name, db->clk_name);
}

static int dvfs_tree_show(struct seq_file *s, void *data)
{
	struct dvfs *d;
	struct dvfs_rail *rail;
	struct dvfs_relationship *rel;

	seq_printf(s, "   clock      rate       mV\n");
	seq_printf(s, "--------------------------------\n");

	mutex_lock(&dvfs_lock);

	list_for_each_entry(rail, &dvfs_rail_list, node) {
		seq_printf(s, "%s %d mV%s:\n", rail->reg_id,
			rail->millivolts, rail->disabled ? " disabled" : "");
		list_for_each_entry(rel, &rail->relationships_from, from_node) {
			seq_printf(s, "   %-10s %-7d mV %-4d mV\n",
				rel->from->reg_id,
				rel->from->millivolts,
				dvfs_solve_relationship(rel));
		}

		list_sort(NULL, &rail->dvfs, dvfs_tree_sort_cmp);

		list_for_each_entry(d, &rail->dvfs, reg_node) {
			seq_printf(s, "   %-10s %-10lu %-4d mV\n", d->clk_name,
				d->cur_rate, d->cur_millivolts);
		}
	}

	mutex_unlock(&dvfs_lock);

	return 0;
}

static int dvfs_tree_open(struct inode *inode, struct file *file)
{
	return single_open(file, dvfs_tree_show, inode->i_private);
}

static const struct file_operations dvfs_tree_fops = {
	.open		= dvfs_tree_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

int __init dvfs_debugfs_init(struct dentry *clk_debugfs_root)
{
	struct dentry *d;

	d = debugfs_create_file("dvfs", S_IRUGO, clk_debugfs_root, NULL,
		&dvfs_tree_fops);
	if (!d)
		return -ENOMEM;

	return 0;
}

#endif
