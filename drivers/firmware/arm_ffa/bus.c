// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 ARM Ltd.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/arm_ffa.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "common.h"

static int ffa_device_match(struct device *dev, struct device_driver *drv)
{
	const struct ffa_device_id *id_table;
	struct ffa_device *ffa_dev;

	id_table = to_ffa_driver(drv)->id_table;
	ffa_dev = to_ffa_dev(dev);

	while (!uuid_is_null(&id_table->uuid)) {
		/*
		 * FF-A v1.0 doesn't provide discovery of UUIDs, just the
		 * partition IDs, so fetch the partitions IDs for this
		 * id_table UUID and assign the UUID to the device if the
		 * partition ID matches
		 */
		if (uuid_is_null(&ffa_dev->uuid))
			ffa_device_match_uuid(ffa_dev, &id_table->uuid);

		if (uuid_equal(&ffa_dev->uuid, &id_table->uuid))
			return 1;
		id_table++;
	}

	return 0;
}

static int ffa_device_probe(struct device *dev)
{
	struct ffa_driver *ffa_drv = to_ffa_driver(dev->driver);
	struct ffa_device *ffa_dev = to_ffa_dev(dev);

	return ffa_drv->probe(ffa_dev);
}

static void ffa_device_remove(struct device *dev)
{
	struct ffa_driver *ffa_drv = to_ffa_driver(dev->driver);

	ffa_drv->remove(to_ffa_dev(dev));
}

static int ffa_device_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct ffa_device *ffa_dev = to_ffa_dev(dev);

	return add_uevent_var(env, "MODALIAS=arm_ffa:%04x:%pUb",
			      ffa_dev->vm_id, &ffa_dev->uuid);
}

static ssize_t partition_id_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct ffa_device *ffa_dev = to_ffa_dev(dev);

	return sprintf(buf, "0x%04x\n", ffa_dev->vm_id);
}
static DEVICE_ATTR_RO(partition_id);

static ssize_t uuid_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct ffa_device *ffa_dev = to_ffa_dev(dev);

	return sprintf(buf, "%pUb\n", &ffa_dev->uuid);
}
static DEVICE_ATTR_RO(uuid);

static struct attribute *ffa_device_attributes_attrs[] = {
	&dev_attr_partition_id.attr,
	&dev_attr_uuid.attr,
	NULL,
};
ATTRIBUTE_GROUPS(ffa_device_attributes);

struct bus_type ffa_bus_type = {
	.name		= "arm_ffa",
	.match		= ffa_device_match,
	.probe		= ffa_device_probe,
	.remove		= ffa_device_remove,
	.uevent		= ffa_device_uevent,
	.dev_groups	= ffa_device_attributes_groups,
};
EXPORT_SYMBOL_GPL(ffa_bus_type);

int ffa_driver_register(struct ffa_driver *driver, struct module *owner,
			const char *mod_name)
{
	int ret;

	if (!driver->probe)
		return -EINVAL;

	driver->driver.bus = &ffa_bus_type;
	driver->driver.name = driver->name;
	driver->driver.owner = owner;
	driver->driver.mod_name = mod_name;

	ret = driver_register(&driver->driver);
	if (!ret)
		pr_debug("registered new ffa driver %s\n", driver->name);

	return ret;
}
EXPORT_SYMBOL_GPL(ffa_driver_register);

void ffa_driver_unregister(struct ffa_driver *driver)
{
	driver_unregister(&driver->driver);
}
EXPORT_SYMBOL_GPL(ffa_driver_unregister);

static void ffa_release_device(struct device *dev)
{
	struct ffa_device *ffa_dev = to_ffa_dev(dev);

	kfree(ffa_dev);
}

static int __ffa_devices_unregister(struct device *dev, void *data)
{
	device_unregister(dev);

	return 0;
}

static void ffa_devices_unregister(void)
{
	bus_for_each_dev(&ffa_bus_type, NULL, NULL,
			 __ffa_devices_unregister);
}

bool ffa_device_is_valid(struct ffa_device *ffa_dev)
{
	bool valid = false;
	struct device *dev = NULL;
	struct ffa_device *tmp_dev;

	do {
		dev = bus_find_next_device(&ffa_bus_type, dev);
		tmp_dev = to_ffa_dev(dev);
		if (tmp_dev == ffa_dev) {
			valid = true;
			break;
		}
		put_device(dev);
	} while (dev);

	put_device(dev);

	return valid;
}

struct ffa_device *ffa_device_register(const uuid_t *uuid, int vm_id)
{
	int ret;
	struct device *dev;
	struct ffa_device *ffa_dev;

	ffa_dev = kzalloc(sizeof(*ffa_dev), GFP_KERNEL);
	if (!ffa_dev)
		return NULL;

	dev = &ffa_dev->dev;
	dev->bus = &ffa_bus_type;
	dev->release = ffa_release_device;
	dev_set_name(&ffa_dev->dev, "arm-ffa-%04x", vm_id);

	ffa_dev->vm_id = vm_id;
	uuid_copy(&ffa_dev->uuid, uuid);

	ret = device_register(&ffa_dev->dev);
	if (ret) {
		dev_err(dev, "unable to register device %s err=%d\n",
			dev_name(dev), ret);
		put_device(dev);
		return NULL;
	}

	return ffa_dev;
}
EXPORT_SYMBOL_GPL(ffa_device_register);

void ffa_device_unregister(struct ffa_device *ffa_dev)
{
	if (!ffa_dev)
		return;

	device_unregister(&ffa_dev->dev);
}
EXPORT_SYMBOL_GPL(ffa_device_unregister);

int arm_ffa_bus_init(void)
{
	return bus_register(&ffa_bus_type);
}

void arm_ffa_bus_exit(void)
{
	ffa_devices_unregister();
	bus_unregister(&ffa_bus_type);
}
