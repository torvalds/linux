/*
 *	drivers/base/dd.c - The core device/driver interactions.
 *
 * 	This file contains the (sometimes tricky) code that controls the
 *	interactions between devices and drivers, which primarily includes
 *	driver binding and unbinding.
 *
 *	All of this code used to exist in drivers/base/bus.c, but was
 *	relocated to here in the name of compartmentalization (since it wasn't
 *	strictly code just for the 'struct bus_type'.
 *
 *	Copyright (c) 2002-5 Patrick Mochel
 *	Copyright (c) 2002-3 Open Source Development Labs
 *
 *	This file is released under the GPLv2
 */

#include <linux/device.h>
#include <linux/module.h>

#include "base.h"
#include "power/power.h"

#define to_drv(node) container_of(node, struct device_driver, kobj.entry)


/**
 *	device_bind_driver - bind a driver to one device.
 *	@dev:	device.
 *
 *	Allow manual attachment of a driver to a device.
 *	Caller must have already set @dev->driver.
 *
 *	Note that this does not modify the bus reference count
 *	nor take the bus's rwsem. Please verify those are accounted
 *	for before calling this. (It is ok to call with no other effort
 *	from a driver's probe() method.)
 */
void device_bind_driver(struct device * dev)
{
	pr_debug("bound device '%s' to driver '%s'\n",
		 dev->bus_id, dev->driver->name);
	klist_add_tail(&dev->driver->klist_devices, &dev->knode_driver);
	sysfs_create_link(&dev->driver->kobj, &dev->kobj,
			  kobject_name(&dev->kobj));
	sysfs_create_link(&dev->kobj, &dev->driver->kobj, "driver");
}

/**
 *	driver_probe_device - attempt to bind device & driver.
 *	@drv:	driver.
 *	@dev:	device.
 *
 *	First, we call the bus's match function, if one present, which
 *	should compare the device IDs the driver supports with the
 *	device IDs of the device. Note we don't do this ourselves
 *	because we don't know the format of the ID structures, nor what
 *	is to be considered a match and what is not.
 *
 *	If we find a match, we call @drv->probe(@dev) if it exists, and
 *	call device_bind_driver() above.
 */
int driver_probe_device(struct device_driver * drv, struct device * dev)
{
	int error = 0;

	if (drv->bus->match && !drv->bus->match(dev, drv))
		return -ENODEV;

	down(&dev->sem);
	dev->driver = drv;
	if (drv->probe) {
		error = drv->probe(dev);
		if (error) {
			dev->driver = NULL;
			up(&dev->sem);
			return error;
		}
	}
	up(&dev->sem);
	device_bind_driver(dev);
	return 0;
}

static int __device_attach(struct device_driver * drv, void * data)
{
	struct device * dev = data;
	int error;

	error = driver_probe_device(drv, dev);

	if (error == -ENODEV && error == -ENXIO) {
		/* Driver matched, but didn't support device
		 * or device not found.
		 * Not an error; keep going.
		 */
		error = 0;
	} else {
		/* driver matched but the probe failed */
		printk(KERN_WARNING
		       "%s: probe of %s failed with error %d\n",
		       drv->name, dev->bus_id, error);
	}
	return 0;
}

/**
 *	device_attach - try to attach device to a driver.
 *	@dev:	device.
 *
 *	Walk the list of drivers that the bus has and call
 *	driver_probe_device() for each pair. If a compatible
 *	pair is found, break out and return.
 */
int device_attach(struct device * dev)
{
	if (dev->driver) {
		device_bind_driver(dev);
		return 1;
	}

	return bus_for_each_drv(dev->bus, NULL, dev, __device_attach);
}

static int __driver_attach(struct device * dev, void * data)
{
	struct device_driver * drv = data;
	int error = 0;

	if (!dev->driver) {
		error = driver_probe_device(drv, dev);
		if (error) {
			if (error != -ENODEV) {
				/* driver matched but the probe failed */
				printk(KERN_WARNING
				       "%s: probe of %s failed with error %d\n",
				       drv->name, dev->bus_id, error);
			} else
				error = 0;
		}
	}
	return 0;
}

/**
 *	driver_attach - try to bind driver to devices.
 *	@drv:	driver.
 *
 *	Walk the list of devices that the bus has on it and try to
 *	match the driver with each one.  If driver_probe_device()
 *	returns 0 and the @dev->driver is set, we've found a
 *	compatible pair.
 *
 *	Note that we ignore the -ENODEV error from driver_probe_device(),
 *	since it's perfectly valid for a driver not to bind to any devices.
 */
void driver_attach(struct device_driver * drv)
{
	bus_for_each_dev(drv->bus, NULL, drv, __driver_attach);
}

/**
 *	device_release_driver - manually detach device from driver.
 *	@dev:	device.
 *
 *	Manually detach device from driver.
 *	Note that this is called without incrementing the bus
 *	reference count nor taking the bus's rwsem. Be sure that
 *	those are accounted for before calling this function.
 */
void device_release_driver(struct device * dev)
{
	struct device_driver * drv = dev->driver;

	if (!drv)
		return;

	sysfs_remove_link(&drv->kobj, kobject_name(&dev->kobj));
	sysfs_remove_link(&dev->kobj, "driver");
	klist_remove(&dev->knode_driver);

	down(&dev->sem);
	if (drv->remove)
		drv->remove(dev);
	dev->driver = NULL;
	up(&dev->sem);
}

static int __remove_driver(struct device * dev, void * unused)
{
	device_release_driver(dev);
	return 0;
}

/**
 * driver_detach - detach driver from all devices it controls.
 * @drv: driver.
 */
void driver_detach(struct device_driver * drv)
{
	driver_for_each_device(drv, NULL, NULL, __remove_driver);
}


EXPORT_SYMBOL_GPL(driver_probe_device);
EXPORT_SYMBOL_GPL(device_bind_driver);
EXPORT_SYMBOL_GPL(device_release_driver);
EXPORT_SYMBOL_GPL(device_attach);
EXPORT_SYMBOL_GPL(driver_attach);

