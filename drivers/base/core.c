/*
 * drivers/base/core.c - core driver model code (device registration, etc)
 *
 * Copyright (c) 2002-3 Patrick Mochel
 * Copyright (c) 2002-3 Open Source Development Labs
 * Copyright (c) 2006 Greg Kroah-Hartman <gregkh@suse.de>
 * Copyright (c) 2006 Novell, Inc.
 *
 * This file is released under the GPLv2
 *
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/kdev_t.h>
#include <linux/notifier.h>
#include <linux/genhd.h>
#include <linux/kallsyms.h>
#include <asm/semaphore.h>

#include "base.h"
#include "power/power.h"

int (*platform_notify)(struct device *dev) = NULL;
int (*platform_notify_remove)(struct device *dev) = NULL;

#ifdef CONFIG_BLOCK
static inline int device_is_not_partition(struct device *dev)
{
	return !(dev->type == &part_type);
}
#else
static inline int device_is_not_partition(struct device *dev)
{
	return 1;
}
#endif

/**
 * dev_driver_string - Return a device's driver name, if at all possible
 * @dev: struct device to get the name of
 *
 * Will return the device's driver's name if it is bound to a device.  If
 * the device is not bound to a device, it will return the name of the bus
 * it is attached to.  If it is not attached to a bus either, an empty
 * string will be returned.
 */
const char *dev_driver_string(struct device *dev)
{
	return dev->driver ? dev->driver->name :
			(dev->bus ? dev->bus->name :
			(dev->class ? dev->class->name : ""));
}
EXPORT_SYMBOL(dev_driver_string);

#define to_dev(obj) container_of(obj, struct device, kobj)
#define to_dev_attr(_attr) container_of(_attr, struct device_attribute, attr)

static ssize_t dev_attr_show(struct kobject *kobj, struct attribute *attr,
			     char *buf)
{
	struct device_attribute *dev_attr = to_dev_attr(attr);
	struct device *dev = to_dev(kobj);
	ssize_t ret = -EIO;

	if (dev_attr->show)
		ret = dev_attr->show(dev, dev_attr, buf);
	if (ret >= (ssize_t)PAGE_SIZE) {
		print_symbol("dev_attr_show: %s returned bad count\n",
				(unsigned long)dev_attr->show);
	}
	return ret;
}

