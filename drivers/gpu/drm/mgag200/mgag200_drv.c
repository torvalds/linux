// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2012 Red Hat
 *
 * Authors: Matthew Garrett
 *          Dave Airlie
 */

#include <linux/console.h>
#include <linux/module.h>
#include <linux/pci.h>

#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_pciids.h>

#include "mgag200_drv.h"

int mgag200_modeset = -1;
MODULE_PARM_DESC(modeset, "Disable/Enable modesetting");
module_param_named(modeset, mgag200_modeset, int, 0400);

/*
 * DRM driver
 */

DEFINE_DRM_GEM_FOPS(mgag200_driver_fops);

static struct drm_driver mgag200_driver = {
	.driver_features = DRIVER_ATOMIC | DRIVER_GEM | DRIVER_MODESET,
	.fops = &mgag200_driver_fops,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
	DRM_GEM_SHMEM_DRIVER_OPS,
};

/*
 * DRM device
 */

static int mgag200_device_init(struct mga_device *mdev, unsigned long flags)
{
	struct drm_device *dev = &mdev->base;
	int ret, option;

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
		return ret;

	ret = mgag200_modeset_init(mdev);
	if (ret) {
		drm_err(dev, "Fatal error during modeset init: %d\n", ret);
		return ret;
	}

	return 0;
}

static struct mga_device *
mgag200_device_create(struct pci_dev *pdev, unsigned long flags)
{
	struct drm_device *dev;
	struct mga_device *mdev;
	int ret;

	mdev = devm_drm_dev_alloc(&pdev->dev, &mgag200_driver,
				  struct mga_device, base);
	if (IS_ERR(mdev))
		return mdev;
	dev = &mdev->base;

	dev->pdev = pdev;
	pci_set_drvdata(pdev, dev);

	ret = mgag200_device_init(mdev, flags);
	if (ret)
		return ERR_PTR(ret);

	return mdev;
}

/*
 * PCI driver
 */

static const struct pci_device_id mgag200_pciidlist[] = {
	{ PCI_VENDOR_ID_MATROX, 0x522, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		G200_SE_A | MGAG200_FLAG_HW_BUG_NO_STARTADD},
	{ PCI_VENDOR_ID_MATROX, 0x524, PCI_ANY_ID, PCI_ANY_ID, 0, 0, G200_SE_B },
	{ PCI_VENDOR_ID_MATROX, 0x530, PCI_ANY_ID, PCI_ANY_ID, 0, 0, G200_EV },
	{ PCI_VENDOR_ID_MATROX, 0x532, PCI_ANY_ID, PCI_ANY_ID, 0, 0, G200_WB },
	{ PCI_VENDOR_ID_MATROX, 0x533, PCI_ANY_ID, PCI_ANY_ID, 0, 0, G200_EH },
	{ PCI_VENDOR_ID_MATROX, 0x534, PCI_ANY_ID, PCI_ANY_ID, 0, 0, G200_ER },
	{ PCI_VENDOR_ID_MATROX, 0x536, PCI_ANY_ID, PCI_ANY_ID, 0, 0, G200_EW3 },
	{ PCI_VENDOR_ID_MATROX, 0x538, PCI_ANY_ID, PCI_ANY_ID, 0, 0, G200_EH3 },
	{0,}
};

MODULE_DEVICE_TABLE(pci, mgag200_pciidlist);

static int
mgag200_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct mga_device *mdev;
	struct drm_device *dev;
	int ret;

	drm_fb_helper_remove_conflicting_pci_framebuffers(pdev, "mgag200drmfb");

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	mdev = mgag200_device_create(pdev, ent->driver_data);
	if (IS_ERR(mdev))
		return PTR_ERR(mdev);
	dev = &mdev->base;

	ret = drm_dev_register(dev, ent->driver_data);
	if (ret)
		return ret;

	drm_fbdev_generic_setup(dev, 0);

	return 0;
}

static void mgag200_pci_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	drm_dev_unregister(dev);
}

static struct pci_driver mgag200_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = mgag200_pciidlist,
	.probe = mgag200_pci_probe,
	.remove = mgag200_pci_remove,
};

static int __init mgag200_init(void)
{
	if (vgacon_text_force() && mgag200_modeset == -1)
		return -EINVAL;

	if (mgag200_modeset == 0)
		return -EINVAL;

	return pci_register_driver(&mgag200_pci_driver);
}

static void __exit mgag200_exit(void)
{
	pci_unregister_driver(&mgag200_pci_driver);
}

module_init(mgag200_init);
module_exit(mgag200_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
