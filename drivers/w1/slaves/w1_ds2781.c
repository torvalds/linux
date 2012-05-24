/*
 * 1-Wire implementation for the ds2781 chip
 *
 * Author: Renata Sayakhova <renata@oktetlabs.ru>
 *
 * Based on w1-ds2780 driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/idr.h>

#include "../w1.h"
#include "../w1_int.h"
#include "../w1_family.h"
#include "w1_ds2781.h"

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

	mutex_lock(&sl->master->mutex);

	ret = w1_ds2781_do_io(dev, buf, addr, count, io);

	mutex_unlock(&sl->master->mutex);

	return ret;
}
EXPORT_SYMBOL(w1_ds2781_io);

int w1_ds2781_io_nolock(struct device *dev, char *buf, int addr, size_t count,
			int io)
{
	int ret;

	if (!dev)
		return -ENODEV;

	ret = w1_ds2781_do_io(dev, buf, addr, count, io);

	return ret;
}
EXPORT_SYMBOL(w1_ds2781_io_nolock);

int w1_ds2781_eeprom_cmd(struct device *dev, int addr, int cmd)
{
	struct w1_slave *sl = container_of(dev, struct w1_slave, dev);

	if (!dev)
		return -EINVAL;

	mutex_lock(&sl->master->mutex);

	if (w1_reset_select_slave(sl) == 0) {
		w1_write_8(sl->master, cmd);
		w1_write_8(sl->master, addr);
	}

	mutex_unlock(&sl->master->mutex);
	return 0;
}
EXPORT_SYMBOL(w1_ds2781_eeprom_cmd);

static ssize_t w1_ds2781_read_bin(struct file *filp,
				  struct kobject *kobj,
				  struct bin_attribute *bin_attr,
				  char *buf, loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	return w1_ds2781_io(dev, buf, off, count, 0);
}

static struct bin_attribute w1_ds2781_bin_attr = {
	.attr = {
		.name = "w1_slave",
		.mode = S_IRUGO,
	},
	.size = DS2781_DATA_SIZE,
	.read = w1_ds2781_read_bin,
};

static DEFINE_IDA(bat_ida);

static int w1_ds2781_add_slave(struct w1_slave *sl)
{
	int ret;
	int id;
	struct platform_device *pdev;

	id = ida_simple_get(&bat_ida, 0, 0, GFP_KERNEL);
	if (id < 0) {
		ret = id;
		goto noid;
	}

	pdev = platform_device_alloc("ds2781-battery", id);
	if (!pdev) {
		ret = -ENOMEM;
		goto pdev_alloc_failed;
	}
	pdev->dev.parent = &sl->dev;

	ret = platform_device_add(pdev);
	if (ret)
		goto pdev_add_failed;

	ret = sysfs_create_bin_file(&sl->dev.kobj, &w1_ds2781_bin_attr);
	if (ret)
		goto bin_attr_failed;

	dev_set_drvdata(&sl->dev, pdev);

	return 0;

bin_attr_failed:
pdev_add_failed:
	platform_device_unregister(pdev);
pdev_alloc_failed:
	ida_simple_remove(&bat_ida, id);
noid:
	return ret;
}

static void w1_ds2781_remove_slave(struct w1_slave *sl)
{
	struct platform_device *pdev = dev_get_drvdata(&sl->dev);
	int id = pdev->id;

	platform_device_unregister(pdev);
	ida_simple_remove(&bat_ida, id);
	sysfs_remove_bin_file(&sl->dev.kobj, &w1_ds2781_bin_attr);
}

static struct w1_family_ops w1_ds2781_fops = {
	.add_slave    = w1_ds2781_add_slave,
	.remove_slave = w1_ds2781_remove_slave,
};

static struct w1_family w1_ds2781_family = {
	.fid = W1_FAMILY_DS2781,
	.fops = &w1_ds2781_fops,
};

static int __init w1_ds2781_init(void)
{
	ida_init(&bat_ida);
	return w1_register_family(&w1_ds2781_family);
}

static void __exit w1_ds2781_exit(void)
{
	w1_unregister_family(&w1_ds2781_family);
	ida_destroy(&bat_ida);
}

module_init(w1_ds2781_init);
module_exit(w1_ds2781_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Renata Sayakhova <renata@oktetlabs.ru>");
MODULE_DESCRIPTION("1-wire Driver for Maxim/Dallas DS2781 Stand-Alone Fuel Gauge IC");