static ssize_t dev_attr_store(struct kobject *kobj, struct attribute *attr,
			      const char *buf, size_t count)
{
	struct device_attribute *dev_attr = to_dev_attr(attr);
	struct device *dev = to_dev(kobj);
	ssize_t ret = -EIO;

	if (dev_attr->store)
		ret = dev_attr->store(dev, dev_attr, buf, count);
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
static void device_release(struct kobject *kobj)
{
	struct device *dev = to_dev(kobj);

	if (dev->release)
		dev->release(dev);
	else if (dev->type && dev->type->release)
		dev->type->release(dev);
	else if (dev->class && dev->class->dev_release)
		dev->class->dev_release(dev);
	else {
		printk(KERN_ERR "Device '%s' does not have a release() "
			"function, it is broken and must be fixed.\n",
			dev->bus_id);
		WARN_ON(1);
	}
}

static struct kobj_type device_ktype = {
	.release	= device_release,
	.sysfs_ops	= &dev_sysfs_ops,
};


static int dev_uevent_filter(struct kset *kset, struct kobject *kobj)
{
	struct kobj_type *ktype = get_ktype(kobj);

	if (ktype == &device_ktype) {
		struct device *dev = to_dev(kobj);
		if (dev->uevent_suppress)
			return 0;
		if (dev->bus)
			return 1;
		if (dev->class)
			return 1;
	}
	return 0;
}

static const char *dev_uevent_name(struct kset *kset, struct kobject *kobj)
{
	struct device *dev = to_dev(kobj);

	if (dev->bus)
		return dev->bus->name;
	if (dev->class)
		return dev->class->name;
	return NULL;
}

static int dev_uevent(struct kset *kset, struct kobject *kobj,
		      struct kobj_uevent_env *env)
{
	struct device *dev = to_dev(kobj);
	int retval = 0;

	/* add the major/minor if present */
	if (MAJOR(dev->devt)) {
		add_uevent_var(env, "MAJOR=%u", MAJOR(dev->devt));
		add_uevent_var(env, "MINOR=%u", MINOR(dev->devt));
	}

	if (dev->type && dev->type->name)
		add_uevent_var(env, "DEVTYPE=%s", dev->type->name);

	if (dev->driver)
		add_uevent_var(env, "DRIVER=%s", dev->driver->name);

#ifdef CONFIG_SYSFS_DEPRECATED
	if (dev->class) {
		struct device *parent = dev->parent;

		/* find first bus device in parent chain */
		while (parent && !parent->bus)
			parent = parent->parent;
		if (parent && parent->bus) {
			const char *path;

			path = kobject_get_path(&parent->kobj, GFP_KERNEL);
			if (path) {
				add_uevent_var(env, "PHYSDEVPATH=%s", path);
				kfree(path);
			}

			add_uevent_var(env, "PHYSDEVBUS=%s", parent->bus->name);

			if (parent->driver)
				add_uevent_var(env, "PHYSDEVDRIVER=%s",
					       parent->driver->name);
		}
	} else if (dev->bus) {
		add_uevent_var(env, "PHYSDEVBUS=%s", dev->bus->name);

		if (dev->driver)
			add_uevent_var(env, "PHYSDEVDRIVER=%s",
				       dev->driver->name);
	}
#endif

	/* have the bus specific function add its stuff */
	if (dev->bus && dev->bus->uevent) {
		retval = dev->bus->uevent(dev, env);
		if (retval)
			pr_debug("device: '%s': %s: bus uevent() returned %d\n",
				 dev->bus_id, __FUNCTION__, retval);
	}

	/* have the class specific function add its stuff */
	if (dev->class && dev->class->dev_uevent) {
		retval = dev->class->dev_uevent(dev, env);
		if (retval)
			pr_debug("device: '%s': %s: class uevent() "
				 "returned %d\n", dev->bus_id,
				 __FUNCTION__, retval);
	}

	/* have the device type specific fuction add its stuff */
	if (dev->type && dev->type->uevent) {
		retval = dev->type->uevent(dev, env);
		if (retval)
			pr_debug("device: '%s': %s: dev_type uevent() "
				 "returned %d\n", dev->bus_id,
				 __FUNCTION__, retval);
	}

	return retval;
}

static struct kset_uevent_ops device_uevent_ops = {
	.filter =	dev_uevent_filter,
	.name =		dev_uevent_name,
	.uevent =	dev_uevent,
};

static ssize_t show_uevent(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct kobject *top_kobj;
	struct kset *kset;
	struct kobj_uevent_env *env = NULL;
	int i;
	size_t count = 0;
	int retval;

	/* search the kset, the device belongs to */
	top_kobj = &dev->kobj;
	while (!top_kobj->kset && top_kobj->parent)
		top_kobj = top_kobj->parent;
	if (!top_kobj->kset)
		goto out;

	kset = top_kobj->kset;
	if (!kset->uevent_ops || !kset->uevent_ops->uevent)
		goto out;

	/* respect filter */
	if (kset->uevent_ops && kset->uevent_ops->filter)
		if (!kset->uevent_ops->filter(kset, &dev->kobj))
			goto out;

	env = kzalloc(sizeof(struct kobj_uevent_env), GFP_KERNEL);
	if (!env)
		return -ENOMEM;

	/* let the kset specific function add its keys */
	retval = kset->uevent_ops->uevent(kset, &dev->kobj, env);
	if (retval)
		goto out;

	/* copy keys to file */
	for (i = 0; i < env->envp_idx; i++)
		count += sprintf(&buf[count], "%s\n", env->envp[i]);
out:
	kfree(env);
	return count;
}

static ssize_t store_uevent(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	enum kobject_action action;

	if (kobject_action_type(buf, count, &action) == 0) {
		kobject_uevent(&dev->kobj, action);
		goto out;
	}

	dev_err(dev, "uevent: unsupported action-string; this will "
		     "be ignored in a future kernel version\n");
	kobject_uevent(&dev->kobj, KOBJ_ADD);
out:
	return count;
}

static struct device_attribute uevent_attr =
	__ATTR(uevent, S_IRUGO | S_IWUSR, show_uevent, store_uevent);

static int device_add_attributes(struct device *dev,
				 struct device_attribute *attrs)
{
	int error = 0;
	int i;

	if (attrs) {
		for (i = 0; attr_name(attrs[i]); i++) {
			error = device_create_file(dev, &attrs[i]);
			if (error)
				break;
		}
		if (error)
			while (--i >= 0)
				device_remove_file(dev, &attrs[i]);
	}
	return error;
}

static void device_remove_attributes(struct device *dev,
				     struct device_attribute *attrs)
{
	int i;

	if (attrs)
		for (i = 0; attr_name(attrs[i]); i++)
			device_remove_file(dev, &attrs[i]);
}

static int device_add_groups(struct device *dev,
			     struct attribute_group **groups)
{
	int error = 0;
	int i;

	if (groups) {
		for (i = 0; groups[i]; i++) {
			error = sysfs_create_group(&dev->kobj, groups[i]);
			if (error) {
				while (--i >= 0)
					sysfs_remove_group(&dev->kobj,
							   groups[i]);
				break;
			}
		}
	}
	return error;
}

static void device_remove_groups(struct device *dev,
				 struct attribute_group **groups)
{
	int i;

	if (groups)
		for (i = 0; groups[i]; i++)
			sysfs_remove_group(&dev->kobj, groups[i]);
}

static int device_add_attrs(struct device *dev)
{
	struct class *class = dev->class;
	struct device_type *type = dev->type;
	int error;

	if (class) {
		error = device_add_attributes(dev, class->dev_attrs);
		if (error)
			return error;
	}

	if (type) {
		error = device_add_groups(dev, type->groups);
		if (error)
			goto err_remove_class_attrs;
	}

	error = device_add_groups(dev, dev->groups);
	if (error)
		goto err_remove_type_groups;

	return 0;

 err_remove_type_groups:
	if (type)
		device_remove_groups(dev, type->groups);
 err_remove_class_attrs:
	if (class)
		device_remove_attributes(dev, class->dev_attrs);

	return error;
}

static void device_remove_attrs(struct device *dev)
{
	struct class *class = dev->class;
	struct device_type *type = dev->type;

	device_remove_groups(dev, dev->groups);

	if (type)
		device_remove_groups(dev, type->groups);

	if (class)
		device_remove_attributes(dev, class->dev_attrs);
}


static ssize_t show_dev(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	return print_dev_t(buf, dev->devt);
}

static struct device_attribute devt_attr =
	__ATTR(dev, S_IRUGO, show_dev, NULL);

/* kset to create /sys/devices/  */
struct kset *devices_kset;

/**
 * device_create_file - create sysfs attribute file for device.
 * @dev: device.
 * @attr: device attribute descriptor.
 */
int device_create_file(struct device *dev, struct device_attribute *attr)
{
	int error = 0;
	if (dev)
		error = sysfs_create_file(&dev->kobj, &attr->attr);
	return error;
}

/**
 * device_remove_file - remove sysfs attribute file.
 * @dev: device.
 * @attr: device attribute descriptor.
 */
void device_remove_file(struct device *dev, struct device_attribute *attr)
{
	if (dev)
		sysfs_remove_file(&dev->kobj, &attr->attr);
}

/**
 * device_create_bin_file - create sysfs binary attribute file for device.
 * @dev: device.
 * @attr: device binary attribute descriptor.
 */
int device_create_bin_file(struct device *dev, struct bin_attribute *attr)
{
	int error = -EINVAL;
	if (dev)
		error = sysfs_create_bin_file(&dev->kobj, attr);
	return error;
}
EXPORT_SYMBOL_GPL(device_create_bin_file);

/**
 * device_remove_bin_file - remove sysfs binary attribute file
 * @dev: device.
 * @attr: device binary attribute descriptor.
 */
void device_remove_bin_file(struct device *dev, struct bin_attribute *attr)
{
	if (dev)
		sysfs_remove_bin_file(&dev->kobj, attr);
}
EXPORT_SYMBOL_GPL(device_remove_bin_file);

/**
 * device_schedule_callback_owner - helper to schedule a callback for a device
 * @dev: device.
 * @func: callback function to invoke later.
 * @owner: module owning the callback routine
 *
 * Attribute methods must not unregister themselves or their parent device
 * (which would amount to the same thing).  Attempts to do so will deadlock,
 * since unregistration is mutually exclusive with driver callbacks.
 *
 * Instead methods can call this routine, which will attempt to allocate
 * and schedule a workqueue request to call back @func with @dev as its
 * argument in the workqueue's process context.  @dev will be pinned until
 * @func returns.
 *
 * This routine is usually called via the inline device_schedule_callback(),
 * which automatically sets @owner to THIS_MODULE.
 *
 * Returns 0 if the request was submitted, -ENOMEM if storage could not
 * be allocated, -ENODEV if a reference to @owner isn't available.
 *
 * NOTE: This routine won't work if CONFIG_SYSFS isn't set!  It uses an
 * underlying sysfs routine (since it is intended for use by attribute
 * methods), and if sysfs isn't available you'll get nothing but -ENOSYS.
 */
int device_schedule_callback_owner(struct device *dev,
		void (*func)(struct device *), struct module *owner)
{
	return sysfs_schedule_callback(&dev->kobj,
			(void (*)(void *)) func, dev, owner);
}
EXPORT_SYMBOL_GPL(device_schedule_callback_owner);

static void klist_children_get(struct klist_node *n)
{
	struct device *dev = container_of(n, struct device, knode_parent);

	get_device(dev);
}

static void klist_children_put(struct klist_node *n)
{
	struct device *dev = container_of(n, struct device, knode_parent);

	put_device(dev);
}

/**
 * device_initialize - init device structure.
 * @dev: device.
 *
 * This prepares the device for use by other layers,
 * including adding it to the device hierarchy.
 * It is the first half of device_register(), if called by
 * that, though it can also be called separately, so one
 * may use @dev's fields (e.g. the refcount).
 */
void device_initialize(struct device *dev)
{
	dev->kobj.kset = devices_kset;
	kobject_init(&dev->kobj, &device_ktype);
	klist_init(&dev->klist_children, klist_children_get,
		   klist_children_put);
	INIT_LIST_HEAD(&dev->dma_pools);
	INIT_LIST_HEAD(&dev->node);
	init_MUTEX(&dev->sem);
	spin_lock_init(&dev->devres_lock);
	INIT_LIST_HEAD(&dev->devres_head);
	device_init_wakeup(dev, 0);
	set_dev_node(dev, -1);
}

#ifdef CONFIG_SYSFS_DEPRECATED
static struct kobject *get_device_parent(struct device *dev,
					 struct device *parent)
{
	/* class devices without a parent live in /sys/class/<classname>/ */
	if (dev->class && (!parent || parent->class != dev->class))
		return &dev->class->subsys.kobj;
	/* all other devices keep their parent */
	else if (parent)
		return &parent->kobj;

	return NULL;
}

static inline void cleanup_device_parent(struct device *dev) {}
static inline void cleanup_glue_dir(struct device *dev,
				    struct kobject *glue_dir) {}
#else
static struct kobject *virtual_device_parent(struct device *dev)
{
	static struct kobject *virtual_dir = NULL;

	if (!virtual_dir)
		virtual_dir = kobject_create_and_add("virtual",
						     &devices_kset->kobj);

	return virtual_dir;
}

static struct kobject *get_device_parent(struct device *dev,
					 struct device *parent)
{
	int retval;

	if (dev->class) {
		struct kobject *kobj = NULL;
		struct kobject *parent_kobj;
		struct kobject *k;

		/*
		 * If we have no parent, we live in "virtual".
		 * Class-devices with a non class-device as parent, live
		 * in a "glue" directory to prevent namespace collisions.
		 */
		if (parent == NULL)
			parent_kobj = virtual_device_parent(dev);
		else if (parent->class)
			return &parent->kobj;
		else
			parent_kobj = &parent->kobj;

		/* find our class-directory at the parent and reference it */
		spin_lock(&dev->class->class_dirs.list_lock);
		list_for_each_entry(k, &dev->class->class_dirs.list, entry)
			if (k->parent == parent_kobj) {
				kobj = kobject_get(k);
				break;
			}
		spin_unlock(&dev->class->class_dirs.list_lock);
		if (kobj)
			return kobj;

		/* or create a new class-directory at the parent device */
		k = kobject_create();
		if (!k)
			return NULL;
		k->kset = &dev->class->class_dirs;
		retval = kobject_add(k, parent_kobj, "%s", dev->class->name);
		if (retval < 0) {
			kobject_put(k);
			return NULL;
		}
		/* do not emit an uevent for this simple "glue" directory */
		return k;
	}

	if (parent)
		return &parent->kobj;
	return NULL;
}

static void cleanup_glue_dir(struct device *dev, struct kobject *glue_dir)
{
	/* see if we live in a "glue" directory */
	if (!glue_dir || !dev->class ||
	    glue_dir->kset != &dev->class->class_dirs)
		return;

	kobject_put(glue_dir);
}

static void cleanup_device_parent(struct device *dev)
{
	cleanup_glue_dir(dev, dev->kobj.parent);
}
#endif

static void setup_parent(struct device *dev, struct device *parent)
{
	struct kobject *kobj;
	kobj = get_device_parent(dev, parent);
	if (kobj)
		dev->kobj.parent = kobj;
}

static int device_add_class_symlinks(struct device *dev)
{
	int error;

	if (!dev->class)
		return 0;

	error = sysfs_create_link(&dev->kobj, &dev->class->subsys.kobj,
				  "subsystem");
	if (error)
		goto out;

#ifdef CONFIG_SYSFS_DEPRECATED
	/* stacked class devices need a symlink in the class directory */
	if (dev->kobj.parent != &dev->class->subsys.kobj &&
	    device_is_not_partition(dev)) {
		error = sysfs_create_link(&dev->class->subsys.kobj, &dev->kobj,
					  dev->bus_id);
		if (error)
			goto out_subsys;
	}

	if (dev->parent && device_is_not_partition(dev)) {
		struct device *parent = dev->parent;
		char *class_name;

		/*
		 * stacked class devices have the 'device' link
		 * pointing to the bus device instead of the parent
		 */
		while (parent->class && !parent->bus && parent->parent)
			parent = parent->parent;

		error = sysfs_create_link(&dev->kobj,
					  &parent->kobj,
					  "device");
		if (error)
			goto out_busid;

		class_name = make_class_name(dev->class->name,
						&dev->kobj);
		if (class_name)
			error = sysfs_create_link(&dev->parent->kobj,
						&dev->kobj, class_name);
		kfree(class_name);
		if (error)
			goto out_device;
	}
	return 0;

out_device:
	if (dev->parent && device_is_not_partition(dev))
		sysfs_remove_link(&dev->kobj, "device");
out_busid:
	if (dev->kobj.parent != &dev->class->subsys.kobj &&
	    device_is_not_partition(dev))
		sysfs_remove_link(&dev->class->subsys.kobj, dev->bus_id);
#else
	/* link in the class directory pointing to the device */
	error = sysfs_create_link(&dev->class->subsys.kobj, &dev->kobj,
				  dev->bus_id);
	if (error)
		goto out_subsys;

	if (dev->parent && device_is_not_partition(dev)) {
		error = sysfs_create_link(&dev->kobj, &dev->parent->kobj,
					  "device");
		if (error)
			goto out_busid;
	}
	return 0;

out_busid:
	sysfs_remove_link(&dev->class->subsys.kobj, dev->bus_id);
#endif

out_subsys:
	sysfs_remove_link(&dev->kobj, "subsystem");
out:
	return error;
}

static void device_remove_class_symlinks(struct device *dev)
{
	if (!dev->class)
		return;

#ifdef CONFIG_SYSFS_DEPRECATED
	if (dev->parent && device_is_not_partition(dev)) {
		char *class_name;

		class_name = make_class_name(dev->class->name, &dev->kobj);
		if (class_name) {
			sysfs_remove_link(&dev->parent->kobj, class_name);
			kfree(class_name);
		}
		sysfs_remove_link(&dev->kobj, "device");
	}

	if (dev->kobj.parent != &dev->class->subsys.kobj &&
	    device_is_not_partition(dev))
		sysfs_remove_link(&dev->class->subsys.kobj, dev->bus_id);
#else
	if (dev->parent && device_is_not_partition(dev))
		sysfs_remove_link(&dev->kobj, "device");

	sysfs_remove_link(&dev->class->subsys.kobj, dev->bus_id);
#endif

	sysfs_remove_link(&dev->kobj, "subsystem");
}

/**
 * device_add - add device to device hierarchy.
 * @dev: device.
 *
 * This is part 2 of device_register(), though may be called
 * separately _iff_ device_initialize() has been called separately.
 *
 * This adds it to the kobject hierarchy via kobject_add(), adds it
 * to the global and sibling lists for the device, then
 * adds it to the other relevant subsystems of the driver model.
 */
int device_add(struct device *dev)
{
	struct device *parent = NULL;
	struct class_interface *class_intf;
	int error;

	dev = get_device(dev);
	if (!dev || !strlen(dev->bus_id)) {
		error = -EINVAL;
		goto Done;
	}

	pr_debug("device: '%s': %s\n", dev->bus_id, __FUNCTION__);

	parent = get_device(dev->parent);
	setup_parent(dev, parent);

	/* first, register with generic layer. */
	error = kobject_add(&dev->kobj, dev->kobj.parent, "%s", dev->bus_id);
	if (error)
		goto Error;

	/* notify platform of device entry */
	if (platform_notify)
		platform_notify(dev);

	/* notify clients of device entry (new way) */
	if (dev->bus)
		blocking_notifier_call_chain(&dev->bus->p->bus_notifier,
					     BUS_NOTIFY_ADD_DEVICE, dev);

	error = device_create_file(dev, &uevent_attr);
	if (error)
		goto attrError;

	if (MAJOR(dev->devt)) {
		error = device_create_file(dev, &devt_attr);
		if (error)
			goto ueventattrError;
	}

	error = device_add_class_symlinks(dev);
	if (error)
		goto SymlinkError;
	error = device_add_attrs(dev);
	if (error)
		goto AttrsError;
	error = dpm_sysfs_add(dev);
	if (error)
		goto PMError;
	device_pm_add(dev);
	error = bus_add_device(dev);
	if (error)
		goto BusError;
	kobject_uevent(&dev->kobj, KOBJ_ADD);
	bus_attach_device(dev);
	if (parent)
		klist_add_tail(&dev->knode_parent, &parent->klist_children);

	if (dev->class) {
		down(&dev->class->sem);
		/* tie the class to the device */
		list_add_tail(&dev->node, &dev->class->devices);

		/* notify any interfaces that the device is here */
		list_for_each_entry(class_intf, &dev->class->interfaces, node)
			if (class_intf->add_dev)
				class_intf->add_dev(dev, class_intf);
		up(&dev->class->sem);
	}
 Done:
	put_device(dev);
	return error;
 BusError:
	device_pm_remove(dev);
 PMError:
	if (dev->bus)
		blocking_notifier_call_chain(&dev->bus->p->bus_notifier,
					     BUS_NOTIFY_DEL_DEVICE, dev);
	device_remove_attrs(dev);
 AttrsError:
	device_remove_class_symlinks(dev);
 SymlinkError:
	if (MAJOR(dev->devt))
		device_remove_file(dev, &devt_attr);
 ueventattrError:
	device_remove_file(dev, &uevent_attr);
 attrError:
	kobject_uevent(&dev->kobj, KOBJ_REMOVE);
	kobject_del(&dev->kobj);
 Error:
	cleanup_device_parent(dev);
	if (parent)
		put_device(parent);
	goto Done;
}

/**
 * device_register - register a device with the system.
 * @dev: pointer to the device structure
 *
 * This happens in two clean steps - initialize the device
 * and add it to the system. The two steps can be called
 * separately, but this is the easiest and most common.
 * I.e. you should only call the two helpers separately if
 * have a clearly defined need to use and refcount the device
 * before it is added to the hierarchy.
 */
int device_register(struct device *dev)
{
	device_initialize(dev);
	return device_add(dev);
}

/**
 * get_device - increment reference count for device.
 * @dev: device.
 *
 * This simply forwards the call to kobject_get(), though
 * we do take care to provide for the case that we get a NULL
 * pointer passed in.
 */
struct device *get_device(struct device *dev)
{
	return dev ? to_dev(kobject_get(&dev->kobj)) : NULL;
}

/**
 * put_device - decrement reference count.
 * @dev: device in question.
 */
void put_device(struct device *dev)
{
	/* might_sleep(); */
	if (dev)
		kobject_put(&dev->kobj);
}

/**
 * device_del - delete device from system.
 * @dev: device.
 *
 * This is the first part of the device unregistration
 * sequence. This removes the device from the lists we control
 * from here, has it removed from the other driver model
 * subsystems it was added to in device_add(), and removes it
 * from the kobject hierarchy.
 *
 * NOTE: this should be called manually _iff_ device_add() was
 * also called manually.
 */
void device_del(struct device *dev)
{
	struct device *parent = dev->parent;
	struct class_interface *class_intf;

	device_pm_remove(dev);
	if (parent)
		klist_del(&dev->knode_parent);
	if (MAJOR(dev->devt))
		device_remove_file(dev, &devt_attr);
	if (dev->class) {
		device_remove_class_symlinks(dev);

		down(&dev->class->sem);
		/* notify any interfaces that the device is now gone */
		list_for_each_entry(class_intf, &dev->class->interfaces, node)
			if (class_intf->remove_dev)
				class_intf->remove_dev(dev, class_intf);
		/* remove the device from the class list */
		list_del_init(&dev->node);
		up(&dev->class->sem);
	}
	device_remove_file(dev, &uevent_attr);
	device_remove_attrs(dev);
	bus_remove_device(dev);

	/*
	 * Some platform devices are driven without driver attached
	 * and managed resources may have been acquired.  Make sure
	 * all resources are released.
	 */
	devres_release_all(dev);

	/* Notify the platform of the removal, in case they
	 * need to do anything...
	 */
	if (platform_notify_remove)
		platform_notify_remove(dev);
	if (dev->bus)
		blocking_notifier_call_chain(&dev->bus->p->bus_notifier,
					     BUS_NOTIFY_DEL_DEVICE, dev);
	kobject_uevent(&dev->kobj, KOBJ_REMOVE);
	cleanup_device_parent(dev);
	kobject_del(&dev->kobj);
	put_device(parent);
}

/**
 * device_unregister - unregister device from system.
 * @dev: device going away.
 *
 * We do this in two parts, like we do device_register(). First,
 * we remove it from all the subsystems with device_del(), then
 * we decrement the reference count via put_device(). If that
 * is the final reference count, the device will be cleaned up
 * via device_release() above. Otherwise, the structure will
 * stick around until the final reference to the device is dropped.
 */
void device_unregister(struct device *dev)
{
	pr_debug("device: '%s': %s\n", dev->bus_id, __FUNCTION__);
	device_del(dev);
	put_device(dev);
}

static struct device *next_device(struct klist_iter *i)
{
	struct klist_node *n = klist_next(i);
	return n ? container_of(n, struct device, knode_parent) : NULL;
}

/**
 * device_for_each_child - device child iterator.
 * @parent: parent struct device.
 * @data: data for the callback.
 * @fn: function to be called for each device.
 *
 * Iterate over @parent's child devices, and call @fn for each,
 * passing it @data.
 *
 * We check the return of @fn each time. If it returns anything
 * other than 0, we break out and return that value.
 */
int device_for_each_child(struct device *parent, void *data,
			  int (*fn)(struct device *dev, void *data))
{
	struct klist_iter i;
	struct device *child;
	int error = 0;

	klist_iter_init(&parent->klist_children, &i);
	while ((child = next_device(&i)) && !error)
		error = fn(child, data);
	klist_iter_exit(&i);
	return error;
}

/**
 * device_find_child - device iterator for locating a particular device.
 * @parent: parent struct device
 * @data: Data to pass to match function
 * @match: Callback function to check device
 *
 * This is similar to the device_for_each_child() function above, but it
 * returns a reference to a device that is 'found' for later use, as
 * determined by the @match callback.
 *
 * The callback should return 0 if the device doesn't match and non-zero
 * if it does.  If the callback returns non-zero and a reference to the
 * current device can be obtained, this function will return to the caller
 * and not iterate over any more devices.
 */
struct device *device_find_child(struct device *parent, void *data,
				 int (*match)(struct device *dev, void *data))
{
	struct klist_iter i;
	struct device *child;

	if (!parent)
		return NULL;

	klist_iter_init(&parent->klist_children, &i);
	while ((child = next_device(&i)))
		if (match(child, data) && get_device(child))
			break;
	klist_iter_exit(&i);
	return child;
}

int __init devices_init(void)
{
	devices_kset = kset_create_and_add("devices", &device_uevent_ops, NULL);
	if (!devices_kset)
		return -ENOMEM;
	return 0;
}

EXPORT_SYMBOL_GPL(device_for_each_child);
EXPORT_SYMBOL_GPL(device_find_child);

EXPORT_SYMBOL_GPL(device_initialize);
EXPORT_SYMBOL_GPL(device_add);
EXPORT_SYMBOL_GPL(device_register);

EXPORT_SYMBOL_GPL(device_del);
EXPORT_SYMBOL_GPL(device_unregister);
EXPORT_SYMBOL_GPL(get_device);
EXPORT_SYMBOL_GPL(put_device);

EXPORT_SYMBOL_GPL(device_create_file);
EXPORT_SYMBOL_GPL(device_remove_file);


static void device_create_release(struct device *dev)
{
	pr_debug("device: '%s': %s\n", dev->bus_id, __FUNCTION__);
	kfree(dev);
}

/**
 * device_create - creates a device and registers it with sysfs
 * @class: pointer to the struct class that this device should be registered to
 * @parent: pointer to the parent struct device of this new device, if any
 * @devt: the dev_t for the char device to be added
 * @fmt: string for the device's name
 *
 * This function can be used by char device classes.  A struct device
 * will be created in sysfs, registered to the specified class.
 *
 * A "dev" file will be created, showing the dev_t for the device, if
 * the dev_t is not 0,0.
 * If a pointer to a parent struct device is passed in, the newly created
 * struct device will be a child of that device in sysfs.
 * The pointer to the struct device will be returned from the call.
 * Any further sysfs files that might be required can be created using this
 * pointer.
 *
 * Note: the struct class passed to this function must have previously
 * been created with a call to class_create().
 */
struct device *device_create(struct class *class, struct device *parent,
			     dev_t devt, const char *fmt, ...)
{
	va_list args;
	struct device *dev = NULL;
	int retval = -ENODEV;

	if (class == NULL || IS_ERR(class))
		goto error;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		retval = -ENOMEM;
		goto error;
	}

	dev->devt = devt;
	dev->class = class;
	dev->parent = parent;
	dev->release = device_create_release;

	va_start(args, fmt);
	vsnprintf(dev->bus_id, BUS_ID_SIZE, fmt, args);
	va_end(args);
	retval = device_register(dev);
	if (retval)
		goto error;

	return dev;

error:
	kfree(dev);
	return ERR_PTR(retval);
}
EXPORT_SYMBOL_GPL(device_create);

