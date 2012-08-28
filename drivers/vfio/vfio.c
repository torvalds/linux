/*
 * VFIO core
 *
 * Copyright (C) 2012 Red Hat, Inc.  All rights reserved.
 *     Author: Alex Williamson <alex.williamson@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Derived from original vfio:
 * Copyright 2010 Cisco Systems, Inc.  All rights reserved.
 * Author: Tom Lyon, pugs@cisco.com
 */

#include <linux/cdev.h>
#include <linux/compat.h>
#include <linux/device.h>
#include <linux/file.h>
#include <linux/anon_inodes.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/iommu.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/vfio.h>
#include <linux/wait.h>

#define DRIVER_VERSION	"0.3"
#define DRIVER_AUTHOR	"Alex Williamson <alex.williamson@redhat.com>"
#define DRIVER_DESC	"VFIO - User Level meta-driver"

static struct vfio {
	struct class			*class;
	struct list_head		iommu_drivers_list;
	struct mutex			iommu_drivers_lock;
	struct list_head		group_list;
	struct idr			group_idr;
	struct mutex			group_lock;
	struct cdev			group_cdev;
	struct device			*dev;
	dev_t				devt;
	struct cdev			cdev;
	wait_queue_head_t		release_q;
} vfio;

struct vfio_iommu_driver {
	const struct vfio_iommu_driver_ops	*ops;
	struct list_head			vfio_next;
};

struct vfio_container {
	struct kref			kref;
	struct list_head		group_list;
	struct mutex			group_lock;
	struct vfio_iommu_driver	*iommu_driver;
	void				*iommu_data;
};

struct vfio_group {
	struct kref			kref;
	int				minor;
	atomic_t			container_users;
	struct iommu_group		*iommu_group;
	struct vfio_container		*container;
	struct list_head		device_list;
	struct mutex			device_lock;
	struct device			*dev;
	struct notifier_block		nb;
	struct list_head		vfio_next;
	struct list_head		container_next;
};

struct vfio_device {
	struct kref			kref;
	struct device			*dev;
	const struct vfio_device_ops	*ops;
	struct vfio_group		*group;
	struct list_head		group_next;
	void				*device_data;
};

/**
 * IOMMU driver registration
 */
