/*
 * Mediated device Core Driver
 *
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
 *     Author: Neo Jia <cjia@nvidia.com>
 *             Kirti Wankhede <kwankhede@nvidia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/uuid.h>
#include <linux/sysfs.h>
#include <linux/mdev.h>

#include "mdev_private.h"

#define DRIVER_VERSION		"0.1"
#define DRIVER_AUTHOR		"NVIDIA Corporation"
#define DRIVER_DESC		"Mediated device Core Driver"

static LIST_HEAD(parent_list);
static DEFINE_MUTEX(parent_list_lock);
static struct class_compat *mdev_bus_compat_class;

static int _find_mdev_device(struct device *dev, void *data)
{
	struct mdev_device *mdev;

	if (!dev_is_mdev(dev))
		return 0;

	mdev = to_mdev_device(dev);

	if (uuid_le_cmp(mdev->uuid, *(uuid_le *)data) == 0)
		return 1;

	return 0;
}

static bool mdev_device_exist(struct parent_device *parent, uuid_le uuid)
{
	struct device *dev;

	dev = device_find_child(parent->dev, &uuid, _find_mdev_device);
	if (dev) {
		put_device(dev);
		return true;
	}

	return false;
}

/* Should be called holding parent_list_lock */
static struct parent_device *__find_parent_device(struct device *dev)
{
	struct parent_device *parent;

	list_for_each_entry(parent, &parent_list, next) {
		if (parent->dev == dev)
			return parent;
	}
	return NULL;
}

static void mdev_release_parent(struct kref *kref)
{
	struct parent_device *parent = container_of(kref, struct parent_device,
						    ref);
	struct device *dev = parent->dev;

	kfree(parent);
	put_device(dev);
}

static
inline struct parent_device *mdev_get_parent(struct parent_device *parent)
{
	if (parent)
		kref_get(&parent->ref);

	return parent;
}

static inline void mdev_put_parent(struct parent_device *parent)
{
	if (parent)
		kref_put(&parent->ref, mdev_release_parent);
}

static int mdev_device_create_ops(struct kobject *kobj,
				  struct mdev_device *mdev)
{
	struct parent_device *parent = mdev->parent;
	int ret;

	ret = parent->ops->create(kobj, mdev);
	if (ret)
		return ret;

	ret = sysfs_create_groups(&mdev->dev.kobj,
				  parent->ops->mdev_attr_groups);
	if (ret)
		parent->ops->remove(mdev);

	return ret;
}

/*
 * mdev_device_remove_ops gets called from sysfs's 'remove' and when parent
 * device is being unregistered from mdev device framework.
 * - 'force_remove' is set to 'false' when called from sysfs's 'remove' which
 *   indicates that if the mdev device is active, used by VMM or userspace
 *   application, vendor driver could return error then don't remove the device.
 * - 'force_remove' is set to 'true' when called from mdev_unregister_device()
 *   which indicate that parent device is being removed from mdev device
 *   framework so remove mdev device forcefully.
 */
static int mdev_device_remove_ops(struct mdev_device *mdev, bool force_remove)
{
	struct parent_device *parent = mdev->parent;
	int ret;

	/*
	 * Vendor driver can return error if VMM or userspace application is
	 * using this mdev device.
	 */
	ret = parent->ops->remove(mdev);
	if (ret && !force_remove)
		return -EBUSY;

	sysfs_remove_groups(&mdev->dev.kobj, parent->ops->mdev_attr_groups);
	return 0;
}

static int mdev_device_remove_cb(struct device *dev, void *data)
{
	if (!dev_is_mdev(dev))
		return 0;

	return mdev_device_remove(dev, data ? *(bool *)data : true);
}

/*
 * mdev_register_device : Register a device
 * @dev: device structure representing parent device.
 * @ops: Parent device operation structure to be registered.
 *
 * Add device to list of registered parent devices.
 * Returns a negative value on error, otherwise 0.
 */
int mdev_register_device(struct device *dev, const struct parent_ops *ops)
{
	int ret;
	struct parent_device *parent;

	/* check for mandatory ops */
	if (!ops || !ops->create || !ops->remove || !ops->supported_type_groups)
		return -EINVAL;

	dev = get_device(dev);
	if (!dev)
		return -EINVAL;

	mutex_lock(&parent_list_lock);

	/* Check for duplicate */
	parent = __find_parent_device(dev);
	if (parent) {
		ret = -EEXIST;
		goto add_dev_err;
	}

	parent = kzalloc(sizeof(*parent), GFP_KERNEL);
	if (!parent) {
		ret = -ENOMEM;
		goto add_dev_err;
	}

	kref_init(&parent->ref);
	mutex_init(&parent->lock);

	parent->dev = dev;
	parent->ops = ops;

	if (!mdev_bus_compat_class) {
		mdev_bus_compat_class = class_compat_register("mdev_bus");
		if (!mdev_bus_compat_class) {
			ret = -ENOMEM;
			goto add_dev_err;
		}
	}

	ret = parent_create_sysfs_files(parent);
	if (ret)
		goto add_dev_err;

	ret = class_compat_create_link(mdev_bus_compat_class, dev, NULL);
	if (ret)
		dev_warn(dev, "Failed to create compatibility class link\n");

	list_add(&parent->next, &parent_list);
	mutex_unlock(&parent_list_lock);

	dev_info(dev, "MDEV: Registered\n");
	return 0;

add_dev_err:
	mutex_unlock(&parent_list_lock);
	if (parent)
		mdev_put_parent(parent);
	else
		put_device(dev);
	return ret;
}
EXPORT_SYMBOL(mdev_register_device);

