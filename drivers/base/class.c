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

#include <linux/config.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/kdev_t.h>
#include "base.h"

#define to_class_attr(_attr) container_of(_attr, struct class_attribute, attr)
#define to_class(obj) container_of(obj, struct class, subsys.kset.kobj)

static ssize_t
class_attr_show(struct kobject * kobj, struct attribute * attr, char * buf)
{
	struct class_attribute * class_attr = to_class_attr(attr);
	struct class * dc = to_class(kobj);
	ssize_t ret = 0;

	if (class_attr->show)
		ret = class_attr->show(dc, buf);
	return ret;
}

static ssize_t
class_attr_store(struct kobject * kobj, struct attribute * attr,
		 const char * buf, size_t count)
{
	struct class_attribute * class_attr = to_class_attr(attr);
	struct class * dc = to_class(kobj);
	ssize_t ret = 0;

	if (class_attr->store)
		ret = class_attr->store(dc, buf, count);
	return ret;
}

static void class_release(struct kobject * kobj)
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

static struct kobj_type ktype_class = {
	.sysfs_ops	= &class_sysfs_ops,
	.release	= class_release,
};

/* Hotplug events for classes go to the class_obj subsys */
static decl_subsys(class, &ktype_class, NULL);


int class_create_file(struct class * cls, const struct class_attribute * attr)
{
	int error;
	if (cls) {
		error = sysfs_create_file(&cls->subsys.kset.kobj, &attr->attr);
	} else
		error = -EINVAL;
	return error;
}

void class_remove_file(struct class * cls, const struct class_attribute * attr)
{
	if (cls)
		sysfs_remove_file(&cls->subsys.kset.kobj, &attr->attr);
}

struct class * class_get(struct class * cls)
{
	if (cls)
		return container_of(subsys_get(&cls->subsys), struct class, subsys);
	return NULL;
}

void class_put(struct class * cls)
{
	subsys_put(&cls->subsys);
}


static int add_class_attrs(struct class * cls)
{
	int i;
	int error = 0;

	if (cls->class_attrs) {
		for (i = 0; attr_name(cls->class_attrs[i]); i++) {
			error = class_create_file(cls,&cls->class_attrs[i]);
			if (error)
				goto Err;
		}
	}
 Done:
	return error;
 Err:
	while (--i >= 0)
		class_remove_file(cls,&cls->class_attrs[i]);
	goto Done;
}

static void remove_class_attrs(struct class * cls)
{
	int i;

	if (cls->class_attrs) {
		for (i = 0; attr_name(cls->class_attrs[i]); i++)
			class_remove_file(cls,&cls->class_attrs[i]);
	}
}

int class_register(struct class * cls)
{
	int error;

	pr_debug("device class '%s': registering\n", cls->name);

	INIT_LIST_HEAD(&cls->children);
	INIT_LIST_HEAD(&cls->interfaces);
	init_MUTEX(&cls->sem);
	error = kobject_set_name(&cls->subsys.kset.kobj, "%s", cls->name);
	if (error)
		return error;

	subsys_set_kset(cls, class_subsys);

	error = subsystem_register(&cls->subsys);
	if (!error) {
		error = add_class_attrs(class_get(cls));
		class_put(cls);
	}
	return error;
}

void class_unregister(struct class * cls)
{
	pr_debug("device class '%s': unregistering\n", cls->name);
	remove_class_attrs(cls);
	subsystem_unregister(&cls->subsys);
}


/* Class Device Stuff */

int class_device_create_file(struct class_device * class_dev,
			     const struct class_device_attribute * attr)
{
	int error = -EINVAL;
	if (class_dev)
		error = sysfs_create_file(&class_dev->kobj, &attr->attr);
	return error;
}

void class_device_remove_file(struct class_device * class_dev,
			      const struct class_device_attribute * attr)
{
	if (class_dev)
		sysfs_remove_file(&class_dev->kobj, &attr->attr);
}