int vfio_register_iommu_driver(const struct vfio_iommu_driver_ops *ops)
{
	struct vfio_iommu_driver *driver, *tmp;

	driver = kzalloc(sizeof(*driver), GFP_KERNEL);
	if (!driver)
		return -ENOMEM;

	driver->ops = ops;

	mutex_lock(&vfio.iommu_drivers_lock);

	/* Check for duplicates */
	list_for_each_entry(tmp, &vfio.iommu_drivers_list, vfio_next) {
		if (tmp->ops == ops) {
			mutex_unlock(&vfio.iommu_drivers_lock);
			kfree(driver);
			return -EINVAL;
		}
	}

	list_add(&driver->vfio_next, &vfio.iommu_drivers_list);

	mutex_unlock(&vfio.iommu_drivers_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(vfio_register_iommu_driver);

void vfio_unregister_iommu_driver(const struct vfio_iommu_driver_ops *ops)
{
	struct vfio_iommu_driver *driver;

	mutex_lock(&vfio.iommu_drivers_lock);
	list_for_each_entry(driver, &vfio.iommu_drivers_list, vfio_next) {
		if (driver->ops == ops) {
			list_del(&driver->vfio_next);
			mutex_unlock(&vfio.iommu_drivers_lock);
			kfree(driver);
			return;
		}
	}
	mutex_unlock(&vfio.iommu_drivers_lock);
}
EXPORT_SYMBOL_GPL(vfio_unregister_iommu_driver);

/**
 * Group minor allocation/free - both called with vfio.group_lock held
 */
static int vfio_alloc_group_minor(struct vfio_group *group)
{
	int ret, minor;

again:
	if (unlikely(idr_pre_get(&vfio.group_idr, GFP_KERNEL) == 0))
		return -ENOMEM;

	/* index 0 is used by /dev/vfio/vfio */
	ret = idr_get_new_above(&vfio.group_idr, group, 1, &minor);
	if (ret == -EAGAIN)
		goto again;
	if (ret || minor > MINORMASK) {
		if (minor > MINORMASK)
			idr_remove(&vfio.group_idr, minor);
		return -ENOSPC;
	}

	return minor;
}

static void vfio_free_group_minor(int minor)
{
	idr_remove(&vfio.group_idr, minor);
}

static int vfio_iommu_group_notifier(struct notifier_block *nb,
				     unsigned long action, void *data);
static void vfio_group_get(struct vfio_group *group);

/**
 * Container objects - containers are created when /dev/vfio/vfio is
 * opened, but their lifecycle extends until the last user is done, so
 * it's freed via kref.  Must support container/group/device being
 * closed in any order.
 */
static void vfio_container_get(struct vfio_container *container)
{
	kref_get(&container->kref);
}

static void vfio_container_release(struct kref *kref)
{
	struct vfio_container *container;
	container = container_of(kref, struct vfio_container, kref);

	kfree(container);
}

static void vfio_container_put(struct vfio_container *container)
{
	kref_put(&container->kref, vfio_container_release);
}

/**
 * Group objects - create, release, get, put, search
 */
static struct vfio_group *vfio_create_group(struct iommu_group *iommu_group)
{
	struct vfio_group *group, *tmp;
	struct device *dev;
	int ret, minor;

	group = kzalloc(sizeof(*group), GFP_KERNEL);
	if (!group)
		return ERR_PTR(-ENOMEM);

	kref_init(&group->kref);
	INIT_LIST_HEAD(&group->device_list);
	mutex_init(&group->device_lock);
	atomic_set(&group->container_users, 0);
	group->iommu_group = iommu_group;

	group->nb.notifier_call = vfio_iommu_group_notifier;

	/*
	 * blocking notifiers acquire a rwsem around registering and hold
	 * it around callback.  Therefore, need to register outside of
	 * vfio.group_lock to avoid A-B/B-A contention.  Our callback won't
	 * do anything unless it can find the group in vfio.group_list, so
	 * no harm in registering early.
	 */
	ret = iommu_group_register_notifier(iommu_group, &group->nb);
	if (ret) {
		kfree(group);
		return ERR_PTR(ret);
	}

	mutex_lock(&vfio.group_lock);

	minor = vfio_alloc_group_minor(group);
	if (minor < 0) {
		mutex_unlock(&vfio.group_lock);
		kfree(group);
		return ERR_PTR(minor);
	}

	/* Did we race creating this group? */
	list_for_each_entry(tmp, &vfio.group_list, vfio_next) {
		if (tmp->iommu_group == iommu_group) {
			vfio_group_get(tmp);
			vfio_free_group_minor(minor);
			mutex_unlock(&vfio.group_lock);
			kfree(group);
			return tmp;
		}
	}

	dev = device_create(vfio.class, NULL, MKDEV(MAJOR(vfio.devt), minor),
			    group, "%d", iommu_group_id(iommu_group));
	if (IS_ERR(dev)) {
		vfio_free_group_minor(minor);
		mutex_unlock(&vfio.group_lock);
		kfree(group);
		return (struct vfio_group *)dev; /* ERR_PTR */
	}

	group->minor = minor;
	group->dev = dev;

	list_add(&group->vfio_next, &vfio.group_list);

	mutex_unlock(&vfio.group_lock);

	return group;
}

/* called with vfio.group_lock held */
static void vfio_group_release(struct kref *kref)
{
	struct vfio_group *group = container_of(kref, struct vfio_group, kref);

	WARN_ON(!list_empty(&group->device_list));

	device_destroy(vfio.class, MKDEV(MAJOR(vfio.devt), group->minor));
	list_del(&group->vfio_next);
	vfio_free_group_minor(group->minor);

	mutex_unlock(&vfio.group_lock);

	/*
	 * Unregister outside of lock.  A spurious callback is harmless now
	 * that the group is no longer in vfio.group_list.
	 */
	iommu_group_unregister_notifier(group->iommu_group, &group->nb);

	kfree(group);
}

static void vfio_group_put(struct vfio_group *group)
{
	kref_put_mutex(&group->kref, vfio_group_release, &vfio.group_lock);
}

/* Assume group_lock or group reference is held */
static void vfio_group_get(struct vfio_group *group)
{
	kref_get(&group->kref);
}

/*
 * Not really a try as we will sleep for mutex, but we need to make
 * sure the group pointer is valid under lock and get a reference.
 */
static struct vfio_group *vfio_group_try_get(struct vfio_group *group)
{
	struct vfio_group *target = group;

	mutex_lock(&vfio.group_lock);
	list_for_each_entry(group, &vfio.group_list, vfio_next) {
		if (group == target) {
			vfio_group_get(group);
			mutex_unlock(&vfio.group_lock);
			return group;
		}
	}
	mutex_unlock(&vfio.group_lock);

	return NULL;
}

static
struct vfio_group *vfio_group_get_from_iommu(struct iommu_group *iommu_group)
{
	struct vfio_group *group;

	mutex_lock(&vfio.group_lock);
	list_for_each_entry(group, &vfio.group_list, vfio_next) {
		if (group->iommu_group == iommu_group) {
			vfio_group_get(group);
			mutex_unlock(&vfio.group_lock);
			return group;
		}
	}
	mutex_unlock(&vfio.group_lock);

	return NULL;
}

static struct vfio_group *vfio_group_get_from_minor(int minor)
{
	struct vfio_group *group;

	mutex_lock(&vfio.group_lock);
	group = idr_find(&vfio.group_idr, minor);
	if (!group) {
		mutex_unlock(&vfio.group_lock);
		return NULL;
	}
	vfio_group_get(group);
	mutex_unlock(&vfio.group_lock);

	return group;
}

/**
 * Device objects - create, release, get, put, search
 */
static
struct vfio_device *vfio_group_create_device(struct vfio_group *group,
					     struct device *dev,
					     const struct vfio_device_ops *ops,
					     void *device_data)
{
	struct vfio_device *device;
	int ret;

	device = kzalloc(sizeof(*device), GFP_KERNEL);
	if (!device)
		return ERR_PTR(-ENOMEM);

	kref_init(&device->kref);
	device->dev = dev;
	device->group = group;
	device->ops = ops;
	device->device_data = device_data;

	ret = dev_set_drvdata(dev, device);
	if (ret) {
		kfree(device);
		return ERR_PTR(ret);
	}

	/* No need to get group_lock, caller has group reference */
	vfio_group_get(group);

	mutex_lock(&group->device_lock);
	list_add(&device->group_next, &group->device_list);
	mutex_unlock(&group->device_lock);

	return device;
}

static void vfio_device_release(struct kref *kref)
{
	struct vfio_device *device = container_of(kref,
						  struct vfio_device, kref);
	struct vfio_group *group = device->group;

	list_del(&device->group_next);
	mutex_unlock(&group->device_lock);

	dev_set_drvdata(device->dev, NULL);

	kfree(device);

	/* vfio_del_group_dev may be waiting for this device */
	wake_up(&vfio.release_q);
}

/* Device reference always implies a group reference */
static void vfio_device_put(struct vfio_device *device)
{
	struct vfio_group *group = device->group;
	kref_put_mutex(&device->kref, vfio_device_release, &group->device_lock);
	vfio_group_put(group);
}

static void vfio_device_get(struct vfio_device *device)
{
	vfio_group_get(device->group);
	kref_get(&device->kref);
}

static struct vfio_device *vfio_group_get_device(struct vfio_group *group,
						 struct device *dev)
{
	struct vfio_device *device;

	mutex_lock(&group->device_lock);
	list_for_each_entry(device, &group->device_list, group_next) {
		if (device->dev == dev) {
			vfio_device_get(device);
			mutex_unlock(&group->device_lock);
			return device;
		}
	}
	mutex_unlock(&group->device_lock);
	return NULL;
}

/*
 * Whitelist some drivers that we know are safe (no dma) or just sit on
 * a device.  It's not always practical to leave a device within a group
 * driverless as it could get re-bound to something unsafe.
 */
static const char * const vfio_driver_whitelist[] = { "pci-stub" };

static bool vfio_whitelisted_driver(struct device_driver *drv)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(vfio_driver_whitelist); i++) {
		if (!strcmp(drv->name, vfio_driver_whitelist[i]))
			return true;
	}

	return false;
}

