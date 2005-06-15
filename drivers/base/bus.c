/*
 * bus.c - bus driver management
 *
 * Copyright (c) 2002-3 Patrick Mochel
 * Copyright (c) 2002-3 Open Source Development Labs
 *
 * This file is released under the GPLv2
 *
 */

#include <linux/config.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/string.h>
#include "base.h"
#include "power/power.h"

#define to_dev(node) container_of(node, struct device, bus_list)
#define to_drv(node) container_of(node, struct device_driver, kobj.entry)

#define to_bus_attr(_attr) container_of(_attr, struct bus_attribute, attr)
#define to_bus(obj) container_of(obj, struct bus_type, subsys.kset.kobj)

/*
 * sysfs bindings for drivers
 */

#define to_drv_attr(_attr) container_of(_attr, struct driver_attribute, attr)
#define to_driver(obj) container_of(obj, struct device_driver, kobj)


static ssize_t
drv_attr_show(struct kobject * kobj, struct attribute * attr, char * buf)
{
	struct driver_attribute * drv_attr = to_drv_attr(attr);
	struct device_driver * drv = to_driver(kobj);
	ssize_t ret = 0;

	if (drv_attr->show)
		ret = drv_attr->show(drv, buf);
	return ret;
}

static ssize_t
drv_attr_store(struct kobject * kobj, struct attribute * attr,
	       const char * buf, size_t count)
{
	struct driver_attribute * drv_attr = to_drv_attr(attr);
	struct device_driver * drv = to_driver(kobj);
	ssize_t ret = 0;

	if (drv_attr->store)
		ret = drv_attr->store(drv, buf, count);
	return ret;
}

static struct sysfs_ops driver_sysfs_ops = {
	.show	= drv_attr_show,
	.store	= drv_attr_store,
};


static void driver_release(struct kobject * kobj)
{
	struct device_driver * drv = to_driver(kobj);
	complete(&drv->unloaded);
}

static struct kobj_type ktype_driver = {
	.sysfs_ops	= &driver_sysfs_ops,
	.release	= driver_release,
};


/*
 * sysfs bindings for buses
 */


static ssize_t
bus_attr_show(struct kobject * kobj, struct attribute * attr, char * buf)
{
	struct bus_attribute * bus_attr = to_bus_attr(attr);
	struct bus_type * bus = to_bus(kobj);
	ssize_t ret = 0;

	if (bus_attr->show)
		ret = bus_attr->show(bus, buf);
	return ret;
}

static ssize_t
bus_attr_store(struct kobject * kobj, struct attribute * attr,
	       const char * buf, size_t count)
{
	struct bus_attribute * bus_attr = to_bus_attr(attr);
	struct bus_type * bus = to_bus(kobj);
	ssize_t ret = 0;

	if (bus_attr->store)
		ret = bus_attr->store(bus, buf, count);
	return ret;
}

static struct sysfs_ops bus_sysfs_ops = {
	.show	= bus_attr_show,
	.store	= bus_attr_store,
};

int bus_create_file(struct bus_type * bus, struct bus_attribute * attr)
{
	int error;
	if (get_bus(bus)) {
		error = sysfs_create_file(&bus->subsys.kset.kobj, &attr->attr);
		put_bus(bus);
	} else
		error = -EINVAL;
	return error;
}

void bus_remove_file(struct bus_type * bus, struct bus_attribute * attr)
{
	if (get_bus(bus)) {
		sysfs_remove_file(&bus->subsys.kset.kobj, &attr->attr);
		put_bus(bus);
	}
}

static struct kobj_type ktype_bus = {
	.sysfs_ops	= &bus_sysfs_ops,

};

decl_subsys(bus, &ktype_bus, NULL);

