// SPDX-License-Identifier: GPL-2.0
/*
 * bus.c - bus driver management
 *
 * Copyright (c) 2002-3 Patrick Mochel
 * Copyright (c) 2002-3 Open Source Development Labs
 * Copyright (c) 2007 Greg Kroah-Hartman <gregkh@suse.de>
 * Copyright (c) 2007 Novell Inc.
 * Copyright (c) 2023 Greg Kroah-Hartman <gregkh@linuxfoundation.org>
 */

#include <linux/async.h>
#include <linux/device/bus.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include "base.h"
#include "power/power.h"

/* /sys/devices/system */
static struct kset *system_kset;

/* /sys/bus */
static struct kset *bus_kset;

#define to_bus_attr(_attr) container_of(_attr, struct bus_attribute, attr)

/*
 * sysfs bindings for drivers
 */

#define to_drv_attr(_attr) container_of(_attr, struct driver_attribute, attr)

#define DRIVER_ATTR_IGNORE_LOCKDEP(_name, _mode, _show, _store) \
	struct driver_attribute driver_attr_##_name =		\
		__ATTR_IGNORE_LOCKDEP(_name, _mode, _show, _store)

static int __must_check bus_rescan_devices_helper(struct device *dev,
						void *data);

/**
 * bus_to_subsys - Turn a struct bus_type into a struct subsys_private
 *
 * @bus: pointer to the struct bus_type to look up
 *
 * The driver core internals needs to work on the subsys_private structure, not
 * the external struct bus_type pointer.  This function walks the list of
 * registered busses in the system and finds the matching one and returns the
 * internal struct subsys_private that relates to that bus.
 *
 * Note, the reference count of the return value is INCREMENTED if it is not
 * NULL.  A call to subsys_put() must be done when finished with the pointer in
 * order for it to be properly freed.
 */
static struct subsys_private *bus_to_subsys(const struct bus_type *bus)
{
	struct subsys_private *sp = NULL;
	struct kobject *kobj;

	if (!bus || !bus_kset)
		return NULL;

	spin_lock(&bus_kset->list_lock);

	if (list_empty(&bus_kset->list))
		goto done;

	list_for_each_entry(kobj, &bus_kset->list, entry) {
		struct kset *kset = container_of(kobj, struct kset, kobj);

		sp = container_of_const(kset, struct subsys_private, subsys);
		if (sp->bus == bus)
			goto done;
	}
	sp = NULL;
done:
	sp = subsys_get(sp);
	spin_unlock(&bus_kset->list_lock);
	return sp;
}

static struct bus_type *bus_get(struct bus_type *bus)
{
	struct subsys_private *sp = bus_to_subsys(bus);

	if (sp)
		return bus;
	return NULL;
}

static void bus_put(const struct bus_type *bus)
{
	struct subsys_private *sp = bus_to_subsys(bus);

	/* two puts are required as the call to bus_to_subsys incremented it again */
	subsys_put(sp);
	subsys_put(sp);
}

static ssize_t drv_attr_show(struct kobject *kobj, struct attribute *attr,
			     char *buf)
{
	struct driver_attribute *drv_attr = to_drv_attr(attr);
	struct driver_private *drv_priv = to_driver(kobj);
	ssize_t ret = -EIO;

	if (drv_attr->show)
		ret = drv_attr->show(drv_priv->driver, buf);
	return ret;
}

static ssize_t drv_attr_store(struct kobject *kobj, struct attribute *attr,
			      const char *buf, size_t count)
{
	struct driver_attribute *drv_attr = to_drv_attr(attr);
	struct driver_private *drv_priv = to_driver(kobj);
	ssize_t ret = -EIO;

	if (drv_attr->store)
		ret = drv_attr->store(drv_priv->driver, buf, count);
	return ret;
}

static const struct sysfs_ops driver_sysfs_ops = {
	.show	= drv_attr_show,
	.store	= drv_attr_store,
};

static void driver_release(struct kobject *kobj)
{
	struct driver_private *drv_priv = to_driver(kobj);

	pr_debug("driver: '%s': %s\n", kobject_name(kobj), __func__);
	kfree(drv_priv);
}

static const struct kobj_type driver_ktype = {
	.sysfs_ops	= &driver_sysfs_ops,
	.release	= driver_release,
};

/*
 * sysfs bindings for buses
 */
static ssize_t bus_attr_show(struct kobject *kobj, struct attribute *attr,
			     char *buf)
{
	struct bus_attribute *bus_attr = to_bus_attr(attr);
	struct subsys_private *subsys_priv = to_subsys_private(kobj);
	ssize_t ret = 0;

	if (bus_attr->show)
		ret = bus_attr->show(subsys_priv->bus, buf);
	return ret;
}

static ssize_t bus_attr_store(struct kobject *kobj, struct attribute *attr,
			      const char *buf, size_t count)
{
	struct bus_attribute *bus_attr = to_bus_attr(attr);
	struct subsys_private *subsys_priv = to_subsys_private(kobj);
	ssize_t ret = 0;

	if (bus_attr->store)
		ret = bus_attr->store(subsys_priv->bus, buf, count);
	return ret;
}

