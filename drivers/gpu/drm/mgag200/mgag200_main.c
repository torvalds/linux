// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2010 Matt Turner.
 * Copyright 2012 Red Hat
 *
 * Authors: Matthew Garrett
 *          Matt Turner
 *          Dave Airlie
 */

#include <linux/pci.h>

#include "mgag200_drv.h"

int mgag200_driver_load(struct drm_device *dev, unsigned long flags)
{
	struct mga_device *mdev;
	int ret, option;

	mdev = devm_kzalloc(dev->dev, sizeof(struct mga_device), GFP_KERNEL);
	if (mdev == NULL)
		return -ENOMEM;
	dev->dev_private = (void *)mdev;
	mdev->dev = dev;

	mdev->flags = mgag200_flags_from_driver_data(flags);
	mdev->type = mgag200_type_from_driver_data(flags);

	pci_read_config_dword(dev->pdev, PCI_MGA_OPTION, &option);
	mdev->has_sdram = !(option & (1 << 14));

	/* BAR 0 is the framebuffer, BAR 1 contains registers */
	mdev->rmmio_base = pci_resource_start(dev->pdev, 1);
	mdev->rmmio_size = pci_resource_len(dev->pdev, 1);

	if (!devm_request_mem_region(dev->dev, mdev->rmmio_base,
				     mdev->rmmio_size, "mgadrmfb_mmio")) {
		drm_err(dev, "can't reserve mmio registers\n");
		return -ENOMEM;
	}

	mdev->rmmio = pcim_iomap(dev->pdev, 1, 0);
	if (mdev->rmmio == NULL)
		return -ENOMEM;

	/* stash G200 SE model number for later use */
	if (IS_G200_SE(mdev)) {
		mdev->unique_rev_id = RREG32(0x1e24);
		drm_dbg(dev, "G200 SE unique revision id is 0x%x\n",
			mdev->unique_rev_id);
	}

	ret = mgag200_mm_init(mdev);
	if (ret)
		goto err_mm;

	ret = mgag200_modeset_init(mdev);
	if (ret) {
		drm_err(dev, "Fatal error during modeset init: %d\n", ret);
		goto err_mm;
	}

	return 0;

err_mm:
	dev->dev_private = NULL;
	return ret;
}

void mgag200_driver_unload(struct drm_device *dev)
{
	struct mga_device *mdev = to_mga_device(dev);

	if (mdev == NULL)
		return;
	dev->dev_private = NULL;
}
