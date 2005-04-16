/*
 * arch/sh/kernel/cpu/bus.c
 *
 * Virtual bus for SuperH.
 *
 * Copyright (C) 2004 Paul Mundt
 *
 * Shamelessly cloned from arch/arm/mach-omap/bus.c, which was written
 * by:
 *
 *  	Copyright (C) 2003 - 2004 Nokia Corporation
 *  	Written by Tony Lindgren <tony@atomide.com>
 *  	Portions of code based on sa1111.c.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/module.h>
#include <asm/bus-sh.h>

static int sh_bus_match(struct device *dev, struct device_driver *drv)
{
	struct sh_driver *shdrv = to_sh_driver(drv);
	struct sh_dev *shdev = to_sh_dev(dev);

	return shdev->dev_id == shdrv->dev_id;
}

static int sh_bus_suspend(struct device *dev, u32 state)
{
	struct sh_dev *shdev = to_sh_dev(dev);
	struct sh_driver *shdrv = to_sh_driver(dev->driver);

	if (shdrv && shdrv->suspend)
		return shdrv->suspend(shdev, state);

	return 0;
}

static int sh_bus_resume(struct device *dev)
{
	struct sh_dev *shdev = to_sh_dev(dev);
	struct sh_driver *shdrv = to_sh_driver(dev->driver);

	if (shdrv && shdrv->resume)
		return shdrv->resume(shdev);

	return 0;
}

static struct device sh_bus_devices[SH_NR_BUSES] = {
	{
		.bus_id		= SH_BUS_NAME_VIRT,
	},
};

struct bus_type sh_bus_types[SH_NR_BUSES] = {
	{
		.name		= SH_BUS_NAME_VIRT,
		.match		= sh_bus_match,
		.suspend	= sh_bus_suspend,
		.resume		= sh_bus_resume,
	},
};

static int sh_device_probe(struct device *dev)
{
	struct sh_dev *shdev = to_sh_dev(dev);
	struct sh_driver *shdrv = to_sh_driver(dev->driver);

	if (shdrv && shdrv->probe)
		return shdrv->probe(shdev);

	return -ENODEV;
}

static int sh_device_remove(struct device *dev)
{
	struct sh_dev *shdev = to_sh_dev(dev);
	struct sh_driver *shdrv = to_sh_driver(dev->driver);

	if (shdrv && shdrv->remove)
		return shdrv->remove(shdev);

	return 0;
}

int sh_device_register(struct sh_dev *dev)
{
	if (!dev)
		return -EINVAL;

	if (dev->bus_id < 0 || dev->bus_id >= SH_NR_BUSES) {
		printk(KERN_ERR "%s: bus_id invalid: %s bus: %d\n",
		       __FUNCTION__, dev->name, dev->bus_id);
		return -EINVAL;
	}

	dev->dev.parent = &sh_bus_devices[dev->bus_id];
	dev->dev.bus    = &sh_bus_types[dev->bus_id];

	/* This is needed for USB OHCI to work */
	if (dev->dma_mask)
		dev->dev.dma_mask = dev->dma_mask;

	snprintf(dev->dev.bus_id, BUS_ID_SIZE, "%s%u",
		 dev->name, dev->dev_id);

	printk(KERN_INFO "Registering SH device '%s'. Parent at %s\n",
	       dev->dev.bus_id, dev->dev.parent->bus_id);

	return device_register(&dev->dev);
}

void sh_device_unregister(struct sh_dev *dev)
{
	device_unregister(&dev->dev);
}

int sh_driver_register(struct sh_driver *drv)
{
	if (!drv)
		return -EINVAL;

	if (drv->bus_id < 0 || drv->bus_id >= SH_NR_BUSES) {
		printk(KERN_ERR "%s: bus_id invalid: bus: %d device %d\n",
		       __FUNCTION__, drv->bus_id, drv->dev_id);
		return -EINVAL;
	}

	drv->drv.probe  = sh_device_probe;
	drv->drv.remove = sh_device_remove;
	drv->drv.bus    = &sh_bus_types[drv->bus_id];

	return driver_register(&drv->drv);
}

void sh_driver_unregister(struct sh_driver *drv)
{
	driver_unregister(&drv->drv);
}

static int __init sh_bus_init(void)
{
	int i, ret = 0;

	for (i = 0; i < SH_NR_BUSES; i++) {
		ret = device_register(&sh_bus_devices[i]);
		if (ret != 0) {
			printk(KERN_ERR "Unable to register bus device %s\n",
			       sh_bus_devices[i].bus_id);
			continue;
		}

		ret = bus_register(&sh_bus_types[i]);
		if (ret != 0) {
			printk(KERN_ERR "Unable to register bus %s\n",
			       sh_bus_types[i].name);
			device_unregister(&sh_bus_devices[i]);
		}
	}

	printk(KERN_INFO "SH Virtual Bus initialized\n");

	return ret;
}

static void __exit sh_bus_exit(void)
{
	int i;

	for (i = 0; i < SH_NR_BUSES; i++) {
		bus_unregister(&sh_bus_types[i]);
		device_unregister(&sh_bus_devices[i]);
	}
}

module_init(sh_bus_init);
module_exit(sh_bus_exit);

MODULE_AUTHOR("Paul Mundt <lethal@linux-sh.org>");
MODULE_DESCRIPTION("SH Virtual Bus");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(sh_bus_types);
EXPORT_SYMBOL(sh_device_register);
EXPORT_SYMBOL(sh_device_unregister);
EXPORT_SYMBOL(sh_driver_register);
EXPORT_SYMBOL(sh_driver_unregister);

