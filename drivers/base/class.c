// SPDX-License-Identifier: GPL-2.0
/*
 * class.c - basic device class management
 *
 * Copyright (c) 2002-3 Patrick Mochel
 * Copyright (c) 2002-3 Open Source Development Labs
 * Copyright (c) 2003-2004 Greg Kroah-Hartman
 * Copyright (c) 2003-2004 IBM Corp.
 */

#include <linux/device/class.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/kdev_t.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/genhd.h>
#include <linux/mutex.h>
#include "base.h"

#define to_class_attr(_attr) container_of(_attr, struct class_attribute, attr)

static ssize_t class_attr_show(struct kobject *kobj, struct attribute *attr,
			       char *buf)
{
	struct class_attribute *class_attr = to_class_attr(attr);
	struct subsys_private *cp = to_subsys_private(kobj);
	ssize_t ret = -EIO;

	if (class_attr->show)
		ret = class_attr->show(cp->class, class_attr, buf);
	return ret;
}

static ssize_t class_attr_store(struct kobject *kobj, struct attribute *attr,
				const char *buf, size_t count)
{
	struct class_attribute *class_attr = to_class_attr(attr);
	struct subsys_private *cp = to_subsys_private(kobj);
	ssize_t ret = -EIO;

	if (class_attr->store)
		ret = class_attr->store(cp->class, class_attr, buf, count);
	return ret;
}

static void class_release(struct kobject *kobj)
{
	struct subsys_private *cp = to_subsys_private(kobj);
	struct class *class = cp->class;

	pr_debug("class '%s': release.\n", class->name);

	if (class->class_release)
		class->class_release(class);
	else
		pr_debug("class '%s' does not have a release() function, "
			 "be careful\n", class->name);

	kfree(cp);
}

static const struct kobj_ns_type_operations *class_child_ns_type(struct kobject *kobj)
{
	struct subsys_private *cp = to_subsys_private(kobj);
	struct class *class = cp->class;

	return class->ns_type;
}

static const struct sysfs_ops class_sysfs_ops = {
	.show	   = class_attr_show,
	.store	   = class_attr_store,
};

static struct kobj_type class_ktype = {
	.sysfs_ops	= &class_sysfs_ops,
	.release	= class_release,
	.child_ns_type	= class_child_ns_type,
};

/* Hotplug events for classes go to the class subsys */
static struct kset *class_kset;


int class_create_file_ns(struct class *cls, const struct class_attribute *attr,
			 const void *ns)
{
	int error;

	if (cls)
		error = sysfs_create_file_ns(&cls->p->subsys.kobj,
					     &attr->attr, ns);
	else
		error = -EINVAL;
	return error;
}

void class_remove_file_ns(struct class *cls, const struct class_attribute *attr,
			  const void *ns)
{
	if (cls)
		sysfs_remove_file_ns(&cls->p->subsys.kobj, &attr->attr, ns);
}

static struct class *class_get(struct class *cls)
{
	if (cls)
		kset_get(&cls->p->subsys);
	return cls;
}

static void class_put(struct class *cls)
{
	if (cls)
		kset_put(&cls->p->subsys);
}

static struct device *klist_class_to_dev(struct klist_node *n)
{
	struct device_private *p = to_device_private_class(n);
	return p->device;
}

static void klist_class_dev_get(struct klist_node *n)
{
	struct device *dev = klist_class_to_dev(n);

	get_device(dev);
}

static void klist_class_dev_put(struct klist_node *n)
{
	struct device *dev = klist_class_to_dev(n);

	put_device(dev);
}

static int class_add_groups(struct class *cls,
			    const struct attribute_group **groups)
{
	return sysfs_create_groups(&cls->p->subsys.kobj, groups);
}

static void class_remove_groups(struct class *cls,
				const struct attribute_group **groups)
{
	return sysfs_remove_groups(&cls->p->subsys.kobj, groups);
}