int class_device_create_bin_file(struct class_device *class_dev,
				 struct bin_attribute *attr)
{
	int error = -EINVAL;
	if (class_dev)
		error = sysfs_create_bin_file(&class_dev->kobj, attr);
	return error;
}

void class_device_remove_bin_file(struct class_device *class_dev,
				  struct bin_attribute *attr)
{
	if (class_dev)
		sysfs_remove_bin_file(&class_dev->kobj, attr);
}

static ssize_t
class_device_attr_show(struct kobject * kobj, struct attribute * attr,
		       char * buf)
{
	struct class_device_attribute * class_dev_attr = to_class_dev_attr(attr);
	struct class_device * cd = to_class_dev(kobj);
	ssize_t ret = 0;

	if (class_dev_attr->show)
		ret = class_dev_attr->show(cd, buf);
	return ret;
}

static ssize_t
class_device_attr_store(struct kobject * kobj, struct attribute * attr,
			const char * buf, size_t count)
{
	struct class_device_attribute * class_dev_attr = to_class_dev_attr(attr);
	struct class_device * cd = to_class_dev(kobj);
	ssize_t ret = 0;

	if (class_dev_attr->store)
		ret = class_dev_attr->store(cd, buf, count);
	return ret;
}

static struct sysfs_ops class_dev_sysfs_ops = {
	.show	= class_device_attr_show,
	.store	= class_device_attr_store,
};

static void class_dev_release(struct kobject * kobj)
{
	struct class_device *cd = to_class_dev(kobj);
	struct class * cls = cd->class;

	pr_debug("device class '%s': release.\n", cd->class_id);

	if (cls->release)
		cls->release(cd);
	else {
		printk(KERN_ERR "Device class '%s' does not have a release() function, "
			"it is broken and must be fixed.\n",
			cd->class_id);
		WARN_ON(1);
	}
}

static struct kobj_type ktype_class_device = {
	.sysfs_ops	= &class_dev_sysfs_ops,
	.release	= class_dev_release,
};

static int class_hotplug_filter(struct kset *kset, struct kobject *kobj)
{
	struct kobj_type *ktype = get_ktype(kobj);

	if (ktype == &ktype_class_device) {
		struct class_device *class_dev = to_class_dev(kobj);
		if (class_dev->class)
			return 1;
	}
	return 0;
}

static char *class_hotplug_name(struct kset *kset, struct kobject *kobj)
{
	struct class_device *class_dev = to_class_dev(kobj);

	return class_dev->class->name;
}

static int class_hotplug(struct kset *kset, struct kobject *kobj, char **envp,
			 int num_envp, char *buffer, int buffer_size)
{
	struct class_device *class_dev = to_class_dev(kobj);
	int i = 0;
	int length = 0;
	int retval = 0;

	pr_debug("%s - name = %s\n", __FUNCTION__, class_dev->class_id);

	if (class_dev->dev) {
		/* add physical device, backing this device  */
		struct device *dev = class_dev->dev;
		char *path = kobject_get_path(&dev->kobj, GFP_KERNEL);

		add_hotplug_env_var(envp, num_envp, &i, buffer, buffer_size,
				    &length, "PHYSDEVPATH=%s", path);
		kfree(path);

		if (dev->bus)
			add_hotplug_env_var(envp, num_envp, &i,
					    buffer, buffer_size, &length,
					    "PHYSDEVBUS=%s", dev->bus->name);

		if (dev->driver)
			add_hotplug_env_var(envp, num_envp, &i,
					    buffer, buffer_size, &length,
					    "PHYSDEVDRIVER=%s", dev->driver->name);
	}

	if (MAJOR(class_dev->devt)) {
		add_hotplug_env_var(envp, num_envp, &i,
				    buffer, buffer_size, &length,
				    "MAJOR=%u", MAJOR(class_dev->devt));

		add_hotplug_env_var(envp, num_envp, &i,
				    buffer, buffer_size, &length,
				    "MINOR=%u", MINOR(class_dev->devt));
	}

	/* terminate, set to next free slot, shrink available space */
	envp[i] = NULL;
	envp = &envp[i];
	num_envp -= i;
	buffer = &buffer[length];
	buffer_size -= length;

	if (class_dev->class->hotplug) {
		/* have the bus specific function add its stuff */
		retval = class_dev->class->hotplug (class_dev, envp, num_envp,
						    buffer, buffer_size);
			if (retval) {
			pr_debug ("%s - hotplug() returned %d\n",
				  __FUNCTION__, retval);
		}
	}

	return retval;
}

