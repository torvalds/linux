// SPDX-License-Identifier: GPL-2.0-only
/*
 * 1-Wire implementation for the ds2780 chip
 *
 * Copyright (C) 2010 Indesign, LLC
 *
 * Author: Clifton Barnes <cabarnes@indesign-llc.com>
 *
 * Based on w1-ds2760 driver
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/idr.h>

#include <linux/w1.h>

#include "w1_ds2780.h"

#define W1_FAMILY_DS2780	0x32

static int w1_ds2780_do_io(struct device *dev, char *buf, int addr,
			size_t count, int io)
{
	struct w1_slave *sl = container_of(dev, struct w1_slave, dev);

	if (addr > DS2780_DATA_SIZE || addr < 0)
		return 0;

	count = min_t(int, count, DS2780_DATA_SIZE - addr);

	if (w1_reset_select_slave(sl) == 0) {
		if (io) {
			w1_write_8(sl->master, W1_DS2780_WRITE_DATA);
			w1_write_8(sl->master, addr);
			w1_write_block(sl->master, buf, count);
		} else {
			w1_write_8(sl->master, W1_DS2780_READ_DATA);
			w1_write_8(sl->master, addr);
			count = w1_read_block(sl->master, buf, count);
		}
	}

	return count;
}

int w1_ds2780_io(struct device *dev, char *buf, int addr, size_t count,
			int io)
{
	struct w1_slave *sl = container_of(dev, struct w1_slave, dev);
	int ret;

	if (!dev)
		return -ENODEV;

	mutex_lock(&sl->master->bus_mutex);

	ret = w1_ds2780_do_io(dev, buf, addr, count, io);

	mutex_unlock(&sl->master->bus_mutex);

	return ret;
}
EXPORT_SYMBOL(w1_ds2780_io);

int w1_ds2780_eeprom_cmd(struct device *dev, int addr, int cmd)
{
	struct w1_slave *sl = container_of(dev, struct w1_slave, dev);

	if (!dev)
		return -EINVAL;

	mutex_lock(&sl->master->bus_mutex);

	if (w1_reset_select_slave(sl) == 0) {
		w1_write_8(sl->master, cmd);
		w1_write_8(sl->master, addr);
	}

	mutex_unlock(&sl->master->bus_mutex);
	return 0;
}
EXPORT_SYMBOL(w1_ds2780_eeprom_cmd);

static ssize_t w1_slave_read(struct file *filp, struct kobject *kobj,
			     struct bin_attribute *bin_attr, char *buf,
			     loff_t off, size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	return w1_ds2780_io(dev, buf, off, count, 0);
}

static BIN_ATTR_RO(w1_slave, DS2780_DATA_SIZE);

static struct bin_attribute *w1_ds2780_bin_attrs[] = {
	&bin_attr_w1_slave,
	NULL,
};

static const struct attribute_group w1_ds2780_group = {
	.bin_attrs = w1_ds2780_bin_attrs,
};

static const struct attribute_group *w1_ds2780_groups[] = {
	&w1_ds2780_group,
	NULL,
};

static int w1_ds2780_add_slave(struct w1_slave *sl)
{
	int ret;
	struct platform_device *pdev;

	pdev = platform_device_alloc("ds2780-battery", PLATFORM_DEVID_AUTO);
	if (!pdev)
		return -ENOMEM;
	pdev->dev.parent = &sl->dev;

	ret = platform_device_add(pdev);
	if (ret)
		goto pdev_add_failed;

	dev_set_drvdata(&sl->dev, pdev);

	return 0;

pdev_add_failed:
	platform_device_put(pdev);

	return ret;
}

static void w1_ds2780_remove_slave(struct w1_slave *sl)
{
	struct platform_device *pdev = dev_get_drvdata(&sl->dev);

	platform_device_unregister(pdev);
}

static const struct w1_family_ops w1_ds2780_fops = {
	.add_slave    = w1_ds2780_add_slave,
	.remove_slave = w1_ds2780_remove_slave,
	.groups       = w1_ds2780_groups,
};

static struct w1_family w1_ds2780_family = {
	.fid = W1_FAMILY_DS2780,
	.fops = &w1_ds2780_fops,
};
module_w1_family(w1_ds2780_family);

MODULE_AUTHOR("Clifton Barnes <cabarnes@indesign-llc.com>");
MODULE_DESCRIPTION("1-wire Driver for Maxim/Dallas DS2780 Stand-Alone Fuel Gauge IC");
MODULE_LICENSE("GPL");
MODULE_ALIAS("w1-family-" __stringify(W1_FAMILY_DS2780));
