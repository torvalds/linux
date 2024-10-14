/*
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 */
/*
 * Authors: Dave Airlie <airlied@redhat.com>
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/pci.h>

#include <drm/drm_aperture.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_shmem.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_module.h>
#include <drm/drm_probe_helper.h>

#include "ast_drv.h"

static int ast_modeset = -1;

MODULE_PARM_DESC(modeset, "Disable/Enable modesetting");
module_param_named(modeset, ast_modeset, int, 0400);

/*
 * DRM driver
 */

DEFINE_DRM_GEM_FOPS(ast_fops);

static const struct drm_driver ast_driver = {
	.driver_features = DRIVER_ATOMIC |
			   DRIVER_GEM |
			   DRIVER_MODESET,

	.fops = &ast_fops,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,

	DRM_GEM_SHMEM_DRIVER_OPS
};

/*
 * PCI driver
 */

#define PCI_VENDOR_ASPEED 0x1a03

#define AST_VGA_DEVICE(id, info) {		\
	.class = PCI_BASE_CLASS_DISPLAY << 16,	\
	.class_mask = 0xff0000,			\
	.vendor = PCI_VENDOR_ASPEED,			\
	.device = id,				\
	.subvendor = PCI_ANY_ID,		\
	.subdevice = PCI_ANY_ID,		\
	.driver_data = (unsigned long) info }

static const struct pci_device_id ast_pciidlist[] = {
	AST_VGA_DEVICE(PCI_CHIP_AST2000, NULL),
	AST_VGA_DEVICE(PCI_CHIP_AST2100, NULL),
	{0, 0, 0},
};

MODULE_DEVICE_TABLE(pci, ast_pciidlist);

static bool ast_is_vga_enabled(void __iomem *ioregs)
{
	u8 vgaer = __ast_read8(ioregs, AST_IO_VGAER);

	return vgaer & AST_IO_VGAER_VGA_ENABLE;
}

static void ast_enable_vga(void __iomem *ioregs)
{
	__ast_write8(ioregs, AST_IO_VGAER, AST_IO_VGAER_VGA_ENABLE);
	__ast_write8(ioregs, AST_IO_VGAMR_W, AST_IO_VGAMR_IOSEL);
}

/*
 * Run this function as part of the HW device cleanup; not
 * when the DRM device gets released.
 */
static void ast_enable_mmio_release(void *data)
{
	void __iomem *ioregs = (void __force __iomem *)data;

	/* enable standard VGA decode */
	__ast_write8_i(ioregs, AST_IO_VGACRI, 0xa1, AST_IO_VGACRA1_MMIO_ENABLED);
}

static int ast_enable_mmio(struct device *dev, void __iomem *ioregs)
{
	void *data = (void __force *)ioregs;

	__ast_write8_i(ioregs, AST_IO_VGACRI, 0xa1,
		       AST_IO_VGACRA1_MMIO_ENABLED |
		       AST_IO_VGACRA1_VGAIO_DISABLED);

	return devm_add_action_or_reset(dev, ast_enable_mmio_release, data);
}

static void ast_open_key(void __iomem *ioregs)
{
	__ast_write8_i(ioregs, AST_IO_VGACRI, 0x80, AST_IO_VGACR80_PASSWORD);
}

