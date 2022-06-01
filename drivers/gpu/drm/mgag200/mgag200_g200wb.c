// SPDX-License-Identifier: GPL-2.0-only

#include <linux/pci.h>

#include <drm/drm_drv.h>

#include "mgag200_drv.h"

/*
 * DRM device
 */

struct mga_device *mgag200_g200wb_device_create(struct pci_dev *pdev, const struct drm_driver *drv,
						enum mga_type type, unsigned long flags)
{
	struct mga_device *mdev;
	struct drm_device *dev;
	resource_size_t vram_available;
	int ret;

	mdev = devm_drm_dev_alloc(&pdev->dev, drv, struct mga_device, base);
	if (IS_ERR(mdev))
		return mdev;
	dev = &mdev->base;

	pci_set_drvdata(pdev, dev);

	ret = mgag200_init_pci_options(pdev, 0x41049120, 0x0000b000);
	if (ret)
		return ERR_PTR(ret);

	mdev->flags = flags;
	mdev->type = type;

	ret = mgag200_regs_init(mdev);
	if (ret)
		return ERR_PTR(ret);

	ret = mgag200_mm_init(mdev);
	if (ret)
		return ERR_PTR(ret);

	vram_available = mgag200_device_probe_vram(mdev);

	ret = mgag200_modeset_init(mdev, vram_available);
	if (ret)
		return ERR_PTR(ret);

	return mdev;
}
