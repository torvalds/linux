// SPDX-License-Identifier: GPL-2.0-only
/*
 * 1-Wire implementation for the ds2781 chip
 *
 * Author: Renata Sayakhova <renata@oktetlabs.ru>
 *
 * Based on w1-ds2780 driver
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>

#include <linux/w1.h>

#include "w1_ds2781.h"

#define W1_FAMILY_DS2781	0x3D

static int w1_ds2781_do_io(struct device *dev, char *buf, int addr,
			size_t count, int io)
{
	struct w1_slave *sl = container_of(dev, struct w1_slave, dev);

	if (addr > DS2781_DATA_SIZE || addr < 0)
		return 0;

	count = min_t(int, count, DS2781_DATA_SIZE - addr);

	if (w1_reset_select_slave(sl) == 0) {
		if (io) {
			w1_write_8(sl->master, W1_DS2781_WRITE_DATA);
			w1_write_8(sl->master, addr);
			w1_write_block(sl->master, buf, count);
		} else {
			w1_write_8(sl->master, W1_DS2781_READ_DATA);
			w1_write_8(sl->master, addr);
			count = w1_read_block(sl->master, buf, count);
		}
	}

	return count;
}

int w1_ds2781_io(struct device *dev, char *buf, int addr, size_t count,
			int io)
{
	struct w1_slave *sl = container_of(dev, struct w1_slave, dev);
	int ret;

	if (!dev)
		return -ENODEV;

	mutex_lock(&sl->master->bus_mutex);

	ret = w1_ds2781_do_io(dev, buf, addr, count, io);

	mutex_unlock(&sl->master->bus_mutex);

	return ret;
}
EXPORT_SYMBOL(w1_ds2781_io);

int w1_ds2781_eeprom_cmd(struct device *dev, int addr, int cmd)
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
EXPORT_SYMBOL(w1_ds2781_eeprom_cmd);

static ssize_t w1_slave_read(struct file *filp, struct kobject *kobj,
			     struct bin_attribute *bin_attr, char *buf,
			     loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	return w1_ds2781_io(dev, buf, off, count, 0);
}

static BIN_ATTR_RO(w1_slave, DS2781_DATA_SIZE);

static struct bin_attribute *w1_ds2781_bin_attrs[] = {
	&bin_attr_w1_slave,
	NULL,
};

static const struct attribute_group w1_ds2781_group = {
	.bin_attrs = w1_ds2781_bin_attrs,
};

static const struct attribute_group *w1_ds2781_groups[] = {
	&w1_ds2781_group,
	NULL,
};

static int w1_ds2781_add_slave(struct w1_slave *sl)
{
	int ret;
	struct platform_device *pdev;

	pdev = platform_device_alloc("ds2781-battery", PLATFORM_DEVID_AUTO);
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

static void w1_ds2781_remove_slave(struct w1_slave *sl)
{
	struct platform_device *pdev = dev_get_drvdata(&sl->dev);

	platform_device_unregister(pdev);
}

static struct w1_family_ops w1_ds2781_fops = {
	.add_slave    = w1_ds2781_add_slave,
	.remove_slave = w1_ds2781_remove_slave,
	.groups       = w1_ds2781_groups,
};

static struct w1_family w1_ds2781_family = {
	.fid = W1_FAMILY_DS2781,
	.fops = &w1_ds2781_fops,
};
module_w1_family(w1_ds2781_family);

MODULE_AUTHOR("Renata Sayakhova <renata@oktetlabs.ru>");
MODULE_DESCRIPTION("1-wire Driver for Maxim/Dallas DS2781 Stand-Alone Fuel Gauge IC");
MODULE_LICENSE("GPL");
MODULE_ALIAS("w1-family-" __stringify(W1_FAMILY_DS2781));