static const struct sysfs_ops bus_sysfs_ops = {
	.show	= bus_attr_show,
	.store	= bus_attr_store,
};

int bus_create_file(const struct bus_type *bus, struct bus_attribute *attr)
{
	struct subsys_private *sp = bus_to_subsys(bus);
	int error;

	if (!sp)
		return -EINVAL;

	error = sysfs_create_file(&sp->subsys.kobj, &attr->attr);

	subsys_put(sp);
	return error;
}
EXPORT_SYMBOL_GPL(bus_create_file);

void bus_remove_file(const struct bus_type *bus, struct bus_attribute *attr)
{
	struct subsys_private *sp = bus_to_subsys(bus);

	if (!sp)
		return;

	sysfs_remove_file(&sp->subsys.kobj, &attr->attr);
	subsys_put(sp);
}
EXPORT_SYMBOL_GPL(bus_remove_file);

static void bus_release(struct kobject *kobj)
{
	struct subsys_private *priv = to_subsys_private(kobj);

	lockdep_unregister_key(&priv->lock_key);
	kfree(priv);
}

static const struct kobj_type bus_ktype = {
	.sysfs_ops	= &bus_sysfs_ops,
	.release	= bus_release,
};

static int bus_uevent_filter(const struct kobject *kobj)
{
	const struct kobj_type *ktype = get_ktype(kobj);

	if (ktype == &bus_ktype)
		return 1;
	return 0;
}

static const struct kset_uevent_ops bus_uevent_ops = {
	.filter = bus_uevent_filter,
};

/* Manually detach a device from its associated driver. */
static ssize_t unbind_store(struct device_driver *drv, const char *buf,
			    size_t count)
{
	struct bus_type *bus = bus_get(drv->bus);
	struct device *dev;
	int err = -ENODEV;

	dev = bus_find_device_by_name(bus, NULL, buf);
	if (dev && dev->driver == drv) {
		device_driver_detach(dev);
		err = count;
	}
	put_device(dev);
	bus_put(bus);
	return err;
}
static DRIVER_ATTR_IGNORE_LOCKDEP(unbind, 0200, NULL, unbind_store);

/*
 * Manually attach a device to a driver.
 * Note: the driver must want to bind to the device,
 * it is not possible to override the driver's id table.
 */
static ssize_t bind_store(struct device_driver *drv, const char *buf,
			  size_t count)
{
	struct bus_type *bus = bus_get(drv->bus);
	struct device *dev;
	int err = -ENODEV;

	dev = bus_find_device_by_name(bus, NULL, buf);
	if (dev && driver_match_device(drv, dev)) {
		err = device_driver_attach(drv, dev);
		if (!err) {
			/* success */
			err = count;
		}
	}
	put_device(dev);
	bus_put(bus);
	return err;
}
static DRIVER_ATTR_IGNORE_LOCKDEP(bind, 0200, NULL, bind_store);

static ssize_t drivers_autoprobe_show(struct bus_type *bus, char *buf)
{
	struct subsys_private *sp = bus_to_subsys(bus);
	int ret;

	if (!sp)
		return -EINVAL;

	ret = sysfs_emit(buf, "%d\n", sp->drivers_autoprobe);
	subsys_put(sp);
	return ret;
}

static ssize_t drivers_autoprobe_store(struct bus_type *bus,
				       const char *buf, size_t count)
{
	struct subsys_private *sp = bus_to_subsys(bus);

	if (!sp)
		return -EINVAL;

	if (buf[0] == '0')
		sp->drivers_autoprobe = 0;
	else
		sp->drivers_autoprobe = 1;

	subsys_put(sp);
	return count;
}

static ssize_t drivers_probe_store(struct bus_type *bus,
				   const char *buf, size_t count)
{
	struct device *dev;
	int err = -EINVAL;

	dev = bus_find_device_by_name(bus, NULL, buf);
	if (!dev)
		return -ENODEV;
	if (bus_rescan_devices_helper(dev, NULL) == 0)
		err = count;
	put_device(dev);
	return err;
}

static struct device *next_device(struct klist_iter *i)
{
	struct klist_node *n = klist_next(i);
	struct device *dev = NULL;
	struct device_private *dev_prv;

	if (n) {
		dev_prv = to_device_private_bus(n);
		dev = dev_prv->device;
	}
	return dev;
}

/**
 * bus_for_each_dev - device iterator.
 * @bus: bus type.
 * @start: device to start iterating from.
 * @data: data for the callback.
 * @fn: function to be called for each device.
 *
 * Iterate over @bus's list of devices, and call @fn for each,
 * passing it @data. If @start is not NULL, we use that device to
 * begin iterating from.
 *
 * We check the return of @fn each time. If it returns anything
 * other than 0, we break out and return that value.
 *
 * NOTE: The device that returns a non-zero value is not retained
 * in any way, nor is its refcount incremented. If the caller needs
 * to retain this data, it should do so, and increment the reference
 * count in the supplied callback.
 */