int __class_register(struct class *cls, struct lock_class_key *key)
{
	struct subsys_private *cp;
	int error;

	pr_debug("device class '%s': registering\n", cls->name);

	cp = kzalloc(sizeof(*cp), GFP_KERNEL);
	if (!cp)
		return -ENOMEM;
	klist_init(&cp->klist_devices, klist_class_dev_get, klist_class_dev_put);
	INIT_LIST_HEAD(&cp->interfaces);
	kset_init(&cp->glue_dirs);
	__mutex_init(&cp->mutex, "subsys mutex", key);
	error = kobject_set_name(&cp->subsys.kobj, "%s", cls->name);
	if (error) {
		kfree(cp);
		return error;
	}

	/* set the default /sys/dev directory for devices of this class */
	if (!cls->dev_kobj)
		cls->dev_kobj = sysfs_dev_char_kobj;

#if defined(CONFIG_BLOCK)
	/* let the block class directory show up in the root of sysfs */
	if (!sysfs_deprecated || cls != &block_class)
		cp->subsys.kobj.kset = class_kset;
#else
	cp->subsys.kobj.kset = class_kset;
#endif
	cp->subsys.kobj.ktype = &class_ktype;
	cp->class = cls;
	cls->p = cp;

	error = kset_register(&cp->subsys);
	if (error) {
		kfree(cp);
		return error;
	}
	error = class_add_groups(class_get(cls), cls->class_groups);
	class_put(cls);
	return error;
}
EXPORT_SYMBOL_GPL(__class_register);

void class_unregister(struct class *cls)
{
	pr_debug("device class '%s': unregistering\n", cls->name);
	class_remove_groups(cls, cls->class_groups);
	kset_unregister(&cls->p->subsys);
}

static void class_create_release(struct class *cls)
{
	pr_debug("%s called for %s\n", __func__, cls->name);
	kfree(cls);
}

/**
 * class_create - create a struct class structure
 * @owner: pointer to the module that is to "own" this struct class
 * @name: pointer to a string for the name of this class.
 * @key: the lock_class_key for this class; used by mutex lock debugging
 *
 * This is used to create a struct class pointer that can then be used
 * in calls to device_create().
 *
 * Returns &struct class pointer on success, or ERR_PTR() on error.
 *
 * Note, the pointer created here is to be destroyed when finished by
 * making a call to class_destroy().
 */
struct class *__class_create(struct module *owner, const char *name,
			     struct lock_class_key *key)
{
	struct class *cls;
	int retval;

	cls = kzalloc(sizeof(*cls), GFP_KERNEL);
	if (!cls) {
		retval = -ENOMEM;
		goto error;
	}

	cls->name = name;
	cls->owner = owner;
	cls->class_release = class_create_release;

	retval = __class_register(cls, key);
	if (retval)
		goto error;

	return cls;

error:
	kfree(cls);
	return ERR_PTR(retval);
}
EXPORT_SYMBOL_GPL(__class_create);

/**
 * class_destroy - destroys a struct class structure
 * @cls: pointer to the struct class that is to be destroyed
 *
 * Note, the pointer to be destroyed must have been created with a call
 * to class_create().
 */
void class_destroy(struct class *cls)
{
	if ((cls == NULL) || (IS_ERR(cls)))
		return;

	class_unregister(cls);
}

/**
 * class_dev_iter_init - initialize class device iterator
 * @iter: class iterator to initialize
 * @class: the class we wanna iterate over
 * @start: the device to start iterating from, if any
 * @type: device_type of the devices to iterate over, NULL for all
 *
 * Initialize class iterator @iter such that it iterates over devices
 * of @class.  If @start is set, the list iteration will start there,
 * otherwise if it is NULL, the iteration starts at the beginning of
 * the list.
 */
void class_dev_iter_init(struct class_dev_iter *iter, struct class *class,
			 struct device *start, const struct device_type *type)
{
	struct klist_node *start_knode = NULL;

	if (start)
		start_knode = &start->p->knode_class;
	klist_iter_init_node(&class->p->klist_devices, &iter->ki, start_knode);
	iter->type = type;
}
EXPORT_SYMBOL_GPL(class_dev_iter_init);

/**
 * class_dev_iter_next - iterate to the next device
 * @iter: class iterator to proceed
 *
 * Proceed @iter to the next device and return it.  Returns NULL if
 * iteration is complete.
 *
 * The returned device is referenced and won't be released till
 * iterator is proceed to the next device or exited.  The caller is
 * free to do whatever it wants to do with the device including
 * calling back into class code.
 */
