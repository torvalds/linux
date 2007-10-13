/*
 * drivers/sh/superhyway/superhyway.c
 *
 * SuperHyway Bus Driver
 *
 * Copyright (C) 2004, 2005  Paul Mundt <lethal@linux-sh.org>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/superhyway.h>
#include <linux/string.h>
#include <linux/slab.h>

static int superhyway_devices;

static struct device superhyway_bus_device = {
	.bus_id = "superhyway",
};

static void superhyway_device_release(struct device *dev)
{
	struct superhyway_device *sdev = to_superhyway_device(dev);

	kfree(sdev->resource);
	kfree(sdev);
}

/**
 * superhyway_add_device - Add a SuperHyway module
 * @base: Physical address where module is mapped.
 * @sdev: SuperHyway device to add, or NULL to allocate a new one.
 * @bus: Bus where SuperHyway module resides.
 *
 * This is responsible for adding a new SuperHyway module. This sets up a new
 * struct superhyway_device for the module being added if @sdev == NULL.
 *
 * Devices are initially added in the order that they are scanned (from the
 * top-down of the memory map), and are assigned an ID based on the order that
 * they are added. Any manual addition of a module will thus get the ID after
 * the devices already discovered regardless of where it resides in memory.
 *
 * Further work can and should be done in superhyway_scan_bus(), to be sure
 * that any new modules are properly discovered and subsequently registered.
 */
int superhyway_add_device(unsigned long base, struct superhyway_device *sdev,
			  struct superhyway_bus *bus)
{
	struct superhyway_device *dev = sdev;

	if (!dev) {
		dev = kzalloc(sizeof(struct superhyway_device), GFP_KERNEL);
		if (!dev)
			return -ENOMEM;

	}

	dev->bus = bus;
	superhyway_read_vcr(dev, base, &dev->vcr);

	if (!dev->resource) {
		dev->resource = kmalloc(sizeof(struct resource), GFP_KERNEL);
		if (!dev->resource) {
			kfree(dev);
			return -ENOMEM;
		}

		dev->resource->name	= dev->name;
		dev->resource->start	= base;
		dev->resource->end	= dev->resource->start + 0x01000000;
	}

	dev->dev.parent		= &superhyway_bus_device;
	dev->dev.bus		= &superhyway_bus_type;
	dev->dev.release	= superhyway_device_release;
	dev->id.id		= dev->vcr.mod_id;

	sprintf(dev->name, "SuperHyway device %04x", dev->id.id);
	sprintf(dev->dev.bus_id, "%02x", superhyway_devices);

	superhyway_devices++;

	return device_register(&dev->dev);
}

int superhyway_add_devices(struct superhyway_bus *bus,
			   struct superhyway_device **devices,
			   int nr_devices)
{
	int i, ret = 0;

	for (i = 0; i < nr_devices; i++) {
		struct superhyway_device *dev = devices[i];
		ret |= superhyway_add_device(dev->resource[0].start, dev, bus);
	}

	return ret;
}

static int __init superhyway_init(void)
{
	struct superhyway_bus *bus;
	int ret = 0;

	device_register(&superhyway_bus_device);

	for (bus = superhyway_channels; bus->ops; bus++)
		ret |= superhyway_scan_bus(bus);

	return ret;
}

postcore_initcall(superhyway_init);

static const struct superhyway_device_id *
superhyway_match_id(const struct superhyway_device_id *ids,
		    struct superhyway_device *dev)
{
	while (ids->id) {
		if (ids->id == dev->id.id)
			return ids;

		ids++;
	}

	return NULL;
}

static int superhyway_device_probe(struct device *dev)
{
	struct superhyway_device *shyway_dev = to_superhyway_device(dev);
	struct superhyway_driver *shyway_drv = to_superhyway_driver(dev->driver);

	if (shyway_drv && shyway_drv->probe) {
		const struct superhyway_device_id *id;

		id = superhyway_match_id(shyway_drv->id_table, shyway_dev);
		if (id)
			return shyway_drv->probe(shyway_dev, id);
	}

	return -ENODEV;
}

static int superhyway_device_remove(struct device *dev)
{
	struct superhyway_device *shyway_dev = to_superhyway_device(dev);
	struct superhyway_driver *shyway_drv = to_superhyway_driver(dev->driver);

	if (shyway_drv && shyway_drv->remove) {
		shyway_drv->remove(shyway_dev);
		return 0;
	}

	return -ENODEV;
}

/**
 * superhyway_register_driver - Register a new SuperHyway driver
 * @drv: SuperHyway driver to register.
 *
 * This registers the passed in @drv. Any devices matching the id table will
 * automatically be populated and handed off to the driver's specified probe
 * routine.
 */
int superhyway_register_driver(struct superhyway_driver *drv)
{
	drv->drv.name	= drv->name;
	drv->drv.bus	= &superhyway_bus_type;

	return driver_register(&drv->drv);
}

/**
 * superhyway_unregister_driver - Unregister a SuperHyway driver
 * @drv: SuperHyway driver to unregister.
 *
 * This cleans up after superhyway_register_driver(), and should be invoked in
 * the exit path of any module drivers.
 */
void superhyway_unregister_driver(struct superhyway_driver *drv)
{
	driver_unregister(&drv->drv);
}

static int superhyway_bus_match(struct device *dev, struct device_driver *drv)
{
	struct superhyway_device *shyway_dev = to_superhyway_device(dev);
	struct superhyway_driver *shyway_drv = to_superhyway_driver(drv);
	const struct superhyway_device_id *ids = shyway_drv->id_table;

	if (!ids)
		return -EINVAL;
	if (superhyway_match_id(ids, shyway_dev))
		return 1;

	return -ENODEV;
}

struct bus_type superhyway_bus_type = {
	.name		= "superhyway",
	.match		= superhyway_bus_match,
#ifdef CONFIG_SYSFS
	.dev_attrs	= superhyway_dev_attrs,
#endif
	.probe		= superhyway_device_probe,
	.remove		= superhyway_device_remove,
};

static int __init superhyway_bus_init(void)
{
	return bus_register(&superhyway_bus_type);
}

static void __exit superhyway_bus_exit(void)
{
	device_unregister(&superhyway_bus_device);
	bus_unregister(&superhyway_bus_type);
}

core_initcall(superhyway_bus_init);
module_exit(superhyway_bus_exit);

EXPORT_SYMBOL(superhyway_bus_type);
EXPORT_SYMBOL(superhyway_add_device);
EXPORT_SYMBOL(superhyway_add_devices);
EXPORT_SYMBOL(superhyway_register_driver);
EXPORT_SYMBOL(superhyway_unregister_driver);

MODULE_LICENSE("GPL");