static struct kset_hotplug_ops class_hotplug_ops = {
	.filter =	class_hotplug_filter,
	.name =		class_hotplug_name,
	.hotplug =	class_hotplug,
};

static decl_subsys(class_obj, &ktype_class_device, &class_hotplug_ops);


static int class_device_add_attrs(struct class_device * cd)
{
	int i;
	int error = 0;
	struct class * cls = cd->class;

	if (cls->class_dev_attrs) {
		for (i = 0; attr_name(cls->class_dev_attrs[i]); i++) {
			error = class_device_create_file(cd,
							 &cls->class_dev_attrs[i]);
			if (error)
				goto Err;
		}
	}
 Done:
	return error;
 Err:
	while (--i >= 0)
		class_device_remove_file(cd,&cls->class_dev_attrs[i]);
	goto Done;
}

static void class_device_remove_attrs(struct class_device * cd)
{
	int i;
	struct class * cls = cd->class;

	if (cls->class_dev_attrs) {
		for (i = 0; attr_name(cls->class_dev_attrs[i]); i++)
			class_device_remove_file(cd,&cls->class_dev_attrs[i]);
	}
}

static ssize_t show_dev(struct class_device *class_dev, char *buf)
{
	return print_dev_t(buf, class_dev->devt);
}
static CLASS_DEVICE_ATTR(dev, S_IRUGO, show_dev, NULL);

void class_device_initialize(struct class_device *class_dev)
{
	kobj_set_kset_s(class_dev, class_obj_subsys);
	kobject_init(&class_dev->kobj);
	INIT_LIST_HEAD(&class_dev->node);
}

int class_device_add(struct class_device *class_dev)
{
	struct class * parent = NULL;
	struct class_interface * class_intf;
	int error;

	class_dev = class_device_get(class_dev);
	if (!class_dev)
		return -EINVAL;

	if (!strlen(class_dev->class_id)) {
		error = -EINVAL;
		goto register_done;
	}

	parent = class_get(class_dev->class);

	pr_debug("CLASS: registering class device: ID = '%s'\n",
		 class_dev->class_id);

	/* first, register with generic layer. */
	kobject_set_name(&class_dev->kobj, "%s", class_dev->class_id);
	if (parent)
		class_dev->kobj.parent = &parent->subsys.kset.kobj;

	if ((error = kobject_add(&class_dev->kobj)))
		goto register_done;

	/* now take care of our own registration */
	if (parent) {
		down(&parent->sem);
		list_add_tail(&class_dev->node, &parent->children);
		list_for_each_entry(class_intf, &parent->interfaces, node)
			if (class_intf->add)
				class_intf->add(class_dev);
		up(&parent->sem);
	}

	if (MAJOR(class_dev->devt))
		class_device_create_file(class_dev, &class_device_attr_dev);

	class_device_add_attrs(class_dev);
	if (class_dev->dev)
		sysfs_create_link(&class_dev->kobj,
				  &class_dev->dev->kobj, "device");

	kobject_hotplug(&class_dev->kobj, KOBJ_ADD);
 register_done:
	if (error && parent)
		class_put(parent);
	class_device_put(class_dev);
	return error;
}

int class_device_register(struct class_device *class_dev)
{
	class_device_initialize(class_dev);
	return class_device_add(class_dev);
}

