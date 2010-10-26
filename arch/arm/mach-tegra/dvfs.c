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
#include <linux/list_sort.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/regulator/consumer.h>
#include <asm/clkdev.h>
#include <mach/clk.h>

#include "board.h"
#include "clock.h"
#include "dvfs.h"

struct dvfs_reg {
	struct list_head node;  /* node in dvfs_reg_list */
	struct list_head dvfs;  /* list head of attached dvfs clocks */
	const char *reg_id;
	struct regulator *reg;
	int max_millivolts;
	int millivolts;
};

static LIST_HEAD(dvfs_list);
static LIST_HEAD(dvfs_debug_list);
static LIST_HEAD(dvfs_reg_list);

static DEFINE_MUTEX(dvfs_lock);

void lock_dvfs(void)
{
	mutex_lock(&dvfs_lock);
}

void unlock_dvfs(void)
{
	mutex_unlock(&dvfs_lock);
}

static int dvfs_reg_set_voltage(struct dvfs_reg *dvfs_reg)
{
	int millivolts = 0;
	struct dvfs *d;

	list_for_each_entry(d, &dvfs_reg->dvfs, reg_node)
		millivolts = max(d->cur_millivolts, millivolts);

	if (millivolts == dvfs_reg->millivolts)
		return 0;

	dvfs_reg->millivolts = millivolts;

	return regulator_set_voltage(dvfs_reg->reg,
		millivolts * 1000, dvfs_reg->max_millivolts * 1000);
}

static int dvfs_reg_get_voltage(struct dvfs_reg *dvfs_reg)
{
	int ret = regulator_get_voltage(dvfs_reg->reg);

	if (ret > 0)
		return ret / 1000;

	return ret;
}

static struct dvfs_reg *get_dvfs_reg(struct dvfs *d)
{
	struct dvfs_reg *dvfs_reg;
	struct regulator *reg;

	list_for_each_entry(dvfs_reg, &dvfs_reg_list, node)
		if (!strcmp(d->reg_id, dvfs_reg->reg_id))
			return dvfs_reg;

	reg = regulator_get(NULL, d->reg_id);
	if (IS_ERR(reg))
		return NULL;

	dvfs_reg = kzalloc(sizeof(struct dvfs_reg), GFP_KERNEL);
	if (!dvfs_reg) {
		pr_err("%s: Failed to allocate dvfs_reg\n", __func__);
		regulator_put(reg);
		return NULL;
	}

	INIT_LIST_HEAD(&dvfs_reg->dvfs);
	dvfs_reg->reg = reg;
	dvfs_reg->reg_id = kstrdup(d->reg_id, GFP_KERNEL);

	list_add_tail(&dvfs_reg->node, &dvfs_reg_list);

	return dvfs_reg;
}

static struct dvfs_reg *attach_dvfs_reg(struct dvfs *d)
{
	struct dvfs_reg *dvfs_reg;

	dvfs_reg = get_dvfs_reg(d);
	if (!dvfs_reg)
		return NULL;

	list_add_tail(&d->reg_node, &dvfs_reg->dvfs);
	d->dvfs_reg = dvfs_reg;
	if (d->max_millivolts > d->dvfs_reg->max_millivolts)
		d->dvfs_reg->max_millivolts = d->max_millivolts;

	d->cur_millivolts = dvfs_reg_get_voltage(d->dvfs_reg);

	return dvfs_reg;
}

static int
__tegra_dvfs_set_rate(struct clk *c, struct dvfs *d, unsigned long rate)
{
	int i = 0;
	int ret;

	if (d->freqs == NULL || d->millivolts == NULL)
		return -ENODEV;

	if (rate > d->freqs[d->num_freqs - 1]) {
		pr_warn("tegra_dvfs: rate %lu too high for dvfs on %s\n", rate,
			c->name);
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

	if (!d->dvfs_reg)
		return 0;

	ret = dvfs_reg_set_voltage(d->dvfs_reg);
	if (ret)
		pr_err("Failed to set regulator %s for clock %s to %d mV\n",
			d->dvfs_reg->reg_id, c->name, d->cur_millivolts);

	return ret;
}

int tegra_dvfs_set_rate(struct clk *c, unsigned long rate)
{
	struct dvfs *d;
	int ret = 0;
	bool freq_up;

	c->dvfs_rate = rate;

	freq_up = (c->refcnt == 0) || (rate > c->rate);

	list_for_each_entry(d, &c->dvfs, node) {
		if (d->higher == freq_up)
			ret = __tegra_dvfs_set_rate(c, d, rate);
		if (ret)
			return ret;
	}

	list_for_each_entry(d, &c->dvfs, node) {
		if (d->higher != freq_up)
			ret = __tegra_dvfs_set_rate(c, d, rate);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL(tegra_dvfs_set_rate);

int tegra_enable_dvfs_on_clk(struct clk *c, struct dvfs *d)
{
	int i;
	struct dvfs_reg *dvfs_reg;

	dvfs_reg = attach_dvfs_reg(d);
	if (!dvfs_reg) {
		pr_err("Failed to get regulator %s for clock %s\n",
			d->reg_id, c->name);
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

	if (d->auto_dvfs)
		c->auto_dvfs = true;

	c->is_dvfs = true;
	smp_wmb();

	list_add_tail(&d->node, &c->dvfs);

	list_add_tail(&d->debug_node, &dvfs_debug_list);

	return 0;
}

int __init tegra_init_dvfs(void)
{
	lock_dvfs();
	tegra2_init_dvfs();

	tegra_clk_set_dvfs_rates();
	unlock_dvfs();

	return 0;
}
late_initcall(tegra_init_dvfs);

#ifdef CONFIG_DEBUG_FS
static int dvfs_tree_sort_cmp(void *p, struct list_head *a, struct list_head *b)
{
	struct dvfs *da = list_entry(a, struct dvfs, debug_node);
	struct dvfs *db = list_entry(b, struct dvfs, debug_node);
	int ret;

	ret = strcmp(da->reg_id, db->reg_id);
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
	const char *last_reg = "";

	seq_printf(s, "   clock      rate       mV\n");
	seq_printf(s, "--------------------------------\n");

	lock_dvfs();

	list_sort(NULL, &dvfs_debug_list, dvfs_tree_sort_cmp);

	list_for_each_entry(d, &dvfs_debug_list, debug_node) {
		if (strcmp(last_reg, d->dvfs_reg->reg_id) != 0) {
			last_reg = d->dvfs_reg->reg_id;
			seq_printf(s, "%s %d mV:\n", d->dvfs_reg->reg_id,
				d->dvfs_reg->millivolts);
		}

		seq_printf(s, "   %-10s %-10lu %-4d mV\n", d->clk_name,
			d->cur_rate, d->cur_millivolts);
	}

	unlock_dvfs();

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
