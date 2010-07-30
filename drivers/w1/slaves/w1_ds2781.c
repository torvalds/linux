/*
 * Copyright (C) 2010 Motorola, Inc.
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
 * Based on w1_ds2784.c which is:
 * Copyright (C) 2009 HTC Corporation
 */

#include <linux/device.h>
#include <linux/idr.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include "../w1.h"
#include "../w1_family.h"
#include "../w1_int.h"
#include "w1_ds2781.h"

static int w1_ds2781_io(struct device *dev, char *buf, int addr, size_t count,
			int io)
{
	struct w1_slave *sl = container_of(dev, struct w1_slave, dev);

	if (!dev)
		return 0;

	mutex_lock(&sl->master->mutex);

	if (addr > DS2781_DATA_SIZE || addr < 0) {
		count = 0;
		goto out;
	}
	if (addr + count > DS2781_DATA_SIZE)
		count = DS2781_DATA_SIZE - addr;

	if (!w1_reset_select_slave(sl)) {
		if (!io) {
			w1_write_8(sl->master, W1_DS2781_READ_DATA);
			w1_write_8(sl->master, addr);
			count = w1_read_block(sl->master, buf, count);
		} else {
			w1_write_8(sl->master, W1_DS2781_WRITE_DATA);
			w1_write_8(sl->master, addr);
			w1_write_block(sl->master, buf, count);
			/* XXX w1_write_block returns void, not n_written */
		}
	}

out:
	mutex_unlock(&sl->master->mutex);

	return count;
}

int w1_ds2781_read(struct device *dev, char *buf, int addr, size_t count)
{
	return w1_ds2781_io(dev, buf, addr, count, 0);
}
EXPORT_SYMBOL(w1_ds2781_read);
int w1_ds2781_write(struct device *dev, char *buf, int addr, size_t count)
{
	return w1_ds2781_io(dev, buf, addr, count, 1);
}
EXPORT_SYMBOL(w1_ds2781_write);
static ssize_t w1_ds2781_read_bin(struct file *file, struct kobject *kobj,
				  struct bin_attribute *bin_attr,
				  char *buf, loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	return w1_ds2781_read(dev, buf, off, count);
}

static struct bin_attribute w1_ds2781_bin_attr = {
	.attr = {
		.name = "w1_slave",
		.mode = S_IRUGO,
	},
	.size = DS2781_DATA_SIZE,
	.read = w1_ds2781_read_bin,
};

static DEFINE_IDR(bat_idr);
static DEFINE_MUTEX(bat_idr_lock);

static int new_bat_id(void)
{
	int ret;

	while (1) {
		int id;

		ret = idr_pre_get(&bat_idr, GFP_KERNEL);
		if (ret == 0)
			return -ENOMEM;

		mutex_lock(&bat_idr_lock);
		ret = idr_get_new(&bat_idr, NULL, &id);
		mutex_unlock(&bat_idr_lock);

		if (ret == 0) {
			ret = id & MAX_ID_MASK;
			break;
		} else if (ret == -EAGAIN) {
			continue;
		} else {
			break;
		}
	}

	return ret;
}

static void release_bat_id(int id)
{
	mutex_lock(&bat_idr_lock);
	idr_remove(&bat_idr, id);
	mutex_unlock(&bat_idr_lock);
}

static int w1_ds2781_add_slave(struct w1_slave *sl)
{
	int ret;
	int id;
	struct platform_device *pdev;

	id = new_bat_id();
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
	platform_device_del(pdev);
pdev_add_failed:
	platform_device_put(pdev);
pdev_alloc_failed:
	release_bat_id(id);
noid:
	return ret;
}

static void w1_ds2781_remove_slave(struct w1_slave *sl)
{
	struct platform_device *pdev = dev_get_drvdata(&sl->dev);
	int id = pdev->id;

	platform_device_unregister(pdev);
	release_bat_id(id);
	sysfs_remove_bin_file(&sl->dev.kobj, &w1_ds2781_bin_attr);
}

static struct w1_family_ops w1_ds2781_fops = {
	.add_slave    = w1_ds2781_add_slave,
	.remove_slave = w1_ds2781_remove_slave,
};

static struct w1_family w1_ds2780_family = {
	.fid = W1_FAMILY_DS2780,
	.fops = &w1_ds2781_fops,
};

static struct w1_family w1_ds2781_family = {
	.fid = W1_FAMILY_DS2781,
	.fops = &w1_ds2781_fops,
};

static int __init w1_ds2781_init(void)
{
	idr_init(&bat_idr);
	w1_register_family(&w1_ds2780_family);
	return w1_register_family(&w1_ds2781_family);
}

static void __exit w1_ds2781_exit(void)
{
	w1_unregister_family(&w1_ds2780_family);
	w1_unregister_family(&w1_ds2781_family);
	idr_destroy(&bat_idr);
}

module_init(w1_ds2781_init);
module_exit(w1_ds2781_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Motorola");
MODULE_DESCRIPTION("1-wire Driver Dallas 2781 battery monitor chip");
