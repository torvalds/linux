/*
 * class.c - basic device class management
 *
 * Copyright (c) 2002-3 Patrick Mochel
 * Copyright (c) 2002-3 Open Source Development Labs
 * Copyright (c) 2003-2004 Greg Kroah-Hartman
 * Copyright (c) 2003-2004 IBM Corp.
 *
 * This file is released under the GPLv2
 *
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/kdev_t.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/genhd.h>
#include "base.h"

#define to_class_attr(_attr) container_of(_attr, struct class_attribute, attr)
#define to_class(obj) container_of(obj, struct class, subsys.kobj)

static ssize_t class_attr_show(struct kobject *kobj, struct attribute *attr,
			       char *buf)
{
	struct class_attribute *class_attr = to_class_attr(attr);
	struct class *dc = to_class(kobj);
	ssize_t ret = -EIO;

	if (class_attr->show)
		ret = class_attr->show(dc, buf);
	return ret;
}

static ssize_t class_attr_store(struct kobject *kobj, struct attribute *attr,
				const char *buf, size_t count)
{
	struct class_attribute *class_attr = to_class_attr(attr);
	struct class *dc = to_class(kobj);
	ssize_t ret = -EIO;

	if (class_attr->store)
		ret = class_attr->store(dc, buf, count);
	return ret;
}

static void class_release(struct kobject *kobj)
{
	struct class *class = to_class(kobj);

	pr_debug("class '%s': release.\n", class->name);

	if (class->class_release)
		class->class_release(class);
	else
		pr_debug("class '%s' does not have a release() function, "
			 "be careful\n", class->name);
}

static struct sysfs_ops class_sysfs_ops = {
	.show	= class_attr_show,
	.store	= class_attr_store,
};

static struct kobj_type class_ktype = {
	.sysfs_ops	= &class_sysfs_ops,
	.release	= class_release,
};

/* Hotplug events for classes go to the class_obj subsys */
static struct kset *class_kset;


int class_create_file(struct class *cls, const struct class_attribute *attr)
{
	int error;
	if (cls)
		error = sysfs_create_file(&cls->subsys.kobj, &attr->attr);
	else
		error = -EINVAL;
	return error;
}

void class_remove_file(struct class *cls, const struct class_attribute *attr)
{
	if (cls)
		sysfs_remove_file(&cls->subsys.kobj, &attr->attr);
}

static struct class *class_get(struct class *cls)
{
	if (cls)
		return container_of(kset_get(&cls->subsys),
				    struct class, subsys);
	return NULL;
}

static void class_put(struct class *cls)
{
	if (cls)
		kset_put(&cls->subsys);
}

static int add_class_attrs(struct class *cls)
{
	int i;
	int error = 0;

	if (cls->class_attrs) {
		for (i = 0; attr_name(cls->class_attrs[i]); i++) {
			error = class_create_file(cls, &cls->class_attrs[i]);
			if (error)
				goto error;
		}
	}
done:
	return error;
error:
	while (--i >= 0)
		class_remove_file(cls, &cls->class_attrs[i]);
	goto done;
}

static void remove_class_attrs(struct class *cls)
{
	int i;

	if (cls->class_attrs) {
		for (i = 0; attr_name(cls->class_attrs[i]); i++)
			class_remove_file(cls, &cls->class_attrs[i]);
	}
}

int class_register(struct class *cls)
{
	int error;

	pr_debug("device class '%s': registering\n", cls->name);

	INIT_LIST_HEAD(&cls->devices);
	INIT_LIST_HEAD(&cls->interfaces);
	kset_init(&cls->class_dirs);
	init_MUTEX(&cls->sem);
	error = kobject_set_name(&cls->subsys.kobj, "%s", cls->name);
	if (error)
		return error;

#if defined(CONFIG_SYSFS_DEPRECATED) && defined(CONFIG_BLOCK)
	/* let the block class directory show up in the root of sysfs */
	if (cls != &block_class)
		cls->subsys.kobj.kset = class_kset;
#else
	cls->subsys.kobj.kset = class_kset;
#endif
	cls->subsys.kobj.ktype = &class_ktype;

	error = kset_register(&cls->subsys);
	if (!error) {
		error = add_class_attrs(class_get(cls));
		class_put(cls);
	}
	return error;
}

