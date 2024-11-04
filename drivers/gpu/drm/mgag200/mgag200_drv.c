// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2012 Red Hat
 *
 * Authors: Matthew Garrett
 *          Dave Airlie
 */

#include <linux/aperture.h>
#include <linux/module.h>
#include <linux/pci.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_client_setup.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_shmem.h>
#include <drm/drm_file.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_managed.h>
#include <drm/drm_module.h>
#include <drm/drm_pciids.h>

#include "mgag200_drv.h"

static int mgag200_modeset = -1;
MODULE_PARM_DESC(modeset, "Disable/Enable modesetting");
module_param_named(modeset, mgag200_modeset, int, 0400);

int mgag200_init_pci_options(struct pci_dev *pdev, u32 option, u32 option2)
{
	struct device *dev = &pdev->dev;
	int err;

	err = pci_write_config_dword(pdev, PCI_MGA_OPTION, option);
	if (err != PCIBIOS_SUCCESSFUL) {
		dev_err(dev, "pci_write_config_dword(PCI_MGA_OPTION) failed: %d\n", err);
		return pcibios_err_to_errno(err);
	}

	err = pci_write_config_dword(pdev, PCI_MGA_OPTION2, option2);
	if (err != PCIBIOS_SUCCESSFUL) {
		dev_err(dev, "pci_write_config_dword(PCI_MGA_OPTION2) failed: %d\n", err);
		return pcibios_err_to_errno(err);
	}

	return 0;
}

resource_size_t mgag200_probe_vram(void __iomem *mem, resource_size_t size)
{
	int offset;
	int orig;
	int test1, test2;
	int orig1, orig2;
	size_t vram_size;

	/* Probe */
	orig = ioread16(mem);
	iowrite16(0, mem);

	vram_size = size;

	for (offset = 0x100000; offset < vram_size; offset += 0x4000) {
		orig1 = ioread8(mem + offset);
		orig2 = ioread8(mem + offset + 0x100);

		iowrite16(0xaa55, mem + offset);
		iowrite16(0xaa55, mem + offset + 0x100);

		test1 = ioread16(mem + offset);
		test2 = ioread16(mem);

		iowrite16(orig1, mem + offset);
		iowrite16(orig2, mem + offset + 0x100);

		if (test1 != 0xaa55)
			break;

		if (test2)
			break;
	}

	iowrite16(orig, mem);

	return offset - 65536;
}

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
	DRM_FBDEV_SHMEM_DRIVER_OPS,
};

/*
 * DRM device
 */

resource_size_t mgag200_device_probe_vram(struct mga_device *mdev)
{
	return mgag200_probe_vram(mdev->vram, resource_size(mdev->vram_res));
}

int mgag200_device_preinit(struct mga_device *mdev)
{
	struct drm_device *dev = &mdev->base;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	resource_size_t start, len;
	struct resource *res;

	/* BAR 1 contains registers */

	start = pci_resource_start(pdev, 1);
	len = pci_resource_len(pdev, 1);

	res = devm_request_mem_region(dev->dev, start, len, "mgadrmfb_mmio");
	if (!res) {
		drm_err(dev, "devm_request_mem_region(MMIO) failed\n");
		return -ENXIO;
	}
	mdev->rmmio_res = res;

	mdev->rmmio = pcim_iomap(pdev, 1, 0);
	if (!mdev->rmmio)
		return -ENOMEM;

	/* BAR 0 is VRAM */

	start = pci_resource_start(pdev, 0);
	len = pci_resource_len(pdev, 0);

	res = devm_request_mem_region(dev->dev, start, len, "mgadrmfb_vram");
	if (!res) {
		drm_err(dev, "devm_request_mem_region(VRAM) failed\n");
		return -ENXIO;
	}
	mdev->vram_res = res;

#if defined(CONFIG_DRM_MGAG200_DISABLE_WRITECOMBINE)
	mdev->vram = devm_ioremap(dev->dev, res->start, resource_size(res));
	if (!mdev->vram)
		return -ENOMEM;
#else
	mdev->vram = devm_ioremap_wc(dev->dev, res->start, resource_size(res));
	if (!mdev->vram)
		return -ENOMEM;

	/* Don't fail on errors, but performance might be reduced. */
	devm_arch_phys_wc_add(dev->dev, res->start, resource_size(res));
#endif

	return 0;
}