int bus_for_each_dev(const struct bus_type *bus, struct device *start,
		     void *data, int (*fn)(struct device *, void *))
{
	struct subsys_private *sp = bus_to_subsys(bus);
	struct klist_iter i;
	struct device *dev;
	int error = 0;

	if (!sp)
		return -EINVAL;

	klist_iter_init_node(&sp->klist_devices, &i,
			     (start ? &start->p->knode_bus : NULL));
	while (!error && (dev = next_device(&i)))
		error = fn(dev, data);
	klist_iter_exit(&i);
	subsys_put(sp);
	return error;
}
EXPORT_SYMBOL_GPL(bus_for_each_dev);

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
struct device *bus_find_device(const struct bus_type *bus,
			       struct device *start, const void *data,
			       int (*match)(struct device *dev, const void *data))
{
	struct subsys_private *sp = bus_to_subsys(bus);
	struct klist_iter i;
	struct device *dev;

	if (!sp)
		return NULL;

	klist_iter_init_node(&sp->klist_devices, &i,
			     (start ? &start->p->knode_bus : NULL));
	while ((dev = next_device(&i)))
		if (match(dev, data) && get_device(dev))
			break;
	klist_iter_exit(&i);
	subsys_put(sp);
	return dev;
}
EXPORT_SYMBOL_GPL(bus_find_device);

static struct device_driver *next_driver(struct klist_iter *i)
{
	struct klist_node *n = klist_next(i);
	struct driver_private *drv_priv;

	if (n) {
		drv_priv = container_of(n, struct driver_private, knode_bus);
		return drv_priv->driver;
	}
	return NULL;
}

/**
 * bus_for_each_drv - driver iterator
 * @bus: bus we're dealing with.
 * @start: driver to start iterating on.
 * @data: data to pass to the callback.
 * @fn: function to call for each driver.
 *
 * This is nearly identical to the device iterator above.
 * We iterate over each driver that belongs to @bus, and call
 * @fn for each. If @fn returns anything but 0, we break out
 * and return it. If @start is not NULL, we use it as the head
 * of the list.
 *
 * NOTE: we don't return the driver that returns a non-zero
 * value, nor do we leave the reference count incremented for that
 * driver. If the caller needs to know that info, it must set it
 * in the callback. It must also be sure to increment the refcount
 * so it doesn't disappear before returning to the caller.
 */
int bus_for_each_drv(const struct bus_type *bus, struct device_driver *start,
		     void *data, int (*fn)(struct device_driver *, void *))
{
	struct subsys_private *sp = bus_to_subsys(bus);
	struct klist_iter i;
	struct device_driver *drv;
	int error = 0;

	if (!sp)
		return -EINVAL;

	klist_iter_init_node(&sp->klist_drivers, &i,
			     start ? &start->p->knode_bus : NULL);
	while ((drv = next_driver(&i)) && !error)
		error = fn(drv, data);
	klist_iter_exit(&i);
	subsys_put(sp);
	return error;
}
EXPORT_SYMBOL_GPL(bus_for_each_drv);

/**
 * bus_add_device - add device to bus
 * @dev: device being added
 *
 * - Add device's bus attributes.
 * - Create links to device's bus.
 * - Add the device to its bus's list of devices.
 */
int bus_add_device(struct device *dev)
{
	struct subsys_private *sp = bus_to_subsys(dev->bus);
	int error;

	if (!sp) {
		/*
		 * This is a normal operation for many devices that do not
		 * have a bus assigned to them, just say that all went
		 * well.
		 */
		return 0;
	}

	/*
	 * Reference in sp is now incremented and will be dropped when
	 * the device is removed from the bus
	 */

	pr_debug("bus: '%s': add device %s\n", sp->bus->name, dev_name(dev));

	error = device_add_groups(dev, sp->bus->dev_groups);
	if (error)
		goto out_put;

	error = sysfs_create_link(&sp->devices_kset->kobj, &dev->kobj, dev_name(dev));
	if (error)
		goto out_groups;

	error = sysfs_create_link(&dev->kobj, &sp->subsys.kobj, "subsystem");
	if (error)
		goto out_subsys;

	klist_add_tail(&dev->p->knode_bus, &sp->klist_devices);
	return 0;

out_subsys:
	sysfs_remove_link(&sp->devices_kset->kobj, dev_name(dev));
out_groups:
	device_remove_groups(dev, sp->bus->dev_groups);
out_put:
	subsys_put(sp);
	return error;
}

/**
 * bus_probe_device - probe drivers for a new device
 * @dev: device to probe
 *
 * - Automatically probe for a driver if the bus allows it.
 */
void bus_probe_device(struct device *dev)
{
	struct subsys_private *sp = bus_to_subsys(dev->bus);
	struct subsys_interface *sif;

	if (!sp)
		return;

	if (sp->drivers_autoprobe)
		device_initial_probe(dev);

	mutex_lock(&sp->mutex);
	list_for_each_entry(sif, &sp->interfaces, node)
		if (sif->add_dev)
			sif->add_dev(dev, sif);
	mutex_unlock(&sp->mutex);
	subsys_put(sp);
}

