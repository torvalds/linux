/*
 * sys.c - pseudo-bus for system 'devices' (cpus, PICs, timers, etc)
 *
 * Copyright (c) 2002-3 Patrick Mochel
 *               2002-3 Open Source Development Lab
 *
 * This file is released under the GPLv2
 *
 * This exports a 'system' bus type.
 * By default, a 'sys' bus gets added to the root of the system. There will
 * always be core system devices. Devices can use sysdev_register() to
 * add themselves as children of the system bus.
 */

#include <linux/sysdev.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/pm.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>

#include "base.h"

#define to_sysdev(k) container_of(k, struct sys_device, kobj)
#define to_sysdev_attr(a) container_of(a, struct sysdev_attribute, attr)


static ssize_t
sysdev_show(struct kobject *kobj, struct attribute *attr, char *buffer)
{
	struct sys_device *sysdev = to_sysdev(kobj);
	struct sysdev_attribute *sysdev_attr = to_sysdev_attr(attr);

	if (sysdev_attr->show)
		return sysdev_attr->show(sysdev, sysdev_attr, buffer);
	return -EIO;
}


static ssize_t
sysdev_store(struct kobject *kobj, struct attribute *attr,
	     const char *buffer, size_t count)
{
	struct sys_device *sysdev = to_sysdev(kobj);
	struct sysdev_attribute *sysdev_attr = to_sysdev_attr(attr);

	if (sysdev_attr->store)
		return sysdev_attr->store(sysdev, sysdev_attr, buffer, count);
	return -EIO;
}

static struct sysfs_ops sysfs_ops = {
	.show	= sysdev_show,
	.store	= sysdev_store,
};

static struct kobj_type ktype_sysdev = {
	.sysfs_ops	= &sysfs_ops,
};


int sysdev_create_file(struct sys_device *s, struct sysdev_attribute *a)
{
	return sysfs_create_file(&s->kobj, &a->attr);
}


void sysdev_remove_file(struct sys_device *s, struct sysdev_attribute *a)
{
	sysfs_remove_file(&s->kobj, &a->attr);
}

EXPORT_SYMBOL_GPL(sysdev_create_file);
EXPORT_SYMBOL_GPL(sysdev_remove_file);

#define to_sysdev_class(k) container_of(k, struct sysdev_class, kset.kobj)
#define to_sysdev_class_attr(a) container_of(a, \
	struct sysdev_class_attribute, attr)

static ssize_t sysdev_class_show(struct kobject *kobj, struct attribute *attr,
				 char *buffer)
{
	struct sysdev_class *class = to_sysdev_class(kobj);
	struct sysdev_class_attribute *class_attr = to_sysdev_class_attr(attr);

	if (class_attr->show)
		return class_attr->show(class, buffer);
	return -EIO;
}

static ssize_t sysdev_class_store(struct kobject *kobj, struct attribute *attr,
				  const char *buffer, size_t count)
{
	struct sysdev_class *class = to_sysdev_class(kobj);
	struct sysdev_class_attribute *class_attr = to_sysdev_class_attr(attr);

	if (class_attr->store)
		return class_attr->store(class, buffer, count);
	return -EIO;
}

static struct sysfs_ops sysfs_class_ops = {
	.show	= sysdev_class_show,
	.store	= sysdev_class_store,
};

static struct kobj_type ktype_sysdev_class = {
	.sysfs_ops	= &sysfs_class_ops,
};

int sysdev_class_create_file(struct sysdev_class *c,
			     struct sysdev_class_attribute *a)
{
	return sysfs_create_file(&c->kset.kobj, &a->attr);
}
EXPORT_SYMBOL_GPL(sysdev_class_create_file);

void sysdev_class_remove_file(struct sysdev_class *c,
			      struct sysdev_class_attribute *a)
{
	sysfs_remove_file(&c->kset.kobj, &a->attr);
}
EXPORT_SYMBOL_GPL(sysdev_class_remove_file);

static struct kset *system_kset;

int sysdev_class_register(struct sysdev_class *cls)
{
	int retval;

	pr_debug("Registering sysdev class '%s'\n", cls->name);

	INIT_LIST_HEAD(&cls->drivers);
	memset(&cls->kset.kobj, 0x00, sizeof(struct kobject));
	cls->kset.kobj.parent = &system_kset->kobj;
	cls->kset.kobj.ktype = &ktype_sysdev_class;
	cls->kset.kobj.kset = system_kset;

	retval = kobject_set_name(&cls->kset.kobj, "%s", cls->name);
	if (retval)
		return retval;

	return kset_register(&cls->kset);
}

