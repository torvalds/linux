/*
 * drivers/base/core.c - core driver model code (device registration, etc)
 *
 * Copyright (c) 2002-3 Patrick Mochel
 * Copyright (c) 2002-3 Open Source Development Labs
 *
 * This file is released under the GPLv2
 *
 */

#include <linux/config.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <asm/semaphore.h>

#include "base.h"
#include "power/power.h"

int (*platform_notify)(struct device * dev) = NULL;
int (*platform_notify_remove)(struct device * dev) = NULL;

/*
 * sysfs bindings for devices.
 */

#define to_dev(obj) container_of(obj, struct device, kobj)
#define to_dev_attr(_attr) container_of(_attr, struct device_attribute, attr)

extern struct attribute * dev_default_attrs[];

static ssize_t
dev_attr_show(struct kobject * kobj, struct attribute * attr, char * buf)
{
	struct device_attribute * dev_attr = to_dev_attr(attr);
	struct device * dev = to_dev(kobj);
	ssize_t ret = 0;

	if (dev_attr->show)
		ret = dev_attr->show(dev, buf);
	return ret;
}

static ssize_t
dev_attr_store(struct kobject * kobj, struct attribute * attr,
	       const char * buf, size_t count)
{
	struct device_attribute * dev_attr = to_dev_attr(attr);
	struct device * dev = to_dev(kobj);
	ssize_t ret = 0;

	if (dev_attr->store)
		ret = dev_attr->store(dev, buf, count);
	return ret;
}

static struct sysfs_ops dev_sysfs_ops = {
	.show	= dev_attr_show,
	.store	= dev_attr_store,
};


/**
 *	device_release - free device structure.
 *	@kobj:	device's kobject.
 *
 *	This is called once the reference count for the object
 *	reaches 0. We forward the call to the device's release
 *	method, which should handle actually freeing the structure.
 */
static void device_release(struct kobject * kobj)
{
	struct device * dev = to_dev(kobj);

	if (dev->release)
		dev->release(dev);
	else {
		printk(KERN_ERR "Device '%s' does not have a release() function, "
			"it is broken and must be fixed.\n",
			dev->bus_id);
		WARN_ON(1);
	}
}

static struct kobj_type ktype_device = {
	.release	= device_release,
	.sysfs_ops	= &dev_sysfs_ops,
	.default_attrs	= dev_default_attrs,
};


static int dev_hotplug_filter(struct kset *kset, struct kobject *kobj)
{
	struct kobj_type *ktype = get_ktype(kobj);

	if (ktype == &ktype_device) {
		struct device *dev = to_dev(kobj);
		if (dev->bus)
			return 1;
	}
	return 0;
}

static char *dev_hotplug_name(struct kset *kset, struct kobject *kobj)
{
	struct device *dev = to_dev(kobj);

	return dev->bus->name;
}

static int dev_hotplug(struct kset *kset, struct kobject *kobj, char **envp,
			int num_envp, char *buffer, int buffer_size)
{
	struct device *dev = to_dev(kobj);
	int i = 0;
	int length = 0;
	int retval = 0;

	/* add bus name of physical device */
	if (dev->bus)
		add_hotplug_env_var(envp, num_envp, &i,
				    buffer, buffer_size, &length,
				    "PHYSDEVBUS=%s", dev->bus->name);

	/* add driver name of physical device */
	if (dev->driver)
		add_hotplug_env_var(envp, num_envp, &i,
				    buffer, buffer_size, &length,
				    "PHYSDEVDRIVER=%s", dev->driver->name);

	/* terminate, set to next free slot, shrink available space */
	envp[i] = NULL;
	envp = &envp[i];
	num_envp -= i;
	buffer = &buffer[length];
	buffer_size -= length;

	if (dev->bus->hotplug) {
		/* have the bus specific function add its stuff */
		retval = dev->bus->hotplug (dev, envp, num_envp, buffer, buffer_size);
			if (retval) {
			pr_debug ("%s - hotplug() returned %d\n",
				  __FUNCTION__, retval);
		}
	}

	return retval;
}

