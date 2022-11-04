// SPDX-License-Identifier: GPL-2.0-only
/*
 * Mediated device Core Driver
 *
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
 *     Author: Neo Jia <cjia@nvidia.com>
 *             Kirti Wankhede <kwankhede@nvidia.com>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/mdev.h>

#include "mdev_private.h"

#define DRIVER_VERSION		"0.1"
#define DRIVER_AUTHOR		"NVIDIA Corporation"
#define DRIVER_DESC		"Mediated device Core Driver"

static struct class_compat *mdev_bus_compat_class;

static LIST_HEAD(mdev_list);
static DEFINE_MUTEX(mdev_list_lock);

/* Caller must hold parent unreg_sem read or write lock */
static void mdev_device_remove_common(struct mdev_device *mdev)
{
	struct mdev_parent *parent = mdev->type->parent;

	mdev_remove_sysfs_files(mdev);
	device_del(&mdev->dev);
	lockdep_assert_held(&parent->unreg_sem);
	/* Balances with device_initialize() */
	put_device(&mdev->dev);
}

static int mdev_device_remove_cb(struct device *dev, void *data)
{
	if (dev->bus == &mdev_bus_type)
		mdev_device_remove_common(to_mdev_device(dev));
	return 0;
}

/*
 * mdev_register_parent: Register a device as parent for mdevs
 * @parent: parent structure registered
 * @dev: device structure representing parent device.
 * @mdev_driver: Device driver to bind to the newly created mdev
 * @types: Array of supported mdev types
 * @nr_types: Number of entries in @types
 *
 * Registers the @parent stucture as a parent for mdev types and thus mdev
 * devices.  The caller needs to hold a reference on @dev that must not be
 * released until after the call to mdev_unregister_parent().
 *
 * Returns a negative value on error, otherwise 0.
 */
int mdev_register_parent(struct mdev_parent *parent, struct device *dev,
		struct mdev_driver *mdev_driver, struct mdev_type **types,
		unsigned int nr_types)
{
	char *env_string = "MDEV_STATE=registered";
	char *envp[] = { env_string, NULL };
	int ret;

	memset(parent, 0, sizeof(*parent));
	init_rwsem(&parent->unreg_sem);
	parent->dev = dev;
	parent->mdev_driver = mdev_driver;
	parent->types = types;
	parent->nr_types = nr_types;
	atomic_set(&parent->available_instances, mdev_driver->max_instances);

	if (!mdev_bus_compat_class) {
		mdev_bus_compat_class = class_compat_register("mdev_bus");
		if (!mdev_bus_compat_class)
			return -ENOMEM;
	}

	ret = parent_create_sysfs_files(parent);
	if (ret)
		return ret;

	ret = class_compat_create_link(mdev_bus_compat_class, dev, NULL);
	if (ret)
		dev_warn(dev, "Failed to create compatibility class link\n");

	dev_info(dev, "MDEV: Registered\n");
	kobject_uevent_env(&dev->kobj, KOBJ_CHANGE, envp);
	return 0;
}
EXPORT_SYMBOL(mdev_register_parent);

/*
 * mdev_unregister_parent : Unregister a parent device
 * @parent: parent structure to unregister
 */
void mdev_unregister_parent(struct mdev_parent *parent)
{
	char *env_string = "MDEV_STATE=unregistered";
	char *envp[] = { env_string, NULL };

	dev_info(parent->dev, "MDEV: Unregistering\n");

	down_write(&parent->unreg_sem);
	class_compat_remove_link(mdev_bus_compat_class, parent->dev, NULL);
	device_for_each_child(parent->dev, NULL, mdev_device_remove_cb);
	parent_remove_sysfs_files(parent);
	up_write(&parent->unreg_sem);

	kobject_uevent_env(&parent->dev->kobj, KOBJ_CHANGE, envp);
}
EXPORT_SYMBOL(mdev_unregister_parent);

