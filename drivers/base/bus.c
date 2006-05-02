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
	ssize_t ret = -EIO;

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
	ssize_t ret = -EIO;

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


#ifdef CONFIG_HOTPLUG

/* Manually detach a device from its associated driver. */
static int driver_helper(struct device *dev, void *data)
{
	const char *name = data;

	if (strcmp(name, dev->bus_id) == 0)
		return 1;
	return 0;
}

static ssize_t driver_unbind(struct device_driver *drv,
			     const char *buf, size_t count)
{
	struct bus_type *bus = get_bus(drv->bus);
	struct device *dev;
	int err = -ENODEV;

	dev = bus_find_device(bus, NULL, (void *)buf, driver_helper);
	if (dev && dev->driver == drv) {
		if (dev->parent)	/* Needed for USB */
			down(&dev->parent->sem);
		device_release_driver(dev);
		if (dev->parent)
			up(&dev->parent->sem);
		err = count;
	}
	put_device(dev);
	put_bus(bus);
	return err;
}
static DRIVER_ATTR(unbind, S_IWUSR, NULL, driver_unbind);

/*
 * Manually attach a device to a driver.
 * Note: the driver must want to bind to the device,
 * it is not possible to override the driver's id table.
 */
static ssize_t driver_bind(struct device_driver *drv,
			   const char *buf, size_t count)
{
	struct bus_type *bus = get_bus(drv->bus);
	struct device *dev;
	int err = -ENODEV;

	dev = bus_find_device(bus, NULL, (void *)buf, driver_helper);
	if (dev && dev->driver == NULL) {
		if (dev->parent)	/* Needed for USB */
			down(&dev->parent->sem);
		down(&dev->sem);
		err = driver_probe_device(drv, dev);
		up(&dev->sem);
		if (dev->parent)
			up(&dev->parent->sem);

		if (err > 0) 		/* success */
			err = count;
		else if (err == 0)	/* driver didn't accept device */
			err = -ENODEV;
	}
	put_device(dev);
	put_bus(bus);
	return err;
}
static DRIVER_ATTR(bind, S_IWUSR, NULL, driver_bind);

#endif

static struct device * next_device(struct klist_iter * i)
{
	struct klist_node * n = klist_next(i);
	return n ? container_of(n, struct device, knode_bus) : NULL;
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
	struct klist_iter i;
	struct device * dev;
	int error = 0;

	if (!bus)
		return -EINVAL;

	klist_iter_init_node(&bus->klist_devices, &i,
			     (start ? &start->knode_bus : NULL));
	while ((dev = next_device(&i)) && !error)
		error = fn(dev, data);
	klist_iter_exit(&i);
	return error;
}

/**
 * bus_find_device - device iterator for locating a particular device.
 * @bus: bus type
 * @start: Device to begin with
 * @data: Data to pass to match function
 * @match: Callback function to check device
 *
 * This is similar to the bus_for_each_dev() function above, but it
 * returns a reference to a device that is 'found' for later use, as
 * determined by the @match callback.
 *
 * The callback should return 0 if the device doesn't match and non-zero
 * if it does.  If the callback returns non-zero, this function will
 * return to the caller and not iterate over any more devices.
 */
struct device * bus_find_device(struct bus_type *bus,
				struct device *start, void *data,
				int (*match)(struct device *, void *))
{
	struct klist_iter i;
	struct device *dev;

	if (!bus)
		return NULL;

	klist_iter_init_node(&bus->klist_devices, &i,
			     (start ? &start->knode_bus : NULL));
	while ((dev = next_device(&i)))
		if (match(dev, data) && get_device(dev))
			break;
	klist_iter_exit(&i);
	return dev;
}


static struct device_driver * next_driver(struct klist_iter * i)
{
	struct klist_node * n = klist_next(i);
	return n ? container_of(n, struct device_driver, knode_bus) : NULL;
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
	struct klist_iter i;
	struct device_driver * drv;
	int error = 0;

	if (!bus)
		return -EINVAL;