/*
 * A vfio group is viable for use by userspace if all devices are either
 * driver-less or bound to a vfio or whitelisted driver.  We test the
 * latter by the existence of a struct vfio_device matching the dev.
 */
static int vfio_dev_viable(struct device *dev, void *data)
{
	struct vfio_group *group = data;
	struct vfio_device *device;

	if (!dev->driver || vfio_whitelisted_driver(dev->driver))
		return 0;

	device = vfio_group_get_device(group, dev);
	if (device) {
		vfio_device_put(device);
		return 0;
	}

	return -EINVAL;
}

/**
 * Async device support
 */
static int vfio_group_nb_add_dev(struct vfio_group *group, struct device *dev)
{
	struct vfio_device *device;

	/* Do we already know about it?  We shouldn't */
	device = vfio_group_get_device(group, dev);
	if (WARN_ON_ONCE(device)) {
		vfio_device_put(device);
		return 0;
	}

	/* Nothing to do for idle groups */
	if (!atomic_read(&group->container_users))
		return 0;

	/* TODO Prevent device auto probing */
	WARN("Device %s added to live group %d!\n", dev_name(dev),
	     iommu_group_id(group->iommu_group));

	return 0;
}

static int vfio_group_nb_del_dev(struct vfio_group *group, struct device *dev)
{
	struct vfio_device *device;

	/*
	 * Expect to fall out here.  If a device was in use, it would
	 * have been bound to a vfio sub-driver, which would have blocked
	 * in .remove at vfio_del_group_dev.  Sanity check that we no
	 * longer track the device, so it's safe to remove.
	 */
	device = vfio_group_get_device(group, dev);
	if (likely(!device))
		return 0;

	WARN("Device %s removed from live group %d!\n", dev_name(dev),
	     iommu_group_id(group->iommu_group));

	vfio_device_put(device);
	return 0;
}

static int vfio_group_nb_verify(struct vfio_group *group, struct device *dev)
{
	/* We don't care what happens when the group isn't in use */
	if (!atomic_read(&group->container_users))
		return 0;

	return vfio_dev_viable(dev, group);
}

