/*
 * Industry-pack bus support functions.
 *
 * (C) 2011 Samuel Iglesias Gonsalvez <siglesia@cern.ch>, CERN
 * (C) 2012 Samuel Iglesias Gonsalvez <siglesias@igalia.com>, Igalia
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; version 2 of the License.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/mutex.h>
#include "ipack.h"

#define to_ipack_dev(device) container_of(device, struct ipack_device, dev)
#define to_ipack_driver(drv) container_of(drv, struct ipack_driver, driver)

/* used when allocating bus numbers */
#define IPACK_MAXBUS              64

static DEFINE_MUTEX(ipack_mutex);

struct ipack_busmap {
	unsigned long busmap[IPACK_MAXBUS / (8*sizeof(unsigned long))];
};
static struct ipack_busmap busmap;

static int ipack_bus_match(struct device *device, struct device_driver *driver)
{
	int ret;
	struct ipack_device *dev = to_ipack_dev(device);
	struct ipack_driver *drv = to_ipack_driver(driver);

	if (!drv->ops->match)
		return -EINVAL;

	ret = drv->ops->match(dev);
	if (ret)
		dev->driver = drv;

	return 0;
}

static int ipack_bus_probe(struct device *device)
{
	struct ipack_device *dev = to_ipack_dev(device);

	if (!dev->driver->ops->probe)
		return -EINVAL;

	return dev->driver->ops->probe(dev);
}

static int ipack_bus_remove(struct device *device)
{
	struct ipack_device *dev = to_ipack_dev(device);

	if (!dev->driver->ops->remove)
		return -EINVAL;

	dev->driver->ops->remove(dev);
	return 0;
}

static struct bus_type ipack_bus_type = {
	.name  = "ipack",
	.probe = ipack_bus_probe,
	.match = ipack_bus_match,
	.remove = ipack_bus_remove,
};

static int ipack_assign_bus_number(void)
{
	int busnum;

	mutex_lock(&ipack_mutex);
	busnum = find_next_zero_bit(busmap.busmap, IPACK_MAXBUS, 1);

	if (busnum >= IPACK_MAXBUS) {
		pr_err("too many buses\n");
		busnum = -1;
		goto error_find_busnum;
	}

	set_bit(busnum, busmap.busmap);

error_find_busnum:
	mutex_unlock(&ipack_mutex);
	return busnum;
}

int ipack_bus_register(struct ipack_bus_device *bus)
{
	int bus_nr;

	bus_nr = ipack_assign_bus_number();
	if (bus_nr < 0)
		return -1;

	bus->bus_nr = bus_nr;
	return 0;
}
EXPORT_SYMBOL_GPL(ipack_bus_register);

int ipack_bus_unregister(struct ipack_bus_device *bus)
{
	mutex_lock(&ipack_mutex);
	clear_bit(bus->bus_nr, busmap.busmap);
	mutex_unlock(&ipack_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(ipack_bus_unregister);

int ipack_driver_register(struct ipack_driver *edrv)
{
	edrv->driver.bus = &ipack_bus_type;
	return driver_register(&edrv->driver);
}
EXPORT_SYMBOL_GPL(ipack_driver_register);

void ipack_driver_unregister(struct ipack_driver *edrv)
{
	driver_unregister(&edrv->driver);
}
EXPORT_SYMBOL_GPL(ipack_driver_unregister);

static void ipack_device_release(struct device *dev)
{
}

int ipack_device_register(struct ipack_device *dev)
{
	int ret;

	dev->dev.bus = &ipack_bus_type;
	dev->dev.release = ipack_device_release;
	dev_set_name(&dev->dev,
		     "%s.%u.%u", dev->board_name, dev->bus_nr, dev->slot);

	ret = device_register(&dev->dev);
	if (ret < 0) {
		pr_err("error registering the device.\n");
		dev->driver->ops->remove(dev);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(ipack_device_register);

void ipack_device_unregister(struct ipack_device *dev)
{
	device_unregister(&dev->dev);
}
EXPORT_SYMBOL_GPL(ipack_device_unregister);

static int __init ipack_init(void)
{
	return bus_register(&ipack_bus_type);
}

static void __exit ipack_exit(void)
{
	bus_unregister(&ipack_bus_type);
}

module_init(ipack_init);
module_exit(ipack_exit);

MODULE_AUTHOR("Samuel Iglesias Gonsalvez <siglesias@igalia.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Industry-pack bus core");
