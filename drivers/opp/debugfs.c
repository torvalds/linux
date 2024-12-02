// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic OPP debugfs interface
 *
 * Copyright (C) 2015-2016 Viresh Kumar <viresh.kumar@linaro.org>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/init.h>
#include <linux/limits.h>
#include <linux/slab.h>

#include "opp.h"

static struct dentry *rootdir;

static void opp_set_dev_name(const struct device *dev, char *name)
{
	if (dev->parent)
		snprintf(name, NAME_MAX, "%s-%s", dev_name(dev->parent),
			 dev_name(dev));
	else
		snprintf(name, NAME_MAX, "%s", dev_name(dev));
}

void opp_debug_remove_one(struct dev_pm_opp *opp)
{
	debugfs_remove_recursive(opp->dentry);
}

static ssize_t bw_name_read(struct file *fp, char __user *userbuf,
			    size_t count, loff_t *ppos)
{
	struct icc_path *path = fp->private_data;
	const char *name = icc_get_name(path);
	char buf[64];
	int i = 0;

	if (name)
		i = scnprintf(buf, sizeof(buf), "%.62s\n", name);

	return simple_read_from_buffer(userbuf, count, ppos, buf, i);
}

static const struct file_operations bw_name_fops = {
	.open = simple_open,
	.read = bw_name_read,
	.llseek = default_llseek,
};

static void opp_debug_create_bw(struct dev_pm_opp *opp,
				struct opp_table *opp_table,
				struct dentry *pdentry)
{
	struct dentry *d;
	char name[11];
	int i;

	for (i = 0; i < opp_table->path_count; i++) {
		snprintf(name, sizeof(name), "icc-path-%.1d", i);

		/* Create per-path directory */
		d = debugfs_create_dir(name, pdentry);

		debugfs_create_file("name", S_IRUGO, d, opp_table->paths[i],
				    &bw_name_fops);
		debugfs_create_u32("peak_bw", S_IRUGO, d,
				   &opp->bandwidth[i].peak);
		debugfs_create_u32("avg_bw", S_IRUGO, d,
				   &opp->bandwidth[i].avg);
	}
}

static void opp_debug_create_clks(struct dev_pm_opp *opp,
				  struct opp_table *opp_table,
				  struct dentry *pdentry)
{
	char name[12];
	int i;

	if (opp_table->clk_count == 1) {
		debugfs_create_ulong("rate_hz", S_IRUGO, pdentry, &opp->rates[0]);
		return;
	}

	for (i = 0; i < opp_table->clk_count; i++) {
		snprintf(name, sizeof(name), "rate_hz_%d", i);
		debugfs_create_ulong(name, S_IRUGO, pdentry, &opp->rates[i]);
	}
}

static void opp_debug_create_supplies(struct dev_pm_opp *opp,
				      struct opp_table *opp_table,
				      struct dentry *pdentry)
{
	struct dentry *d;
	int i;

	for (i = 0; i < opp_table->regulator_count; i++) {
		char name[15];

		snprintf(name, sizeof(name), "supply-%d", i);

		/* Create per-opp directory */
		d = debugfs_create_dir(name, pdentry);

		debugfs_create_ulong("u_volt_target", S_IRUGO, d,
				     &opp->supplies[i].u_volt);

		debugfs_create_ulong("u_volt_min", S_IRUGO, d,
				     &opp->supplies[i].u_volt_min);

		debugfs_create_ulong("u_volt_max", S_IRUGO, d,
				     &opp->supplies[i].u_volt_max);

		debugfs_create_ulong("u_amp", S_IRUGO, d,
				     &opp->supplies[i].u_amp);

		debugfs_create_ulong("u_watt", S_IRUGO, d,
				     &opp->supplies[i].u_watt);
	}
}

