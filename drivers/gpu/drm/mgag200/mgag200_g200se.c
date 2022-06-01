// SPDX-License-Identifier: GPL-2.0-only

#include <linux/pci.h>

#include <drm/drm_drv.h>

#include "mgag200_drv.h"

/*
 * DRM device
 */

static void mgag200_g200se_init_unique_id(struct mgag200_g200se_device *g200se)
{
	struct mga_device *mdev = &g200se->base;
	struct drm_device *dev = &mdev->base;

	/* stash G200 SE model number for later use */
	g200se->unique_rev_id = RREG32(0x1e24);

	drm_dbg(dev, "G200 SE unique revision id is 0x%x\n", g200se->unique_rev_id);
}

struct mga_device *mgag200_g200se_device_create(struct pci_dev *pdev, const struct drm_driver *drv,
						enum mga_type type, unsigned long flags)
{
	struct mgag200_g200se_device *g200se;
	struct mga_device *mdev;
	struct drm_device *dev;
	int ret;

	g200se = devm_drm_dev_alloc(&pdev->dev, drv, struct mgag200_g200se_device, base.base);
	if (IS_ERR(g200se))
		return ERR_CAST(g200se);
	mdev = &g200se->base;
	dev = &mdev->base;

	pci_set_drvdata(pdev, dev);

	mdev->flags = flags;
	mdev->type = type;

	ret = mgag200_regs_init(mdev);
	if (ret)
		return ERR_PTR(ret);

	mgag200_g200se_init_unique_id(g200se);

	ret = mgag200_mm_init(mdev);
	if (ret)
		return ERR_PTR(ret);

	ret = mgag200_modeset_init(mdev);
	if (ret)
		return ERR_PTR(ret);

	return mdev;
}