static int ast_detect_chip(struct pci_dev *pdev,
			   void __iomem *regs, void __iomem *ioregs,
			   enum ast_chip *chip_out,
			   enum ast_config_mode *config_mode_out)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	enum ast_config_mode config_mode = ast_use_defaults;
	uint32_t scu_rev = 0xffffffff;
	enum ast_chip chip;
	u32 data;
	u8 vgacrd0, vgacrd1;

	/*
	 * Find configuration mode and read SCU revision
	 */

	/* Check if we have device-tree properties */
	if (np && !of_property_read_u32(np, "aspeed,scu-revision-id", &data)) {
		/* We do, disable P2A access */
		config_mode = ast_use_dt;
		scu_rev = data;
	} else if (pdev->device == PCI_CHIP_AST2000) { // Not all families have a P2A bridge
		/*
		 * The BMC will set SCU 0x40 D[12] to 1 if the P2 bridge
		 * is disabled. We force using P2A if VGA only mode bit
		 * is set D[7]
		 */
		vgacrd0 = __ast_read8_i(ioregs, AST_IO_VGACRI, 0xd0);
		vgacrd1 = __ast_read8_i(ioregs, AST_IO_VGACRI, 0xd1);
		if (!(vgacrd0 & 0x80) || !(vgacrd1 & 0x10)) {

			/*
			 * We have a P2A bridge and it is enabled.
			 */

			/* Patch AST2500/AST2510 */
			if ((pdev->revision & 0xf0) == 0x40) {
				if (!(vgacrd0 & AST_VRAM_INIT_STATUS_MASK))
					ast_patch_ahb_2500(regs);
			}

			/* Double check that it's actually working */
			data = __ast_read32(regs, 0xf004);
			if ((data != 0xffffffff) && (data != 0x00)) {
				config_mode = ast_use_p2a;

				/* Read SCU7c (silicon revision register) */
				__ast_write32(regs, 0xf004, 0x1e6e0000);
				__ast_write32(regs, 0xf000, 0x1);
				scu_rev = __ast_read32(regs, 0x1207c);
			}
		}
	}

	switch (config_mode) {
	case ast_use_defaults:
		dev_info(dev, "Using default configuration\n");
		break;
	case ast_use_dt:
		dev_info(dev, "Using device-tree for configuration\n");
		break;
	case ast_use_p2a:
		dev_info(dev, "Using P2A bridge for configuration\n");
		break;
	}

	/*
	 * Identify chipset
	 */

	if (pdev->revision >= 0x50) {
		chip = AST2600;
		dev_info(dev, "AST 2600 detected\n");
	} else if (pdev->revision >= 0x40) {
		switch (scu_rev & 0x300) {
		case 0x0100:
			chip = AST2510;
			dev_info(dev, "AST 2510 detected\n");
			break;
		default:
			chip = AST2500;
			dev_info(dev, "AST 2500 detected\n");
			break;
		}
	} else if (pdev->revision >= 0x30) {
		switch (scu_rev & 0x300) {
		case 0x0100:
			chip = AST1400;
			dev_info(dev, "AST 1400 detected\n");
			break;
		default:
			chip = AST2400;
			dev_info(dev, "AST 2400 detected\n");
			break;
		}
	} else if (pdev->revision >= 0x20) {
		switch (scu_rev & 0x300) {
		case 0x0000:
			chip = AST1300;
			dev_info(dev, "AST 1300 detected\n");
			break;
		default:
			chip = AST2300;
			dev_info(dev, "AST 2300 detected\n");
			break;
		}
	} else if (pdev->revision >= 0x10) {
		switch (scu_rev & 0x0300) {
		case 0x0200:
			chip = AST1100;
			dev_info(dev, "AST 1100 detected\n");
			break;
		case 0x0100:
			chip = AST2200;
			dev_info(dev, "AST 2200 detected\n");
			break;
		case 0x0000:
			chip = AST2150;
			dev_info(dev, "AST 2150 detected\n");
			break;
		default:
			chip = AST2100;
			dev_info(dev, "AST 2100 detected\n");
			break;
		}
	} else {
		chip = AST2000;
		dev_info(dev, "AST 2000 detected\n");
	}

	*chip_out = chip;
	*config_mode_out = config_mode;

	return 0;
}

