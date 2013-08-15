/*
 * 1-Wire implementation for the ds2760 chip
 *
 * Copyright © 2004-2005, Szabolcs Gyurko <szabolcs.gyurko@tlt.hu>
 *
 * Use consistent with the GNU GPL is permitted,
 * provided that this copyright notice is
 * preserved in its entirety in all copies and derived works.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/idr.h>
#include <linux/gfp.h>

#include "../w1.h"
#include "../w1_int.h"
#include "../w1_family.h"
#include "w1_ds2760.h"

static int w1_ds2760_io(struct device *dev, char *buf, int addr, size_t count,
			int io)
{
	struct w1_slave *sl = container_of(dev, struct w1_slave, dev);

	if (!dev)
		return 0;

	mutex_lock(&sl->master->bus_mutex);

	if (addr > DS2760_DATA_SIZE || addr < 0) {
		count = 0;
		goto out;
	}
	if (addr + count > DS2760_DATA_SIZE)
		count = DS2760_DATA_SIZE - addr;

	if (!w1_reset_select_slave(sl)) {
		if (!io) {
			w1_write_8(sl->master, W1_DS2760_READ_DATA);
			w1_write_8(sl->master, addr);
			count = w1_read_block(sl->master, buf, count);
		} else {
			w1_write_8(sl->master, W1_DS2760_WRITE_DATA);
			w1_write_8(sl->master, addr);
			w1_write_block(sl->master, buf, count);
			/* XXX w1_write_block returns void, not n_written */
		}
	}

out:
	mutex_unlock(&sl->master->bus_mutex);

	return count;
}

int w1_ds2760_read(struct device *dev, char *buf, int addr, size_t count)
{
	return w1_ds2760_io(dev, buf, addr, count, 0);
}

int w1_ds2760_write(struct device *dev, char *buf, int addr, size_t count)
{
	return w1_ds2760_io(dev, buf, addr, count, 1);
}

static int w1_ds2760_eeprom_cmd(struct device *dev, int addr, int cmd)
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

int w1_ds2760_store_eeprom(struct device *dev, int addr)
{
	return w1_ds2760_eeprom_cmd(dev, addr, W1_DS2760_COPY_DATA);
}

int w1_ds2760_recall_eeprom(struct device *dev, int addr)
{
	return w1_ds2760_eeprom_cmd(dev, addr, W1_DS2760_RECALL_DATA);
}

static ssize_t w1_ds2760_read_bin(struct file *filp, struct kobject *kobj,
				  struct bin_attribute *bin_attr,
				  char *buf, loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	return w1_ds2760_read(dev, buf, off, count);
}

static struct bin_attribute w1_ds2760_bin_attr = {
	.attr = {
		.name = "w1_slave",
		.mode = S_IRUGO,
	},
	.size = DS2760_DATA_SIZE,
	.read = w1_ds2760_read_bin,
};

static DEFINE_IDA(bat_ida);

static int w1_ds2760_add_slave(struct w1_slave *sl)
{
	int ret;
	int id;
	struct platform_device *pdev;

	id = ida_simple_get(&bat_ida, 0, 0, GFP_KERNEL);
	if (id < 0) {
		ret = id;
		goto noid;
	}

	pdev = platform_device_alloc("ds2760-battery", id);
	if (!pdev) {
		ret = -ENOMEM;
		goto pdev_alloc_failed;
	}
	pdev->dev.parent = &sl->dev;

	ret = platform_device_add(pdev);
	if (ret)
		goto pdev_add_failed;

	ret = sysfs_create_bin_file(&sl->dev.kobj, &w1_ds2760_bin_attr);
	if (ret)
		goto bin_attr_failed;

	dev_set_drvdata(&sl->dev, pdev);

	goto success;

bin_attr_failed:
	platform_device_del(pdev);
pdev_add_failed:
	platform_device_put(pdev);
pdev_alloc_failed:
	ida_simple_remove(&bat_ida, id);
noid:
success:
	return ret;
}

static void w1_ds2760_remove_slave(struct w1_slave *sl)
{
	struct platform_device *pdev = dev_get_drvdata(&sl->dev);
	int id = pdev->id;

	platform_device_unregister(pdev);
	ida_simple_remove(&bat_ida, id);
	sysfs_remove_bin_file(&sl->dev.kobj, &w1_ds2760_bin_attr);
}

static struct w1_family_ops w1_ds2760_fops = {
	.add_slave    = w1_ds2760_add_slave,
	.remove_slave = w1_ds2760_remove_slave,
};

static struct w1_family w1_ds2760_family = {
	.fid = W1_FAMILY_DS2760,
	.fops = &w1_ds2760_fops,
};

static int __init w1_ds2760_init(void)
{
	printk(KERN_INFO "1-Wire driver for the DS2760 battery monitor "
	       " chip  - (c) 2004-2005, Szabolcs Gyurko\n");
	ida_init(&bat_ida);
	return w1_register_family(&w1_ds2760_family);
}

static void __exit w1_ds2760_exit(void)
{
	w1_unregister_family(&w1_ds2760_family);
	ida_destroy(&bat_ida);
}

EXPORT_SYMBOL(w1_ds2760_read);
EXPORT_SYMBOL(w1_ds2760_write);
EXPORT_SYMBOL(w1_ds2760_store_eeprom);
EXPORT_SYMBOL(w1_ds2760_recall_eeprom);

module_init(w1_ds2760_init);
module_exit(w1_ds2760_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Szabolcs Gyurko <szabolcs.gyurko@tlt.hu>");
MODULE_DESCRIPTION("1-wire Driver Dallas 2760 battery monitor chip");
MODULE_ALIAS("w1-family-" __stringify(W1_FAMILY_DS2760));