static struct kset_hotplug_ops device_hotplug_ops = {
	.filter =	dev_hotplug_filter,
	.name =		dev_hotplug_name,
	.hotplug =	dev_hotplug,
};

/**
 *	device_subsys - structure to be registered with kobject core.
 */

decl_subsys(devices, &ktype_device, &device_hotplug_ops);


/**
 *	device_create_file - create sysfs attribute file for device.
 *	@dev:	device.
 *	@attr:	device attribute descriptor.
 */

int device_create_file(struct device * dev, struct device_attribute * attr)
{
	int error = 0;
	if (get_device(dev)) {
		error = sysfs_create_file(&dev->kobj, &attr->attr);
		put_device(dev);
	}
	return error;
}

/**
 *	device_remove_file - remove sysfs attribute file.
 *	@dev:	device.
 *	@attr:	device attribute descriptor.
 */

void device_remove_file(struct device * dev, struct device_attribute * attr)
{
	if (get_device(dev)) {
		sysfs_remove_file(&dev->kobj, &attr->attr);
		put_device(dev);
	}
}


/**
 *	device_initialize - init device structure.
 *	@dev:	device.
 *
 *	This prepares the device for use by other layers,
 *	including adding it to the device hierarchy.
 *	It is the first half of device_register(), if called by
 *	that, though it can also be called separately, so one
 *	may use @dev's fields (e.g. the refcount).
 */

void device_initialize(struct device *dev)
{
	kobj_set_kset_s(dev, devices_subsys);
	kobject_init(&dev->kobj);
	INIT_LIST_HEAD(&dev->node);
	INIT_LIST_HEAD(&dev->children);
	INIT_LIST_HEAD(&dev->driver_list);
	INIT_LIST_HEAD(&dev->bus_list);
	INIT_LIST_HEAD(&dev->dma_pools);
}

/**
 *	device_add - add device to device hierarchy.
 *	@dev:	device.
 *
 *	This is part 2 of device_register(), though may be called
 *	separately _iff_ device_initialize() has been called separately.
 *
 *	This adds it to the kobject hierarchy via kobject_add(), adds it
 *	to the global and sibling lists for the device, then
 *	adds it to the other relevant subsystems of the driver model.
 */
int device_add(struct device *dev)
{
	struct device *parent = NULL;
	int error = -EINVAL;

	dev = get_device(dev);
	if (!dev || !strlen(dev->bus_id))
		goto Error;

	parent = get_device(dev->parent);

	pr_debug("DEV: registering device: ID = '%s'\n", dev->bus_id);

	/* first, register with generic layer. */
	kobject_set_name(&dev->kobj, "%s", dev->bus_id);
	if (parent)
		dev->kobj.parent = &parent->kobj;

	if ((error = kobject_add(&dev->kobj)))
		goto Error;
	if ((error = device_pm_add(dev)))
		goto PMError;
	if ((error = bus_add_device(dev)))
		goto BusError;
	down_write(&devices_subsys.rwsem);
	if (parent)
		list_add_tail(&dev->node, &parent->children);
	up_write(&devices_subsys.rwsem);

	/* notify platform of device entry */
	if (platform_notify)
		platform_notify(dev);
 Done:
	put_device(dev);
	return error;
 BusError:
	device_pm_remove(dev);
 PMError:
	kobject_del(&dev->kobj);
 Error:
	if (parent)
		put_device(parent);
	goto Done;
}


/**
 *	device_register - register a device with the system.
 *	@dev:	pointer to the device structure
 *
 *	This happens in two clean steps - initialize the device
 *	and add it to the system. The two steps can be called
 *	separately, but this is the easiest and most common.
 *	I.e. you should only call the two helpers separately if
 *	have a clearly defined need to use and refcount the device
 *	before it is added to the hierarchy.
 */

int device_register(struct device *dev)
{
	device_initialize(dev);
	return device_add(dev);
}


