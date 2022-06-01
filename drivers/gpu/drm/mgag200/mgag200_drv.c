// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2012 Red Hat
 *
 * Authors: Matthew Garrett
 *          Dave Airlie
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>

#include <drm/drm_aperture.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_managed.h>
#include <drm/drm_module.h>
#include <drm/drm_pciids.h>

#include "mgag200_drv.h"

int mgag200_modeset = -1;
MODULE_PARM_DESC(modeset, "Disable/Enable modesetting");
module_param_named(modeset, mgag200_modeset, int, 0400);

/*
 * DRM driver
 */

DEFINE_DRM_GEM_FOPS(mgag200_driver_fops);

static const struct drm_driver mgag200_driver = {
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

static bool mgag200_has_sgram(struct mga_device *mdev)
{
	struct drm_device *dev = &mdev->base;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	u32 option;
	int ret;

	ret = pci_read_config_dword(pdev, PCI_MGA_OPTION, &option);
	if (drm_WARN(dev, ret, "failed to read PCI config dword: %d\n", ret))
		return false;

	return !!(option & PCI_MGA_OPTION_HARDPWMSK);
}

int mgag200_regs_init(struct mga_device *mdev)
{
	struct drm_device *dev = &mdev->base;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	u32 option, option2;
	u8 crtcext3;
	int ret;

	ret = drmm_mutex_init(dev, &mdev->rmmio_lock);
	if (ret)
		return ret;

	switch (mdev->type) {
	case G200_PCI:
	case G200_AGP:
		if (mgag200_has_sgram(mdev))
			option = 0x4049cd21;
		else
			option = 0x40499121;
		option2 = 0x00008000;
		break;
	case G200_SE_A:
	case G200_SE_B:
		option = 0x40049120;
		if (mgag200_has_sgram(mdev))
			option |= PCI_MGA_OPTION_HARDPWMSK;
		option2 = 0x00008000;
		break;
	case G200_WB:
	case G200_EW3:
		option = 0x41049120;
		option2 = 0x0000b000;
		break;
	case G200_EV:
		option = 0x00000120;
		option2 = 0x0000b000;
		break;
	case G200_EH:
	case G200_EH3:
		option = 0x00000120;
		option2 = 0x0000b000;
		break;
	default:
		option = 0;
		option2 = 0;
	}

	if (option)
		pci_write_config_dword(pdev, PCI_MGA_OPTION, option);
	if (option2)
		pci_write_config_dword(pdev, PCI_MGA_OPTION2, option2);

	/* BAR 1 contains registers */
	mdev->rmmio_base = pci_resource_start(pdev, 1);
	mdev->rmmio_size = pci_resource_len(pdev, 1);

	if (!devm_request_mem_region(dev->dev, mdev->rmmio_base,
				     mdev->rmmio_size, "mgadrmfb_mmio")) {
		drm_err(dev, "can't reserve mmio registers\n");
		return -ENOMEM;
	}

	mdev->rmmio = pcim_iomap(pdev, 1, 0);
	if (mdev->rmmio == NULL)
		return -ENOMEM;

	RREG_ECRT(0x03, crtcext3);
	crtcext3 |= MGAREG_CRTCEXT3_MGAMODE;
	WREG_ECRT(0x03, crtcext3);

	return 0;
}

/*
 * PCI driver
 */

static const struct pci_device_id mgag200_pciidlist[] = {
	{ PCI_VENDOR_ID_MATROX, 0x520, PCI_ANY_ID, PCI_ANY_ID, 0, 0, G200_PCI },
	{ PCI_VENDOR_ID_MATROX, 0x521, PCI_ANY_ID, PCI_ANY_ID, 0, 0, G200_AGP },
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

static enum mga_type mgag200_type_from_driver_data(kernel_ulong_t driver_data)
{
	return (enum mga_type)(driver_data & MGAG200_TYPE_MASK);
}

static unsigned long mgag200_flags_from_driver_data(kernel_ulong_t driver_data)
{
	return driver_data & MGAG200_FLAG_MASK;
}

static int
mgag200_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	kernel_ulong_t driver_data = ent->driver_data;
	enum mga_type type = mgag200_type_from_driver_data(driver_data);
	unsigned long flags = mgag200_flags_from_driver_data(driver_data);
	struct mga_device *mdev;
	struct drm_device *dev;
	int ret;

	ret = drm_aperture_remove_conflicting_pci_framebuffers(pdev, &mgag200_driver);
	if (ret)
		return ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	switch (type) {
	case G200_PCI:
	case G200_AGP:
		mdev = mgag200_g200_device_create(pdev, &mgag200_driver, type, flags);
		break;
	case G200_SE_A:
	case G200_SE_B:
		mdev = mgag200_g200se_device_create(pdev, &mgag200_driver, type, flags);
		break;
	case G200_WB:
		mdev = mgag200_g200wb_device_create(pdev, &mgag200_driver, type, flags);
		break;
	case G200_EV:
		mdev = mgag200_g200ev_device_create(pdev, &mgag200_driver, type, flags);
		break;
	case G200_EH:
		mdev = mgag200_g200eh_device_create(pdev, &mgag200_driver, type, flags);
		break;
	case G200_EH3:
		mdev = mgag200_g200eh3_device_create(pdev, &mgag200_driver, type, flags);
		break;
	case G200_ER:
		mdev = mgag200_g200er_device_create(pdev, &mgag200_driver, type, flags);
		break;
	case G200_EW3:
		mdev = mgag200_g200ew3_device_create(pdev, &mgag200_driver, type, flags);
		break;
	default:
		dev_err(&pdev->dev, "Device type %d is unsupported\n", type);
		return -ENODEV;
	}
	if (IS_ERR(mdev))
		return PTR_ERR(mdev);
	dev = &mdev->base;

	ret = drm_dev_register(dev, 0);
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

drm_module_pci_driver_if_modeset(mgag200_pci_driver, mgag200_modeset);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
