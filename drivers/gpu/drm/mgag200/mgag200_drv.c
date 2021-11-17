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
#include <linux/vmalloc.h>

#include <drm/drm_aperture.h>
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

static int mgag200_regs_init(struct mga_device *mdev)
{
	struct drm_device *dev = &mdev->base;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	u32 option, option2;
	u8 crtcext3;

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

static void mgag200_g200_interpret_bios(struct mga_device *mdev,
					const unsigned char *bios,
					size_t size)
{
	static const char matrox[] = {'M', 'A', 'T', 'R', 'O', 'X'};
	static const unsigned int expected_length[6] = {
		0, 64, 64, 64, 128, 128
	};
	struct drm_device *dev = &mdev->base;
	const unsigned char *pins;
	unsigned int pins_len, version;
	int offset;
	int tmp;

	/* Test for MATROX string. */
	if (size < 45 + sizeof(matrox))
		return;
	if (memcmp(&bios[45], matrox, sizeof(matrox)) != 0)
		return;

	/* Get the PInS offset. */
	if (size < MGA_BIOS_OFFSET + 2)
		return;
	offset = (bios[MGA_BIOS_OFFSET + 1] << 8) | bios[MGA_BIOS_OFFSET];

	/* Get PInS data structure. */

	if (size < offset + 6)
		return;
	pins = bios + offset;
	if (pins[0] == 0x2e && pins[1] == 0x41) {
		version = pins[5];
		pins_len = pins[2];
	} else {
		version = 1;
		pins_len = pins[0] + (pins[1] << 8);
	}

	if (version < 1 || version > 5) {
		drm_warn(dev, "Unknown BIOS PInS version: %d\n", version);
		return;
	}
	if (pins_len != expected_length[version]) {
		drm_warn(dev, "Unexpected BIOS PInS size: %d expected: %d\n",
			 pins_len, expected_length[version]);
		return;
	}
	if (size < offset + pins_len)
		return;

	drm_dbg_kms(dev, "MATROX BIOS PInS version %d size: %d found\n",
		    version, pins_len);

	/* Extract the clock values */

	switch (version) {
	case 1:
		tmp = pins[24] + (pins[25] << 8);
		if (tmp)
			mdev->model.g200.pclk_max = tmp * 10;
		break;
	case 2:
		if (pins[41] != 0xff)
			mdev->model.g200.pclk_max = (pins[41] + 100) * 1000;
		break;
	case 3:
		if (pins[36] != 0xff)
			mdev->model.g200.pclk_max = (pins[36] + 100) * 1000;
		if (pins[52] & 0x20)
			mdev->model.g200.ref_clk = 14318;
		break;
	case 4:
		if (pins[39] != 0xff)
			mdev->model.g200.pclk_max = pins[39] * 4 * 1000;
		if (pins[92] & 0x01)
			mdev->model.g200.ref_clk = 14318;
		break;
	case 5:
		tmp = pins[4] ? 8000 : 6000;
		if (pins[123] != 0xff)
			mdev->model.g200.pclk_min = pins[123] * tmp;
		if (pins[38] != 0xff)
			mdev->model.g200.pclk_max = pins[38] * tmp;
		if (pins[110] & 0x01)
			mdev->model.g200.ref_clk = 14318;
		break;
	default:
		break;
	}
}

static void mgag200_g200_init_refclk(struct mga_device *mdev)
{
	struct drm_device *dev = &mdev->base;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	unsigned char __iomem *rom;
	unsigned char *bios;
	size_t size;

	mdev->model.g200.pclk_min = 50000;
	mdev->model.g200.pclk_max = 230000;
	mdev->model.g200.ref_clk = 27050;

	rom = pci_map_rom(pdev, &size);
	if (!rom)
		return;

	bios = vmalloc(size);
	if (!bios)
		goto out;
	memcpy_fromio(bios, rom, size);

	if (size != 0 && bios[0] == 0x55 && bios[1] == 0xaa)
		mgag200_g200_interpret_bios(mdev, bios, size);

	drm_dbg_kms(dev, "pclk_min: %ld pclk_max: %ld ref_clk: %ld\n",
		    mdev->model.g200.pclk_min, mdev->model.g200.pclk_max,
		    mdev->model.g200.ref_clk);

	vfree(bios);
out:
	pci_unmap_rom(pdev, rom);
}

static void mgag200_g200se_init_unique_id(struct mga_device *mdev)
{
	struct drm_device *dev = &mdev->base;

	/* stash G200 SE model number for later use */
	mdev->model.g200se.unique_rev_id = RREG32(0x1e24);

	drm_dbg(dev, "G200 SE unique revision id is 0x%x\n",
		mdev->model.g200se.unique_rev_id);
}

static struct mga_device *
mgag200_device_create(struct pci_dev *pdev, enum mga_type type, unsigned long flags)
{
	struct mga_device *mdev;
	struct drm_device *dev;
	int ret;

	mdev = devm_drm_dev_alloc(&pdev->dev, &mgag200_driver, struct mga_device, base);
	if (IS_ERR(mdev))
		return mdev;
	dev = &mdev->base;

	pci_set_drvdata(pdev, dev);

	mdev->flags = flags;
	mdev->type = type;

	ret = mgag200_regs_init(mdev);
	if (ret)
		return ERR_PTR(ret);

	if (mdev->type == G200_PCI || mdev->type == G200_AGP)
		mgag200_g200_init_refclk(mdev);
	else if (IS_G200_SE(mdev))
		mgag200_g200se_init_unique_id(mdev);

	ret = mgag200_mm_init(mdev);
	if (ret)
		return ERR_PTR(ret);

	ret = mgag200_modeset_init(mdev);
	if (ret)
		return ERR_PTR(ret);

	return mdev;
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

	mdev = mgag200_device_create(pdev, type, flags);
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