void class_device_del(struct class_device *class_dev)
{
	struct class * parent = class_dev->class;
	struct class_interface * class_intf;

	if (parent) {
		down(&parent->sem);
		list_del_init(&class_dev->node);
		list_for_each_entry(class_intf, &parent->interfaces, node)
			if (class_intf->remove)
				class_intf->remove(class_dev);
		up(&parent->sem);
	}

	if (class_dev->dev)
		sysfs_remove_link(&class_dev->kobj, "device");
	class_device_remove_attrs(class_dev);

	kobject_hotplug(&class_dev->kobj, KOBJ_REMOVE);
	kobject_del(&class_dev->kobj);

	if (parent)
		class_put(parent);
}

void class_device_unregister(struct class_device *class_dev)
{
	pr_debug("CLASS: Unregistering class device. ID = '%s'\n",
		 class_dev->class_id);
	class_device_del(class_dev);
	class_device_put(class_dev);
}

int class_device_rename(struct class_device *class_dev, char *new_name)
{
	int error = 0;

	class_dev = class_device_get(class_dev);
	if (!class_dev)
		return -EINVAL;

	pr_debug("CLASS: renaming '%s' to '%s'\n", class_dev->class_id,
		 new_name);

	strlcpy(class_dev->class_id, new_name, KOBJ_NAME_LEN);

	error = kobject_rename(&class_dev->kobj, new_name);

	class_device_put(class_dev);

	return error;
}

struct class_device * class_device_get(struct class_device *class_dev)
{
	if (class_dev)
		return to_class_dev(kobject_get(&class_dev->kobj));
	return NULL;
}

void class_device_put(struct class_device *class_dev)
{
	kobject_put(&class_dev->kobj);
}


int class_interface_register(struct class_interface *class_intf)
{
	struct class *parent;
	struct class_device *class_dev;

	if (!class_intf || !class_intf->class)
		return -ENODEV;

	parent = class_get(class_intf->class);
	if (!parent)
		return -EINVAL;

	down(&parent->sem);
	list_add_tail(&class_intf->node, &parent->interfaces);
	if (class_intf->add) {
		list_for_each_entry(class_dev, &parent->children, node)
			class_intf->add(class_dev);
	}
	up(&parent->sem);

	return 0;
}

void class_interface_unregister(struct class_interface *class_intf)
{
	struct class * parent = class_intf->class;
	struct class_device *class_dev;

	if (!parent)
		return;

	down(&parent->sem);
	list_del_init(&class_intf->node);
	if (class_intf->remove) {
		list_for_each_entry(class_dev, &parent->children, node)
			class_intf->remove(class_dev);
	}
	up(&parent->sem);

	class_put(parent);
}



int __init classes_init(void)
{
	int retval;

	retval = subsystem_register(&class_subsys);
	if (retval)
		return retval;

	/* ick, this is ugly, the things we go through to keep from showing up
	 * in sysfs... */
	subsystem_init(&class_obj_subsys);
	if (!class_obj_subsys.kset.subsys)
			class_obj_subsys.kset.subsys = &class_obj_subsys;
	return 0;
}

EXPORT_SYMBOL_GPL(class_create_file);
EXPORT_SYMBOL_GPL(class_remove_file);
EXPORT_SYMBOL_GPL(class_register);
EXPORT_SYMBOL_GPL(class_unregister);
EXPORT_SYMBOL_GPL(class_get);
EXPORT_SYMBOL_GPL(class_put);

EXPORT_SYMBOL_GPL(class_device_register);
EXPORT_SYMBOL_GPL(class_device_unregister);
EXPORT_SYMBOL_GPL(class_device_initialize);
EXPORT_SYMBOL_GPL(class_device_add);
EXPORT_SYMBOL_GPL(class_device_del);
EXPORT_SYMBOL_GPL(class_device_get);
EXPORT_SYMBOL_GPL(class_device_put);
EXPORT_SYMBOL_GPL(class_device_create_file);
EXPORT_SYMBOL_GPL(class_device_remove_file);
EXPORT_SYMBOL_GPL(class_device_create_bin_file);
EXPORT_SYMBOL_GPL(class_device_remove_bin_file);

EXPORT_SYMBOL_GPL(class_interface_register);
EXPORT_SYMBOL_GPL(class_interface_unregister);