int mgag200_device_init(struct mga_device *mdev,
			const struct mgag200_device_info *info,
			const struct mgag200_device_funcs *funcs)
{
	struct drm_device *dev = &mdev->base;
	u8 crtcext3, misc;
	int ret;

	mdev->info = info;
	mdev->funcs = funcs;

	ret = drmm_mutex_init(dev, &mdev->rmmio_lock);
	if (ret)
		return ret;

	mutex_lock(&mdev->rmmio_lock);

	RREG_ECRT(0x03, crtcext3);
	crtcext3 |= MGAREG_CRTCEXT3_MGAMODE;
	WREG_ECRT(0x03, crtcext3);

	WREG_ECRT(0x04, 0x00);

	misc = RREG8(MGA_MISC_IN);
	misc |= MGAREG_MISC_RAMMAPEN |
		MGAREG_MISC_HIGH_PG_SEL;
	WREG8(MGA_MISC_OUT, misc);

	mutex_unlock(&mdev->rmmio_lock);

	WREG32(MGAREG_IEN, 0);

	return 0;
}

/*
 * PCI driver
 */

static const struct pci_device_id mgag200_pciidlist[] = {
	{ PCI_VENDOR_ID_MATROX, 0x520, PCI_ANY_ID, PCI_ANY_ID, 0, 0, G200_PCI },
	{ PCI_VENDOR_ID_MATROX, 0x521, PCI_ANY_ID, PCI_ANY_ID, 0, 0, G200_AGP },
	{ PCI_VENDOR_ID_MATROX, 0x522, PCI_ANY_ID, PCI_ANY_ID, 0, 0, G200_SE_A },
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
	enum mga_type type = (enum mga_type)ent->driver_data;
	struct mga_device *mdev;
	struct drm_device *dev;
	int ret;

	ret = aperture_remove_conflicting_pci_devices(pdev, mgag200_driver.name);
	if (ret)
		return ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	switch (type) {
	case G200_PCI:
	case G200_AGP:
		mdev = mgag200_g200_device_create(pdev, &mgag200_driver);
		break;
	case G200_SE_A:
	case G200_SE_B:
		mdev = mgag200_g200se_device_create(pdev, &mgag200_driver, type);
		break;
	case G200_WB:
		mdev = mgag200_g200wb_device_create(pdev, &mgag200_driver);
		break;
	case G200_EV:
		mdev = mgag200_g200ev_device_create(pdev, &mgag200_driver);
		break;
	case G200_EH:
		mdev = mgag200_g200eh_device_create(pdev, &mgag200_driver);
		break;
	case G200_EH3:
		mdev = mgag200_g200eh3_device_create(pdev, &mgag200_driver);
		break;
	case G200_ER:
		mdev = mgag200_g200er_device_create(pdev, &mgag200_driver);
		break;
	case G200_EW3:
		mdev = mgag200_g200ew3_device_create(pdev, &mgag200_driver);
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

	/*
	 * FIXME: A 24-bit color depth does not work with 24 bpp on
	 * G200ER. Force 32 bpp.
	 */
	drm_client_setup_with_fourcc(dev, DRM_FORMAT_XRGB8888);

	return 0;
}

static void mgag200_pci_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	drm_dev_unregister(dev);
	drm_atomic_helper_shutdown(dev);
}

static void mgag200_pci_shutdown(struct pci_dev *pdev)
{
	drm_atomic_helper_shutdown(pci_get_drvdata(pdev));
}

static struct pci_driver mgag200_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = mgag200_pciidlist,
	.probe = mgag200_pci_probe,
	.remove = mgag200_pci_remove,
	.shutdown = mgag200_pci_shutdown,
};

drm_module_pci_driver_if_modeset(mgag200_pci_driver, mgag200_modeset);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