/**
 * bus_remove_device - remove device from bus
 * @dev: device to be removed
 *
 * - Remove device from all interfaces.
 * - Remove symlink from bus' directory.
 * - Delete device from bus's list.
 * - Detach from its driver.
 * - Drop reference taken in bus_add_device().
 */
void bus_remove_device(struct device *dev)
{
	struct subsys_private *sp = bus_to_subsys(dev->bus);
	struct subsys_interface *sif;

	if (!sp)
		return;

	mutex_lock(&sp->mutex);
	list_for_each_entry(sif, &sp->interfaces, node)
		if (sif->remove_dev)
			sif->remove_dev(dev, sif);
	mutex_unlock(&sp->mutex);

	sysfs_remove_link(&dev->kobj, "subsystem");
	sysfs_remove_link(&sp->devices_kset->kobj, dev_name(dev));
	device_remove_groups(dev, dev->bus->dev_groups);
	if (klist_node_attached(&dev->p->knode_bus))
		klist_del(&dev->p->knode_bus);

	pr_debug("bus: '%s': remove device %s\n",
		 dev->bus->name, dev_name(dev));
	device_release_driver(dev);

	/*
	 * Decrement the reference count twice, once for the bus_to_subsys()
	 * call in the start of this function, and the second one from the
	 * reference increment in bus_add_device()
	 */
	subsys_put(sp);
	subsys_put(sp);
}

static int __must_check add_bind_files(struct device_driver *drv)
{
	int ret;

	ret = driver_create_file(drv, &driver_attr_unbind);
	if (ret == 0) {
		ret = driver_create_file(drv, &driver_attr_bind);
		if (ret)
			driver_remove_file(drv, &driver_attr_unbind);
	}
	return ret;
}

static void remove_bind_files(struct device_driver *drv)
{
	driver_remove_file(drv, &driver_attr_bind);
	driver_remove_file(drv, &driver_attr_unbind);
}

static BUS_ATTR_WO(drivers_probe);
static BUS_ATTR_RW(drivers_autoprobe);

static int add_probe_files(const struct bus_type *bus)
{
	int retval;

	retval = bus_create_file(bus, &bus_attr_drivers_probe);
	if (retval)
		goto out;

	retval = bus_create_file(bus, &bus_attr_drivers_autoprobe);
	if (retval)
		bus_remove_file(bus, &bus_attr_drivers_probe);
out:
	return retval;
}

static void remove_probe_files(const struct bus_type *bus)
{
	bus_remove_file(bus, &bus_attr_drivers_autoprobe);
	bus_remove_file(bus, &bus_attr_drivers_probe);
}

static ssize_t uevent_store(struct device_driver *drv, const char *buf,
			    size_t count)
{
	int rc;

	rc = kobject_synth_uevent(&drv->p->kobj, buf, count);
	return rc ? rc : count;
}
static DRIVER_ATTR_WO(uevent);

/**
 * bus_add_driver - Add a driver to the bus.
 * @drv: driver.
 */
int bus_add_driver(struct device_driver *drv)
{
	struct subsys_private *sp = bus_to_subsys(drv->bus);
	struct driver_private *priv;
	int error = 0;

	if (!sp)
		return -EINVAL;

	/*
	 * Reference in sp is now incremented and will be dropped when
	 * the driver is removed from the bus
	 */
	pr_debug("bus: '%s': add driver %s\n", sp->bus->name, drv->name);

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		error = -ENOMEM;
		goto out_put_bus;
	}
	klist_init(&priv->klist_devices, NULL, NULL);
	priv->driver = drv;
	drv->p = priv;
	priv->kobj.kset = sp->drivers_kset;
	error = kobject_init_and_add(&priv->kobj, &driver_ktype, NULL,
				     "%s", drv->name);
	if (error)
		goto out_unregister;

	klist_add_tail(&priv->knode_bus, &sp->klist_drivers);
	if (sp->drivers_autoprobe) {
		error = driver_attach(drv);
		if (error)
			goto out_del_list;
	}
	module_add_driver(drv->owner, drv);

	error = driver_create_file(drv, &driver_attr_uevent);
	if (error) {
		printk(KERN_ERR "%s: uevent attr (%s) failed\n",
			__func__, drv->name);
	}
	error = driver_add_groups(drv, sp->bus->drv_groups);
	if (error) {
		/* How the hell do we get out of this pickle? Give up */
		printk(KERN_ERR "%s: driver_add_groups(%s) failed\n",
			__func__, drv->name);
	}

	if (!drv->suppress_bind_attrs) {
		error = add_bind_files(drv);
		if (error) {
			/* Ditto */
			printk(KERN_ERR "%s: add_bind_files(%s) failed\n",
				__func__, drv->name);
		}
	}

	return 0;

