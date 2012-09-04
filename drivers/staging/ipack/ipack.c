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

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include "ipack.h"

#define to_ipack_dev(device) container_of(device, struct ipack_device, dev)
#define to_ipack_driver(drv) container_of(drv, struct ipack_driver, driver)

static DEFINE_IDA(ipack_ida);

static void ipack_device_release(struct device *dev)
{
	struct ipack_device *device = to_ipack_dev(dev);
	kfree(device->id);
	kfree(device);
}

static int ipack_bus_match(struct device *device, struct device_driver *driver)
{
	int ret;
	struct ipack_device *dev = to_ipack_dev(device);
	struct ipack_driver *drv = to_ipack_driver(driver);

	if ((!drv->ops) || (!drv->ops->match))
		return -EINVAL;

	ret = drv->ops->match(dev);
	if (ret)
		dev->driver = drv;

	return ret;
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

struct ipack_bus_device *ipack_bus_register(struct device *parent, int slots,
					    struct ipack_bus_ops *ops)
{
	int bus_nr;
	struct ipack_bus_device *bus;

	bus = kzalloc(sizeof(struct ipack_bus_device), GFP_KERNEL);
	if (!bus)
		return NULL;

	bus_nr = ida_simple_get(&ipack_ida, 0, 0, GFP_KERNEL);
	if (bus_nr < 0) {
		kfree(bus);
		return NULL;
	}

	bus->bus_nr = bus_nr;
	bus->parent = parent;
	bus->slots = slots;
	bus->ops = ops;
	return bus;
}
EXPORT_SYMBOL_GPL(ipack_bus_register);

int ipack_bus_unregister(struct ipack_bus_device *bus)
{
	ida_simple_remove(&ipack_ida, bus->bus_nr);
	kfree(bus);
	return 0;
}
EXPORT_SYMBOL_GPL(ipack_bus_unregister);

int ipack_driver_register(struct ipack_driver *edrv, struct module *owner,
			  char *name)
{
	edrv->driver.owner = owner;
	edrv->driver.name = name;
	edrv->driver.bus = &ipack_bus_type;
	return driver_register(&edrv->driver);
}
EXPORT_SYMBOL_GPL(ipack_driver_register);

void ipack_driver_unregister(struct ipack_driver *edrv)
{
	driver_unregister(&edrv->driver);
}
EXPORT_SYMBOL_GPL(ipack_driver_unregister);

static int ipack_device_read_id(struct ipack_device *dev)
{
	u8 __iomem *idmem;
	int i;
	int ret = 0;

	ret = dev->bus->ops->map_space(dev, 0, IPACK_ID_SPACE);
	if (ret) {
		dev_err(&dev->dev, "error mapping memory\n");
		return ret;
	}
	idmem = dev->id_space.address;

	/* Determine ID PROM Data Format.  If we find the ids "IPAC" or "IPAH"
	 * we are dealing with a IndustryPack  format 1 device.  If we detect
	 * "VITA4 " (16 bit big endian formatted) we are dealing with a
	 * IndustryPack format 2 device */
	if ((ioread8(idmem + 1) == 'I') &&
			(ioread8(idmem + 3) == 'P') &&
			(ioread8(idmem + 5) == 'A') &&
			((ioread8(idmem + 7) == 'C') ||
			 (ioread8(idmem + 7) == 'H'))) {
		dev->id_format = IPACK_ID_VERSION_1;
		dev->id_avail = ioread8(idmem + 0x15);
		if ((dev->id_avail < 0x0c) || (dev->id_avail > 0x40)) {
			dev_warn(&dev->dev, "invalid id size");
			dev->id_avail = 0x0c;
		}
	} else if ((ioread8(idmem + 0) == 'I') &&
			(ioread8(idmem + 1) == 'V') &&
			(ioread8(idmem + 2) == 'A') &&
			(ioread8(idmem + 3) == 'T') &&
			(ioread8(idmem + 4) == ' ') &&
			(ioread8(idmem + 5) == '4')) {
		dev->id_format = IPACK_ID_VERSION_2;
		dev->id_avail = ioread16be(idmem + 0x16);
		if ((dev->id_avail < 0x1a) || (dev->id_avail > 0x40)) {
			dev_warn(&dev->dev, "invalid id size");
			dev->id_avail = 0x1a;
		}
	} else {
		dev->id_format = IPACK_ID_VERSION_INVALID;
		dev->id_avail = 0;
	}

	if (!dev->id_avail) {
		ret = -ENODEV;
		goto out;
	}

	/* Obtain the amount of memory required to store a copy of the complete
	 * ID ROM contents */
	dev->id = kmalloc(dev->id_avail, GFP_KERNEL);
	if (!dev->id) {
		dev_err(&dev->dev, "dev->id alloc failed.\n");
		ret = -ENOMEM;
		goto out;
	}
	for (i = 0; i < dev->id_avail; i++) {
		if (dev->id_format == IPACK_ID_VERSION_1)
			dev->id[i] = ioread8(idmem + (i << 1) + 1);
		else
			dev->id[i] = ioread8(idmem + i);
	}

out:
	dev->bus->ops->unmap_space(dev, IPACK_ID_SPACE);

	return ret;
}

struct ipack_device *ipack_device_register(struct ipack_bus_device *bus,
					   int slot, int irqv)
{
	int ret;
	struct ipack_device *dev;

	dev = kzalloc(sizeof(struct ipack_device), GFP_KERNEL);
	if (!dev)
		return NULL;

	dev->dev.bus = &ipack_bus_type;
	dev->dev.release = ipack_device_release;
	dev->dev.parent = bus->parent;
	dev->slot = slot;
	dev->bus_nr = bus->bus_nr;
	dev->irq = irqv;
	dev->bus = bus;
	dev_set_name(&dev->dev,
		     "ipack-dev.%u.%u", dev->bus_nr, dev->slot);

	ret = ipack_device_read_id(dev);
	if (ret < 0) {
		dev_err(&dev->dev, "error reading device id section.\n");
		kfree(dev);
		return NULL;
	}

	ret = device_register(&dev->dev);
	if (ret < 0) {
		kfree(dev->id);
		kfree(dev);
		return NULL;
	}

	return dev;
}
EXPORT_SYMBOL_GPL(ipack_device_register);

void ipack_device_unregister(struct ipack_device *dev)
{
	device_unregister(&dev->dev);
}
EXPORT_SYMBOL_GPL(ipack_device_unregister);

static int __init ipack_init(void)
{
	ida_init(&ipack_ida);
	return bus_register(&ipack_bus_type);
}

static void __exit ipack_exit(void)
{
	bus_unregister(&ipack_bus_type);
	ida_destroy(&ipack_ida);
}

module_init(ipack_init);
module_exit(ipack_exit);

MODULE_AUTHOR("Samuel Iglesias Gonsalvez <siglesias@igalia.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Industry-pack bus core");
