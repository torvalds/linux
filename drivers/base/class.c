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

	INIT_LIST_HEAD(&cls->children);
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
	pr_debug("%s called for %s\n", __FUNCTION__, cls->name);
	kfree(cls);
}

static void class_device_create_release(struct class_device *class_dev)
{
	pr_debug("%s called for %s\n", __FUNCTION__, class_dev->class_id);
	kfree(class_dev);
}

/* needed to allow these devices to have parent class devices */
static int class_device_create_uevent(struct class_device *class_dev,
				      struct kobj_uevent_env *env)
{
	pr_debug("%s called for %s\n", __FUNCTION__, class_dev->class_id);
	return 0;
}

/**
 * class_create - create a struct class structure
 * @owner: pointer to the module that is to "own" this struct class
 * @name: pointer to a string for the name of this class.
 *
 * This is used to create a struct class pointer that can then be used
 * in calls to class_device_create().
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
	cls->release = class_device_create_release;

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

/* Class Device Stuff */

int class_device_create_file(struct class_device *class_dev,
			     const struct class_device_attribute *attr)
{
	int error = -EINVAL;
	if (class_dev)
		error = sysfs_create_file(&class_dev->kobj, &attr->attr);
	return error;
}

void class_device_remove_file(struct class_device *class_dev,
			      const struct class_device_attribute *attr)
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

static ssize_t class_device_attr_show(struct kobject *kobj,
				      struct attribute *attr, char *buf)
{
	struct class_device_attribute *class_dev_attr = to_class_dev_attr(attr);
	struct class_device *cd = to_class_dev(kobj);
	ssize_t ret = 0;

	if (class_dev_attr->show)
		ret = class_dev_attr->show(cd, buf);
	return ret;
}

static ssize_t class_device_attr_store(struct kobject *kobj,
				       struct attribute *attr,
				       const char *buf, size_t count)
{
	struct class_device_attribute *class_dev_attr = to_class_dev_attr(attr);
	struct class_device *cd = to_class_dev(kobj);
	ssize_t ret = 0;

	if (class_dev_attr->store)
		ret = class_dev_attr->store(cd, buf, count);
	return ret;
}

static struct sysfs_ops class_dev_sysfs_ops = {
	.show	= class_device_attr_show,
	.store	= class_device_attr_store,
};

static void class_dev_release(struct kobject *kobj)
{
	struct class_device *cd = to_class_dev(kobj);
	struct class *cls = cd->class;

	pr_debug("device class '%s': release.\n", cd->class_id);

	if (cd->release)
		cd->release(cd);
	else if (cls->release)
		cls->release(cd);
	else {
		printk(KERN_ERR "Class Device '%s' does not have a release() "
			"function, it is broken and must be fixed.\n",
			cd->class_id);
		WARN_ON(1);
	}
}

static struct kobj_type class_device_ktype = {
	.sysfs_ops	= &class_dev_sysfs_ops,
	.release	= class_dev_release,
};

static int class_uevent_filter(struct kset *kset, struct kobject *kobj)
{
	struct kobj_type *ktype = get_ktype(kobj);

	if (ktype == &class_device_ktype) {
		struct class_device *class_dev = to_class_dev(kobj);
		if (class_dev->class)
			return 1;
	}
	return 0;
}