	klist_iter_init_node(&bus->klist_drivers, &i,
			     start ? &start->knode_bus : NULL);
	while ((drv = next_driver(&i)) && !error)
		error = fn(drv, data);
	klist_iter_exit(&i);
	return error;
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
 *	- Create link to device's bus.
 */
int bus_add_device(struct device * dev)
{
	struct bus_type * bus = get_bus(dev->bus);
	int error = 0;

	if (bus) {
		pr_debug("bus %s: add device %s\n", bus->name, dev->bus_id);
		error = device_add_attrs(bus, dev);
		if (!error) {
			sysfs_create_link(&bus->devices.kobj, &dev->kobj, dev->bus_id);
			sysfs_create_link(&dev->kobj, &dev->bus->subsys.kset.kobj, "bus");
		}
	}
	return error;
}

/**
 *	bus_attach_device - add device to bus
 *	@dev:	device tried to attach to a driver
 *
 *	- Try to attach to driver.
 */
void bus_attach_device(struct device * dev)
{
	struct bus_type * bus = dev->bus;

	if (bus) {
		device_attach(dev);
		klist_add_tail(&dev->knode_bus, &bus->klist_devices);
	}
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
		klist_remove(&dev->knode_bus);
		pr_debug("bus %s: remove device %s\n", dev->bus->name, dev->bus_id);
		device_release_driver(dev);
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

#ifdef CONFIG_HOTPLUG
/*
 * Thanks to drivers making their tables __devinit, we can't allow manual
 * bind and unbind from userspace unless CONFIG_HOTPLUG is enabled.
 */
static void add_bind_files(struct device_driver *drv)
{
	driver_create_file(drv, &driver_attr_unbind);
	driver_create_file(drv, &driver_attr_bind);
}

static void remove_bind_files(struct device_driver *drv)
{
	driver_remove_file(drv, &driver_attr_bind);
	driver_remove_file(drv, &driver_attr_unbind);
}
#else
static inline void add_bind_files(struct device_driver *drv) {}
static inline void remove_bind_files(struct device_driver *drv) {}
#endif

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

		driver_attach(drv);
		klist_add_tail(&drv->knode_bus, &bus->klist_drivers);
		module_add_driver(drv->owner, drv);

		driver_add_attrs(bus, drv);
		add_bind_files(drv);
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
		remove_bind_files(drv);
		driver_remove_attrs(drv->bus, drv);
		klist_remove(&drv->knode_bus);
		pr_debug("bus %s: remove driver %s\n", drv->bus->name, drv->name);
		driver_detach(drv);
		module_remove_driver(drv);
		kobject_unregister(&drv->kobj);
		put_bus(drv->bus);
	}
}


/* Helper for bus_rescan_devices's iter */
static int bus_rescan_devices_helper(struct device *dev, void *data)
{
	if (!dev->driver) {
		if (dev->parent)	/* Needed for USB */
			down(&dev->parent->sem);
		device_attach(dev);
		if (dev->parent)
			up(&dev->parent->sem);
	}
	return 0;
}

/**
 * bus_rescan_devices - rescan devices on the bus for possible drivers
 * @bus: the bus to scan.
 *
 * This function will look for devices on the bus with no driver
 * attached and rescan it against existing drivers to see if it matches
 * any by calling device_attach() for the unbound devices.
 */
void bus_rescan_devices(struct bus_type * bus)
{
	bus_for_each_dev(bus, NULL, NULL, bus_rescan_devices_helper);
}

/**
 * device_reprobe - remove driver for a device and probe for a new driver
 * @dev: the device to reprobe
 *
 * This function detaches the attached driver (if any) for the given
 * device and restarts the driver probing process.  It is intended
 * to use if probing criteria changed during a devices lifetime and
 * driver attachment should change accordingly.
 */
void device_reprobe(struct device *dev)
{
	if (dev->driver) {
		if (dev->parent)        /* Needed for USB */
			down(&dev->parent->sem);
		device_release_driver(dev);
		if (dev->parent)
			up(&dev->parent->sem);
	}

	bus_rescan_devices_helper(dev, NULL);
}
EXPORT_SYMBOL_GPL(device_reprobe);

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

static void klist_devices_get(struct klist_node *n)
{
	struct device *dev = container_of(n, struct device, knode_bus);

	get_device(dev);
}

static void klist_devices_put(struct klist_node *n)
{
	struct device *dev = container_of(n, struct device, knode_bus);

	put_device(dev);
}

static void klist_drivers_get(struct klist_node *n)
{
	struct device_driver *drv = container_of(n, struct device_driver,
						 knode_bus);

	get_driver(drv);
}

static void klist_drivers_put(struct klist_node *n)
{
	struct device_driver *drv = container_of(n, struct device_driver,
						 knode_bus);

	put_driver(drv);
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

	klist_init(&bus->klist_devices, klist_devices_get, klist_devices_put);
	klist_init(&bus->klist_drivers, klist_drivers_get, klist_drivers_put);
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
EXPORT_SYMBOL_GPL(bus_find_device);
EXPORT_SYMBOL_GPL(bus_for_each_drv);

EXPORT_SYMBOL_GPL(bus_register);
EXPORT_SYMBOL_GPL(bus_unregister);
EXPORT_SYMBOL_GPL(bus_rescan_devices);

EXPORT_SYMBOL_GPL(bus_create_file);
EXPORT_SYMBOL_GPL(bus_remove_file);