static int __match_devt(struct device *dev, void *data)
{
	dev_t *devt = data;

	return dev->devt == *devt;
}

/**
 * device_destroy - removes a device that was created with device_create()
 * @class: pointer to the struct class that this device was registered with
 * @devt: the dev_t of the device that was previously registered
 *
 * This call unregisters and cleans up a device that was created with a
 * call to device_create().
 */
void device_destroy(struct class *class, dev_t devt)
{
	struct device *dev;

	dev = class_find_device(class, &devt, __match_devt);
	if (dev) {
		put_device(dev);
		device_unregister(dev);
	}
}
EXPORT_SYMBOL_GPL(device_destroy);

#ifdef CONFIG_PM_SLEEP
/**
 * destroy_suspended_device - asks the PM core to remove a suspended device
 * @class: pointer to the struct class that this device was registered with
 * @devt: the dev_t of the device that was previously registered
 *
 * This call notifies the PM core of the necessity to unregister a suspended
 * device created with a call to device_create() (devices cannot be
 * unregistered directly while suspended, since the PM core holds their
 * semaphores at that time).
 *
 * It can only be called within the scope of a system sleep transition.  In
 * practice this means it has to be directly or indirectly invoked either by
 * a suspend or resume method, or by the PM core (e.g. via
 * disable_nonboot_cpus() or enable_nonboot_cpus()).
 */