static int __bus_for_each_dev(struct bus_type *bus, struct device *start,
			      void *data, int (*fn)(struct device *, void *))
{
	struct list_head *head;
	struct device *dev;
	int error = 0;

	if (!(bus = get_bus(bus)))
		return -EINVAL;

	head = &bus->devices.list;
	dev = list_prepare_entry(start, head, bus_list);
	list_for_each_entry_continue(dev, head, bus_list) {
		get_device(dev);
		error = fn(dev, data);
		put_device(dev);
		if (error)
			break;
	}
	put_bus(bus);
	return error;
}

static int __bus_for_each_drv(struct bus_type *bus, struct device_driver *start,
			      void * data, int (*fn)(struct device_driver *, void *))
{
	struct list_head *head;
	struct device_driver *drv;
	int error = 0;

	if (!(bus = get_bus(bus)))
		return -EINVAL;

	head = &bus->drivers.list;
	drv = list_prepare_entry(start, head, kobj.entry);
	list_for_each_entry_continue(drv, head, kobj.entry) {
		get_driver(drv);
		error = fn(drv, data);
		put_driver(drv);
		if (error)
			break;
	}
	put_bus(bus);
	return error;
}

/**
 *	bus_for_each_dev - device iterator.
 *	@bus:	bus type.
 *	@start:	device to start iterating from.
 *	@data:	data for the callback.
 *	@fn:	function to be called for each device.
 *
 *	Iterate over @bus's list of devices, and call @fn for each,
 *	passing it @data. If @start is not NULL, we use that device to
 *	begin iterating from.
 *
 *	We check the return of @fn each time. If it returns anything
 *	other than 0, we break out and return that value.
 *
 *	NOTE: The device that returns a non-zero value is not retained
 *	in any way, nor is its refcount incremented. If the caller needs
 *	to retain this data, it should do, and increment the reference
 *	count in the supplied callback.
 */

int bus_for_each_dev(struct bus_type * bus, struct device * start,
		     void * data, int (*fn)(struct device *, void *))
{
	int ret;

	down_read(&bus->subsys.rwsem);
	ret = __bus_for_each_dev(bus, start, data, fn);
	up_read(&bus->subsys.rwsem);
	return ret;
}

/**
 *	bus_for_each_drv - driver iterator
 *	@bus:	bus we're dealing with.
 *	@start:	driver to start iterating on.
 *	@data:	data to pass to the callback.
 *	@fn:	function to call for each driver.
 *
 *	This is nearly identical to the device iterator above.
 *	We iterate over each driver that belongs to @bus, and call
 *	@fn for each. If @fn returns anything but 0, we break out
 *	and return it. If @start is not NULL, we use it as the head
 *	of the list.
 *
 *	NOTE: we don't return the driver that returns a non-zero
 *	value, nor do we leave the reference count incremented for that
 *	driver. If the caller needs to know that info, it must set it
 *	in the callback. It must also be sure to increment the refcount
 *	so it doesn't disappear before returning to the caller.
 */

