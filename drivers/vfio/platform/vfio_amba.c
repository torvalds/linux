// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 - Virtual Open Systems
 * Author: Antonios Motakis <a.motakis@virtualopensystems.com>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vfio.h>
#include <linux/pm_runtime.h>
#include <linux/amba/bus.h>

#include "vfio_platform_private.h"

#define DRIVER_VERSION  "0.10"
#define DRIVER_AUTHOR   "Antonios Motakis <a.motakis@virtualopensystems.com>"
#define DRIVER_DESC     "VFIO for AMBA devices - User Level meta-driver"

/* probing devices from the AMBA bus */

static struct resource *get_amba_resource(struct vfio_platform_device *vdev,
					  int i)
{
	struct amba_device *adev = (struct amba_device *) vdev->opaque;

	if (i == 0)
		return &adev->res;

	return NULL;
}

static int get_amba_irq(struct vfio_platform_device *vdev, int i)
{
	struct amba_device *adev = (struct amba_device *) vdev->opaque;
	int ret = 0;

	if (i < AMBA_NR_IRQS)
		ret = adev->irq[i];

	/* zero is an unset IRQ for AMBA devices */
	return ret ? ret : -ENXIO;
}

static int vfio_amba_init_dev(struct vfio_device *core_vdev)
{
	struct vfio_platform_device *vdev =
		container_of(core_vdev, struct vfio_platform_device, vdev);
	struct amba_device *adev = to_amba_device(core_vdev->dev);
	int ret;

	vdev->name = kasprintf(GFP_KERNEL, "vfio-amba-%08x", adev->periphid);
	if (!vdev->name)
		return -ENOMEM;

	vdev->opaque = (void *) adev;
	vdev->flags = VFIO_DEVICE_FLAGS_AMBA;
	vdev->get_resource = get_amba_resource;
	vdev->get_irq = get_amba_irq;
	vdev->reset_required = false;

	ret = vfio_platform_init_common(vdev);
	if (ret)
		kfree(vdev->name);
	return ret;
}

static const struct vfio_device_ops vfio_amba_ops;
static int vfio_amba_probe(struct amba_device *adev, const struct amba_id *id)
{
	struct vfio_platform_device *vdev;
	int ret;

	vdev = vfio_alloc_device(vfio_platform_device, vdev, &adev->dev,
				 &vfio_amba_ops);
	if (IS_ERR(vdev))
		return PTR_ERR(vdev);

	ret = vfio_register_group_dev(&vdev->vdev);
	if (ret)
		goto out_put_vdev;

	pm_runtime_enable(&adev->dev);
	dev_set_drvdata(&adev->dev, vdev);
	return 0;

out_put_vdev:
	vfio_put_device(&vdev->vdev);
	return ret;
}

static void vfio_amba_release_dev(struct vfio_device *core_vdev)
{
	struct vfio_platform_device *vdev =
		container_of(core_vdev, struct vfio_platform_device, vdev);

	vfio_platform_release_common(vdev);
	kfree(vdev->name);
}

static void vfio_amba_remove(struct amba_device *adev)
{
	struct vfio_platform_device *vdev = dev_get_drvdata(&adev->dev);

	vfio_unregister_group_dev(&vdev->vdev);
	pm_runtime_disable(vdev->device);
	vfio_put_device(&vdev->vdev);
}

static const struct vfio_device_ops vfio_amba_ops = {
	.name		= "vfio-amba",
	.init		= vfio_amba_init_dev,
	.release	= vfio_amba_release_dev,
	.open_device	= vfio_platform_open_device,
	.close_device	= vfio_platform_close_device,
	.ioctl		= vfio_platform_ioctl,
	.read		= vfio_platform_read,
	.write		= vfio_platform_write,
	.mmap		= vfio_platform_mmap,
	.bind_iommufd	= vfio_iommufd_physical_bind,
	.unbind_iommufd	= vfio_iommufd_physical_unbind,
	.attach_ioas	= vfio_iommufd_physical_attach_ioas,
	.detach_ioas	= vfio_iommufd_physical_detach_ioas,
};

static const struct amba_id vfio_amba_ids[] = {
	{ 0, 0 },
};

MODULE_DEVICE_TABLE(amba, vfio_amba_ids);

static struct amba_driver vfio_amba_driver = {
	.probe = vfio_amba_probe,
	.remove = vfio_amba_remove,
	.id_table = vfio_amba_ids,
	.drv = {
		.name = "vfio-amba",
		.owner = THIS_MODULE,
	},
	.driver_managed_dma = true,
};

module_amba_driver(vfio_amba_driver);

MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
