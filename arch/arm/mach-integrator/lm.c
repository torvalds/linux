/*
 *  linux/arch/arm/mach-integrator/lm.c
 *
 *  Copyright (C) 2003 Deep Blue Solutions Ltd, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>

#include <asm/arch/lm.h>

#define to_lm_device(d)	container_of(d, struct lm_device, dev)
#define to_lm_driver(d)	container_of(d, struct lm_driver, drv)

static int lm_match(struct device *dev, struct device_driver *drv)
{
	return 1;
}

static struct bus_type lm_bustype = {
	.name		= "logicmodule",
	.match		= lm_match,
//	.suspend	= lm_suspend,
//	.resume		= lm_resume,
};

static int __init lm_init(void)
{
	return bus_register(&lm_bustype);
}

postcore_initcall(lm_init);

static int lm_bus_probe(struct device *dev)
{
	struct lm_device *lmdev = to_lm_device(dev);
	struct lm_driver *lmdrv = to_lm_driver(dev->driver);

	return lmdrv->probe(lmdev);
}

static int lm_bus_remove(struct device *dev)
{
	struct lm_device *lmdev = to_lm_device(dev);
	struct lm_driver *lmdrv = to_lm_driver(dev->driver);

	lmdrv->remove(lmdev);
	return 0;
}

int lm_driver_register(struct lm_driver *drv)
{
	drv->drv.bus = &lm_bustype;
	drv->drv.probe = lm_bus_probe;
	drv->drv.remove = lm_bus_remove;

	return driver_register(&drv->drv);
}

void lm_driver_unregister(struct lm_driver *drv)
{
	driver_unregister(&drv->drv);
}

static void lm_device_release(struct device *dev)
{
	struct lm_device *d = to_lm_device(dev);

	kfree(d);
}

int lm_device_register(struct lm_device *dev)
{
	int ret;

	dev->dev.release = lm_device_release;
	dev->dev.bus = &lm_bustype;

	snprintf(dev->dev.bus_id, sizeof(dev->dev.bus_id), "lm%d", dev->id);
	dev->resource.name = dev->dev.bus_id;

	ret = request_resource(&iomem_resource, &dev->resource);
	if (ret == 0) {
		ret = device_register(&dev->dev);
		if (ret)
			release_resource(&dev->resource);
	}
	return ret;
}

EXPORT_SYMBOL(lm_driver_register);
EXPORT_SYMBOL(lm_driver_unregister);