static int vfio_iommu_group_notifier(struct notifier_block *nb,
				     unsigned long action, void *data)
{
	struct vfio_group *group = container_of(nb, struct vfio_group, nb);
	struct device *dev = data;

	/*
	 * Need to go through a group_lock lookup to get a reference or
	 * we risk racing a group being removed.  Leave a WARN_ON for
	 * debuging, but if the group no longer exists, a spurious notify
	 * is harmless.
	 */
	group = vfio_group_try_get(group);
	if (WARN_ON(!group))
		return NOTIFY_OK;

	switch (action) {
	case IOMMU_GROUP_NOTIFY_ADD_DEVICE:
		vfio_group_nb_add_dev(group, dev);
		break;
	case IOMMU_GROUP_NOTIFY_DEL_DEVICE:
		vfio_group_nb_del_dev(group, dev);
		break;
	case IOMMU_GROUP_NOTIFY_BIND_DRIVER:
		pr_debug("%s: Device %s, group %d binding to driver\n",
			 __func__, dev_name(dev),
			 iommu_group_id(group->iommu_group));
		break;
	case IOMMU_GROUP_NOTIFY_BOUND_DRIVER:
		pr_debug("%s: Device %s, group %d bound to driver %s\n",
			 __func__, dev_name(dev),
			 iommu_group_id(group->iommu_group), dev->driver->name);
		BUG_ON(vfio_group_nb_verify(group, dev));
		break;
	case IOMMU_GROUP_NOTIFY_UNBIND_DRIVER:
		pr_debug("%s: Device %s, group %d unbinding from driver %s\n",
			 __func__, dev_name(dev),
			 iommu_group_id(group->iommu_group), dev->driver->name);
		break;
	case IOMMU_GROUP_NOTIFY_UNBOUND_DRIVER:
		pr_debug("%s: Device %s, group %d unbound from driver\n",
			 __func__, dev_name(dev),
			 iommu_group_id(group->iommu_group));
		/*
		 * XXX An unbound device in a live group is ok, but we'd
		 * really like to avoid the above BUG_ON by preventing other
		 * drivers from binding to it.  Once that occurs, we have to
		 * stop the system to maintain isolation.  At a minimum, we'd
		 * want a toggle to disable driver auto probe for this device.
		 */
		break;
	}

	vfio_group_put(group);
	return NOTIFY_OK;
}

/**
 * VFIO driver API
 */
int vfio_add_group_dev(struct device *dev,
		       const struct vfio_device_ops *ops, void *device_data)
{
	struct iommu_group *iommu_group;
	struct vfio_group *group;
	struct vfio_device *device;

	iommu_group = iommu_group_get(dev);
	if (!iommu_group)
		return -EINVAL;

	group = vfio_group_get_from_iommu(iommu_group);
	if (!group) {
		group = vfio_create_group(iommu_group);
		if (IS_ERR(group)) {
			iommu_group_put(iommu_group);
			return PTR_ERR(group);
		}
	}

	device = vfio_group_get_device(group, dev);
	if (device) {
		WARN(1, "Device %s already exists on group %d\n",
		     dev_name(dev), iommu_group_id(iommu_group));
		vfio_device_put(device);
		vfio_group_put(group);
		iommu_group_put(iommu_group);
		return -EBUSY;
	}

	device = vfio_group_create_device(group, dev, ops, device_data);
	if (IS_ERR(device)) {
		vfio_group_put(group);
		iommu_group_put(iommu_group);
		return PTR_ERR(device);
	}

	/*
	 * Added device holds reference to iommu_group and vfio_device
	 * (which in turn holds reference to vfio_group).  Drop extra
	 * group reference used while acquiring device.
	 */
	vfio_group_put(group);

	return 0;
}
EXPORT_SYMBOL_GPL(vfio_add_group_dev);

/* Test whether a struct device is present in our tracking */
static bool vfio_dev_present(struct device *dev)
{
	struct iommu_group *iommu_group;
	struct vfio_group *group;
	struct vfio_device *device;

	iommu_group = iommu_group_get(dev);
	if (!iommu_group)
		return false;

	group = vfio_group_get_from_iommu(iommu_group);
	if (!group) {
		iommu_group_put(iommu_group);
		return false;
	}

	device = vfio_group_get_device(group, dev);
	if (!device) {
		vfio_group_put(group);
		iommu_group_put(iommu_group);
		return false;
	}

	vfio_device_put(device);
	vfio_group_put(group);
	iommu_group_put(iommu_group);
	return true;
}

/*
 * Decrement the device reference count and wait for the device to be
 * removed.  Open file descriptors for the device... */
void *vfio_del_group_dev(struct device *dev)
{
	struct vfio_device *device = dev_get_drvdata(dev);
	struct vfio_group *group = device->group;
	struct iommu_group *iommu_group = group->iommu_group;
	void *device_data = device->device_data;

	vfio_device_put(device);

	/* TODO send a signal to encourage this to be released */
	wait_event(vfio.release_q, !vfio_dev_present(dev));

	iommu_group_put(iommu_group);

	return device_data;
}
EXPORT_SYMBOL_GPL(vfio_del_group_dev);

