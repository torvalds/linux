/*
 * Generic OPP debugfs interface
 *
 * Copyright (C) 2015-2016 Viresh Kumar <viresh.kumar@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/limits.h>

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

int opp_debug_create_one(struct dev_pm_opp *opp, struct device_opp *dev_opp)
{
	struct dentry *pdentry = dev_opp->dentry;
	struct dentry *d;
	char name[25];	/* 20 chars for 64 bit value + 5 (opp:\0) */

	/* Rate is unique to each OPP, use it to give opp-name */
	snprintf(name, sizeof(name), "opp:%lu", opp->rate);

	/* Create per-opp directory */
	d = debugfs_create_dir(name, pdentry);
	if (!d)
		return -ENOMEM;

	if (!debugfs_create_bool("available", S_IRUGO, d, &opp->available))
		return -ENOMEM;

	if (!debugfs_create_bool("dynamic", S_IRUGO, d, &opp->dynamic))
		return -ENOMEM;

	if (!debugfs_create_bool("turbo", S_IRUGO, d, &opp->turbo))
		return -ENOMEM;

	if (!debugfs_create_bool("suspend", S_IRUGO, d, &opp->suspend))
		return -ENOMEM;

	if (!debugfs_create_ulong("rate_hz", S_IRUGO, d, &opp->rate))
		return -ENOMEM;

	if (!debugfs_create_ulong("u_volt_target", S_IRUGO, d, &opp->u_volt))
		return -ENOMEM;

	if (!debugfs_create_ulong("u_volt_min", S_IRUGO, d, &opp->u_volt_min))
		return -ENOMEM;

	if (!debugfs_create_ulong("u_volt_max", S_IRUGO, d, &opp->u_volt_max))
		return -ENOMEM;

	if (!debugfs_create_ulong("u_amp", S_IRUGO, d, &opp->u_amp))
		return -ENOMEM;

	if (!debugfs_create_ulong("clock_latency_ns", S_IRUGO, d,
				  &opp->clock_latency_ns))
		return -ENOMEM;

	opp->dentry = d;
	return 0;
}

static int device_opp_debug_create_dir(struct device_list_opp *list_dev,
				       struct device_opp *dev_opp)
{
	const struct device *dev = list_dev->dev;
	struct dentry *d;

	opp_set_dev_name(dev, dev_opp->dentry_name);

	/* Create device specific directory */
	d = debugfs_create_dir(dev_opp->dentry_name, rootdir);
	if (!d) {
		dev_err(dev, "%s: Failed to create debugfs dir\n", __func__);
		return -ENOMEM;
	}

	list_dev->dentry = d;
	dev_opp->dentry = d;

	return 0;
}

static int device_opp_debug_create_link(struct device_list_opp *list_dev,
					struct device_opp *dev_opp)
{
	const struct device *dev = list_dev->dev;
	char name[NAME_MAX];
	struct dentry *d;

	opp_set_dev_name(list_dev->dev, name);

	/* Create device specific directory link */
	d = debugfs_create_symlink(name, rootdir, dev_opp->dentry_name);
	if (!d) {
		dev_err(dev, "%s: Failed to create link\n", __func__);
		return -ENOMEM;
	}

	list_dev->dentry = d;

	return 0;
}

/**
 * opp_debug_register - add a device opp node to the debugfs 'opp' directory
 * @list_dev: list-dev pointer for device
 * @dev_opp: the device-opp being added
 *
 * Dynamically adds device specific directory in debugfs 'opp' directory. If the
 * device-opp is shared with other devices, then links will be created for all
 * devices except the first.
 *
 * Return: 0 on success, otherwise negative error.
 */
int opp_debug_register(struct device_list_opp *list_dev,
		       struct device_opp *dev_opp)
{
	if (!rootdir) {
		pr_debug("%s: Uninitialized rootdir\n", __func__);
		return -EINVAL;
	}

	if (dev_opp->dentry)
		return device_opp_debug_create_link(list_dev, dev_opp);

	return device_opp_debug_create_dir(list_dev, dev_opp);
}

static void opp_migrate_dentry(struct device_list_opp *list_dev,
			       struct device_opp *dev_opp)
{
	struct device_list_opp *new_dev;
	const struct device *dev;
	struct dentry *dentry;

	/* Look for next list-dev */
	list_for_each_entry(new_dev, &dev_opp->dev_list, node)
		if (new_dev != list_dev)
			break;

	/* new_dev is guaranteed to be valid here */
	dev = new_dev->dev;
	debugfs_remove_recursive(new_dev->dentry);

	opp_set_dev_name(dev, dev_opp->dentry_name);

	dentry = debugfs_rename(rootdir, list_dev->dentry, rootdir,
				dev_opp->dentry_name);
	if (!dentry) {
		dev_err(dev, "%s: Failed to rename link from: %s to %s\n",
			__func__, dev_name(list_dev->dev), dev_name(dev));
		return;
	}

	new_dev->dentry = dentry;
	dev_opp->dentry = dentry;
}

/**
 * opp_debug_unregister - remove a device opp node from debugfs opp directory
 * @list_dev: list-dev pointer for device
 * @dev_opp: the device-opp being removed
 *
 * Dynamically removes device specific directory from debugfs 'opp' directory.
 */
void opp_debug_unregister(struct device_list_opp *list_dev,
			  struct device_opp *dev_opp)
{
	if (list_dev->dentry == dev_opp->dentry) {
		/* Move the real dentry object under another device */
		if (!list_is_singular(&dev_opp->dev_list)) {
			opp_migrate_dentry(list_dev, dev_opp);
			goto out;
		}
		dev_opp->dentry = NULL;
	}

	debugfs_remove_recursive(list_dev->dentry);

out:
	list_dev->dentry = NULL;
}

static int __init opp_debug_init(void)
{
	/* Create /sys/kernel/debug/opp directory */
	rootdir = debugfs_create_dir("opp", NULL);
	if (!rootdir) {
		pr_err("%s: Failed to create root directory\n", __func__);
		return -ENOMEM;
	}

	return 0;
}
core_initcall(opp_debug_init);
