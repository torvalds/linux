/*
 * VFIO based driver for Mediated device
 *
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
 *     Author: Neo Jia <cjia@nvidia.com>
 *             Kirti Wankhede <kwankhede@nvidia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vfio.h>
#include <linux/mdev.h>

#include "mdev_private.h"

#define DRIVER_VERSION  "0.1"
#define DRIVER_AUTHOR   "NVIDIA Corporation"
#define DRIVER_DESC     "VFIO based driver for Mediated device"

static int vfio_mdev_open(void *device_data)
{
	struct mdev_device *mdev = device_data;
	struct mdev_parent *parent = mdev->parent;
	int ret;

	if (unlikely(!parent->ops->open))
		return -EINVAL;

	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	ret = parent->ops->open(mdev);
	if (ret)
		module_put(THIS_MODULE);

	return ret;
}

static void vfio_mdev_release(void *device_data)
{
	struct mdev_device *mdev = device_data;
	struct mdev_parent *parent = mdev->parent;

	if (likely(parent->ops->release))
		parent->ops->release(mdev);

	module_put(THIS_MODULE);
}

static long vfio_mdev_unlocked_ioctl(void *device_data,
				     unsigned int cmd, unsigned long arg)
{
	struct mdev_device *mdev = device_data;
	struct mdev_parent *parent = mdev->parent;

	if (unlikely(!parent->ops->ioctl))
		return -EINVAL;

	return parent->ops->ioctl(mdev, cmd, arg);
}

static ssize_t vfio_mdev_read(void *device_data, char __user *buf,
			      size_t count, loff_t *ppos)
{
	struct mdev_device *mdev = device_data;
	struct mdev_parent *parent = mdev->parent;

	if (unlikely(!parent->ops->read))
		return -EINVAL;

	return parent->ops->read(mdev, buf, count, ppos);
}

static ssize_t vfio_mdev_write(void *device_data, const char __user *buf,
			       size_t count, loff_t *ppos)
{
	struct mdev_device *mdev = device_data;
	struct mdev_parent *parent = mdev->parent;

	if (unlikely(!parent->ops->write))
		return -EINVAL;

	return parent->ops->write(mdev, buf, count, ppos);
}

static int vfio_mdev_mmap(void *device_data, struct vm_area_struct *vma)
{
	struct mdev_device *mdev = device_data;
	struct mdev_parent *parent = mdev->parent;

	if (unlikely(!parent->ops->mmap))
		return -EINVAL;

	return parent->ops->mmap(mdev, vma);
}

static const struct vfio_device_ops vfio_mdev_dev_ops = {
	.name		= "vfio-mdev",
	.open		= vfio_mdev_open,
	.release	= vfio_mdev_release,
	.ioctl		= vfio_mdev_unlocked_ioctl,
	.read		= vfio_mdev_read,
	.write		= vfio_mdev_write,
	.mmap		= vfio_mdev_mmap,
};

static int vfio_mdev_probe(struct device *dev)
{
	struct mdev_device *mdev = to_mdev_device(dev);

	return vfio_add_group_dev(dev, &vfio_mdev_dev_ops, mdev);
}

static void vfio_mdev_remove(struct device *dev)
{
	vfio_del_group_dev(dev);
}

static struct mdev_driver vfio_mdev_driver = {
	.name	= "vfio_mdev",
	.probe	= vfio_mdev_probe,
	.remove	= vfio_mdev_remove,
};

static int __init vfio_mdev_init(void)
{
	return mdev_register_driver(&vfio_mdev_driver, THIS_MODULE);
}

static void __exit vfio_mdev_exit(void)
{
	mdev_unregister_driver(&vfio_mdev_driver);
}

module_init(vfio_mdev_init)
module_exit(vfio_mdev_exit)

MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
