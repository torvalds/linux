// SPDX-License-Identifier: GPL-2.0-only
/*
 * VFIO based driver for Mediated device
 *
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
 *     Author: Neo Jia <cjia@nvidia.com>
 *             Kirti Wankhede <kwankhede@nvidia.com>
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

static int vfio_mdev_open(struct vfio_device *core_vdev)
{
	struct mdev_device *mdev = to_mdev_device(core_vdev->dev);
	struct mdev_parent *parent = mdev->type->parent;

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

static void vfio_mdev_release(struct vfio_device *core_vdev)
{
	struct mdev_device *mdev = to_mdev_device(core_vdev->dev);
	struct mdev_parent *parent = mdev->type->parent;

	if (likely(parent->ops->release))
		parent->ops->release(mdev);

	module_put(THIS_MODULE);
}

static long vfio_mdev_unlocked_ioctl(struct vfio_device *core_vdev,
				     unsigned int cmd, unsigned long arg)
{
	struct mdev_device *mdev = to_mdev_device(core_vdev->dev);
	struct mdev_parent *parent = mdev->type->parent;

	if (unlikely(!parent->ops->ioctl))
		return -EINVAL;

	return parent->ops->ioctl(mdev, cmd, arg);
}

static ssize_t vfio_mdev_read(struct vfio_device *core_vdev, char __user *buf,
			      size_t count, loff_t *ppos)
{
	struct mdev_device *mdev = to_mdev_device(core_vdev->dev);
	struct mdev_parent *parent = mdev->type->parent;

	if (unlikely(!parent->ops->read))
		return -EINVAL;

	return parent->ops->read(mdev, buf, count, ppos);
}

static ssize_t vfio_mdev_write(struct vfio_device *core_vdev,
			       const char __user *buf, size_t count,
			       loff_t *ppos)
{
	struct mdev_device *mdev = to_mdev_device(core_vdev->dev);
	struct mdev_parent *parent = mdev->type->parent;

	if (unlikely(!parent->ops->write))
		return -EINVAL;

	return parent->ops->write(mdev, buf, count, ppos);
}

static int vfio_mdev_mmap(struct vfio_device *core_vdev,
			  struct vm_area_struct *vma)
{
	struct mdev_device *mdev = to_mdev_device(core_vdev->dev);
	struct mdev_parent *parent = mdev->type->parent;

	if (unlikely(!parent->ops->mmap))
		return -EINVAL;

	return parent->ops->mmap(mdev, vma);
}

static void vfio_mdev_request(struct vfio_device *core_vdev, unsigned int count)
{
	struct mdev_device *mdev = to_mdev_device(core_vdev->dev);
	struct mdev_parent *parent = mdev->type->parent;

	if (parent->ops->request)
		parent->ops->request(mdev, count);
	else if (count == 0)
		dev_notice(mdev_dev(mdev),
			   "No mdev vendor driver request callback support, blocked until released by user\n");
}

static const struct vfio_device_ops vfio_mdev_dev_ops = {
	.name		= "vfio-mdev",
	.open		= vfio_mdev_open,
	.release	= vfio_mdev_release,
	.ioctl		= vfio_mdev_unlocked_ioctl,
	.read		= vfio_mdev_read,
	.write		= vfio_mdev_write,
	.mmap		= vfio_mdev_mmap,
	.request	= vfio_mdev_request,
};

static int vfio_mdev_probe(struct mdev_device *mdev)
{
	struct vfio_device *vdev;
	int ret;

	vdev = kzalloc(sizeof(*vdev), GFP_KERNEL);
	if (!vdev)
		return -ENOMEM;

	vfio_init_group_dev(vdev, &mdev->dev, &vfio_mdev_dev_ops);
	ret = vfio_register_group_dev(vdev);
	if (ret) {
		kfree(vdev);
		return ret;
	}
	dev_set_drvdata(&mdev->dev, vdev);
	return 0;
}

static void vfio_mdev_remove(struct mdev_device *mdev)
{
	struct vfio_device *vdev = dev_get_drvdata(&mdev->dev);

	vfio_unregister_group_dev(vdev);
	kfree(vdev);
}

static struct mdev_driver vfio_mdev_driver = {
	.driver = {
		.name = "vfio_mdev",
		.owner = THIS_MODULE,
		.mod_name = KBUILD_MODNAME,
	},
	.probe	= vfio_mdev_probe,
	.remove	= vfio_mdev_remove,
};

static int __init vfio_mdev_init(void)
{
	return mdev_register_driver(&vfio_mdev_driver);
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
