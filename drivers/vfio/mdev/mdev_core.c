// SPDX-License-Identifier: GPL-2.0-only
/*
 * Mediated device Core Driver
 *
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
 *     Author: Neo Jia <cjia@nvidia.com>
 *             Kirti Wankhede <kwankhede@nvidia.com>
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

const guid_t *mdev_uuid(struct mdev_device *mdev)
{
	return &mdev->uuid;
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

static struct mdev_parent *mdev_get_parent(struct mdev_parent *parent)
{
	if (parent)
		kref_get(&parent->ref);

	return parent;
}

static void mdev_put_parent(struct mdev_parent *parent)
{
	if (parent)
		kref_put(&parent->ref, mdev_release_parent);
}

/* Caller must hold parent unreg_sem read or write lock */
static void mdev_device_remove_common(struct mdev_device *mdev)
{
	struct mdev_parent *parent;
	struct mdev_type *type;
	int ret;

	type = to_mdev_type(mdev->type_kobj);
	mdev_remove_sysfs_files(&mdev->dev, type);
	device_del(&mdev->dev);
	parent = mdev->parent;
	lockdep_assert_held(&parent->unreg_sem);
	ret = parent->ops->remove(mdev);
	if (ret)
		dev_err(&mdev->dev, "Remove failed: err=%d\n", ret);

	/* Balances with device_initialize() */
	put_device(&mdev->dev);
	mdev_put_parent(parent);
}

static int mdev_device_remove_cb(struct device *dev, void *data)
{
	if (dev_is_mdev(dev)) {
		struct mdev_device *mdev;

		mdev = to_mdev_device(dev);
		mdev_device_remove_common(mdev);
	}
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
	char *env_string = "MDEV_STATE=registered";
	char *envp[] = { env_string, NULL };

	/* check for mandatory ops */
	if (!ops || !ops->create || !ops->remove || !ops->supported_type_groups)
		return -EINVAL;

	dev = get_device(dev);
	if (!dev)
		return -EINVAL;

	/* Not mandatory, but its absence could be a problem */
	if (!ops->request)
		dev_info(dev, "Driver cannot be asked to release device\n");

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
	init_rwsem(&parent->unreg_sem);

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
	kobject_uevent_env(&dev->kobj, KOBJ_CHANGE, envp);

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
	char *env_string = "MDEV_STATE=unregistered";
	char *envp[] = { env_string, NULL };

	mutex_lock(&parent_list_lock);
	parent = __find_parent_device(dev);

	if (!parent) {
		mutex_unlock(&parent_list_lock);
		return;
	}
	dev_info(dev, "MDEV: Unregistering\n");

	list_del(&parent->next);
	mutex_unlock(&parent_list_lock);

	down_write(&parent->unreg_sem);

	class_compat_remove_link(mdev_bus_compat_class, dev, NULL);

	device_for_each_child(dev, NULL, mdev_device_remove_cb);

	parent_remove_sysfs_files(parent);
	up_write(&parent->unreg_sem);

	mdev_put_parent(parent);

	/* We still have the caller's reference to use for the uevent */
	kobject_uevent_env(&dev->kobj, KOBJ_CHANGE, envp);
}
EXPORT_SYMBOL(mdev_unregister_device);

static void mdev_device_free(struct mdev_device *mdev)
{
	mutex_lock(&mdev_list_lock);
	list_del(&mdev->next);
	mutex_unlock(&mdev_list_lock);

	dev_dbg(&mdev->dev, "MDEV: destroying\n");
	kfree(mdev);
}

static void mdev_device_release(struct device *dev)
{
	struct mdev_device *mdev = to_mdev_device(dev);

	mdev_device_free(mdev);
}

int mdev_device_create(struct kobject *kobj,
		       struct device *dev, const guid_t *uuid)
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
		if (guid_equal(&tmp->uuid, uuid)) {
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

	guid_copy(&mdev->uuid, uuid);
	list_add(&mdev->next, &mdev_list);
	mutex_unlock(&mdev_list_lock);

	mdev->parent = parent;

	/* Check if parent unregistration has started */
	if (!down_read_trylock(&parent->unreg_sem)) {
		mdev_device_free(mdev);
		ret = -ENODEV;
		goto mdev_fail;
	}

	device_initialize(&mdev->dev);
	mdev->dev.parent  = dev;
	mdev->dev.bus     = &mdev_bus_type;
	mdev->dev.release = mdev_device_release;
	dev_set_name(&mdev->dev, "%pUl", uuid);
	mdev->dev.groups = parent->ops->mdev_attr_groups;
	mdev->type_kobj = kobj;

	ret = parent->ops->create(kobj, mdev);
	if (ret)
		goto ops_create_fail;

	ret = device_add(&mdev->dev);
	if (ret)
		goto add_fail;

	ret = mdev_create_sysfs_files(&mdev->dev, type);
	if (ret)
		goto sysfs_fail;

	mdev->active = true;
	dev_dbg(&mdev->dev, "MDEV: created\n");
	up_read(&parent->unreg_sem);

	return 0;

sysfs_fail:
	device_del(&mdev->dev);
add_fail:
	parent->ops->remove(mdev);
ops_create_fail:
	up_read(&parent->unreg_sem);
	put_device(&mdev->dev);
mdev_fail:
	mdev_put_parent(parent);
	return ret;
}

int mdev_device_remove(struct device *dev)
{
	struct mdev_device *mdev, *tmp;
	struct mdev_parent *parent;

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

	parent = mdev->parent;
	/* Check if parent unregistration has started */
	if (!down_read_trylock(&parent->unreg_sem))
		return -ENODEV;

	mdev_device_remove_common(mdev);
	up_read(&parent->unreg_sem);
	return 0;
}

int mdev_set_iommu_device(struct device *dev, struct device *iommu_device)
{
	struct mdev_device *mdev = to_mdev_device(dev);

	mdev->iommu_device = iommu_device;

	return 0;
}
EXPORT_SYMBOL(mdev_set_iommu_device);

struct device *mdev_get_iommu_device(struct device *dev)
{
	struct mdev_device *mdev = to_mdev_device(dev);

	return mdev->iommu_device;
}
EXPORT_SYMBOL(mdev_get_iommu_device);

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