out_del_list:
	klist_del(&priv->knode_bus);
out_unregister:
	kobject_put(&priv->kobj);
	/* drv->p is freed in driver_release()  */
	drv->p = NULL;
out_put_bus:
	subsys_put(sp);
	return error;
}

/**
 * bus_remove_driver - delete driver from bus's knowledge.
 * @drv: driver.
 *
 * Detach the driver from the devices it controls, and remove
 * it from its bus's list of drivers. Finally, we drop the reference
 * to the bus we took in bus_add_driver().
 */
void bus_remove_driver(struct device_driver *drv)
{
	struct subsys_private *sp = bus_to_subsys(drv->bus);

	if (!sp)
		return;

	pr_debug("bus: '%s': remove driver %s\n", sp->bus->name, drv->name);

	if (!drv->suppress_bind_attrs)
		remove_bind_files(drv);
	driver_remove_groups(drv, sp->bus->drv_groups);
	driver_remove_file(drv, &driver_attr_uevent);
	klist_remove(&drv->p->knode_bus);
	driver_detach(drv);
	module_remove_driver(drv);
	kobject_put(&drv->p->kobj);

	/*
	 * Decrement the reference count twice, once for the bus_to_subsys()
	 * call in the start of this function, and the second one from the
	 * reference increment in bus_add_driver()
	 */
	subsys_put(sp);
	subsys_put(sp);
}

/* Helper for bus_rescan_devices's iter */
static int __must_check bus_rescan_devices_helper(struct device *dev,
						  void *data)
{
	int ret = 0;

	if (!dev->driver) {
		if (dev->parent && dev->bus->need_parent_lock)
			device_lock(dev->parent);
		ret = device_attach(dev);
		if (dev->parent && dev->bus->need_parent_lock)
			device_unlock(dev->parent);
	}
	return ret < 0 ? ret : 0;
}

/**
 * bus_rescan_devices - rescan devices on the bus for possible drivers
 * @bus: the bus to scan.
 *
 * This function will look for devices on the bus with no driver
 * attached and rescan it against existing drivers to see if it matches
 * any by calling device_attach() for the unbound devices.
 */
int bus_rescan_devices(struct bus_type *bus)
{
	return bus_for_each_dev(bus, NULL, NULL, bus_rescan_devices_helper);
}
EXPORT_SYMBOL_GPL(bus_rescan_devices);

/**
 * device_reprobe - remove driver for a device and probe for a new driver
 * @dev: the device to reprobe
 *
 * This function detaches the attached driver (if any) for the given
 * device and restarts the driver probing process.  It is intended
 * to use if probing criteria changed during a devices lifetime and
 * driver attachment should change accordingly.
 */
int device_reprobe(struct device *dev)
{
	if (dev->driver)
		device_driver_detach(dev);
	return bus_rescan_devices_helper(dev, NULL);
}
EXPORT_SYMBOL_GPL(device_reprobe);

static void klist_devices_get(struct klist_node *n)
{
	struct device_private *dev_prv = to_device_private_bus(n);
	struct device *dev = dev_prv->device;

	get_device(dev);
}

static void klist_devices_put(struct klist_node *n)
{
	struct device_private *dev_prv = to_device_private_bus(n);
	struct device *dev = dev_prv->device;

	put_device(dev);
}

static ssize_t bus_uevent_store(struct bus_type *bus,
				const char *buf, size_t count)
{
	struct subsys_private *sp = bus_to_subsys(bus);
	int ret;

	if (!sp)
		return -EINVAL;

	ret = kobject_synth_uevent(&sp->subsys.kobj, buf, count);
	subsys_put(sp);

	if (ret)
		return ret;
	return count;
}
/*
 * "open code" the old BUS_ATTR() macro here.  We want to use BUS_ATTR_WO()
 * here, but can not use it as earlier in the file we have
 * DEVICE_ATTR_WO(uevent), which would cause a clash with the with the store
 * function name.
 */
static struct bus_attribute bus_attr_uevent = __ATTR(uevent, 0200, NULL,
						     bus_uevent_store);

/**
 * bus_register - register a driver-core subsystem
 * @bus: bus to register
 *
 * Once we have that, we register the bus with the kobject
 * infrastructure, then register the children subsystems it has:
 * the devices and drivers that belong to the subsystem.
 */
