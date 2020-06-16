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

static int mga_probe_vram(struct mga_device *mdev, void __iomem *mem)
{
	int offset;
	int orig;
	int test1, test2;
	int orig1, orig2;
	unsigned int vram_size;

	/* Probe */
	orig = ioread16(mem);
	iowrite16(0, mem);

	vram_size = mdev->mc.vram_window;

	if ((mdev->type == G200_EW3) && (vram_size >= 0x1000000)) {
		vram_size = vram_size - 0x400000;
	}

	for (offset = 0x100000; offset < vram_size; offset += 0x4000) {
		orig1 = ioread8(mem + offset);
		orig2 = ioread8(mem + offset + 0x100);

		iowrite16(0xaa55, mem + offset);
		iowrite16(0xaa55, mem + offset + 0x100);

		test1 = ioread16(mem + offset);
		test2 = ioread16(mem);

		iowrite16(orig1, mem + offset);
		iowrite16(orig2, mem + offset + 0x100);

		if (test1 != 0xaa55) {
			break;
		}

		if (test2) {
			break;
		}
	}

	iowrite16(orig, mem);
	return offset - 65536;
}

/* Map the framebuffer from the card and configure the core */
static int mga_vram_init(struct mga_device *mdev)
{
	struct drm_device *dev = mdev->dev;
	void __iomem *mem;

	/* BAR 0 is VRAM */
	mdev->mc.vram_base = pci_resource_start(dev->pdev, 0);
	mdev->mc.vram_window = pci_resource_len(dev->pdev, 0);

	if (!devm_request_mem_region(dev->dev, mdev->mc.vram_base,
				     mdev->mc.vram_window, "mgadrmfb_vram")) {
		DRM_ERROR("can't reserve VRAM\n");
		return -ENXIO;
	}

	mem = pci_iomap(dev->pdev, 0, 0);
	if (!mem)
		return -ENOMEM;

	mdev->mc.vram_size = mga_probe_vram(mdev, mem);

	pci_iounmap(dev->pdev, mem);

	return 0;
}

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

	ret = mga_vram_init(mdev);
	if (ret)
		return ret;

	ret = mgag200_mm_init(mdev);
	if (ret)
		goto err_mm;

	ret = mgag200_modeset_init(mdev);
	if (ret) {
		drm_err(dev, "Fatal error during modeset init: %d\n", ret);
		goto err_mgag200_mm_fini;
	}

	ret = mgag200_cursor_init(mdev);
	if (ret)
		drm_err(dev, "Could not initialize cursors. Not doing hardware cursors.\n");

	return 0;

err_mgag200_mm_fini:
	mgag200_mm_fini(mdev);
err_mm:
	dev->dev_private = NULL;
	return ret;
}

void mgag200_driver_unload(struct drm_device *dev)
{
	struct mga_device *mdev = to_mga_device(dev);

	if (mdev == NULL)
		return;
	mgag200_cursor_fini(mdev);
	mgag200_mm_fini(mdev);
	dev->dev_private = NULL;
}