void opp_debug_create_one(struct dev_pm_opp *opp, struct opp_table *opp_table)
{
	struct dentry *pdentry = opp_table->dentry;
	struct dentry *d;
	unsigned long id;
	char name[25];	/* 20 chars for 64 bit value + 5 (opp:\0) */

	/*
	 * Get directory name for OPP.
	 *
	 * - Normally rate is unique to each OPP, use it to get unique opp-name.
	 * - For some devices rate isn't available or there are multiple, use
	 *   index instead for them.
	 */
	if (likely(opp_table->clk_count == 1 && opp->rates[0]))
		id = opp->rates[0];
	else
		id = _get_opp_count(opp_table);

	snprintf(name, sizeof(name), "opp:%lu", id);

	/* Create per-opp directory */
	d = debugfs_create_dir(name, pdentry);

	debugfs_create_bool("available", S_IRUGO, d, &opp->available);
	debugfs_create_bool("dynamic", S_IRUGO, d, &opp->dynamic);
	debugfs_create_bool("turbo", S_IRUGO, d, &opp->turbo);
	debugfs_create_bool("suspend", S_IRUGO, d, &opp->suspend);
	debugfs_create_u32("performance_state", S_IRUGO, d, &opp->pstate);
	debugfs_create_u32("level", S_IRUGO, d, &opp->level);
	debugfs_create_ulong("clock_latency_ns", S_IRUGO, d,
			     &opp->clock_latency_ns);

	opp->of_name = of_node_full_name(opp->np);
	debugfs_create_str("of_name", S_IRUGO, d, (char **)&opp->of_name);

	opp_debug_create_clks(opp, opp_table, d);
	opp_debug_create_supplies(opp, opp_table, d);
	opp_debug_create_bw(opp, opp_table, d);

	opp->dentry = d;
}

static void opp_list_debug_create_dir(struct opp_device *opp_dev,
				      struct opp_table *opp_table)
{
	const struct device *dev = opp_dev->dev;
	struct dentry *d;

	opp_set_dev_name(dev, opp_table->dentry_name);

	/* Create device specific directory */
	d = debugfs_create_dir(opp_table->dentry_name, rootdir);

	opp_dev->dentry = d;
	opp_table->dentry = d;
}

static void opp_list_debug_create_link(struct opp_device *opp_dev,
				       struct opp_table *opp_table)
{
	char name[NAME_MAX];

	opp_set_dev_name(opp_dev->dev, name);

	/* Create device specific directory link */
	opp_dev->dentry = debugfs_create_symlink(name, rootdir,
						 opp_table->dentry_name);
}

/**
 * opp_debug_register - add a device opp node to the debugfs 'opp' directory
 * @opp_dev: opp-dev pointer for device
 * @opp_table: the device-opp being added
 *
 * Dynamically adds device specific directory in debugfs 'opp' directory. If the
 * device-opp is shared with other devices, then links will be created for all
 * devices except the first.
 */
void opp_debug_register(struct opp_device *opp_dev, struct opp_table *opp_table)
{
	if (opp_table->dentry)
		opp_list_debug_create_link(opp_dev, opp_table);
	else
		opp_list_debug_create_dir(opp_dev, opp_table);
}

static void opp_migrate_dentry(struct opp_device *opp_dev,
			       struct opp_table *opp_table)
{
	struct opp_device *new_dev = NULL, *iter;
	const struct device *dev;
	struct dentry *dentry;

	/* Look for next opp-dev */
	list_for_each_entry(iter, &opp_table->dev_list, node)
		if (iter != opp_dev) {
			new_dev = iter;
			break;
		}

	BUG_ON(!new_dev);

	/* new_dev is guaranteed to be valid here */
	dev = new_dev->dev;
	debugfs_remove_recursive(new_dev->dentry);

	opp_set_dev_name(dev, opp_table->dentry_name);

	dentry = debugfs_rename(rootdir, opp_dev->dentry, rootdir,
				opp_table->dentry_name);
	if (IS_ERR(dentry)) {
		dev_err(dev, "%s: Failed to rename link from: %s to %s\n",
			__func__, dev_name(opp_dev->dev), dev_name(dev));
		return;
	}

	new_dev->dentry = dentry;
	opp_table->dentry = dentry;
}

/**
 * opp_debug_unregister - remove a device opp node from debugfs opp directory
 * @opp_dev: opp-dev pointer for device
 * @opp_table: the device-opp being removed
 *
 * Dynamically removes device specific directory from debugfs 'opp' directory.
 */
void opp_debug_unregister(struct opp_device *opp_dev,
			  struct opp_table *opp_table)
{
	if (opp_dev->dentry == opp_table->dentry) {
		/* Move the real dentry object under another device */
		if (!list_is_singular(&opp_table->dev_list)) {
			opp_migrate_dentry(opp_dev, opp_table);
			goto out;
		}
		opp_table->dentry = NULL;
	}

	debugfs_remove_recursive(opp_dev->dentry);

out:
	opp_dev->dentry = NULL;
}

static int __init opp_debug_init(void)
{
	/* Create /sys/kernel/debug/opp directory */
	rootdir = debugfs_create_dir("opp", NULL);

	return 0;
}
core_initcall(opp_debug_init);
