/*
 * driver.c - centralized device driver management
 *
 * Copyright (c) 2002-3 Patrick Mochel
 * Copyright (c) 2002-3 Open Source Development Labs
 * Copyright (c) 2007 Greg Kroah-Hartman <gregkh@suse.de>
 * Copyright (c) 2007 Novell Inc.
 *
 * This file is released under the GPLv2
 *
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/string.h>
#include "base.h"

static struct device *next_device(struct klist_iter *i)
{
	struct klist_node *n = klist_next(i);
	struct device *dev = NULL;
	struct device_private *dev_prv;

	if (n) {
		dev_prv = to_device_private_driver(n);
		dev = dev_prv->device;
	}
	return dev;
}

/**
 * driver_for_each_device - Iterator for devices bound to a driver.
 * @drv: Driver we're iterating.
 * @start: Device to begin with
 * @data: Data to pass to the callback.
 * @fn: Function to call for each device.
 *
 * Iterate over the @drv's list of devices calling @fn for each one.
 */
int driver_for_each_device(struct device_driver *drv, struct device *start,
			   void *data, int (*fn)(struct device *, void *))
{
	struct klist_iter i;
	struct device *dev;
	int error = 0;

	if (!drv)
		return -EINVAL;

	klist_iter_init_node(&drv->p->klist_devices, &i,
			     start ? &start->p->knode_driver : NULL);
	while ((dev = next_device(&i)) && !error)
		error = fn(dev, data);
	klist_iter_exit(&i);
	return error;
}
EXPORT_SYMBOL_GPL(driver_for_each_device);

/**
 * driver_find_device - device iterator for locating a particular device.
 * @drv: The device's driver
 * @start: Device to begin with
 * @data: Data to pass to match function
 * @match: Callback function to check device
 *
 * This is similar to the driver_for_each_device() function above, but
 * it returns a reference to a device that is 'found' for later use, as
 * determined by the @match callback.
 *
 * The callback should return 0 if the device doesn't match and non-zero
 * if it does.  If the callback returns non-zero, this function will
 * return to the caller and not iterate over any more devices.
 */
struct device *driver_find_device(struct device_driver *drv,
				  struct device *start, void *data,
				  int (*match)(struct device *dev, void *data))
{
	struct klist_iter i;
	struct device *dev;

	if (!drv || !drv->p)
		return NULL;

	klist_iter_init_node(&drv->p->klist_devices, &i,
			     (start ? &start->p->knode_driver : NULL));
	while ((dev = next_device(&i)))
		if (match(dev, data) && get_device(dev))
			break;
	klist_iter_exit(&i);
	return dev;
}
EXPORT_SYMBOL_GPL(driver_find_device);

/**
 * driver_create_file - create sysfs file for driver.
 * @drv: driver.
 * @attr: driver attribute descriptor.
 */
int driver_create_file(struct device_driver *drv,
		       const struct driver_attribute *attr)
{
	int error;
	if (drv)
		error = sysfs_create_file(&drv->p->kobj, &attr->attr);
	else
		error = -EINVAL;
	return error;
}
EXPORT_SYMBOL_GPL(driver_create_file);

/**
 * driver_remove_file - remove sysfs file for driver.
 * @drv: driver.
 * @attr: driver attribute descriptor.
 */
void driver_remove_file(struct device_driver *drv,
			const struct driver_attribute *attr)
{
	if (drv)
		sysfs_remove_file(&drv->p->kobj, &attr->attr);
}
EXPORT_SYMBOL_GPL(driver_remove_file);

static int driver_add_groups(struct device_driver *drv,
			     const struct attribute_group **groups)
{
	int error = 0;
	int i;

	if (groups) {
		for (i = 0; groups[i]; i++) {
			error = sysfs_create_group(&drv->p->kobj, groups[i]);
			if (error) {
				while (--i >= 0)
					sysfs_remove_group(&drv->p->kobj,
							   groups[i]);
				break;
			}
		}
	}
	return error;
}

static void driver_remove_groups(struct device_driver *drv,
				 const struct attribute_group **groups)
{
	int i;

	if (groups)
		for (i = 0; groups[i]; i++)
			sysfs_remove_group(&drv->p->kobj, groups[i]);
}

/**
 * driver_register - register driver with bus
 * @drv: driver to register
 *
 * We pass off most of the work to the bus_add_driver() call,
 * since most of the things we have to do deal with the bus
 * structures.
 */
int driver_register(struct device_driver *drv)
{
	int ret;
	struct device_driver *other;

	BUG_ON(!drv->bus->p);

	if ((drv->bus->probe && drv->probe) ||
	    (drv->bus->remove && drv->remove) ||
	    (drv->bus->shutdown && drv->shutdown))
		printk(KERN_WARNING "Driver '%s' needs updating - please use "
			"bus_type methods\n", drv->name);

	other = driver_find(drv->name, drv->bus);
	if (other) {
		printk(KERN_ERR "Error: Driver '%s' is already registered, "
			"aborting...\n", drv->name);
		return -EBUSY;
	}

	ret = bus_add_driver(drv);
	if (ret)
		return ret;
	ret = driver_add_groups(drv, drv->groups);
	if (ret) {
		bus_remove_driver(drv);
		return ret;
	}
	kobject_uevent(&drv->p->kobj, KOBJ_ADD);

	return ret;
}
EXPORT_SYMBOL_GPL(driver_register);

/**
 * driver_unregister - remove driver from system.
 * @drv: driver.
 *
 * Again, we pass off most of the work to the bus-level call.
 */
void driver_unregister(struct device_driver *drv)
{
	if (!drv || !drv->p) {
		WARN(1, "Unexpected driver unregister!\n");
		return;
	}
	driver_remove_groups(drv, drv->groups);
	bus_remove_driver(drv);
}
EXPORT_SYMBOL_GPL(driver_unregister);

/**
 * driver_find - locate driver on a bus by its name.
 * @name: name of the driver.
 * @bus: bus to scan for the driver.
 *
 * Call kset_find_obj() to iterate over list of drivers on
 * a bus to find driver by name. Return driver if found.
 *
 * This routine provides no locking to prevent the driver it returns
 * from being unregistered or unloaded while the caller is using it.
 * The caller is responsible for preventing this.
 */
struct device_driver *driver_find(const char *name, struct bus_type *bus)
{
	struct kobject *k = kset_find_obj(bus->p->drivers_kset, name);
	struct driver_private *priv;

	if (k) {
		/* Drop reference added by kset_find_obj() */
		kobject_put(k);
		priv = to_driver(k);
		return priv->driver;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(driver_find);