struct device *class_dev_iter_next(struct class_dev_iter *iter)
{
	struct klist_node *knode;
	struct device *dev;

	while (1) {
		knode = klist_next(&iter->ki);
		if (!knode)
			return NULL;
		dev = klist_class_to_dev(knode);
		if (!iter->type || iter->type == dev->type)
			return dev;
	}
}
EXPORT_SYMBOL_GPL(class_dev_iter_next);

/**
 * class_dev_iter_exit - finish iteration
 * @iter: class iterator to finish
 *
 * Finish an iteration.  Always call this function after iteration is
 * complete whether the iteration ran till the end or not.
 */
void class_dev_iter_exit(struct class_dev_iter *iter)
{
	klist_iter_exit(&iter->ki);
}
EXPORT_SYMBOL_GPL(class_dev_iter_exit);

/**
 * class_for_each_device - device iterator
 * @class: the class we're iterating
 * @start: the device to start with in the list, if any.
 * @data: data for the callback
 * @fn: function to be called for each device
 *
 * Iterate over @class's list of devices, and call @fn for each,
 * passing it @data.  If @start is set, the list iteration will start
 * there, otherwise if it is NULL, the iteration starts at the
 * beginning of the list.
 *
 * We check the return of @fn each time. If it returns anything
 * other than 0, we break out and return that value.
 *
 * @fn is allowed to do anything including calling back into class
 * code.  There's no locking restriction.
 */
int class_for_each_device(struct class *class, struct device *start,
			  void *data, int (*fn)(struct device *, void *))
{
	struct class_dev_iter iter;
	struct device *dev;
	int error = 0;

	if (!class)
		return -EINVAL;
	if (!class->p) {
		WARN(1, "%s called for class '%s' before it was initialized",
		     __func__, class->name);
		return -EINVAL;
	}

	class_dev_iter_init(&iter, class, start, NULL);
	while ((dev = class_dev_iter_next(&iter))) {
		error = fn(dev, data);
		if (error)
			break;
	}
	class_dev_iter_exit(&iter);

	return error;
}
EXPORT_SYMBOL_GPL(class_for_each_device);

/**
 * class_find_device - device iterator for locating a particular device
 * @class: the class we're iterating
 * @start: Device to begin with
 * @data: data for the match function
 * @match: function to check device
 *
 * This is similar to the class_for_each_dev() function above, but it
 * returns a reference to a device that is 'found' for later use, as
 * determined by the @match callback.
 *
 * The callback should return 0 if the device doesn't match and non-zero
 * if it does.  If the callback returns non-zero, this function will
 * return to the caller and not iterate over any more devices.
 *
 * Note, you will need to drop the reference with put_device() after use.
 *
 * @match is allowed to do anything including calling back into class
 * code.  There's no locking restriction.
 */
struct device *class_find_device(struct class *class, struct device *start,
				 const void *data,
				 int (*match)(struct device *, const void *))
{
	struct class_dev_iter iter;
	struct device *dev;

	if (!class)
		return NULL;
	if (!class->p) {
		WARN(1, "%s called for class '%s' before it was initialized",
		     __func__, class->name);
		return NULL;
	}

	class_dev_iter_init(&iter, class, start, NULL);
	while ((dev = class_dev_iter_next(&iter))) {
		if (match(dev, data)) {
			get_device(dev);
			break;
		}
	}
	class_dev_iter_exit(&iter);

	return dev;
}
EXPORT_SYMBOL_GPL(class_find_device);

int class_interface_register(struct class_interface *class_intf)
{
	struct class *parent;
	struct class_dev_iter iter;
	struct device *dev;

	if (!class_intf || !class_intf->class)
		return -ENODEV;

	parent = class_get(class_intf->class);
	if (!parent)
		return -EINVAL;

	mutex_lock(&parent->p->mutex);
	list_add_tail(&class_intf->node, &parent->p->interfaces);
	if (class_intf->add_dev) {
		class_dev_iter_init(&iter, parent, NULL, NULL);
		while ((dev = class_dev_iter_next(&iter)))
			class_intf->add_dev(dev, class_intf);
		class_dev_iter_exit(&iter);
	}
	mutex_unlock(&parent->p->mutex);

	return 0;
}