void sysdev_class_unregister(struct sysdev_class *cls)
{
	pr_debug("Unregistering sysdev class '%s'\n",
		 kobject_name(&cls->kset.kobj));
	kset_unregister(&cls->kset);
}

EXPORT_SYMBOL_GPL(sysdev_class_register);
EXPORT_SYMBOL_GPL(sysdev_class_unregister);

static DEFINE_MUTEX(sysdev_drivers_lock);

/**
 *	sysdev_driver_register - Register auxillary driver
 *	@cls:	Device class driver belongs to.
 *	@drv:	Driver.
 *
 *	@drv is inserted into @cls->drivers to be
 *	called on each operation on devices of that class. The refcount
 *	of @cls is incremented.
 */

int sysdev_driver_register(struct sysdev_class *cls, struct sysdev_driver *drv)
{
	int err = 0;

	if (!cls) {
		WARN(1, KERN_WARNING "sysdev: invalid class passed to "
			"sysdev_driver_register!\n");
		return -EINVAL;
	}

	/* Check whether this driver has already been added to a class. */
	if (drv->entry.next && !list_empty(&drv->entry))
		WARN(1, KERN_WARNING "sysdev: class %s: driver (%p) has already"
			" been registered to a class, something is wrong, but "
			"will forge on!\n", cls->name, drv);

	mutex_lock(&sysdev_drivers_lock);
	if (cls && kset_get(&cls->kset)) {
		list_add_tail(&drv->entry, &cls->drivers);

		/* If devices of this class already exist, tell the driver */
		if (drv->add) {
			struct sys_device *dev;
			list_for_each_entry(dev, &cls->kset.list, kobj.entry)
				drv->add(dev);
		}
	} else {
		err = -EINVAL;
		WARN(1, KERN_ERR "%s: invalid device class\n", __func__);
	}
	mutex_unlock(&sysdev_drivers_lock);
	return err;
}


/**
 *	sysdev_driver_unregister - Remove an auxillary driver.
 *	@cls:	Class driver belongs to.
 *	@drv:	Driver.
 */
void sysdev_driver_unregister(struct sysdev_class *cls,
			      struct sysdev_driver *drv)
{
	mutex_lock(&sysdev_drivers_lock);
	list_del_init(&drv->entry);
	if (cls) {
		if (drv->remove) {
			struct sys_device *dev;
			list_for_each_entry(dev, &cls->kset.list, kobj.entry)
				drv->remove(dev);
		}
		kset_put(&cls->kset);
	}
	mutex_unlock(&sysdev_drivers_lock);
}

EXPORT_SYMBOL_GPL(sysdev_driver_register);
EXPORT_SYMBOL_GPL(sysdev_driver_unregister);



/**
 *	sysdev_register - add a system device to the tree
 *	@sysdev:	device in question
 *
 */
int sysdev_register(struct sys_device *sysdev)
{
	int error;
	struct sysdev_class *cls = sysdev->cls;

	if (!cls)
		return -EINVAL;

	pr_debug("Registering sys device of class '%s'\n",
		 kobject_name(&cls->kset.kobj));

	/* initialize the kobject to 0, in case it had previously been used */
	memset(&sysdev->kobj, 0x00, sizeof(struct kobject));

	/* Make sure the kset is set */
	sysdev->kobj.kset = &cls->kset;

	/* Register the object */
	error = kobject_init_and_add(&sysdev->kobj, &ktype_sysdev, NULL,
				     "%s%d", kobject_name(&cls->kset.kobj),
				     sysdev->id);

	if (!error) {
		struct sysdev_driver *drv;

		pr_debug("Registering sys device '%s'\n",
			 kobject_name(&sysdev->kobj));

		mutex_lock(&sysdev_drivers_lock);
		/* Generic notification is implicit, because it's that
		 * code that should have called us.
		 */

		/* Notify class auxillary drivers */
		list_for_each_entry(drv, &cls->drivers, entry) {
			if (drv->add)
				drv->add(sysdev);
		}
		mutex_unlock(&sysdev_drivers_lock);
	}

	kobject_uevent(&sysdev->kobj, KOBJ_ADD);
	return error;
}

