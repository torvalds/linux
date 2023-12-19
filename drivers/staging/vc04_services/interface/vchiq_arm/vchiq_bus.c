// SPDX-License-Identifier: GPL-2.0
/*
 * vchiq_device.c - VCHIQ generic device and bus-type
 *
 * Copyright (c) 2023 Ideas On Board Oy
 */

#include <linux/device/bus.h>
#include <linux/dma-mapping.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "vchiq_bus.h"

static int vchiq_bus_type_match(struct device *dev, struct device_driver *drv)
{
	if (dev->bus == &vchiq_bus_type &&
	    strcmp(dev_name(dev), drv->name) == 0)
		return true;

	return false;
}

static int vchiq_bus_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
	const struct vchiq_device *device = container_of_const(dev, struct vchiq_device, dev);

	return add_uevent_var(env, "MODALIAS=vchiq:%s", dev_name(&device->dev));
}

static int vchiq_bus_probe(struct device *dev)
{
	struct vchiq_device *device = to_vchiq_device(dev);
	struct vchiq_driver *driver = to_vchiq_driver(dev->driver);

	return driver->probe(device);
}

struct bus_type vchiq_bus_type = {
	.name   = "vchiq-bus",
	.match  = vchiq_bus_type_match,
	.uevent = vchiq_bus_uevent,
	.probe  = vchiq_bus_probe,
};

static void vchiq_device_release(struct device *dev)
{
	struct vchiq_device *device = to_vchiq_device(dev);

	kfree(device);
}

struct vchiq_device *
vchiq_device_register(struct device *parent, const char *name)
{
	struct vchiq_device *device;
	int ret;

	device = kzalloc(sizeof(*device), GFP_KERNEL);
	if (!device)
		return NULL;

	device->dev.init_name = name;
	device->dev.parent = parent;
	device->dev.bus = &vchiq_bus_type;
	device->dev.dma_mask = &device->dev.coherent_dma_mask;
	device->dev.release = vchiq_device_release;

	of_dma_configure(&device->dev, parent->of_node, true);

	ret = device_register(&device->dev);
	if (ret) {
		dev_err(parent, "Cannot register %s: %d\n", name, ret);
		put_device(&device->dev);
		return NULL;
	}

	return device;
}

void vchiq_device_unregister(struct vchiq_device *vchiq_dev)
{
	device_unregister(&vchiq_dev->dev);
}

int vchiq_driver_register(struct vchiq_driver *vchiq_drv)
{
	vchiq_drv->driver.bus = &vchiq_bus_type;

	return driver_register(&vchiq_drv->driver);
}
EXPORT_SYMBOL_GPL(vchiq_driver_register);

void vchiq_driver_unregister(struct vchiq_driver *vchiq_drv)
{
	driver_unregister(&vchiq_drv->driver);
}
EXPORT_SYMBOL_GPL(vchiq_driver_unregister);
