/*
 *    Copyright (C) 2006 Benjamin Herrenschmidt, IBM Corp.
 *			 <benh@kernel.crashing.org>
 *    and		 Arnd Bergmann, IBM Corp.
 *    Merged from powerpc/kernel/of_platform.c and
 *    sparc{,64}/kernel/of_device.c by Stephen Rothwell
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>

extern struct device_attribute of_platform_device_attrs[];

static int of_platform_bus_match(struct device *dev, struct device_driver *drv)
{
	struct of_device *of_dev = to_of_device(dev);
	struct of_platform_driver *of_drv = to_of_platform_driver(drv);
	const struct of_device_id *matches = of_drv->match_table;

	if (!matches)
		return 0;

	return of_match_device(matches, of_dev) != NULL;
}

static int of_platform_device_probe(struct device *dev)
{
	int error = -ENODEV;
	struct of_platform_driver *drv;
	struct of_device *of_dev;
	const struct of_device_id *match;

	drv = to_of_platform_driver(dev->driver);
	of_dev = to_of_device(dev);

	if (!drv->probe)
		return error;

	of_dev_get(of_dev);

	match = of_match_device(drv->match_table, of_dev);
	if (match)
		error = drv->probe(of_dev, match);
	if (error)
		of_dev_put(of_dev);

	return error;
}

static int of_platform_device_remove(struct device *dev)
{
	struct of_device *of_dev = to_of_device(dev);
	struct of_platform_driver *drv = to_of_platform_driver(dev->driver);

	if (dev->driver && drv->remove)
		drv->remove(of_dev);
	return 0;
}

static int of_platform_device_suspend(struct device *dev, pm_message_t state)
{
	struct of_device *of_dev = to_of_device(dev);
	struct of_platform_driver *drv = to_of_platform_driver(dev->driver);
	int error = 0;

	if (dev->driver && drv->suspend)
		error = drv->suspend(of_dev, state);
	return error;
}

static int of_platform_device_resume(struct device * dev)
{
	struct of_device *of_dev = to_of_device(dev);
	struct of_platform_driver *drv = to_of_platform_driver(dev->driver);
	int error = 0;

	if (dev->driver && drv->resume)
		error = drv->resume(of_dev);
	return error;
}

static void of_platform_device_shutdown(struct device *dev)
{
	struct of_device *of_dev = to_of_device(dev);
	struct of_platform_driver *drv = to_of_platform_driver(dev->driver);

	if (dev->driver && drv->shutdown)
		drv->shutdown(of_dev);
}

int of_bus_type_init(struct bus_type *bus, const char *name)
{
	bus->name = name;
	bus->match = of_platform_bus_match;
	bus->probe = of_platform_device_probe;
	bus->remove = of_platform_device_remove;
	bus->suspend = of_platform_device_suspend;
	bus->resume = of_platform_device_resume;
	bus->shutdown = of_platform_device_shutdown;
	bus->dev_attrs = of_platform_device_attrs;
	return bus_register(bus);
}

int of_register_driver(struct of_platform_driver *drv, struct bus_type *bus)
{
	/* initialize common driver fields */
	if (!drv->driver.name)
		drv->driver.name = drv->name;
	if (!drv->driver.owner)
		drv->driver.owner = drv->owner;
	drv->driver.bus = bus;

	/* register with core */
	return driver_register(&drv->driver);
}
EXPORT_SYMBOL(of_register_driver);

void of_unregister_driver(struct of_platform_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL(of_unregister_driver);