int bus_for_each_drv(struct bus_type * bus, struct device_driver * start,
		     void * data, int (*fn)(struct device_driver *, void *))
{
	int ret;

	down_read(&bus->subsys.rwsem);
	ret = __bus_for_each_drv(bus, start, data, fn);
	up_read(&bus->subsys.rwsem);
	return ret;
}

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
	list_add_tail(&dev->driver_list, &dev->driver->devices);
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
	if (drv->bus->match && !drv->bus->match(dev, drv))
		return -ENODEV;

	dev->driver = drv;
	if (drv->probe) {
		int error = drv->probe(dev);
		if (error) {
			dev->driver = NULL;
			return error;
		}
	}

	device_bind_driver(dev);
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
 	struct bus_type * bus = dev->bus;
	struct list_head * entry;
	int error;

	if (dev->driver) {
		device_bind_driver(dev);
		return 1;
	}

	if (bus->match) {
		list_for_each(entry, &bus->drivers.list) {
			struct device_driver * drv = to_drv(entry);
			error = driver_probe_device(drv, dev);
			if (!error)
				/* success, driver matched */
				return 1;
			if (error != -ENODEV && error != -ENXIO)
				/* driver matched but the probe failed */
				printk(KERN_WARNING
				    "%s: probe of %s failed with error %d\n",
				    drv->name, dev->bus_id, error);
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
	struct bus_type * bus = drv->bus;
	struct list_head * entry;
	int error;

	if (!bus->match)
		return;

	list_for_each(entry, &bus->devices.list) {
		struct device * dev = container_of(entry, struct device, bus_list);
		if (!dev->driver) {
			error = driver_probe_device(drv, dev);
			if (error && (error != -ENODEV))
				/* driver matched but the probe failed */
				printk(KERN_WARNING
				    "%s: probe of %s failed with error %d\n",
				    drv->name, dev->bus_id, error);
		}
	}
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
	if (drv) {
		sysfs_remove_link(&drv->kobj, kobject_name(&dev->kobj));
		sysfs_remove_link(&dev->kobj, "driver");
		list_del_init(&dev->driver_list);
		if (drv->remove)
			drv->remove(dev);
		dev->driver = NULL;
	}
}


/**
 *	driver_detach - detach driver from all devices it controls.
 *	@drv:	driver.
 */

static void driver_detach(struct device_driver * drv)
{
	while (!list_empty(&drv->devices)) {
		struct device * dev = container_of(drv->devices.next, struct device, driver_list);
		device_release_driver(dev);
	}
}

static int device_add_attrs(struct bus_type * bus, struct device * dev)
{
	int error = 0;
	int i;

	if (bus->dev_attrs) {
		for (i = 0; attr_name(bus->dev_attrs[i]); i++) {
			error = device_create_file(dev,&bus->dev_attrs[i]);
			if (error)
				goto Err;
		}
	}
 Done:
	return error;
 Err:
	while (--i >= 0)
		device_remove_file(dev,&bus->dev_attrs[i]);
	goto Done;
}


static void device_remove_attrs(struct bus_type * bus, struct device * dev)
{
	int i;

	if (bus->dev_attrs) {
		for (i = 0; attr_name(bus->dev_attrs[i]); i++)
			device_remove_file(dev,&bus->dev_attrs[i]);
	}
}


/**
 *	bus_add_device - add device to bus
 *	@dev:	device being added
 *
 *	- Add the device to its bus's list of devices.
 *	- Try to attach to driver.
 *	- Create link to device's physical location.
 */
int bus_add_device(struct device * dev)
{
	struct bus_type * bus = get_bus(dev->bus);
	int error = 0;

	if (bus) {
		down_write(&dev->bus->subsys.rwsem);
		pr_debug("bus %s: add device %s\n", bus->name, dev->bus_id);
		list_add_tail(&dev->bus_list, &dev->bus->devices.list);
		device_attach(dev);
		up_write(&dev->bus->subsys.rwsem);
		device_add_attrs(bus, dev);
		sysfs_create_link(&bus->devices.kobj, &dev->kobj, dev->bus_id);
		sysfs_create_link(&dev->kobj, &dev->bus->subsys.kset.kobj, "bus");
	}
	return error;
}

/**
 *	bus_remove_device - remove device from bus
 *	@dev:	device to be removed
 *
 *	- Remove symlink from bus's directory.
 *	- Delete device from bus's list.
 *	- Detach from its driver.
 *	- Drop reference taken in bus_add_device().
 */
void bus_remove_device(struct device * dev)
{
	if (dev->bus) {
		sysfs_remove_link(&dev->kobj, "bus");
		sysfs_remove_link(&dev->bus->devices.kobj, dev->bus_id);
		device_remove_attrs(dev->bus, dev);
		down_write(&dev->bus->subsys.rwsem);
		pr_debug("bus %s: remove device %s\n", dev->bus->name, dev->bus_id);
		device_release_driver(dev);
		list_del_init(&dev->bus_list);
		up_write(&dev->bus->subsys.rwsem);
		put_bus(dev->bus);
	}
}

static int driver_add_attrs(struct bus_type * bus, struct device_driver * drv)
{
	int error = 0;
	int i;

	if (bus->drv_attrs) {
		for (i = 0; attr_name(bus->drv_attrs[i]); i++) {
			error = driver_create_file(drv, &bus->drv_attrs[i]);
			if (error)
				goto Err;
		}
	}
 Done:
	return error;
 Err:
	while (--i >= 0)
		driver_remove_file(drv, &bus->drv_attrs[i]);
	goto Done;
}


static void driver_remove_attrs(struct bus_type * bus, struct device_driver * drv)
{
	int i;

	if (bus->drv_attrs) {
		for (i = 0; attr_name(bus->drv_attrs[i]); i++)
			driver_remove_file(drv, &bus->drv_attrs[i]);
	}
}


/**
 *	bus_add_driver - Add a driver to the bus.
 *	@drv:	driver.
 *
 */
int bus_add_driver(struct device_driver * drv)
{
	struct bus_type * bus = get_bus(drv->bus);
	int error = 0;

	if (bus) {
		pr_debug("bus %s: add driver %s\n", bus->name, drv->name);
		error = kobject_set_name(&drv->kobj, "%s", drv->name);
		if (error) {
			put_bus(bus);
			return error;
		}
		drv->kobj.kset = &bus->drivers;
		if ((error = kobject_register(&drv->kobj))) {
			put_bus(bus);
			return error;
		}

		down_write(&bus->subsys.rwsem);
		driver_attach(drv);
		up_write(&bus->subsys.rwsem);
		module_add_driver(drv->owner, drv);

		driver_add_attrs(bus, drv);
	}
	return error;
}


/**
 *	bus_remove_driver - delete driver from bus's knowledge.
 *	@drv:	driver.
 *
 *	Detach the driver from the devices it controls, and remove
 *	it from its bus's list of drivers. Finally, we drop the reference
 *	to the bus we took in bus_add_driver().
 */

void bus_remove_driver(struct device_driver * drv)
{
	if (drv->bus) {
		driver_remove_attrs(drv->bus, drv);
		down_write(&drv->bus->subsys.rwsem);
		pr_debug("bus %s: remove driver %s\n", drv->bus->name, drv->name);
		driver_detach(drv);
		up_write(&drv->bus->subsys.rwsem);
		module_remove_driver(drv);
		kobject_unregister(&drv->kobj);
		put_bus(drv->bus);
	}
}


/* Helper for bus_rescan_devices's iter */
static int bus_rescan_devices_helper(struct device *dev, void *data)
{
	int *count = data;

	if (!dev->driver && device_attach(dev))
		(*count)++;

	return 0;
}


/**
 *	bus_rescan_devices - rescan devices on the bus for possible drivers
 *	@bus:	the bus to scan.
 *
 *	This function will look for devices on the bus with no driver
 *	attached and rescan it against existing drivers to see if it
 *	matches any. Calls device_attach(). Returns the number of devices
 *	that were sucessfully bound to a driver.
 */
int bus_rescan_devices(struct bus_type * bus)
{
	int count = 0;

	down_write(&bus->subsys.rwsem);
	__bus_for_each_dev(bus, NULL, &count, bus_rescan_devices_helper);
	up_write(&bus->subsys.rwsem);

	return count;
}


struct bus_type * get_bus(struct bus_type * bus)
{
	return bus ? container_of(subsys_get(&bus->subsys), struct bus_type, subsys) : NULL;
}

void put_bus(struct bus_type * bus)
{
	subsys_put(&bus->subsys);
}


/**
 *	find_bus - locate bus by name.
 *	@name:	name of bus.
 *
 *	Call kset_find_obj() to iterate over list of buses to
 *	find a bus by name. Return bus if found.
 *
 *	Note that kset_find_obj increments bus' reference count.
 */

struct bus_type * find_bus(char * name)
{
	struct kobject * k = kset_find_obj(&bus_subsys.kset, name);
	return k ? to_bus(k) : NULL;
}


/**
 *	bus_add_attrs - Add default attributes for this bus.
 *	@bus:	Bus that has just been registered.
 */

static int bus_add_attrs(struct bus_type * bus)
{
	int error = 0;
	int i;

	if (bus->bus_attrs) {
		for (i = 0; attr_name(bus->bus_attrs[i]); i++) {
			if ((error = bus_create_file(bus,&bus->bus_attrs[i])))
				goto Err;
		}
	}
 Done:
	return error;
 Err:
	while (--i >= 0)
		bus_remove_file(bus,&bus->bus_attrs[i]);
	goto Done;
}

static void bus_remove_attrs(struct bus_type * bus)
{
	int i;

	if (bus->bus_attrs) {
		for (i = 0; attr_name(bus->bus_attrs[i]); i++)
			bus_remove_file(bus,&bus->bus_attrs[i]);
	}
}

/**
 *	bus_register - register a bus with the system.
 *	@bus:	bus.
 *
 *	Once we have that, we registered the bus with the kobject
 *	infrastructure, then register the children subsystems it has:
 *	the devices and drivers that belong to the bus.
 */
int bus_register(struct bus_type * bus)
{
	int retval;

	retval = kobject_set_name(&bus->subsys.kset.kobj, "%s", bus->name);
	if (retval)
		goto out;

	subsys_set_kset(bus, bus_subsys);
	retval = subsystem_register(&bus->subsys);
	if (retval)
		goto out;

	kobject_set_name(&bus->devices.kobj, "devices");
	bus->devices.subsys = &bus->subsys;
	retval = kset_register(&bus->devices);
	if (retval)
		goto bus_devices_fail;

	kobject_set_name(&bus->drivers.kobj, "drivers");
	bus->drivers.subsys = &bus->subsys;
	bus->drivers.ktype = &ktype_driver;
	retval = kset_register(&bus->drivers);
	if (retval)
		goto bus_drivers_fail;
	bus_add_attrs(bus);

	pr_debug("bus type '%s' registered\n", bus->name);
	return 0;

bus_drivers_fail:
	kset_unregister(&bus->devices);
bus_devices_fail:
	subsystem_unregister(&bus->subsys);
out:
	return retval;
}


/**
 *	bus_unregister - remove a bus from the system
 *	@bus:	bus.
 *
 *	Unregister the child subsystems and the bus itself.
 *	Finally, we call put_bus() to release the refcount
 */
void bus_unregister(struct bus_type * bus)
{
	pr_debug("bus %s: unregistering\n", bus->name);
	bus_remove_attrs(bus);
	kset_unregister(&bus->drivers);
	kset_unregister(&bus->devices);
	subsystem_unregister(&bus->subsys);
}

int __init buses_init(void)
{
	return subsystem_register(&bus_subsys);
}


EXPORT_SYMBOL_GPL(bus_for_each_dev);
EXPORT_SYMBOL_GPL(bus_for_each_drv);

EXPORT_SYMBOL_GPL(driver_probe_device);
EXPORT_SYMBOL_GPL(device_bind_driver);
EXPORT_SYMBOL_GPL(device_release_driver);
EXPORT_SYMBOL_GPL(device_attach);
EXPORT_SYMBOL_GPL(driver_attach);

EXPORT_SYMBOL_GPL(bus_add_device);
EXPORT_SYMBOL_GPL(bus_remove_device);
EXPORT_SYMBOL_GPL(bus_register);
EXPORT_SYMBOL_GPL(bus_unregister);
EXPORT_SYMBOL_GPL(bus_rescan_devices);
EXPORT_SYMBOL_GPL(get_bus);
EXPORT_SYMBOL_GPL(put_bus);
EXPORT_SYMBOL_GPL(find_bus);

EXPORT_SYMBOL_GPL(bus_create_file);
EXPORT_SYMBOL_GPL(bus_remove_file);
