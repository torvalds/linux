// SPDX-License-Identifier: GPL-2.0-only

#include <linux/pci.h>

#include <drm/drm_drv.h>

#include "mgag200_drv.h"

/*
 * DRM device
 */

struct mga_device *mgag200_g200eh3_device_create(struct pci_dev *pdev,
						 const struct drm_driver *drv,
						 enum mga_type type, unsigned long flags)
{
	struct mga_device *mdev;
	struct drm_device *dev;
	int ret;

	mdev = devm_drm_dev_alloc(&pdev->dev, drv, struct mga_device, base);
	if (IS_ERR(mdev))
		return mdev;
	dev = &mdev->base;

	pci_set_drvdata(pdev, dev);

	mdev->flags = flags;
	mdev->type = type;

	ret = mgag200_regs_init(mdev);
	if (ret)
		return ERR_PTR(ret);

	ret = mgag200_mm_init(mdev);
	if (ret)
		return ERR_PTR(ret);

	ret = mgag200_modeset_init(mdev);
	if (ret)
		return ERR_PTR(ret);

	return mdev;
}