/**
 *	get_device - increment reference count for device.
 *	@dev:	device.
 *
 *	This simply forwards the call to kobject_get(), though
 *	we do take care to provide for the case that we get a NULL
 *	pointer passed in.
 */

struct device * get_device(struct device * dev)
{
	return dev ? to_dev(kobject_get(&dev->kobj)) : NULL;
}


/**
 *	put_device - decrement reference count.
 *	@dev:	device in question.
 */
void put_device(struct device * dev)
{
	if (dev)
		kobject_put(&dev->kobj);
}


/**
 *	device_del - delete device from system.
 *	@dev:	device.
 *
 *	This is the first part of the device unregistration
 *	sequence. This removes the device from the lists we control
 *	from here, has it removed from the other driver model
 *	subsystems it was added to in device_add(), and removes it
 *	from the kobject hierarchy.
 *
 *	NOTE: this should be called manually _iff_ device_add() was
 *	also called manually.
 */

void device_del(struct device * dev)
{
	struct device * parent = dev->parent;

	down_write(&devices_subsys.rwsem);
	if (parent)
		list_del_init(&dev->node);
	up_write(&devices_subsys.rwsem);

	/* Notify the platform of the removal, in case they
	 * need to do anything...
	 */
	if (platform_notify_remove)
		platform_notify_remove(dev);
	bus_remove_device(dev);
	device_pm_remove(dev);
	kobject_del(&dev->kobj);
	if (parent)
		put_device(parent);
}

/**
 *	device_unregister - unregister device from system.
 *	@dev:	device going away.
 *
 *	We do this in two parts, like we do device_register(). First,
 *	we remove it from all the subsystems with device_del(), then
 *	we decrement the reference count via put_device(). If that
 *	is the final reference count, the device will be cleaned up
 *	via device_release() above. Otherwise, the structure will
 *	stick around until the final reference to the device is dropped.
 */
void device_unregister(struct device * dev)
{
	pr_debug("DEV: Unregistering device. ID = '%s'\n", dev->bus_id);
	device_del(dev);
	put_device(dev);
}


/**
 *	device_for_each_child - device child iterator.
 *	@dev:	parent struct device.
 *	@data:	data for the callback.
 *	@fn:	function to be called for each device.
 *
 *	Iterate over @dev's child devices, and call @fn for each,
 *	passing it @data.
 *
 *	We check the return of @fn each time. If it returns anything
 *	other than 0, we break out and return that value.
 */
int device_for_each_child(struct device * dev, void * data,
		     int (*fn)(struct device *, void *))
{
	struct device * child;
	int error = 0;

	down_read(&devices_subsys.rwsem);
	list_for_each_entry(child, &dev->children, node) {
		if((error = fn(child, data)))
			break;
	}
	up_read(&devices_subsys.rwsem);
	return error;
}

/**
 *	device_find - locate device on a bus by name.
 *	@name:	name of the device.
 *	@bus:	bus to scan for the device.
 *
 *	Call kset_find_obj() to iterate over list of devices on
 *	a bus to find device by name. Return device if found.
 *
 *	Note that kset_find_obj increments device's reference count.
 */
struct device *device_find(const char *name, struct bus_type *bus)
{
	struct kobject *k = kset_find_obj(&bus->devices, name);
	if (k)
		return to_dev(k);
	return NULL;
}

int __init devices_init(void)
{
	return subsystem_register(&devices_subsys);
}

EXPORT_SYMBOL_GPL(device_for_each_child);

EXPORT_SYMBOL_GPL(device_initialize);
EXPORT_SYMBOL_GPL(device_add);
EXPORT_SYMBOL_GPL(device_register);

EXPORT_SYMBOL_GPL(device_del);
EXPORT_SYMBOL_GPL(device_unregister);
EXPORT_SYMBOL_GPL(get_device);
EXPORT_SYMBOL_GPL(put_device);
EXPORT_SYMBOL_GPL(device_find);

EXPORT_SYMBOL_GPL(device_create_file);
EXPORT_SYMBOL_GPL(device_remove_file);