void destroy_suspended_device(struct class *class, dev_t devt)
{
	struct device *dev;

	dev = class_find_device(class, &devt, __match_devt);
	if (dev) {
		device_pm_schedule_removal(dev);
		put_device(dev);
	}
}
EXPORT_SYMBOL_GPL(destroy_suspended_device);
#endif /* CONFIG_PM_SLEEP */

/**
 * device_rename - renames a device
 * @dev: the pointer to the struct device to be renamed
 * @new_name: the new name of the device
 */
int device_rename(struct device *dev, char *new_name)
{
	char *old_class_name = NULL;
	char *new_class_name = NULL;
	char *old_device_name = NULL;
	int error;

	dev = get_device(dev);
	if (!dev)
		return -EINVAL;

	pr_debug("device: '%s': %s: renaming to '%s'\n", dev->bus_id,
		 __FUNCTION__, new_name);

#ifdef CONFIG_SYSFS_DEPRECATED
	if ((dev->class) && (dev->parent))
		old_class_name = make_class_name(dev->class->name, &dev->kobj);
#endif

	old_device_name = kmalloc(BUS_ID_SIZE, GFP_KERNEL);
	if (!old_device_name) {
		error = -ENOMEM;
		goto out;
	}
	strlcpy(old_device_name, dev->bus_id, BUS_ID_SIZE);
	strlcpy(dev->bus_id, new_name, BUS_ID_SIZE);

	error = kobject_rename(&dev->kobj, new_name);
	if (error) {
		strlcpy(dev->bus_id, old_device_name, BUS_ID_SIZE);
		goto out;
	}

#ifdef CONFIG_SYSFS_DEPRECATED
	if (old_class_name) {
		new_class_name = make_class_name(dev->class->name, &dev->kobj);
		if (new_class_name) {
			error = sysfs_create_link(&dev->parent->kobj,
						  &dev->kobj, new_class_name);
			if (error)
				goto out;
			sysfs_remove_link(&dev->parent->kobj, old_class_name);
		}
	}
#else
	if (dev->class) {
		sysfs_remove_link(&dev->class->subsys.kobj, old_device_name);
		error = sysfs_create_link(&dev->class->subsys.kobj, &dev->kobj,
					  dev->bus_id);
		if (error) {
			dev_err(dev, "%s: sysfs_create_symlink failed (%d)\n",
				__FUNCTION__, error);
		}
	}
#endif

out:
	put_device(dev);

	kfree(new_class_name);
	kfree(old_class_name);
	kfree(old_device_name);

	return error;
}
EXPORT_SYMBOL_GPL(device_rename);