void class_unregister(struct class *cls)
{
	pr_debug("device class '%s': unregistering\n", cls->name);
	remove_class_attrs(cls);
	kset_unregister(&cls->subsys);
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
 *
 * This is used to create a struct class pointer that can then be used
 * in calls to device_create().
 *
 * Note, the pointer created here is to be destroyed when finished by
 * making a call to class_destroy().
 */
struct class *class_create(struct module *owner, const char *name)
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

	retval = class_register(cls);
	if (retval)
		goto error;

	return cls;

error:
	kfree(cls);
	return ERR_PTR(retval);
}

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

#ifdef CONFIG_SYSFS_DEPRECATED
char *make_class_name(const char *name, struct kobject *kobj)
{
	char *class_name;
	int size;

	size = strlen(name) + strlen(kobject_name(kobj)) + 2;

	class_name = kmalloc(size, GFP_KERNEL);
	if (!class_name)
		return NULL;

	strcpy(class_name, name);
	strcat(class_name, ":");
	strcat(class_name, kobject_name(kobj));
	return class_name;
}
#endif

/**
 * class_for_each_device - device iterator
 * @class: the class we're iterating
 * @data: data for the callback
 * @fn: function to be called for each device
 *
 * Iterate over @class's list of devices, and call @fn for each,
 * passing it @data.
 *
 * We check the return of @fn each time. If it returns anything
 * other than 0, we break out and return that value.
 *
 * Note, we hold class->sem in this function, so it can not be
 * re-acquired in @fn, otherwise it will self-deadlocking. For
 * example, calls to add or remove class members would be verboten.
 */
int class_for_each_device(struct class *class, void *data,
			   int (*fn)(struct device *, void *))
{
	struct device *dev;
	int error = 0;

	if (!class)
		return -EINVAL;
	down(&class->sem);
	list_for_each_entry(dev, &class->devices, node) {
		dev = get_device(dev);
		if (dev) {
			error = fn(dev, data);
			put_device(dev);
		} else
			error = -ENODEV;
		if (error)
			break;
	}
	up(&class->sem);

	return error;
}
EXPORT_SYMBOL_GPL(class_for_each_device);

/**
 * class_find_device - device iterator for locating a particular device
 * @class: the class we're iterating
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
 * We hold class->sem in this function, so it can not be
 * re-acquired in @match, otherwise it will self-deadlocking. For
 * example, calls to add or remove class members would be verboten.
 */
struct device *class_find_device(struct class *class, void *data,
				   int (*match)(struct device *, void *))
{
	struct device *dev;
	int found = 0;

	if (!class)
		return NULL;

	down(&class->sem);
	list_for_each_entry(dev, &class->devices, node) {
		dev = get_device(dev);
		if (dev) {
			if (match(dev, data)) {
				found = 1;
				break;
			} else
				put_device(dev);
		} else
			break;
	}
	up(&class->sem);

	return found ? dev : NULL;
}
EXPORT_SYMBOL_GPL(class_find_device);

int class_interface_register(struct class_interface *class_intf)
{
	struct class *parent;
	struct device *dev;

	if (!class_intf || !class_intf->class)
		return -ENODEV;

	parent = class_get(class_intf->class);
	if (!parent)
		return -EINVAL;

	down(&parent->sem);
	list_add_tail(&class_intf->node, &parent->interfaces);
	if (class_intf->add_dev) {
		list_for_each_entry(dev, &parent->devices, node)
			class_intf->add_dev(dev, class_intf);
	}
	up(&parent->sem);

	return 0;
}

void class_interface_unregister(struct class_interface *class_intf)
{
	struct class *parent = class_intf->class;
	struct device *dev;

	if (!parent)
		return;

	down(&parent->sem);
	list_del_init(&class_intf->node);
	if (class_intf->remove_dev) {
		list_for_each_entry(dev, &parent->devices, node)
			class_intf->remove_dev(dev, class_intf);
	}
	up(&parent->sem);

	class_put(parent);
}

int __init classes_init(void)
{
	class_kset = kset_create_and_add("class", NULL, NULL);
	if (!class_kset)
		return -ENOMEM;
	return 0;
}

EXPORT_SYMBOL_GPL(class_create_file);
EXPORT_SYMBOL_GPL(class_remove_file);
EXPORT_SYMBOL_GPL(class_register);
EXPORT_SYMBOL_GPL(class_unregister);
EXPORT_SYMBOL_GPL(class_create);
EXPORT_SYMBOL_GPL(class_destroy);

EXPORT_SYMBOL_GPL(class_interface_register);
EXPORT_SYMBOL_GPL(class_interface_unregister);
