// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/*
 * Copyright 2013-2016 Freescale Semiconductor Inc.
 * Copyright 2016-2017,2019-2020 NXP
 */

#include <linux/device.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/vfio.h>
#include <linux/fsl/mc.h>

#include "vfio_fsl_mc_private.h"

static int vfio_fsl_mc_open(void *device_data)
{
	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	return 0;
}

static void vfio_fsl_mc_release(void *device_data)
{
	module_put(THIS_MODULE);
}

static long vfio_fsl_mc_ioctl(void *device_data, unsigned int cmd,
			      unsigned long arg)
{
	switch (cmd) {
	case VFIO_DEVICE_GET_INFO:
	{
		return -ENOTTY;
	}
	case VFIO_DEVICE_GET_REGION_INFO:
	{
		return -ENOTTY;
	}
	case VFIO_DEVICE_GET_IRQ_INFO:
	{
		return -ENOTTY;
	}
	case VFIO_DEVICE_SET_IRQS:
	{
		return -ENOTTY;
	}
	case VFIO_DEVICE_RESET:
	{
		return -ENOTTY;
	}
	default:
		return -ENOTTY;
	}
}

static ssize_t vfio_fsl_mc_read(void *device_data, char __user *buf,
				size_t count, loff_t *ppos)
{
	return -EINVAL;
}

static ssize_t vfio_fsl_mc_write(void *device_data, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	return -EINVAL;
}

static int vfio_fsl_mc_mmap(void *device_data, struct vm_area_struct *vma)
{
	return -EINVAL;
}

static const struct vfio_device_ops vfio_fsl_mc_ops = {
	.name		= "vfio-fsl-mc",
	.open		= vfio_fsl_mc_open,
	.release	= vfio_fsl_mc_release,
	.ioctl		= vfio_fsl_mc_ioctl,
	.read		= vfio_fsl_mc_read,
	.write		= vfio_fsl_mc_write,
	.mmap		= vfio_fsl_mc_mmap,
};

static int vfio_fsl_mc_probe(struct fsl_mc_device *mc_dev)
{
	struct iommu_group *group;
	struct vfio_fsl_mc_device *vdev;
	struct device *dev = &mc_dev->dev;
	int ret;

	group = vfio_iommu_group_get(dev);
	if (!group) {
		dev_err(dev, "VFIO_FSL_MC: No IOMMU group\n");
		return -EINVAL;
	}

	vdev = devm_kzalloc(dev, sizeof(*vdev), GFP_KERNEL);
	if (!vdev) {
		ret = -ENOMEM;
		goto out_group_put;
	}

	vdev->mc_dev = mc_dev;

	ret = vfio_add_group_dev(dev, &vfio_fsl_mc_ops, vdev);
	if (ret) {
		dev_err(dev, "VFIO_FSL_MC: Failed to add to vfio group\n");
		goto out_group_put;
	}
	return 0;

out_group_put:
	vfio_iommu_group_put(group, dev);
	return ret;
}

static int vfio_fsl_mc_remove(struct fsl_mc_device *mc_dev)
{
	struct vfio_fsl_mc_device *vdev;
	struct device *dev = &mc_dev->dev;

	vdev = vfio_del_group_dev(dev);
	if (!vdev)
		return -EINVAL;

	vfio_iommu_group_put(mc_dev->dev.iommu_group, dev);

	return 0;
}

static struct fsl_mc_driver vfio_fsl_mc_driver = {
	.probe		= vfio_fsl_mc_probe,
	.remove		= vfio_fsl_mc_remove,
	.driver	= {
		.name	= "vfio-fsl-mc",
		.owner	= THIS_MODULE,
	},
};

static int __init vfio_fsl_mc_driver_init(void)
{
	return fsl_mc_driver_register(&vfio_fsl_mc_driver);
}

static void __exit vfio_fsl_mc_driver_exit(void)
{
	fsl_mc_driver_unregister(&vfio_fsl_mc_driver);
}

module_init(vfio_fsl_mc_driver_init);
module_exit(vfio_fsl_mc_driver_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("VFIO for FSL-MC devices - User Level meta-driver");