static int device_move_class_links(struct device *dev,
				   struct device *old_parent,
				   struct device *new_parent)
{
	int error = 0;
#ifdef CONFIG_SYSFS_DEPRECATED
	char *class_name;

	class_name = make_class_name(dev->class->name, &dev->kobj);
	if (!class_name) {
		error = -ENOMEM;
		goto out;
	}
	if (old_parent) {
		sysfs_remove_link(&dev->kobj, "device");
		sysfs_remove_link(&old_parent->kobj, class_name);
	}
	if (new_parent) {
		error = sysfs_create_link(&dev->kobj, &new_parent->kobj,
					  "device");
		if (error)
			goto out;
		error = sysfs_create_link(&new_parent->kobj, &dev->kobj,
					  class_name);
		if (error)
			sysfs_remove_link(&dev->kobj, "device");
	} else
		error = 0;
out:
	kfree(class_name);
	return error;
#else
	if (old_parent)
		sysfs_remove_link(&dev->kobj, "device");
	if (new_parent)
		error = sysfs_create_link(&dev->kobj, &new_parent->kobj,
					  "device");
	return error;
#endif
}

/**
 * device_move - moves a device to a new parent
 * @dev: the pointer to the struct device to be moved
 * @new_parent: the new parent of the device (can by NULL)
 */