static int ast_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct device *dev = &pdev->dev;
	int ret;
	void __iomem *regs;
	void __iomem *ioregs;
	enum ast_config_mode config_mode;
	enum ast_chip chip;
	struct drm_device *drm;
	bool need_post = false;

	ret = drm_aperture_remove_conflicting_pci_framebuffers(pdev, &ast_driver);
	if (ret)
		return ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	regs = pcim_iomap_region(pdev, 1, "ast");
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	if (pdev->revision >= 0x40) {
		/*
		 * On AST2500 and later models, MMIO is enabled by
		 * default. Adopt it to be compatible with ARM.
		 */
		resource_size_t len = pci_resource_len(pdev, 1);

		if (len < AST_IO_MM_OFFSET)
			return -EIO;
		if ((len - AST_IO_MM_OFFSET) < AST_IO_MM_LENGTH)
			return -EIO;
		ioregs = regs + AST_IO_MM_OFFSET;
	} else if (pci_resource_flags(pdev, 2) & IORESOURCE_IO) {
		/*
		 * Map I/O registers if we have a PCI BAR for I/O.
		 */
		resource_size_t len = pci_resource_len(pdev, 2);

		if (len < AST_IO_MM_LENGTH)
			return -EIO;
		ioregs = pcim_iomap_region(pdev, 2, "ast");
		if (IS_ERR(ioregs))
			return PTR_ERR(ioregs);
	} else {
		/*
		 * Anything else is best effort.
		 */
		resource_size_t len = pci_resource_len(pdev, 1);

		if (len < AST_IO_MM_OFFSET)
			return -EIO;
		if ((len - AST_IO_MM_OFFSET) < AST_IO_MM_LENGTH)
			return -EIO;
		ioregs = regs + AST_IO_MM_OFFSET;

		dev_info(dev, "Platform has no I/O space, using MMIO\n");
	}

	if (!ast_is_vga_enabled(ioregs)) {
		dev_info(dev, "VGA not enabled on entry, requesting chip POST\n");
		need_post = true;
	}

	/*
	 * If VGA isn't enabled, we need to enable now or subsequent
	 * access to the scratch registers will fail.
	 */
	if (need_post)
		ast_enable_vga(ioregs);
	/* Enable extended register access */
	ast_open_key(ioregs);

	ret = ast_enable_mmio(dev, ioregs);
	if (ret)
		return ret;

	ret = ast_detect_chip(pdev, regs, ioregs, &chip, &config_mode);
	if (ret)
		return ret;

	drm = ast_device_create(pdev, &ast_driver, chip, config_mode, regs, ioregs, need_post);
	if (IS_ERR(drm))
		return PTR_ERR(drm);
	pci_set_drvdata(pdev, drm);

	ret = drm_dev_register(drm, ent->driver_data);
	if (ret)
		return ret;

	drm_fbdev_shmem_setup(drm, 32);

	return 0;
}

static void ast_pci_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	drm_dev_unregister(dev);
	drm_atomic_helper_shutdown(dev);
}

static void ast_pci_shutdown(struct pci_dev *pdev)
{
	drm_atomic_helper_shutdown(pci_get_drvdata(pdev));
}

static int ast_drm_freeze(struct drm_device *dev)
{
	int error;

	error = drm_mode_config_helper_suspend(dev);
	if (error)
		return error;
	pci_save_state(to_pci_dev(dev->dev));
	return 0;
}

static int ast_drm_thaw(struct drm_device *dev)
{
	struct ast_device *ast = to_ast_device(dev);

	ast_enable_vga(ast->ioregs);
	ast_open_key(ast->ioregs);
	ast_enable_mmio(dev->dev, ast->ioregs);
	ast_post_gpu(dev);

	return drm_mode_config_helper_resume(dev);
}

static int ast_drm_resume(struct drm_device *dev)
{
	if (pci_enable_device(to_pci_dev(dev->dev)))
		return -EIO;

	return ast_drm_thaw(dev);
}

static int ast_pm_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *ddev = pci_get_drvdata(pdev);
	int error;

	error = ast_drm_freeze(ddev);
	if (error)
		return error;

	pci_disable_device(pdev);
	pci_set_power_state(pdev, PCI_D3hot);
	return 0;
}

static int ast_pm_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *ddev = pci_get_drvdata(pdev);
	return ast_drm_resume(ddev);
}

static int ast_pm_freeze(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *ddev = pci_get_drvdata(pdev);
	return ast_drm_freeze(ddev);
}

static int ast_pm_thaw(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *ddev = pci_get_drvdata(pdev);
	return ast_drm_thaw(ddev);
}

static int ast_pm_poweroff(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *ddev = pci_get_drvdata(pdev);

	return ast_drm_freeze(ddev);
}

static const struct dev_pm_ops ast_pm_ops = {
	.suspend = ast_pm_suspend,
	.resume = ast_pm_resume,
	.freeze = ast_pm_freeze,
	.thaw = ast_pm_thaw,
	.poweroff = ast_pm_poweroff,
	.restore = ast_pm_resume,
};

static struct pci_driver ast_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = ast_pciidlist,
	.probe = ast_pci_probe,
	.remove = ast_pci_remove,
	.shutdown = ast_pci_shutdown,
	.driver.pm = &ast_pm_ops,
};

drm_module_pci_driver_if_modeset(ast_pci_driver, ast_modeset);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
