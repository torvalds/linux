/*
 * Intel Management Engine Interface (Intel MEI) Linux driver
 * Copyright (c) 2012-2013, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/mei_cl_bus.h>

#include "mei_dev.h"

#define to_mei_cl_driver(d) container_of(d, struct mei_cl_driver, driver)
#define to_mei_cl_device(d) container_of(d, struct mei_cl_device, dev)

static int mei_cl_device_match(struct device *dev, struct device_driver *drv)
{
	struct mei_cl_device *device = to_mei_cl_device(dev);
	struct mei_cl_driver *driver = to_mei_cl_driver(drv);
	const struct mei_cl_device_id *id;

	if (!device)
		return 0;

	if (!driver || !driver->id_table)
		return 0;

	id = driver->id_table;

	while (id->name[0]) {
		if (!strcmp(dev_name(dev), id->name))
			return 1;

		id++;
	}

	return 0;
}

static int mei_cl_device_probe(struct device *dev)
{
	struct mei_cl_device *device = to_mei_cl_device(dev);
	struct mei_cl_driver *driver;
	struct mei_cl_device_id id;

	if (!device)
		return 0;

	driver = to_mei_cl_driver(dev->driver);
	if (!driver || !driver->probe)
		return -ENODEV;

	dev_dbg(dev, "Device probe\n");

	strncpy(id.name, dev_name(dev), MEI_CL_NAME_SIZE);

	return driver->probe(device, &id);
}

static int mei_cl_device_remove(struct device *dev)
{
	struct mei_cl_device *device = to_mei_cl_device(dev);
	struct mei_cl_driver *driver;

	if (!device || !dev->driver)
		return 0;

	driver = to_mei_cl_driver(dev->driver);
	if (!driver->remove) {
		dev->driver = NULL;

		return 0;
	}

	return driver->remove(device);
}

static ssize_t modalias_show(struct device *dev, struct device_attribute *a,
			     char *buf)
{
	int len;

	len = snprintf(buf, PAGE_SIZE, "mei:%s\n", dev_name(dev));

	return (len >= PAGE_SIZE) ? (PAGE_SIZE - 1) : len;
}

static struct device_attribute mei_cl_dev_attrs[] = {
	__ATTR_RO(modalias),
	__ATTR_NULL,
};

static int mei_cl_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	if (add_uevent_var(env, "MODALIAS=mei:%s", dev_name(dev)))
		return -ENOMEM;

	return 0;
}

static struct bus_type mei_cl_bus_type = {
	.name		= "mei",
	.dev_attrs	= mei_cl_dev_attrs,
	.match		= mei_cl_device_match,
	.probe		= mei_cl_device_probe,
	.remove		= mei_cl_device_remove,
	.uevent		= mei_cl_uevent,
};

static void mei_cl_dev_release(struct device *dev)
{
	kfree(to_mei_cl_device(dev));
}

static struct device_type mei_cl_device_type = {
	.release	= mei_cl_dev_release,
};

struct mei_cl_device *mei_cl_add_device(struct mei_device *mei_device,
				  uuid_le uuid, char *name)
{
	struct mei_cl_device *device;
	int status;

	device = kzalloc(sizeof(struct mei_cl_device), GFP_KERNEL);
	if (!device)
		return NULL;

	device->dev.parent = &mei_device->pdev->dev;
	device->dev.bus = &mei_cl_bus_type;
	device->dev.type = &mei_cl_device_type;

	dev_set_name(&device->dev, "%s", name);

	status = device_register(&device->dev);
	if (status)
		goto out_err;

	dev_dbg(&device->dev, "client %s registered\n", name);

	return device;

out_err:
	dev_err(device->dev.parent, "Failed to register MEI client\n");

	kfree(device);

	return NULL;
}
EXPORT_SYMBOL_GPL(mei_cl_add_device);

void mei_cl_remove_device(struct mei_cl_device *device)
{
	device_unregister(&device->dev);
}
EXPORT_SYMBOL_GPL(mei_cl_remove_device);

int __mei_cl_driver_register(struct mei_cl_driver *driver, struct module *owner)
{
	int err;

	driver->driver.name = driver->name;
	driver->driver.owner = owner;
	driver->driver.bus = &mei_cl_bus_type;

	err = driver_register(&driver->driver);
	if (err)
		return err;

	pr_debug("mei: driver [%s] registered\n", driver->driver.name);

	return 0;
}
EXPORT_SYMBOL_GPL(__mei_cl_driver_register);

void mei_cl_driver_unregister(struct mei_cl_driver *driver)
{
	driver_unregister(&driver->driver);

	pr_debug("mei: driver [%s] unregistered\n", driver->driver.name);
}
EXPORT_SYMBOL_GPL(mei_cl_driver_unregister);