void sysdev_unregister(struct sys_device *sysdev)
{
	struct sysdev_driver *drv;

	mutex_lock(&sysdev_drivers_lock);
	list_for_each_entry(drv, &sysdev->cls->drivers, entry) {
		if (drv->remove)
			drv->remove(sysdev);
	}
	mutex_unlock(&sysdev_drivers_lock);

	kobject_put(&sysdev->kobj);
}



/**
 *	sysdev_shutdown - Shut down all system devices.
 *
 *	Loop over each class of system devices, and the devices in each
 *	of those classes. For each device, we call the shutdown method for
 *	each driver registered for the device - the auxillaries,
 *	and the class driver.
 *
 *	Note: The list is iterated in reverse order, so that we shut down
 *	child devices before we shut down their parents. The list ordering
 *	is guaranteed by virtue of the fact that child devices are registered
 *	after their parents.
 */
void sysdev_shutdown(void)
{
	struct sysdev_class *cls;

	pr_debug("Shutting Down System Devices\n");

	mutex_lock(&sysdev_drivers_lock);
	list_for_each_entry_reverse(cls, &system_kset->list, kset.kobj.entry) {
		struct sys_device *sysdev;

		pr_debug("Shutting down type '%s':\n",
			 kobject_name(&cls->kset.kobj));

		list_for_each_entry(sysdev, &cls->kset.list, kobj.entry) {
			struct sysdev_driver *drv;
			pr_debug(" %s\n", kobject_name(&sysdev->kobj));

			/* Call auxillary drivers first */
			list_for_each_entry(drv, &cls->drivers, entry) {
				if (drv->shutdown)
					drv->shutdown(sysdev);
			}

			/* Now call the generic one */
			if (cls->shutdown)
				cls->shutdown(sysdev);
		}
	}
	mutex_unlock(&sysdev_drivers_lock);
}

static void __sysdev_resume(struct sys_device *dev)
{
	struct sysdev_class *cls = dev->cls;
	struct sysdev_driver *drv;

	/* First, call the class-specific one */
	if (cls->resume)
		cls->resume(dev);
	WARN_ONCE(!irqs_disabled(),
		"Interrupts enabled after %pF\n", cls->resume);

	/* Call auxillary drivers next. */
	list_for_each_entry(drv, &cls->drivers, entry) {
		if (drv->resume)
			drv->resume(dev);
		WARN_ONCE(!irqs_disabled(),
			"Interrupts enabled after %pF\n", drv->resume);
	}
}

/**
 *	sysdev_suspend - Suspend all system devices.
 *	@state:		Power state to enter.
 *
 *	We perform an almost identical operation as sysdev_shutdown()
 *	above, though calling ->suspend() instead. Interrupts are disabled
 *	when this called. Devices are responsible for both saving state and
 *	quiescing or powering down the device.
 *
 *	This is only called by the device PM core, so we let them handle
 *	all synchronization.
 */
int sysdev_suspend(pm_message_t state)
{
	struct sysdev_class *cls;
	struct sys_device *sysdev, *err_dev;
	struct sysdev_driver *drv, *err_drv;
	int ret;

	pr_debug("Checking wake-up interrupts\n");

	/* Return error code if there are any wake-up interrupts pending */
	ret = check_wakeup_irqs();
	if (ret)
		return ret;

	WARN_ONCE(!irqs_disabled(),
		"Interrupts enabled while suspending system devices\n");

	pr_debug("Suspending System Devices\n");

	list_for_each_entry_reverse(cls, &system_kset->list, kset.kobj.entry) {
		pr_debug("Suspending type '%s':\n",
			 kobject_name(&cls->kset.kobj));

		list_for_each_entry(sysdev, &cls->kset.list, kobj.entry) {
			pr_debug(" %s\n", kobject_name(&sysdev->kobj));

			/* Call auxillary drivers first */
			list_for_each_entry(drv, &cls->drivers, entry) {
				if (drv->suspend) {
					ret = drv->suspend(sysdev, state);
					if (ret)
						goto aux_driver;
				}
				WARN_ONCE(!irqs_disabled(),
					"Interrupts enabled after %pF\n",
					drv->suspend);
			}

			/* Now call the generic one */
			if (cls->suspend) {
				ret = cls->suspend(sysdev, state);
				if (ret)
					goto cls_driver;
				WARN_ONCE(!irqs_disabled(),
					"Interrupts enabled after %pF\n",
					cls->suspend);
			}
		}
	}
	return 0;
	/* resume current sysdev */
cls_driver:
	drv = NULL;
	printk(KERN_ERR "Class suspend failed for %s\n",
		kobject_name(&sysdev->kobj));

aux_driver:
	if (drv)
		printk(KERN_ERR "Class driver suspend failed for %s\n",
				kobject_name(&sysdev->kobj));
	list_for_each_entry(err_drv, &cls->drivers, entry) {
		if (err_drv == drv)
			break;
		if (err_drv->resume)
			err_drv->resume(sysdev);
	}

	/* resume other sysdevs in current class */
	list_for_each_entry(err_dev, &cls->kset.list, kobj.entry) {
		if (err_dev == sysdev)
			break;
		pr_debug(" %s\n", kobject_name(&err_dev->kobj));
		__sysdev_resume(err_dev);
	}

	/* resume other classes */
	list_for_each_entry_continue(cls, &system_kset->list, kset.kobj.entry) {
		list_for_each_entry(err_dev, &cls->kset.list, kobj.entry) {
			pr_debug(" %s\n", kobject_name(&err_dev->kobj));
			__sysdev_resume(err_dev);
		}
	}
	return ret;
}
EXPORT_SYMBOL_GPL(sysdev_suspend);