/*
 * mdev_unregister_device : Unregister a parent device
 * @dev: device structure representing parent device.
 *
 * Remove device from list of registered parent devices. Give a chance to free
 * existing mediated devices for given device.
 */

void mdev_unregister_device(struct device *dev)
{
	struct parent_device *parent;
	bool force_remove = true;

	mutex_lock(&parent_list_lock);
	parent = __find_parent_device(dev);

	if (!parent) {
		mutex_unlock(&parent_list_lock);
		return;
	}
	dev_info(dev, "MDEV: Unregistering\n");

	list_del(&parent->next);
	class_compat_remove_link(mdev_bus_compat_class, dev, NULL);

	device_for_each_child(dev, (void *)&force_remove,
			      mdev_device_remove_cb);

	parent_remove_sysfs_files(parent);

	mutex_unlock(&parent_list_lock);
	mdev_put_parent(parent);
}
EXPORT_SYMBOL(mdev_unregister_device);

static void mdev_device_release(struct device *dev)
{
	struct mdev_device *mdev = to_mdev_device(dev);

	dev_dbg(&mdev->dev, "MDEV: destroying\n");
	kfree(mdev);
}

int mdev_device_create(struct kobject *kobj, struct device *dev, uuid_le uuid)
{
	int ret;
	struct mdev_device *mdev;
	struct parent_device *parent;
	struct mdev_type *type = to_mdev_type(kobj);

	parent = mdev_get_parent(type->parent);
	if (!parent)
		return -EINVAL;

	mutex_lock(&parent->lock);

	/* Check for duplicate */
	if (mdev_device_exist(parent, uuid)) {
		ret = -EEXIST;
		goto create_err;
	}

	mdev = kzalloc(sizeof(*mdev), GFP_KERNEL);
	if (!mdev) {
		ret = -ENOMEM;
		goto create_err;
	}

	memcpy(&mdev->uuid, &uuid, sizeof(uuid_le));
	mdev->parent = parent;
	kref_init(&mdev->ref);

	mdev->dev.parent  = dev;
	mdev->dev.bus     = &mdev_bus_type;
	mdev->dev.release = mdev_device_release;
	dev_set_name(&mdev->dev, "%pUl", uuid.b);

	ret = device_register(&mdev->dev);
	if (ret) {
		put_device(&mdev->dev);
		goto create_err;
	}

	ret = mdev_device_create_ops(kobj, mdev);
	if (ret)
		goto create_failed;

	ret = mdev_create_sysfs_files(&mdev->dev, type);
	if (ret) {
		mdev_device_remove_ops(mdev, true);
		goto create_failed;
	}

	mdev->type_kobj = kobj;
	dev_dbg(&mdev->dev, "MDEV: created\n");

	mutex_unlock(&parent->lock);
	return ret;

create_failed:
	device_unregister(&mdev->dev);

create_err:
	mutex_unlock(&parent->lock);
	mdev_put_parent(parent);
	return ret;
}

int mdev_device_remove(struct device *dev, bool force_remove)
{
	struct mdev_device *mdev;
	struct parent_device *parent;
	struct mdev_type *type;
	int ret;

	mdev = to_mdev_device(dev);
	type = to_mdev_type(mdev->type_kobj);
	parent = mdev->parent;
	mutex_lock(&parent->lock);

	ret = mdev_device_remove_ops(mdev, force_remove);
	if (ret) {
		mutex_unlock(&parent->lock);
		return ret;
	}

	mdev_remove_sysfs_files(dev, type);
	device_unregister(dev);
	mutex_unlock(&parent->lock);
	mdev_put_parent(parent);
	return ret;
}

static int __init mdev_init(void)
{
	int ret;

	ret = mdev_bus_register();

	/*
	 * Attempt to load known vfio_mdev.  This gives us a working environment
	 * without the user needing to explicitly load vfio_mdev driver.
	 */
	if (!ret)
		request_module_nowait("vfio_mdev");

	return ret;
}

static void __exit mdev_exit(void)
{
	if (mdev_bus_compat_class)
		class_compat_unregister(mdev_bus_compat_class);

	mdev_bus_unregister();
}

module_init(mdev_init)
module_exit(mdev_exit)

MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
