/*
 * Copyright (C) 2013 - Virtual Open Systems
 * Author: Antonios Motakis <a.motakis@virtualopensystems.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/vfio.h>

#include "vfio_platform_private.h"

static void vfio_platform_release(void *device_data)
{
	module_put(THIS_MODULE);
}

static int vfio_platform_open(void *device_data)
{
	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	return 0;
}

static long vfio_platform_ioctl(void *device_data,
				unsigned int cmd, unsigned long arg)
{
	struct vfio_platform_device *vdev = device_data;
	unsigned long minsz;

	if (cmd == VFIO_DEVICE_GET_INFO) {
		struct vfio_device_info info;

		minsz = offsetofend(struct vfio_device_info, num_irqs);

		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		if (info.argsz < minsz)
			return -EINVAL;

		info.flags = vdev->flags;
		info.num_regions = 0;
		info.num_irqs = 0;

		return copy_to_user((void __user *)arg, &info, minsz);

	} else if (cmd == VFIO_DEVICE_GET_REGION_INFO)
		return -EINVAL;

	else if (cmd == VFIO_DEVICE_GET_IRQ_INFO)
		return -EINVAL;

	else if (cmd == VFIO_DEVICE_SET_IRQS)
		return -EINVAL;

	else if (cmd == VFIO_DEVICE_RESET)
		return -EINVAL;

	return -ENOTTY;
}

static ssize_t vfio_platform_read(void *device_data, char __user *buf,
				  size_t count, loff_t *ppos)
{
	return -EINVAL;
}

static ssize_t vfio_platform_write(void *device_data, const char __user *buf,
				   size_t count, loff_t *ppos)
{
	return -EINVAL;
}

static int vfio_platform_mmap(void *device_data, struct vm_area_struct *vma)
{
	return -EINVAL;
}

static const struct vfio_device_ops vfio_platform_ops = {
	.name		= "vfio-platform",
	.open		= vfio_platform_open,
	.release	= vfio_platform_release,
	.ioctl		= vfio_platform_ioctl,
	.read		= vfio_platform_read,
	.write		= vfio_platform_write,
	.mmap		= vfio_platform_mmap,
};

int vfio_platform_probe_common(struct vfio_platform_device *vdev,
			       struct device *dev)
{
	struct iommu_group *group;
	int ret;

	if (!vdev)
		return -EINVAL;

	group = iommu_group_get(dev);
	if (!group) {
		pr_err("VFIO: No IOMMU group for device %s\n", vdev->name);
		return -EINVAL;
	}

	ret = vfio_add_group_dev(dev, &vfio_platform_ops, vdev);
	if (ret) {
		iommu_group_put(group);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(vfio_platform_probe_common);

struct vfio_platform_device *vfio_platform_remove_common(struct device *dev)
{
	struct vfio_platform_device *vdev;

	vdev = vfio_del_group_dev(dev);
	if (vdev)
		iommu_group_put(dev->iommu_group);

	return vdev;
}
EXPORT_SYMBOL_GPL(vfio_platform_remove_common);
