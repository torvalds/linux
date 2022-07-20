// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 - Virtual Open Systems
 * Author: Antonios Motakis <a.motakis@virtualopensystems.com>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vfio.h>
#include <linux/platform_device.h>

#include "vfio_platform_private.h"

#define DRIVER_VERSION  "0.10"
#define DRIVER_AUTHOR   "Antonios Motakis <a.motakis@virtualopensystems.com>"
#define DRIVER_DESC     "VFIO for platform devices - User Level meta-driver"

static bool reset_required = true;
module_param(reset_required, bool, 0444);
MODULE_PARM_DESC(reset_required, "override reset requirement (default: 1)");

/* probing devices from the linux platform bus */

static struct resource *get_platform_resource(struct vfio_platform_device *vdev,
					      int num)
{
	struct platform_device *dev = (struct platform_device *) vdev->opaque;

	return platform_get_mem_or_io(dev, num);
}

static int get_platform_irq(struct vfio_platform_device *vdev, int i)
{
	struct platform_device *pdev = (struct platform_device *) vdev->opaque;

	return platform_get_irq_optional(pdev, i);
}

static int vfio_platform_probe(struct platform_device *pdev)
{
	struct vfio_platform_device *vdev;
	int ret;

	vdev = kzalloc(sizeof(*vdev), GFP_KERNEL);
	if (!vdev)
		return -ENOMEM;

	vdev->opaque = (void *) pdev;
	vdev->name = pdev->name;
	vdev->flags = VFIO_DEVICE_FLAGS_PLATFORM;
	vdev->get_resource = get_platform_resource;
	vdev->get_irq = get_platform_irq;
	vdev->reset_required = reset_required;

	ret = vfio_platform_probe_common(vdev, &pdev->dev);
	if (ret) {
		kfree(vdev);
		return ret;
	}
	dev_set_drvdata(&pdev->dev, vdev);
	return 0;
}

static int vfio_platform_remove(struct platform_device *pdev)
{
	struct vfio_platform_device *vdev = dev_get_drvdata(&pdev->dev);

	vfio_platform_remove_common(vdev);
	kfree(vdev);
	return 0;
}

static struct platform_driver vfio_platform_driver = {
	.probe		= vfio_platform_probe,
	.remove		= vfio_platform_remove,
	.driver	= {
		.name	= "vfio-platform",
	},
	.driver_managed_dma = true,
};

module_platform_driver(vfio_platform_driver);

MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