static void mdev_device_release(struct device *dev)
{
	struct mdev_device *mdev = to_mdev_device(dev);
	struct mdev_parent *parent = mdev->type->parent;

	mutex_lock(&mdev_list_lock);
	list_del(&mdev->next);
	if (!parent->mdev_driver->get_available)
		atomic_inc(&parent->available_instances);
	mutex_unlock(&mdev_list_lock);

	/* Pairs with the get in mdev_device_create() */
	kobject_put(&mdev->type->kobj);

	dev_dbg(&mdev->dev, "MDEV: destroying\n");
	kfree(mdev);
}

int mdev_device_create(struct mdev_type *type, const guid_t *uuid)
{
	int ret;
	struct mdev_device *mdev, *tmp;
	struct mdev_parent *parent = type->parent;
	struct mdev_driver *drv = parent->mdev_driver;

	mutex_lock(&mdev_list_lock);

	/* Check for duplicate */
	list_for_each_entry(tmp, &mdev_list, next) {
		if (guid_equal(&tmp->uuid, uuid)) {
			mutex_unlock(&mdev_list_lock);
			return -EEXIST;
		}
	}

	if (!drv->get_available) {
		/*
		 * Note: that non-atomic read and dec is fine here because
		 * all modifications are under mdev_list_lock.
		 */
		if (!atomic_read(&parent->available_instances)) {
			mutex_unlock(&mdev_list_lock);
			return -EUSERS;
		}
		atomic_dec(&parent->available_instances);
	}

	mdev = kzalloc(sizeof(*mdev), GFP_KERNEL);
	if (!mdev) {
		mutex_unlock(&mdev_list_lock);
		return -ENOMEM;
	}

	device_initialize(&mdev->dev);
	mdev->dev.parent  = parent->dev;
	mdev->dev.bus = &mdev_bus_type;
	mdev->dev.release = mdev_device_release;
	mdev->dev.groups = mdev_device_groups;
	mdev->type = type;
	/* Pairs with the put in mdev_device_release() */
	kobject_get(&type->kobj);

	guid_copy(&mdev->uuid, uuid);
	list_add(&mdev->next, &mdev_list);
	mutex_unlock(&mdev_list_lock);

	ret = dev_set_name(&mdev->dev, "%pUl", uuid);
	if (ret)
		goto out_put_device;

	/* Check if parent unregistration has started */
	if (!down_read_trylock(&parent->unreg_sem)) {
		ret = -ENODEV;
		goto out_put_device;
	}

	ret = device_add(&mdev->dev);
	if (ret)
		goto out_unlock;

	ret = device_driver_attach(&drv->driver, &mdev->dev);
	if (ret)
		goto out_del;

	ret = mdev_create_sysfs_files(mdev);
	if (ret)
		goto out_del;

	mdev->active = true;
	dev_dbg(&mdev->dev, "MDEV: created\n");
	up_read(&parent->unreg_sem);

	return 0;

out_del:
	device_del(&mdev->dev);
out_unlock:
	up_read(&parent->unreg_sem);
out_put_device:
	put_device(&mdev->dev);
	return ret;
}

int mdev_device_remove(struct mdev_device *mdev)
{
	struct mdev_device *tmp;
	struct mdev_parent *parent = mdev->type->parent;

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

	/* Check if parent unregistration has started */
	if (!down_read_trylock(&parent->unreg_sem))
		return -ENODEV;

	mdev_device_remove_common(mdev);
	up_read(&parent->unreg_sem);
	return 0;
}

static int __init mdev_init(void)
{
	return bus_register(&mdev_bus_type);
}

static void __exit mdev_exit(void)
{
	if (mdev_bus_compat_class)
		class_compat_unregister(mdev_bus_compat_class);
	bus_unregister(&mdev_bus_type);
}

subsys_initcall(mdev_init)
module_exit(mdev_exit)

MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
