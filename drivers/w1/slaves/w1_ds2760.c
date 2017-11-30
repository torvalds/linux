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

#include <linux/w1.h>

#include "w1_ds2760.h"

#define W1_FAMILY_DS2760	0x30

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
EXPORT_SYMBOL(w1_ds2760_read);

int w1_ds2760_write(struct device *dev, char *buf, int addr, size_t count)
{
	return w1_ds2760_io(dev, buf, addr, count, 1);
}
EXPORT_SYMBOL(w1_ds2760_write);

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
EXPORT_SYMBOL(w1_ds2760_store_eeprom);

int w1_ds2760_recall_eeprom(struct device *dev, int addr)
{
	return w1_ds2760_eeprom_cmd(dev, addr, W1_DS2760_RECALL_DATA);
}
EXPORT_SYMBOL(w1_ds2760_recall_eeprom);

static ssize_t w1_slave_read(struct file *filp, struct kobject *kobj,
			     struct bin_attribute *bin_attr, char *buf,
			     loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	return w1_ds2760_read(dev, buf, off, count);
}

static BIN_ATTR_RO(w1_slave, DS2760_DATA_SIZE);

static struct bin_attribute *w1_ds2760_bin_attrs[] = {
	&bin_attr_w1_slave,
	NULL,
};

static const struct attribute_group w1_ds2760_group = {
	.bin_attrs = w1_ds2760_bin_attrs,
};

static const struct attribute_group *w1_ds2760_groups[] = {
	&w1_ds2760_group,
	NULL,
};

static int w1_ds2760_add_slave(struct w1_slave *sl)
{
	int ret;
	struct platform_device *pdev;

	pdev = platform_device_alloc("ds2760-battery", PLATFORM_DEVID_AUTO);
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

static void w1_ds2760_remove_slave(struct w1_slave *sl)
{
	struct platform_device *pdev = dev_get_drvdata(&sl->dev);

	platform_device_unregister(pdev);
}

static struct w1_family_ops w1_ds2760_fops = {
	.add_slave    = w1_ds2760_add_slave,
	.remove_slave = w1_ds2760_remove_slave,
	.groups       = w1_ds2760_groups,
};

static struct w1_family w1_ds2760_family = {
	.fid = W1_FAMILY_DS2760,
	.fops = &w1_ds2760_fops,
};
module_w1_family(w1_ds2760_family);

MODULE_AUTHOR("Szabolcs Gyurko <szabolcs.gyurko@tlt.hu>");
MODULE_DESCRIPTION("1-wire Driver Dallas 2760 battery monitor chip");
MODULE_LICENSE("GPL");
MODULE_ALIAS("w1-family-" __stringify(W1_FAMILY_DS2760));