/**
 *	sysdev_resume - Bring system devices back to life.
 *
 *	Similar to sysdev_suspend(), but we iterate the list forwards
 *	to guarantee that parent devices are resumed before their children.
 *
 *	Note: Interrupts are disabled when called.
 */
int sysdev_resume(void)
{
	struct sysdev_class *cls;

	WARN_ONCE(!irqs_disabled(),
		"Interrupts enabled while resuming system devices\n");

	pr_debug("Resuming System Devices\n");

	list_for_each_entry(cls, &system_kset->list, kset.kobj.entry) {
		struct sys_device *sysdev;

		pr_debug("Resuming type '%s':\n",
			 kobject_name(&cls->kset.kobj));

		list_for_each_entry(sysdev, &cls->kset.list, kobj.entry) {
			pr_debug(" %s\n", kobject_name(&sysdev->kobj));

			__sysdev_resume(sysdev);
		}
	}
	return 0;
}
EXPORT_SYMBOL_GPL(sysdev_resume);

int __init system_bus_init(void)
{
	system_kset = kset_create_and_add("system", NULL, &devices_kset->kobj);
	if (!system_kset)
		return -ENOMEM;
	return 0;
}

EXPORT_SYMBOL_GPL(sysdev_register);
EXPORT_SYMBOL_GPL(sysdev_unregister);

#define to_ext_attr(x) container_of(x, struct sysdev_ext_attribute, attr)

ssize_t sysdev_store_ulong(struct sys_device *sysdev,
			   struct sysdev_attribute *attr,
			   const char *buf, size_t size)
{
	struct sysdev_ext_attribute *ea = to_ext_attr(attr);
	char *end;
	unsigned long new = simple_strtoul(buf, &end, 0);
	if (end == buf)
		return -EINVAL;
	*(unsigned long *)(ea->var) = new;
	/* Always return full write size even if we didn't consume all */
	return size;
}
EXPORT_SYMBOL_GPL(sysdev_store_ulong);

ssize_t sysdev_show_ulong(struct sys_device *sysdev,
			  struct sysdev_attribute *attr,
			  char *buf)
{
	struct sysdev_ext_attribute *ea = to_ext_attr(attr);
	return snprintf(buf, PAGE_SIZE, "%lx\n", *(unsigned long *)(ea->var));
}
EXPORT_SYMBOL_GPL(sysdev_show_ulong);

ssize_t sysdev_store_int(struct sys_device *sysdev,
			   struct sysdev_attribute *attr,
			   const char *buf, size_t size)
{
	struct sysdev_ext_attribute *ea = to_ext_attr(attr);
	char *end;
	long new = simple_strtol(buf, &end, 0);
	if (end == buf || new > INT_MAX || new < INT_MIN)
		return -EINVAL;
	*(int *)(ea->var) = new;
	/* Always return full write size even if we didn't consume all */
	return size;
}
EXPORT_SYMBOL_GPL(sysdev_store_int);

ssize_t sysdev_show_int(struct sys_device *sysdev,
			  struct sysdev_attribute *attr,
			  char *buf)
{
	struct sysdev_ext_attribute *ea = to_ext_attr(attr);
	return snprintf(buf, PAGE_SIZE, "%d\n", *(int *)(ea->var));
}
EXPORT_SYMBOL_GPL(sysdev_show_int);