int device_move(struct device *dev, struct device *new_parent)
{
	int error;
	struct device *old_parent;
	struct kobject *new_parent_kobj;

	dev = get_device(dev);
	if (!dev)
		return -EINVAL;

	new_parent = get_device(new_parent);
	new_parent_kobj = get_device_parent(dev, new_parent);

	pr_debug("device: '%s': %s: moving to '%s'\n", dev->bus_id,
		 __FUNCTION__, new_parent ? new_parent->bus_id : "<NULL>");
	error = kobject_move(&dev->kobj, new_parent_kobj);
	if (error) {
		cleanup_glue_dir(dev, new_parent_kobj);
		put_device(new_parent);
		goto out;
	}
	old_parent = dev->parent;
	dev->parent = new_parent;
	if (old_parent)
		klist_remove(&dev->knode_parent);
	if (new_parent)
		klist_add_tail(&dev->knode_parent, &new_parent->klist_children);
	if (!dev->class)
		goto out_put;
	error = device_move_class_links(dev, old_parent, new_parent);
	if (error) {
		/* We ignore errors on cleanup since we're hosed anyway... */
		device_move_class_links(dev, new_parent, old_parent);
		if (!kobject_move(&dev->kobj, &old_parent->kobj)) {
			if (new_parent)
				klist_remove(&dev->knode_parent);
			if (old_parent)
				klist_add_tail(&dev->knode_parent,
					       &old_parent->klist_children);
		}
		cleanup_glue_dir(dev, new_parent_kobj);
		put_device(new_parent);
		goto out;
	}
out_put:
	put_device(old_parent);
out:
	put_device(dev);
	return error;
}
EXPORT_SYMBOL_GPL(device_move);

/**
 * device_shutdown - call ->shutdown() on each device to shutdown.
 */
void device_shutdown(void)
{
	struct device *dev, *devn;

	list_for_each_entry_safe_reverse(dev, devn, &devices_kset->list,
				kobj.entry) {
		if (dev->bus && dev->bus->shutdown) {
			dev_dbg(dev, "shutdown\n");
			dev->bus->shutdown(dev);
		} else if (dev->driver && dev->driver->shutdown) {
			dev_dbg(dev, "shutdown\n");
			dev->driver->shutdown(dev);
		}
	}
}