/**
 * VFIO base fd, /dev/vfio/vfio
 */
static long vfio_ioctl_check_extension(struct vfio_container *container,
				       unsigned long arg)
{
	struct vfio_iommu_driver *driver = container->iommu_driver;
	long ret = 0;

	switch (arg) {
		/* No base extensions yet */
	default:
		/*
		 * If no driver is set, poll all registered drivers for
		 * extensions and return the first positive result.  If
		 * a driver is already set, further queries will be passed
		 * only to that driver.
		 */
		if (!driver) {
			mutex_lock(&vfio.iommu_drivers_lock);
			list_for_each_entry(driver, &vfio.iommu_drivers_list,
					    vfio_next) {
				if (!try_module_get(driver->ops->owner))
					continue;

				ret = driver->ops->ioctl(NULL,
							 VFIO_CHECK_EXTENSION,
							 arg);
				module_put(driver->ops->owner);
				if (ret > 0)
					break;
			}
			mutex_unlock(&vfio.iommu_drivers_lock);
		} else
			ret = driver->ops->ioctl(container->iommu_data,
						 VFIO_CHECK_EXTENSION, arg);
	}

	return ret;
}

/* hold container->group_lock */
static int __vfio_container_attach_groups(struct vfio_container *container,
					  struct vfio_iommu_driver *driver,
					  void *data)
{
	struct vfio_group *group;
	int ret = -ENODEV;

	list_for_each_entry(group, &container->group_list, container_next) {
		ret = driver->ops->attach_group(data, group->iommu_group);
		if (ret)
			goto unwind;
	}

	return ret;

unwind:
	list_for_each_entry_continue_reverse(group, &container->group_list,
					     container_next) {
		driver->ops->detach_group(data, group->iommu_group);
	}

	return ret;
}

static long vfio_ioctl_set_iommu(struct vfio_container *container,
				 unsigned long arg)
{
	struct vfio_iommu_driver *driver;
	long ret = -ENODEV;

	mutex_lock(&container->group_lock);

	/*
	 * The container is designed to be an unprivileged interface while
	 * the group can be assigned to specific users.  Therefore, only by
	 * adding a group to a container does the user get the privilege of
	 * enabling the iommu, which may allocate finite resources.  There
	 * is no unset_iommu, but by removing all the groups from a container,
	 * the container is deprivileged and returns to an unset state.
	 */
	if (list_empty(&container->group_list) || container->iommu_driver) {
		mutex_unlock(&container->group_lock);
		return -EINVAL;
	}

	mutex_lock(&vfio.iommu_drivers_lock);
	list_for_each_entry(driver, &vfio.iommu_drivers_list, vfio_next) {
		void *data;

		if (!try_module_get(driver->ops->owner))
			continue;

		/*
		 * The arg magic for SET_IOMMU is the same as CHECK_EXTENSION,
		 * so test which iommu driver reported support for this
		 * extension and call open on them.  We also pass them the
		 * magic, allowing a single driver to support multiple
		 * interfaces if they'd like.
		 */
		if (driver->ops->ioctl(NULL, VFIO_CHECK_EXTENSION, arg) <= 0) {
			module_put(driver->ops->owner);
			continue;
		}

		/* module reference holds the driver we're working on */
		mutex_unlock(&vfio.iommu_drivers_lock);

		data = driver->ops->open(arg);
		if (IS_ERR(data)) {
			ret = PTR_ERR(data);
			module_put(driver->ops->owner);
			goto skip_drivers_unlock;
		}

		ret = __vfio_container_attach_groups(container, driver, data);
		if (!ret) {
			container->iommu_driver = driver;
			container->iommu_data = data;
		} else {
			driver->ops->release(data);
			module_put(driver->ops->owner);
		}

		goto skip_drivers_unlock;
	}

	mutex_unlock(&vfio.iommu_drivers_lock);
skip_drivers_unlock:
	mutex_unlock(&container->group_lock);

	return ret;
}