void class_interface_unregister(struct class_interface *class_intf)
{
	struct class *parent = class_intf->class;
	struct class_dev_iter iter;
	struct device *dev;

	if (!parent)
		return;

	mutex_lock(&parent->p->mutex);
	list_del_init(&class_intf->node);
	if (class_intf->remove_dev) {
		class_dev_iter_init(&iter, parent, NULL, NULL);
		while ((dev = class_dev_iter_next(&iter)))
			class_intf->remove_dev(dev, class_intf);
		class_dev_iter_exit(&iter);
	}
	mutex_unlock(&parent->p->mutex);

	class_put(parent);
}

ssize_t show_class_attr_string(struct class *class,
			       struct class_attribute *attr, char *buf)
{
	struct class_attribute_string *cs;

	cs = container_of(attr, struct class_attribute_string, attr);
	return snprintf(buf, PAGE_SIZE, "%s\n", cs->str);
}

EXPORT_SYMBOL_GPL(show_class_attr_string);

struct class_compat {
	struct kobject *kobj;
};

/**
 * class_compat_register - register a compatibility class
 * @name: the name of the class
 *
 * Compatibility class are meant as a temporary user-space compatibility
 * workaround when converting a family of class devices to a bus devices.
 */
struct class_compat *class_compat_register(const char *name)
{
	struct class_compat *cls;

	cls = kmalloc(sizeof(struct class_compat), GFP_KERNEL);
	if (!cls)
		return NULL;
	cls->kobj = kobject_create_and_add(name, &class_kset->kobj);
	if (!cls->kobj) {
		kfree(cls);
		return NULL;
	}
	return cls;
}
EXPORT_SYMBOL_GPL(class_compat_register);

/**
 * class_compat_unregister - unregister a compatibility class
 * @cls: the class to unregister
 */
void class_compat_unregister(struct class_compat *cls)
{
	kobject_put(cls->kobj);
	kfree(cls);
}
EXPORT_SYMBOL_GPL(class_compat_unregister);

/**
 * class_compat_create_link - create a compatibility class device link to
 *			      a bus device
 * @cls: the compatibility class
 * @dev: the target bus device
 * @device_link: an optional device to which a "device" link should be created
 */
int class_compat_create_link(struct class_compat *cls, struct device *dev,
			     struct device *device_link)
{
	int error;

	error = sysfs_create_link(cls->kobj, &dev->kobj, dev_name(dev));
	if (error)
		return error;

	/*
	 * Optionally add a "device" link (typically to the parent), as a
	 * class device would have one and we want to provide as much
	 * backwards compatibility as possible.
	 */
	if (device_link) {
		error = sysfs_create_link(&dev->kobj, &device_link->kobj,
					  "device");
		if (error)
			sysfs_remove_link(cls->kobj, dev_name(dev));
	}

	return error;
}
EXPORT_SYMBOL_GPL(class_compat_create_link);

/**
 * class_compat_remove_link - remove a compatibility class device link to
 *			      a bus device
 * @cls: the compatibility class
 * @dev: the target bus device
 * @device_link: an optional device to which a "device" link was previously
 * 		 created
 */
void class_compat_remove_link(struct class_compat *cls, struct device *dev,
			      struct device *device_link)
{
	if (device_link)
		sysfs_remove_link(&dev->kobj, "device");
	sysfs_remove_link(cls->kobj, dev_name(dev));
}
EXPORT_SYMBOL_GPL(class_compat_remove_link);

int __init classes_init(void)
{
	class_kset = kset_create_and_add("class", NULL, NULL);
	if (!class_kset)
		return -ENOMEM;
	return 0;
}

EXPORT_SYMBOL_GPL(class_create_file_ns);
EXPORT_SYMBOL_GPL(class_remove_file_ns);
EXPORT_SYMBOL_GPL(class_unregister);
EXPORT_SYMBOL_GPL(class_destroy);

EXPORT_SYMBOL_GPL(class_interface_register);
EXPORT_SYMBOL_GPL(class_interface_unregister);