int bus_register(struct bus_type *bus)
{
	int retval;
	struct subsys_private *priv;
	struct kobject *bus_kobj;
	struct lock_class_key *key;

	priv = kzalloc(sizeof(struct subsys_private), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->bus = bus;

	BLOCKING_INIT_NOTIFIER_HEAD(&priv->bus_notifier);

	bus_kobj = &priv->subsys.kobj;
	retval = kobject_set_name(bus_kobj, "%s", bus->name);
	if (retval)
		goto out;

	bus_kobj->kset = bus_kset;
	bus_kobj->ktype = &bus_ktype;
	priv->drivers_autoprobe = 1;

	retval = kset_register(&priv->subsys);
	if (retval)
		goto out;

	retval = bus_create_file(bus, &bus_attr_uevent);
	if (retval)
		goto bus_uevent_fail;

	priv->devices_kset = kset_create_and_add("devices", NULL, bus_kobj);
	if (!priv->devices_kset) {
		retval = -ENOMEM;
		goto bus_devices_fail;
	}

	priv->drivers_kset = kset_create_and_add("drivers", NULL, bus_kobj);
	if (!priv->drivers_kset) {
		retval = -ENOMEM;
		goto bus_drivers_fail;
	}

	INIT_LIST_HEAD(&priv->interfaces);
	key = &priv->lock_key;
	lockdep_register_key(key);
	__mutex_init(&priv->mutex, "subsys mutex", key);
	klist_init(&priv->klist_devices, klist_devices_get, klist_devices_put);
	klist_init(&priv->klist_drivers, NULL, NULL);

	retval = add_probe_files(bus);
	if (retval)
		goto bus_probe_files_fail;

	retval = sysfs_create_groups(bus_kobj, bus->bus_groups);
	if (retval)
		goto bus_groups_fail;

	pr_debug("bus: '%s': registered\n", bus->name);
	return 0;

bus_groups_fail:
	remove_probe_files(bus);
bus_probe_files_fail:
	kset_unregister(priv->drivers_kset);
bus_drivers_fail:
	kset_unregister(priv->devices_kset);
bus_devices_fail:
	bus_remove_file(bus, &bus_attr_uevent);
bus_uevent_fail:
	kset_unregister(&priv->subsys);
out:
	kfree(priv);
	return retval;
}
EXPORT_SYMBOL_GPL(bus_register);

/**
 * bus_unregister - remove a bus from the system
 * @bus: bus.
 *
 * Unregister the child subsystems and the bus itself.
 * Finally, we call bus_put() to release the refcount
 */
void bus_unregister(const struct bus_type *bus)
{
	struct subsys_private *sp = bus_to_subsys(bus);
	struct kobject *bus_kobj;

	if (!sp)
		return;

	pr_debug("bus: '%s': unregistering\n", bus->name);
	if (bus->dev_root)
		device_unregister(bus->dev_root);

	bus_kobj = &sp->subsys.kobj;
	sysfs_remove_groups(bus_kobj, bus->bus_groups);
	remove_probe_files(bus);
	bus_remove_file(bus, &bus_attr_uevent);

	kset_unregister(sp->drivers_kset);
	kset_unregister(sp->devices_kset);
	kset_unregister(&sp->subsys);
	subsys_put(sp);
}
EXPORT_SYMBOL_GPL(bus_unregister);

int bus_register_notifier(const struct bus_type *bus, struct notifier_block *nb)
{
	struct subsys_private *sp = bus_to_subsys(bus);
	int retval;

	if (!sp)
		return -EINVAL;

	retval = blocking_notifier_chain_register(&sp->bus_notifier, nb);
	subsys_put(sp);
	return retval;
}
EXPORT_SYMBOL_GPL(bus_register_notifier);

int bus_unregister_notifier(const struct bus_type *bus, struct notifier_block *nb)
{
	struct subsys_private *sp = bus_to_subsys(bus);
	int retval;

	if (!sp)
		return -EINVAL;
	retval = blocking_notifier_chain_unregister(&sp->bus_notifier, nb);
	subsys_put(sp);
	return retval;
}
EXPORT_SYMBOL_GPL(bus_unregister_notifier);

void bus_notify(struct device *dev, enum bus_notifier_event value)
{
	struct subsys_private *sp = bus_to_subsys(dev->bus);

	if (!sp)
		return;

	blocking_notifier_call_chain(&sp->bus_notifier, value, dev);
	subsys_put(sp);
}

struct kset *bus_get_kset(const struct bus_type *bus)
{
	struct subsys_private *sp = bus_to_subsys(bus);
	struct kset *kset;

	if (!sp)
		return NULL;

	kset = &sp->subsys;
	subsys_put(sp);

	return kset;
}
EXPORT_SYMBOL_GPL(bus_get_kset);

/*
 * Yes, this forcibly breaks the klist abstraction temporarily.  It
 * just wants to sort the klist, not change reference counts and
 * take/drop locks rapidly in the process.  It does all this while
 * holding the lock for the list, so objects can't otherwise be
 * added/removed while we're swizzling.
 */
static void device_insertion_sort_klist(struct device *a, struct list_head *list,
					int (*compare)(const struct device *a,
							const struct device *b))
{
	struct klist_node *n;
	struct device_private *dev_prv;
	struct device *b;

	list_for_each_entry(n, list, n_node) {
		dev_prv = to_device_private_bus(n);
		b = dev_prv->device;
		if (compare(a, b) <= 0) {
			list_move_tail(&a->p->knode_bus.n_node,
				       &b->p->knode_bus.n_node);
			return;
		}
	}
	list_move_tail(&a->p->knode_bus.n_node, list);
}

void bus_sort_breadthfirst(struct bus_type *bus,
			   int (*compare)(const struct device *a,
					  const struct device *b))
{
	struct subsys_private *sp = bus_to_subsys(bus);
	LIST_HEAD(sorted_devices);
	struct klist_node *n, *tmp;
	struct device_private *dev_prv;
	struct device *dev;
	struct klist *device_klist;

	if (!sp)
		return;
	device_klist = &sp->klist_devices;

	spin_lock(&device_klist->k_lock);
	list_for_each_entry_safe(n, tmp, &device_klist->k_list, n_node) {
		dev_prv = to_device_private_bus(n);
		dev = dev_prv->device;
		device_insertion_sort_klist(dev, &sorted_devices, compare);
	}
	list_splice(&sorted_devices, &device_klist->k_list);
	spin_unlock(&device_klist->k_lock);
	subsys_put(sp);
}
EXPORT_SYMBOL_GPL(bus_sort_breadthfirst);

struct subsys_dev_iter {
	struct klist_iter		ki;
	const struct device_type	*type;
};

/**
 * subsys_dev_iter_init - initialize subsys device iterator
 * @iter: subsys iterator to initialize
 * @sp: the subsys private (i.e. bus) we wanna iterate over
 * @start: the device to start iterating from, if any
 * @type: device_type of the devices to iterate over, NULL for all
 *
 * Initialize subsys iterator @iter such that it iterates over devices
 * of @subsys.  If @start is set, the list iteration will start there,
 * otherwise if it is NULL, the iteration starts at the beginning of
 * the list.
 */
static void subsys_dev_iter_init(struct subsys_dev_iter *iter, struct subsys_private *sp,
				 struct device *start, const struct device_type *type)
{
	struct klist_node *start_knode = NULL;

	if (start)
		start_knode = &start->p->knode_bus;
	klist_iter_init_node(&sp->klist_devices, &iter->ki, start_knode);
	iter->type = type;
}

/**
 * subsys_dev_iter_next - iterate to the next device
 * @iter: subsys iterator to proceed
 *
 * Proceed @iter to the next device and return it.  Returns NULL if
 * iteration is complete.
 *
 * The returned device is referenced and won't be released till
 * iterator is proceed to the next device or exited.  The caller is
 * free to do whatever it wants to do with the device including
 * calling back into subsys code.
 */
static struct device *subsys_dev_iter_next(struct subsys_dev_iter *iter)
{
	struct klist_node *knode;
	struct device *dev;

	for (;;) {
		knode = klist_next(&iter->ki);
		if (!knode)
			return NULL;
		dev = to_device_private_bus(knode)->device;
		if (!iter->type || iter->type == dev->type)
			return dev;
	}
}

/**
 * subsys_dev_iter_exit - finish iteration
 * @iter: subsys iterator to finish
 *
 * Finish an iteration.  Always call this function after iteration is
 * complete whether the iteration ran till the end or not.
 */
static void subsys_dev_iter_exit(struct subsys_dev_iter *iter)
{
	klist_iter_exit(&iter->ki);
}

int subsys_interface_register(struct subsys_interface *sif)
{
	struct subsys_private *sp;
	struct subsys_dev_iter iter;
	struct device *dev;

	if (!sif || !sif->subsys)
		return -ENODEV;

	sp = bus_to_subsys(sif->subsys);
	if (!sp)
		return -EINVAL;

	/*
	 * Reference in sp is now incremented and will be dropped when
	 * the interface is removed from the bus
	 */

	mutex_lock(&sp->mutex);
	list_add_tail(&sif->node, &sp->interfaces);
	if (sif->add_dev) {
		subsys_dev_iter_init(&iter, sp, NULL, NULL);
		while ((dev = subsys_dev_iter_next(&iter)))
			sif->add_dev(dev, sif);
		subsys_dev_iter_exit(&iter);
	}
	mutex_unlock(&sp->mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(subsys_interface_register);

void subsys_interface_unregister(struct subsys_interface *sif)
{
	struct subsys_private *sp;
	struct subsys_dev_iter iter;
	struct device *dev;

	if (!sif || !sif->subsys)
		return;

	sp = bus_to_subsys(sif->subsys);
	if (!sp)
		return;

	mutex_lock(&sp->mutex);
	list_del_init(&sif->node);
	if (sif->remove_dev) {
		subsys_dev_iter_init(&iter, sp, NULL, NULL);
		while ((dev = subsys_dev_iter_next(&iter)))
			sif->remove_dev(dev, sif);
		subsys_dev_iter_exit(&iter);
	}
	mutex_unlock(&sp->mutex);

	/*
	 * Decrement the reference count twice, once for the bus_to_subsys()
	 * call in the start of this function, and the second one from the
	 * reference increment in subsys_interface_register()
	 */
	subsys_put(sp);
	subsys_put(sp);
}
EXPORT_SYMBOL_GPL(subsys_interface_unregister);

static void system_root_device_release(struct device *dev)
{
	kfree(dev);
}

static int subsys_register(struct bus_type *subsys,
			   const struct attribute_group **groups,
			   struct kobject *parent_of_root)
{
	struct device *dev;
	int err;

	err = bus_register(subsys);
	if (err < 0)
		return err;

	dev = kzalloc(sizeof(struct device), GFP_KERNEL);
	if (!dev) {
		err = -ENOMEM;
		goto err_dev;
	}

	err = dev_set_name(dev, "%s", subsys->name);
	if (err < 0)
		goto err_name;

	dev->kobj.parent = parent_of_root;
	dev->groups = groups;
	dev->release = system_root_device_release;

	err = device_register(dev);
	if (err < 0)
		goto err_dev_reg;

	subsys->dev_root = dev;
	return 0;

err_dev_reg:
	put_device(dev);
	dev = NULL;
err_name:
	kfree(dev);
err_dev:
	bus_unregister(subsys);
	return err;
}

/**
 * subsys_system_register - register a subsystem at /sys/devices/system/
 * @subsys: system subsystem
 * @groups: default attributes for the root device
 *
 * All 'system' subsystems have a /sys/devices/system/<name> root device
 * with the name of the subsystem. The root device can carry subsystem-
 * wide attributes. All registered devices are below this single root
 * device and are named after the subsystem with a simple enumeration
 * number appended. The registered devices are not explicitly named;
 * only 'id' in the device needs to be set.
 *
 * Do not use this interface for anything new, it exists for compatibility
 * with bad ideas only. New subsystems should use plain subsystems; and
 * add the subsystem-wide attributes should be added to the subsystem
 * directory itself and not some create fake root-device placed in
 * /sys/devices/system/<name>.
 */
int subsys_system_register(struct bus_type *subsys,
			   const struct attribute_group **groups)
{
	return subsys_register(subsys, groups, &system_kset->kobj);
}
EXPORT_SYMBOL_GPL(subsys_system_register);

/**
 * subsys_virtual_register - register a subsystem at /sys/devices/virtual/
 * @subsys: virtual subsystem
 * @groups: default attributes for the root device
 *
 * All 'virtual' subsystems have a /sys/devices/system/<name> root device
 * with the name of the subystem.  The root device can carry subsystem-wide
 * attributes.  All registered devices are below this single root device.
 * There's no restriction on device naming.  This is for kernel software
 * constructs which need sysfs interface.
 */
int subsys_virtual_register(struct bus_type *subsys,
			    const struct attribute_group **groups)
{
	struct kobject *virtual_dir;

	virtual_dir = virtual_device_parent(NULL);
	if (!virtual_dir)
		return -ENOMEM;

	return subsys_register(subsys, groups, virtual_dir);
}
EXPORT_SYMBOL_GPL(subsys_virtual_register);

/**
 * driver_find - locate driver on a bus by its name.
 * @name: name of the driver.
 * @bus: bus to scan for the driver.
 *
 * Call kset_find_obj() to iterate over list of drivers on
 * a bus to find driver by name. Return driver if found.
 *
 * This routine provides no locking to prevent the driver it returns
 * from being unregistered or unloaded while the caller is using it.
 * The caller is responsible for preventing this.
 */
struct device_driver *driver_find(const char *name, struct bus_type *bus)
{
	struct subsys_private *sp = bus_to_subsys(bus);
	struct kobject *k;
	struct driver_private *priv;

	if (!sp)
		return NULL;

	k = kset_find_obj(sp->drivers_kset, name);
	subsys_put(sp);
	if (!k)
		return NULL;

	priv = to_driver(k);

	/* Drop reference added by kset_find_obj() */
	kobject_put(k);
	return priv->driver;
}
EXPORT_SYMBOL_GPL(driver_find);

/*
 * Warning, the value could go to "removed" instantly after calling this function, so be very
 * careful when calling it...
 */
bool bus_is_registered(const struct bus_type *bus)
{
	struct subsys_private *sp = bus_to_subsys(bus);
	bool is_initialized = false;

	if (sp) {
		is_initialized = true;
		subsys_put(sp);
	}
	return is_initialized;
}

/**
 * bus_get_dev_root - return a pointer to the "device root" of a bus
 * @bus: bus to return the device root of.
 *
 * If a bus has a "device root" structure, return it, WITH THE REFERENCE
 * COUNT INCREMENTED.
 *
 * Note, when finished with the device, a call to put_device() is required.
 *
 * If the device root is not present (or bus is not a valid pointer), NULL
 * will be returned.
 */
struct device *bus_get_dev_root(const struct bus_type *bus)
{
	if (bus)
		return get_device(bus->dev_root);
	return NULL;
}
EXPORT_SYMBOL_GPL(bus_get_dev_root);

int __init buses_init(void)
{
	bus_kset = kset_create_and_add("bus", &bus_uevent_ops, NULL);
	if (!bus_kset)
		return -ENOMEM;

	system_kset = kset_create_and_add("system", NULL, &devices_kset->kobj);
	if (!system_kset)
		return -ENOMEM;

	return 0;
}