static const char *class_uevent_name(struct kset *kset, struct kobject *kobj)
{
	struct class_device *class_dev = to_class_dev(kobj);

	return class_dev->class->name;
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

static int make_deprecated_class_device_links(struct class_device *class_dev)
{
	char *class_name;
	int error;

	if (!class_dev->dev)
		return 0;

	class_name = make_class_name(class_dev->class->name, &class_dev->kobj);
	if (class_name)
		error = sysfs_create_link(&class_dev->dev->kobj,
					  &class_dev->kobj, class_name);
	else
		error = -ENOMEM;
	kfree(class_name);
	return error;
}

static void remove_deprecated_class_device_links(struct class_device *class_dev)
{
	char *class_name;

	if (!class_dev->dev)
		return;

	class_name = make_class_name(class_dev->class->name, &class_dev->kobj);
	if (class_name)
		sysfs_remove_link(&class_dev->dev->kobj, class_name);
	kfree(class_name);
}
#else
static inline int make_deprecated_class_device_links(struct class_device *cd)
{ return 0; }
static void remove_deprecated_class_device_links(struct class_device *cd)
{ }
#endif

static int class_uevent(struct kset *kset, struct kobject *kobj,
			struct kobj_uevent_env *env)
{
	struct class_device *class_dev = to_class_dev(kobj);
	struct device *dev = class_dev->dev;
	int retval = 0;

	pr_debug("%s - name = %s\n", __FUNCTION__, class_dev->class_id);

	if (MAJOR(class_dev->devt)) {
		add_uevent_var(env, "MAJOR=%u", MAJOR(class_dev->devt));

		add_uevent_var(env, "MINOR=%u", MINOR(class_dev->devt));
	}

	if (dev) {
		const char *path = kobject_get_path(&dev->kobj, GFP_KERNEL);
		if (path) {
			add_uevent_var(env, "PHYSDEVPATH=%s", path);
			kfree(path);
		}

		if (dev->bus)
			add_uevent_var(env, "PHYSDEVBUS=%s", dev->bus->name);

		if (dev->driver)
			add_uevent_var(env, "PHYSDEVDRIVER=%s",
				       dev->driver->name);
	}

	if (class_dev->uevent) {
		/* have the class device specific function add its stuff */
		retval = class_dev->uevent(class_dev, env);
		if (retval)
			pr_debug("class_dev->uevent() returned %d\n", retval);
	} else if (class_dev->class->uevent) {
		/* have the class specific function add its stuff */
		retval = class_dev->class->uevent(class_dev, env);
		if (retval)
			pr_debug("class->uevent() returned %d\n", retval);
	}

	return retval;
}

static struct kset_uevent_ops class_uevent_ops = {
	.filter =	class_uevent_filter,
	.name =		class_uevent_name,
	.uevent =	class_uevent,
};

/*
 * DO NOT copy how this is created, kset_create_and_add() should be
 * called, but this is a hold-over from the old-way and will be deleted
 * entirely soon.
 */
static struct kset class_obj_subsys = {
	.uevent_ops = &class_uevent_ops,
};

static int class_device_add_attrs(struct class_device *cd)
{
	int i;
	int error = 0;
	struct class *cls = cd->class;

	if (cls->class_dev_attrs) {
		for (i = 0; attr_name(cls->class_dev_attrs[i]); i++) {
			error = class_device_create_file(cd,
						&cls->class_dev_attrs[i]);
			if (error)
				goto err;
		}
	}
done:
	return error;
err:
	while (--i >= 0)
		class_device_remove_file(cd, &cls->class_dev_attrs[i]);
	goto done;
}

static void class_device_remove_attrs(struct class_device *cd)
{
	int i;
	struct class *cls = cd->class;

	if (cls->class_dev_attrs) {
		for (i = 0; attr_name(cls->class_dev_attrs[i]); i++)
			class_device_remove_file(cd, &cls->class_dev_attrs[i]);
	}
}

static int class_device_add_groups(struct class_device *cd)
{
	int i;
	int error = 0;

	if (cd->groups) {
		for (i = 0; cd->groups[i]; i++) {
			error = sysfs_create_group(&cd->kobj, cd->groups[i]);
			if (error) {
				while (--i >= 0)
					sysfs_remove_group(&cd->kobj,
							   cd->groups[i]);
				goto out;
			}
		}
	}
out:
	return error;
}

static void class_device_remove_groups(struct class_device *cd)
{
	int i;
	if (cd->groups)
		for (i = 0; cd->groups[i]; i++)
			sysfs_remove_group(&cd->kobj, cd->groups[i]);
}

static ssize_t show_dev(struct class_device *class_dev, char *buf)
{
	return print_dev_t(buf, class_dev->devt);
}

static struct class_device_attribute class_devt_attr =
	__ATTR(dev, S_IRUGO, show_dev, NULL);

static ssize_t store_uevent(struct class_device *class_dev,
			    const char *buf, size_t count)
{
	kobject_uevent(&class_dev->kobj, KOBJ_ADD);
	return count;
}

static struct class_device_attribute class_uevent_attr =
	__ATTR(uevent, S_IWUSR, NULL, store_uevent);

void class_device_initialize(struct class_device *class_dev)
{
	class_dev->kobj.kset = &class_obj_subsys;
	kobject_init(&class_dev->kobj, &class_device_ktype);
	INIT_LIST_HEAD(&class_dev->node);
}

int class_device_add(struct class_device *class_dev)
{
	struct class *parent_class = NULL;
	struct class_device *parent_class_dev = NULL;
	struct class_interface *class_intf;
	int error = -EINVAL;

	class_dev = class_device_get(class_dev);
	if (!class_dev)
		return -EINVAL;

	if (!strlen(class_dev->class_id))
		goto out1;

	parent_class = class_get(class_dev->class);
	if (!parent_class)
		goto out1;

	parent_class_dev = class_device_get(class_dev->parent);

	pr_debug("CLASS: registering class device: ID = '%s'\n",
		 class_dev->class_id);

	/* first, register with generic layer. */
	if (parent_class_dev)
		class_dev->kobj.parent = &parent_class_dev->kobj;
	else
		class_dev->kobj.parent = &parent_class->subsys.kobj;

	error = kobject_add(&class_dev->kobj, class_dev->kobj.parent,
			    "%s", class_dev->class_id);
	if (error)
		goto out2;

	/* add the needed attributes to this device */
	error = sysfs_create_link(&class_dev->kobj,
				  &parent_class->subsys.kobj, "subsystem");
	if (error)
		goto out3;

	error = class_device_create_file(class_dev, &class_uevent_attr);
	if (error)
		goto out3;

	if (MAJOR(class_dev->devt)) {
		error = class_device_create_file(class_dev, &class_devt_attr);
		if (error)
			goto out4;
	}

	error = class_device_add_attrs(class_dev);
	if (error)
		goto out5;

	if (class_dev->dev) {
		error = sysfs_create_link(&class_dev->kobj,
					  &class_dev->dev->kobj, "device");
		if (error)
			goto out6;
	}

	error = class_device_add_groups(class_dev);
	if (error)
		goto out7;

	error = make_deprecated_class_device_links(class_dev);
	if (error)
		goto out8;

	kobject_uevent(&class_dev->kobj, KOBJ_ADD);

	/* notify any interfaces this device is now here */
	down(&parent_class->sem);
	list_add_tail(&class_dev->node, &parent_class->children);
	list_for_each_entry(class_intf, &parent_class->interfaces, node) {
		if (class_intf->add)
			class_intf->add(class_dev, class_intf);
	}
	up(&parent_class->sem);

	goto out1;

 out8:
	class_device_remove_groups(class_dev);
 out7:
	if (class_dev->dev)
		sysfs_remove_link(&class_dev->kobj, "device");
 out6:
	class_device_remove_attrs(class_dev);
 out5:
	if (MAJOR(class_dev->devt))
		class_device_remove_file(class_dev, &class_devt_attr);
 out4:
	class_device_remove_file(class_dev, &class_uevent_attr);
 out3:
	kobject_del(&class_dev->kobj);
 out2:
	if (parent_class_dev)
		class_device_put(parent_class_dev);
	class_put(parent_class);
 out1:
	class_device_put(class_dev);
	return error;
}

int class_device_register(struct class_device *class_dev)
{
	class_device_initialize(class_dev);
	return class_device_add(class_dev);
}

/**
 * class_device_create - creates a class device and registers it with sysfs
 * @cls: pointer to the struct class that this device should be registered to.
 * @parent: pointer to the parent struct class_device of this new device, if
 * any.
 * @devt: the dev_t for the char device to be added.
 * @device: a pointer to a struct device that is assiociated with this class
 * device.
 * @fmt: string for the class device's name
 *
 * This function can be used by char device classes.  A struct
 * class_device will be created in sysfs, registered to the specified
 * class.
 * A "dev" file will be created, showing the dev_t for the device, if
 * the dev_t is not 0,0.
 * If a pointer to a parent struct class_device is passed in, the newly
 * created struct class_device will be a child of that device in sysfs.
 * The pointer to the struct class_device will be returned from the
 * call.  Any further sysfs files that might be required can be created
 * using this pointer.
 *
 * Note: the struct class passed to this function must have previously
 * been created with a call to class_create().
 */
struct class_device *class_device_create(struct class *cls,
					 struct class_device *parent,
					 dev_t devt,
					 struct device *device,
					 const char *fmt, ...)
{
	va_list args;
	struct class_device *class_dev = NULL;
	int retval = -ENODEV;

	if (cls == NULL || IS_ERR(cls))
		goto error;

	class_dev = kzalloc(sizeof(*class_dev), GFP_KERNEL);
	if (!class_dev) {
		retval = -ENOMEM;
		goto error;
	}

	class_dev->devt = devt;
	class_dev->dev = device;
	class_dev->class = cls;
	class_dev->parent = parent;
	class_dev->release = class_device_create_release;
	class_dev->uevent = class_device_create_uevent;

	va_start(args, fmt);
	vsnprintf(class_dev->class_id, BUS_ID_SIZE, fmt, args);
	va_end(args);
	retval = class_device_register(class_dev);
	if (retval)
		goto error;

	return class_dev;

error:
	kfree(class_dev);
	return ERR_PTR(retval);
}

void class_device_del(struct class_device *class_dev)
{
	struct class *parent_class = class_dev->class;
	struct class_device *parent_device = class_dev->parent;
	struct class_interface *class_intf;

	if (parent_class) {
		down(&parent_class->sem);
		list_del_init(&class_dev->node);
		list_for_each_entry(class_intf, &parent_class->interfaces, node)
			if (class_intf->remove)
				class_intf->remove(class_dev, class_intf);
		up(&parent_class->sem);
	}

	if (class_dev->dev) {
		remove_deprecated_class_device_links(class_dev);
		sysfs_remove_link(&class_dev->kobj, "device");
	}
	sysfs_remove_link(&class_dev->kobj, "subsystem");
	class_device_remove_file(class_dev, &class_uevent_attr);
	if (MAJOR(class_dev->devt))
		class_device_remove_file(class_dev, &class_devt_attr);
	class_device_remove_attrs(class_dev);
	class_device_remove_groups(class_dev);

	kobject_uevent(&class_dev->kobj, KOBJ_REMOVE);
	kobject_del(&class_dev->kobj);

	class_device_put(parent_device);
	class_put(parent_class);
}

void class_device_unregister(struct class_device *class_dev)
{
	pr_debug("CLASS: Unregistering class device. ID = '%s'\n",
		 class_dev->class_id);
	class_device_del(class_dev);
	class_device_put(class_dev);
}

/**
 * class_device_destroy - removes a class device that was created with class_device_create()
 * @cls: the pointer to the struct class that this device was registered * with.
 * @devt: the dev_t of the device that was previously registered.
 *
 * This call unregisters and cleans up a class device that was created with a
 * call to class_device_create()
 */
void class_device_destroy(struct class *cls, dev_t devt)
{
	struct class_device *class_dev = NULL;
	struct class_device *class_dev_tmp;

	down(&cls->sem);
	list_for_each_entry(class_dev_tmp, &cls->children, node) {
		if (class_dev_tmp->devt == devt) {
			class_dev = class_dev_tmp;
			break;
		}
	}
	up(&cls->sem);

	if (class_dev)
		class_device_unregister(class_dev);
}

struct class_device *class_device_get(struct class_device *class_dev)
{
	if (class_dev)
		return to_class_dev(kobject_get(&class_dev->kobj));
	return NULL;
}

void class_device_put(struct class_device *class_dev)
{
	if (class_dev)
		kobject_put(&class_dev->kobj);
}

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

/**
 * class_find_child - device iterator for locating a particular class_device
 * @class: the class we're iterating
 * @data: data for the match function
 * @match: function to check class_device
 *
 * This function returns a reference to a class_device that is 'found' for
 * later use, as determined by the @match callback.
 *
 * The callback should return 0 if the class_device doesn't match and non-zero
 * if it does.  If the callback returns non-zero, this function will
 * return to the caller and not iterate over any more class_devices.
 *
 * Note, you will need to drop the reference with class_device_put() after use.
 *
 * We hold class->sem in this function, so it can not be
 * re-acquired in @match, otherwise it will self-deadlocking. For
 * example, calls to add or remove class members would be verboten.
 */
struct class_device *class_find_child(struct class *class, void *data,
				   int (*match)(struct class_device *, void *))
{
	struct class_device *dev;
	int found = 0;

	if (!class)
		return NULL;

	down(&class->sem);
	list_for_each_entry(dev, &class->children, node) {
		dev = class_device_get(dev);
		if (dev) {
			if (match(dev, data)) {
				found = 1;
				break;
			} else
				class_device_put(dev);
		} else
			break;
	}
	up(&class->sem);

	return found ? dev : NULL;
}
EXPORT_SYMBOL_GPL(class_find_child);

int class_interface_register(struct class_interface *class_intf)
{
	struct class *parent;
	struct class_device *class_dev;
	struct device *dev;

	if (!class_intf || !class_intf->class)
		return -ENODEV;

	parent = class_get(class_intf->class);
	if (!parent)
		return -EINVAL;

	down(&parent->sem);
	list_add_tail(&class_intf->node, &parent->interfaces);
	if (class_intf->add) {
		list_for_each_entry(class_dev, &parent->children, node)
			class_intf->add(class_dev, class_intf);
	}
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
	struct class_device *class_dev;
	struct device *dev;

	if (!parent)
		return;

	down(&parent->sem);
	list_del_init(&class_intf->node);
	if (class_intf->remove) {
		list_for_each_entry(class_dev, &parent->children, node)
			class_intf->remove(class_dev, class_intf);
	}
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

	/* ick, this is ugly, the things we go through to keep from showing up
	 * in sysfs... */
	kset_init(&class_obj_subsys);
	kobject_set_name(&class_obj_subsys.kobj, "class_obj");
	if (!class_obj_subsys.kobj.parent)
		class_obj_subsys.kobj.parent = &class_obj_subsys.kobj;
	return 0;
}

EXPORT_SYMBOL_GPL(class_create_file);
EXPORT_SYMBOL_GPL(class_remove_file);
EXPORT_SYMBOL_GPL(class_register);
EXPORT_SYMBOL_GPL(class_unregister);
EXPORT_SYMBOL_GPL(class_create);
EXPORT_SYMBOL_GPL(class_destroy);

EXPORT_SYMBOL_GPL(class_device_register);
EXPORT_SYMBOL_GPL(class_device_unregister);
EXPORT_SYMBOL_GPL(class_device_initialize);
EXPORT_SYMBOL_GPL(class_device_add);
EXPORT_SYMBOL_GPL(class_device_del);
EXPORT_SYMBOL_GPL(class_device_get);
EXPORT_SYMBOL_GPL(class_device_put);
EXPORT_SYMBOL_GPL(class_device_create);
EXPORT_SYMBOL_GPL(class_device_destroy);
EXPORT_SYMBOL_GPL(class_device_create_file);
EXPORT_SYMBOL_GPL(class_device_remove_file);
EXPORT_SYMBOL_GPL(class_device_create_bin_file);
EXPORT_SYMBOL_GPL(class_device_remove_bin_file);

EXPORT_SYMBOL_GPL(class_interface_register);
EXPORT_SYMBOL_GPL(class_interface_unregister);