static long vfio_fops_unl_ioctl(struct file *filep,
				unsigned int cmd, unsigned long arg)
{
	struct vfio_container *container = filep->private_data;
	struct vfio_iommu_driver *driver;
	void *data;
	long ret = -EINVAL;

	if (!container)
		return ret;

	driver = container->iommu_driver;
	data = container->iommu_data;

	switch (cmd) {
	case VFIO_GET_API_VERSION:
		ret = VFIO_API_VERSION;
		break;
	case VFIO_CHECK_EXTENSION:
		ret = vfio_ioctl_check_extension(container, arg);
		break;
	case VFIO_SET_IOMMU:
		ret = vfio_ioctl_set_iommu(container, arg);
		break;
	default:
		if (driver) /* passthrough all unrecognized ioctls */
			ret = driver->ops->ioctl(data, cmd, arg);
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long vfio_fops_compat_ioctl(struct file *filep,
				   unsigned int cmd, unsigned long arg)
{
	arg = (unsigned long)compat_ptr(arg);
	return vfio_fops_unl_ioctl(filep, cmd, arg);
}
#endif	/* CONFIG_COMPAT */

static int vfio_fops_open(struct inode *inode, struct file *filep)
{
	struct vfio_container *container;

	container = kzalloc(sizeof(*container), GFP_KERNEL);
	if (!container)
		return -ENOMEM;

	INIT_LIST_HEAD(&container->group_list);
	mutex_init(&container->group_lock);
	kref_init(&container->kref);

	filep->private_data = container;

	return 0;
}

static int vfio_fops_release(struct inode *inode, struct file *filep)
{
	struct vfio_container *container = filep->private_data;

	filep->private_data = NULL;

	vfio_container_put(container);

	return 0;
}

/*
 * Once an iommu driver is set, we optionally pass read/write/mmap
 * on to the driver, allowing management interfaces beyond ioctl.
 */
static ssize_t vfio_fops_read(struct file *filep, char __user *buf,
			      size_t count, loff_t *ppos)
{
	struct vfio_container *container = filep->private_data;
	struct vfio_iommu_driver *driver = container->iommu_driver;

	if (unlikely(!driver || !driver->ops->read))
		return -EINVAL;

	return driver->ops->read(container->iommu_data, buf, count, ppos);
}

static ssize_t vfio_fops_write(struct file *filep, const char __user *buf,
			       size_t count, loff_t *ppos)
{
	struct vfio_container *container = filep->private_data;
	struct vfio_iommu_driver *driver = container->iommu_driver;

	if (unlikely(!driver || !driver->ops->write))
		return -EINVAL;

	return driver->ops->write(container->iommu_data, buf, count, ppos);
}

static int vfio_fops_mmap(struct file *filep, struct vm_area_struct *vma)
{
	struct vfio_container *container = filep->private_data;
	struct vfio_iommu_driver *driver = container->iommu_driver;

	if (unlikely(!driver || !driver->ops->mmap))
		return -EINVAL;

	return driver->ops->mmap(container->iommu_data, vma);
}

static const struct file_operations vfio_fops = {
	.owner		= THIS_MODULE,
	.open		= vfio_fops_open,
	.release	= vfio_fops_release,
	.read		= vfio_fops_read,
	.write		= vfio_fops_write,
	.unlocked_ioctl	= vfio_fops_unl_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= vfio_fops_compat_ioctl,
#endif
	.mmap		= vfio_fops_mmap,
};

/**
 * VFIO Group fd, /dev/vfio/$GROUP
 */
static void __vfio_group_unset_container(struct vfio_group *group)
{
	struct vfio_container *container = group->container;
	struct vfio_iommu_driver *driver;

	mutex_lock(&container->group_lock);

	driver = container->iommu_driver;
	if (driver)
		driver->ops->detach_group(container->iommu_data,
					  group->iommu_group);

	group->container = NULL;
	list_del(&group->container_next);

	/* Detaching the last group deprivileges a container, remove iommu */
	if (driver && list_empty(&container->group_list)) {
		driver->ops->release(container->iommu_data);
		module_put(driver->ops->owner);
		container->iommu_driver = NULL;
		container->iommu_data = NULL;
	}

	mutex_unlock(&container->group_lock);

	vfio_container_put(container);
}

/*
 * VFIO_GROUP_UNSET_CONTAINER should fail if there are other users or
 * if there was no container to unset.  Since the ioctl is called on
 * the group, we know that still exists, therefore the only valid
 * transition here is 1->0.
 */
static int vfio_group_unset_container(struct vfio_group *group)
{
	int users = atomic_cmpxchg(&group->container_users, 1, 0);

	if (!users)
		return -EINVAL;
	if (users != 1)
		return -EBUSY;

	__vfio_group_unset_container(group);

	return 0;
}

/*
 * When removing container users, anything that removes the last user
 * implicitly removes the group from the container.  That is, if the
 * group file descriptor is closed, as well as any device file descriptors,
 * the group is free.
 */
static void vfio_group_try_dissolve_container(struct vfio_group *group)
{
	if (0 == atomic_dec_if_positive(&group->container_users))
		__vfio_group_unset_container(group);
}

static int vfio_group_set_container(struct vfio_group *group, int container_fd)
{
	struct file *filep;
	struct vfio_container *container;
	struct vfio_iommu_driver *driver;
	int ret = 0;

	if (atomic_read(&group->container_users))
		return -EINVAL;

	filep = fget(container_fd);
	if (!filep)
		return -EBADF;

	/* Sanity check, is this really our fd? */
	if (filep->f_op != &vfio_fops) {
		fput(filep);
		return -EINVAL;
	}

	container = filep->private_data;
	WARN_ON(!container); /* fget ensures we don't race vfio_release */

	mutex_lock(&container->group_lock);

	driver = container->iommu_driver;
	if (driver) {
		ret = driver->ops->attach_group(container->iommu_data,
						group->iommu_group);
		if (ret)
			goto unlock_out;
	}

	group->container = container;
	list_add(&group->container_next, &container->group_list);

	/* Get a reference on the container and mark a user within the group */
	vfio_container_get(container);
	atomic_inc(&group->container_users);

unlock_out:
	mutex_unlock(&container->group_lock);
	fput(filep);

	return ret;
}

static bool vfio_group_viable(struct vfio_group *group)
{
	return (iommu_group_for_each_dev(group->iommu_group,
					 group, vfio_dev_viable) == 0);
}

static const struct file_operations vfio_device_fops;

static int vfio_group_get_device_fd(struct vfio_group *group, char *buf)
{
	struct vfio_device *device;
	struct file *filep;
	int ret = -ENODEV;

	if (0 == atomic_read(&group->container_users) ||
	    !group->container->iommu_driver || !vfio_group_viable(group))
		return -EINVAL;

	mutex_lock(&group->device_lock);
	list_for_each_entry(device, &group->device_list, group_next) {
		if (strcmp(dev_name(device->dev), buf))
			continue;

		ret = device->ops->open(device->device_data);
		if (ret)
			break;
		/*
		 * We can't use anon_inode_getfd() because we need to modify
		 * the f_mode flags directly to allow more than just ioctls
		 */
		ret = get_unused_fd();
		if (ret < 0) {
			device->ops->release(device->device_data);
			break;
		}

		filep = anon_inode_getfile("[vfio-device]", &vfio_device_fops,
					   device, O_RDWR);
		if (IS_ERR(filep)) {
			put_unused_fd(ret);
			ret = PTR_ERR(filep);
			device->ops->release(device->device_data);
			break;
		}

		/*
		 * TODO: add an anon_inode interface to do this.
		 * Appears to be missing by lack of need rather than
		 * explicitly prevented.  Now there's need.
		 */
		filep->f_mode |= (FMODE_LSEEK | FMODE_PREAD | FMODE_PWRITE);

		vfio_device_get(device);
		atomic_inc(&group->container_users);

		fd_install(ret, filep);
		break;
	}
	mutex_unlock(&group->device_lock);

	return ret;
}

static long vfio_group_fops_unl_ioctl(struct file *filep,
				      unsigned int cmd, unsigned long arg)
{
	struct vfio_group *group = filep->private_data;
	long ret = -ENOTTY;

	switch (cmd) {
	case VFIO_GROUP_GET_STATUS:
	{
		struct vfio_group_status status;
		unsigned long minsz;

		minsz = offsetofend(struct vfio_group_status, flags);

		if (copy_from_user(&status, (void __user *)arg, minsz))
			return -EFAULT;

		if (status.argsz < minsz)
			return -EINVAL;

		status.flags = 0;

		if (vfio_group_viable(group))
			status.flags |= VFIO_GROUP_FLAGS_VIABLE;

		if (group->container)
			status.flags |= VFIO_GROUP_FLAGS_CONTAINER_SET;

		if (copy_to_user((void __user *)arg, &status, minsz))
			return -EFAULT;

		ret = 0;
		break;
	}
	case VFIO_GROUP_SET_CONTAINER:
	{
		int fd;

		if (get_user(fd, (int __user *)arg))
			return -EFAULT;

		if (fd < 0)
			return -EINVAL;

		ret = vfio_group_set_container(group, fd);
		break;
	}
	case VFIO_GROUP_UNSET_CONTAINER:
		ret = vfio_group_unset_container(group);
		break;
	case VFIO_GROUP_GET_DEVICE_FD:
	{
		char *buf;

		buf = strndup_user((const char __user *)arg, PAGE_SIZE);
		if (IS_ERR(buf))
			return PTR_ERR(buf);

		ret = vfio_group_get_device_fd(group, buf);
		kfree(buf);
		break;
	}
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long vfio_group_fops_compat_ioctl(struct file *filep,
					 unsigned int cmd, unsigned long arg)
{
	arg = (unsigned long)compat_ptr(arg);
	return vfio_group_fops_unl_ioctl(filep, cmd, arg);
}
#endif	/* CONFIG_COMPAT */

static int vfio_group_fops_open(struct inode *inode, struct file *filep)
{
	struct vfio_group *group;

	group = vfio_group_get_from_minor(iminor(inode));
	if (!group)
		return -ENODEV;

	if (group->container) {
		vfio_group_put(group);
		return -EBUSY;
	}

	filep->private_data = group;

	return 0;
}

static int vfio_group_fops_release(struct inode *inode, struct file *filep)
{
	struct vfio_group *group = filep->private_data;

	filep->private_data = NULL;

	vfio_group_try_dissolve_container(group);

	vfio_group_put(group);

	return 0;
}

static const struct file_operations vfio_group_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= vfio_group_fops_unl_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= vfio_group_fops_compat_ioctl,
#endif
	.open		= vfio_group_fops_open,
	.release	= vfio_group_fops_release,
};

/**
 * VFIO Device fd
 */
static int vfio_device_fops_release(struct inode *inode, struct file *filep)
{
	struct vfio_device *device = filep->private_data;

	device->ops->release(device->device_data);

	vfio_group_try_dissolve_container(device->group);

	vfio_device_put(device);

	return 0;
}

static long vfio_device_fops_unl_ioctl(struct file *filep,
				       unsigned int cmd, unsigned long arg)
{
	struct vfio_device *device = filep->private_data;

	if (unlikely(!device->ops->ioctl))
		return -EINVAL;

	return device->ops->ioctl(device->device_data, cmd, arg);
}

static ssize_t vfio_device_fops_read(struct file *filep, char __user *buf,
				     size_t count, loff_t *ppos)
{
	struct vfio_device *device = filep->private_data;

	if (unlikely(!device->ops->read))
		return -EINVAL;

	return device->ops->read(device->device_data, buf, count, ppos);
}

static ssize_t vfio_device_fops_write(struct file *filep,
				      const char __user *buf,
				      size_t count, loff_t *ppos)
{
	struct vfio_device *device = filep->private_data;

	if (unlikely(!device->ops->write))
		return -EINVAL;

	return device->ops->write(device->device_data, buf, count, ppos);
}

static int vfio_device_fops_mmap(struct file *filep, struct vm_area_struct *vma)
{
	struct vfio_device *device = filep->private_data;

	if (unlikely(!device->ops->mmap))
		return -EINVAL;

	return device->ops->mmap(device->device_data, vma);
}

#ifdef CONFIG_COMPAT
static long vfio_device_fops_compat_ioctl(struct file *filep,
					  unsigned int cmd, unsigned long arg)
{
	arg = (unsigned long)compat_ptr(arg);
	return vfio_device_fops_unl_ioctl(filep, cmd, arg);
}
#endif	/* CONFIG_COMPAT */

static const struct file_operations vfio_device_fops = {
	.owner		= THIS_MODULE,
	.release	= vfio_device_fops_release,
	.read		= vfio_device_fops_read,
	.write		= vfio_device_fops_write,
	.unlocked_ioctl	= vfio_device_fops_unl_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= vfio_device_fops_compat_ioctl,
#endif
	.mmap		= vfio_device_fops_mmap,
};

/**
 * Module/class support
 */
static char *vfio_devnode(struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "vfio/%s", dev_name(dev));
}

static int __init vfio_init(void)
{
	int ret;

	idr_init(&vfio.group_idr);
	mutex_init(&vfio.group_lock);
	mutex_init(&vfio.iommu_drivers_lock);
	INIT_LIST_HEAD(&vfio.group_list);
	INIT_LIST_HEAD(&vfio.iommu_drivers_list);
	init_waitqueue_head(&vfio.release_q);

	vfio.class = class_create(THIS_MODULE, "vfio");
	if (IS_ERR(vfio.class)) {
		ret = PTR_ERR(vfio.class);
		goto err_class;
	}

	vfio.class->devnode = vfio_devnode;

	ret = alloc_chrdev_region(&vfio.devt, 0, MINORMASK, "vfio");
	if (ret)
		goto err_base_chrdev;

	cdev_init(&vfio.cdev, &vfio_fops);
	ret = cdev_add(&vfio.cdev, vfio.devt, 1);
	if (ret)
		goto err_base_cdev;

	vfio.dev = device_create(vfio.class, NULL, vfio.devt, NULL, "vfio");
	if (IS_ERR(vfio.dev)) {
		ret = PTR_ERR(vfio.dev);
		goto err_base_dev;
	}

	/* /dev/vfio/$GROUP */
	cdev_init(&vfio.group_cdev, &vfio_group_fops);
	ret = cdev_add(&vfio.group_cdev,
		       MKDEV(MAJOR(vfio.devt), 1), MINORMASK - 1);
	if (ret)
		goto err_groups_cdev;

	pr_info(DRIVER_DESC " version: " DRIVER_VERSION "\n");

	/*
	 * Attempt to load known iommu-drivers.  This gives us a working
	 * environment without the user needing to explicitly load iommu
	 * drivers.
	 */
	request_module_nowait("vfio_iommu_type1");

	return 0;

err_groups_cdev:
	device_destroy(vfio.class, vfio.devt);
err_base_dev:
	cdev_del(&vfio.cdev);
err_base_cdev:
	unregister_chrdev_region(vfio.devt, MINORMASK);
err_base_chrdev:
	class_destroy(vfio.class);
	vfio.class = NULL;
err_class:
	return ret;
}

static void __exit vfio_cleanup(void)
{
	WARN_ON(!list_empty(&vfio.group_list));

	idr_destroy(&vfio.group_idr);
	cdev_del(&vfio.group_cdev);
	device_destroy(vfio.class, vfio.devt);
	cdev_del(&vfio.cdev);
	unregister_chrdev_region(vfio.devt, MINORMASK);
	class_destroy(vfio.class);
	vfio.class = NULL;
}

module_init(vfio_init);
module_exit(vfio_cleanup);

MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
