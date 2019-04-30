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

static LIST_HEAD(mdev_list);
static DEFINE_MUTEX(mdev_list_lock);

struct device *mdev_parent_dev(struct mdev_device *mdev)
{
	return mdev->parent->dev;
}
EXPORT_SYMBOL(mdev_parent_dev);

void *mdev_get_drvdata(struct mdev_device *mdev)
{
	return mdev->driver_data;
}
EXPORT_SYMBOL(mdev_get_drvdata);

void mdev_set_drvdata(struct mdev_device *mdev, void *data)
{
	mdev->driver_data = data;
}
EXPORT_SYMBOL(mdev_set_drvdata);

struct device *mdev_dev(struct mdev_device *mdev)
{
	return &mdev->dev;
}
EXPORT_SYMBOL(mdev_dev);

struct mdev_device *mdev_from_dev(struct device *dev)
{
	return dev_is_mdev(dev) ? to_mdev_device(dev) : NULL;
}
EXPORT_SYMBOL(mdev_from_dev);

uuid_le mdev_uuid(struct mdev_device *mdev)
{
	return mdev->uuid;
}
EXPORT_SYMBOL(mdev_uuid);

/* Should be called holding parent_list_lock */
static struct mdev_parent *__find_parent_device(struct device *dev)
{
	struct mdev_parent *parent;

	list_for_each_entry(parent, &parent_list, next) {
		if (parent->dev == dev)
			return parent;
	}
	return NULL;
}

static void mdev_release_parent(struct kref *kref)
{
	struct mdev_parent *parent = container_of(kref, struct mdev_parent,
						  ref);
	struct device *dev = parent->dev;

	kfree(parent);
	put_device(dev);
}

static
inline struct mdev_parent *mdev_get_parent(struct mdev_parent *parent)
{
	if (parent)
		kref_get(&parent->ref);

	return parent;
}

static inline void mdev_put_parent(struct mdev_parent *parent)
{
	if (parent)
		kref_put(&parent->ref, mdev_release_parent);
}

static int mdev_device_create_ops(struct kobject *kobj,
				  struct mdev_device *mdev)
{
	struct mdev_parent *parent = mdev->parent;
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
	struct mdev_parent *parent = mdev->parent;
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
	if (dev_is_mdev(dev))
		mdev_device_remove(dev, true);

	return 0;
}

/*
 * mdev_register_device : Register a device
 * @dev: device structure representing parent device.
 * @ops: Parent device operation structure to be registered.
 *
 * Add device to list of registered parent devices.
 * Returns a negative value on error, otherwise 0.
 */
int mdev_register_device(struct device *dev, const struct mdev_parent_ops *ops)
{
	int ret;
	struct mdev_parent *parent;

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
		parent = NULL;
		ret = -EEXIST;
		goto add_dev_err;
	}

	parent = kzalloc(sizeof(*parent), GFP_KERNEL);
	if (!parent) {
		ret = -ENOMEM;
		goto add_dev_err;
	}

	kref_init(&parent->ref);

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
	struct mdev_parent *parent;

	mutex_lock(&parent_list_lock);
	parent = __find_parent_device(dev);

	if (!parent) {
		mutex_unlock(&parent_list_lock);
		return;
	}
	dev_info(dev, "MDEV: Unregistering\n");

	list_del(&parent->next);
	class_compat_remove_link(mdev_bus_compat_class, dev, NULL);

	device_for_each_child(dev, NULL, mdev_device_remove_cb);

	parent_remove_sysfs_files(parent);

	mutex_unlock(&parent_list_lock);
	mdev_put_parent(parent);
}
EXPORT_SYMBOL(mdev_unregister_device);

static void mdev_device_release(struct device *dev)
{
	struct mdev_device *mdev = to_mdev_device(dev);

	mutex_lock(&mdev_list_lock);
	list_del(&mdev->next);
	mutex_unlock(&mdev_list_lock);

	dev_dbg(&mdev->dev, "MDEV: destroying\n");
	kfree(mdev);
}

int mdev_device_create(struct kobject *kobj, struct device *dev, uuid_le uuid)
{
	int ret;
	struct mdev_device *mdev, *tmp;
	struct mdev_parent *parent;
	struct mdev_type *type = to_mdev_type(kobj);

	parent = mdev_get_parent(type->parent);
	if (!parent)
		return -EINVAL;

	mutex_lock(&mdev_list_lock);

	/* Check for duplicate */
	list_for_each_entry(tmp, &mdev_list, next) {
		if (!uuid_le_cmp(tmp->uuid, uuid)) {
			mutex_unlock(&mdev_list_lock);
			ret = -EEXIST;
			goto mdev_fail;
		}
	}

	mdev = kzalloc(sizeof(*mdev), GFP_KERNEL);
	if (!mdev) {
		mutex_unlock(&mdev_list_lock);
		ret = -ENOMEM;
		goto mdev_fail;
	}

	memcpy(&mdev->uuid, &uuid, sizeof(uuid_le));
	list_add(&mdev->next, &mdev_list);
	mutex_unlock(&mdev_list_lock);

	mdev->parent = parent;
	kref_init(&mdev->ref);

	mdev->dev.parent  = dev;
	mdev->dev.bus     = &mdev_bus_type;
	mdev->dev.release = mdev_device_release;
	dev_set_name(&mdev->dev, "%pUl", uuid.b);

	ret = device_register(&mdev->dev);
	if (ret) {
		put_device(&mdev->dev);
		goto mdev_fail;
	}

	ret = mdev_device_create_ops(kobj, mdev);
	if (ret)
		goto create_fail;

	ret = mdev_create_sysfs_files(&mdev->dev, type);
	if (ret) {
		mdev_device_remove_ops(mdev, true);
		goto create_fail;
	}

	mdev->type_kobj = kobj;
	mdev->active = true;
	dev_dbg(&mdev->dev, "MDEV: created\n");

	return 0;

create_fail:
	device_unregister(&mdev->dev);
mdev_fail:
	mdev_put_parent(parent);
	return ret;
}

int mdev_device_remove(struct device *dev, bool force_remove)
{
	struct mdev_device *mdev, *tmp;
	struct mdev_parent *parent;
	struct mdev_type *type;
	int ret;

	mdev = to_mdev_device(dev);

	mutex_lock(&mdev_list_lock);
	list_for_each_entry(tmp, &mdev_list, next) {
		if (tmp == mdev)
			break;
	}

	if (tmp != mdev) {
		mutex_unlock(&mdev_list_lock);
		return -ENODEV;
	}

	if (!mdev->active) {
		mutex_unlock(&mdev_list_lock);
		return -EAGAIN;
	}

	mdev->active = false;
	mutex_unlock(&mdev_list_lock);

	type = to_mdev_type(mdev->type_kobj);
	parent = mdev->parent;

	ret = mdev_device_remove_ops(mdev, force_remove);
	if (ret) {
		mdev->active = true;
		return ret;
	}

	mdev_remove_sysfs_files(dev, type);
	device_unregister(dev);
	mdev_put_parent(parent);

	return 0;
}

static int __init mdev_init(void)
{
	return mdev_bus_register();
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
MODULE_SOFTDEP("post: vfio_mdev");
