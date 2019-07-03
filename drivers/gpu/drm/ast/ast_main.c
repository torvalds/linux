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
#include <drm/drmP.h>
#include "ast_drv.h"

#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>

void ast_set_index_reg_mask(struct ast_private *ast,
			    uint32_t base, uint8_t index,
			    uint8_t mask, uint8_t val)
{
	u8 tmp;
	ast_io_write8(ast, base, index);
	tmp = (ast_io_read8(ast, base + 1) & mask) | val;
	ast_set_index_reg(ast, base, index, tmp);
}

uint8_t ast_get_index_reg(struct ast_private *ast,
			  uint32_t base, uint8_t index)
{
	uint8_t ret;
	ast_io_write8(ast, base, index);
	ret = ast_io_read8(ast, base + 1);
	return ret;
}

uint8_t ast_get_index_reg_mask(struct ast_private *ast,
			       uint32_t base, uint8_t index, uint8_t mask)
{
	uint8_t ret;
	ast_io_write8(ast, base, index);
	ret = ast_io_read8(ast, base + 1) & mask;
	return ret;
}

static void ast_detect_config_mode(struct drm_device *dev, u32 *scu_rev)
{
	struct device_node *np = dev->pdev->dev.of_node;
	struct ast_private *ast = dev->dev_private;
	uint32_t data, jregd0, jregd1;

	/* Defaults */
	ast->config_mode = ast_use_defaults;
	*scu_rev = 0xffffffff;

	/* Check if we have device-tree properties */
	if (np && !of_property_read_u32(np, "aspeed,scu-revision-id",
					scu_rev)) {
		/* We do, disable P2A access */
		ast->config_mode = ast_use_dt;
		DRM_INFO("Using device-tree for configuration\n");
		return;
	}

	/* Not all families have a P2A bridge */
	if (dev->pdev->device != PCI_CHIP_AST2000)
		return;

	/*
	 * The BMC will set SCU 0x40 D[12] to 1 if the P2 bridge
	 * is disabled. We force using P2A if VGA only mode bit
	 * is set D[7]
	 */
	jregd0 = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xd0, 0xff);
	jregd1 = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xd1, 0xff);
	if (!(jregd0 & 0x80) || !(jregd1 & 0x10)) {
		/* Double check it's actually working */
		data = ast_read32(ast, 0xf004);
		if (data != 0xFFFFFFFF) {
			/* P2A works, grab silicon revision */
			ast->config_mode = ast_use_p2a;

			DRM_INFO("Using P2A bridge for configuration\n");

			/* Read SCU7c (silicon revision register) */
			ast_write32(ast, 0xf004, 0x1e6e0000);
			ast_write32(ast, 0xf000, 0x1);
			*scu_rev = ast_read32(ast, 0x1207c);
			return;
		}
	}

	/* We have a P2A bridge but it's disabled */
	DRM_INFO("P2A bridge disabled, using default configuration\n");
}

static int ast_detect_chip(struct drm_device *dev, bool *need_post)
{
	struct ast_private *ast = dev->dev_private;
	uint32_t jreg, scu_rev;

	/*
	 * If VGA isn't enabled, we need to enable now or subsequent
	 * access to the scratch registers will fail. We also inform
	 * our caller that it needs to POST the chip
	 * (Assumption: VGA not enabled -> need to POST)
	 */
	if (!ast_is_vga_enabled(dev)) {
		ast_enable_vga(dev);
		DRM_INFO("VGA not enabled on entry, requesting chip POST\n");
		*need_post = true;
	} else
		*need_post = false;


	/* Enable extended register access */
	ast_enable_mmio(dev);
	ast_open_key(ast);

	/* Find out whether P2A works or whether to use device-tree */
	ast_detect_config_mode(dev, &scu_rev);

	/* Identify chipset */
	if (dev->pdev->device == PCI_CHIP_AST1180) {
		ast->chip = AST1100;
		DRM_INFO("AST 1180 detected\n");
	} else {
		if (dev->pdev->revision >= 0x40) {
			ast->chip = AST2500;
			DRM_INFO("AST 2500 detected\n");
		} else if (dev->pdev->revision >= 0x30) {
			ast->chip = AST2400;
			DRM_INFO("AST 2400 detected\n");
		} else if (dev->pdev->revision >= 0x20) {
			ast->chip = AST2300;
			DRM_INFO("AST 2300 detected\n");
		} else if (dev->pdev->revision >= 0x10) {
			switch (scu_rev & 0x0300) {
			case 0x0200:
				ast->chip = AST1100;
				DRM_INFO("AST 1100 detected\n");
				break;
			case 0x0100:
				ast->chip = AST2200;
				DRM_INFO("AST 2200 detected\n");
				break;
			case 0x0000:
				ast->chip = AST2150;
				DRM_INFO("AST 2150 detected\n");
				break;
			default:
				ast->chip = AST2100;
				DRM_INFO("AST 2100 detected\n");
				break;
			}
			ast->vga2_clone = false;
		} else {
			ast->chip = AST2000;
			DRM_INFO("AST 2000 detected\n");
		}
	}

	/* Check if we support wide screen */
	switch (ast->chip) {
	case AST1180:
		ast->support_wide_screen = true;
		break;
	case AST2000:
		ast->support_wide_screen = false;
		break;
	default:
		jreg = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xd0, 0xff);
		if (!(jreg & 0x80))
			ast->support_wide_screen = true;
		else if (jreg & 0x01)
			ast->support_wide_screen = true;
		else {
			ast->support_wide_screen = false;
			if (ast->chip == AST2300 &&
			    (scu_rev & 0x300) == 0x0) /* ast1300 */
				ast->support_wide_screen = true;
			if (ast->chip == AST2400 &&
			    (scu_rev & 0x300) == 0x100) /* ast1400 */
				ast->support_wide_screen = true;
			if (ast->chip == AST2500 &&
			    scu_rev == 0x100)           /* ast2510 */
				ast->support_wide_screen = true;
		}
		break;
	}

	/* Check 3rd Tx option (digital output afaik) */
	ast->tx_chip_type = AST_TX_NONE;

	/*
	 * VGACRA3 Enhanced Color Mode Register, check if DVO is already
	 * enabled, in that case, assume we have a SIL164 TMDS transmitter
	 *
	 * Don't make that assumption if we the chip wasn't enabled and
	 * is at power-on reset, otherwise we'll incorrectly "detect" a
	 * SIL164 when there is none.
	 */
	if (!*need_post) {
		jreg = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xa3, 0xff);
		if (jreg & 0x80)
			ast->tx_chip_type = AST_TX_SIL164;
	}

	if ((ast->chip == AST2300) || (ast->chip == AST2400)) {
		/*
		 * On AST2300 and 2400, look the configuration set by the SoC in
		 * the SOC scratch register #1 bits 11:8 (interestingly marked
		 * as "reserved" in the spec)
		 */
		jreg = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xd1, 0xff);
		switch (jreg) {
		case 0x04:
			ast->tx_chip_type = AST_TX_SIL164;
			break;
		case 0x08:
			ast->dp501_fw_addr = kzalloc(32*1024, GFP_KERNEL);
			if (ast->dp501_fw_addr) {
				/* backup firmware */
				if (ast_backup_fw(dev, ast->dp501_fw_addr, 32*1024)) {
					kfree(ast->dp501_fw_addr);
					ast->dp501_fw_addr = NULL;
				}
			}
			/* fallthrough */
		case 0x0c:
			ast->tx_chip_type = AST_TX_DP501;
		}
	}

	/* Print stuff for diagnostic purposes */
	switch(ast->tx_chip_type) {
	case AST_TX_SIL164:
		DRM_INFO("Using Sil164 TMDS transmitter\n");
		break;
	case AST_TX_DP501:
		DRM_INFO("Using DP501 DisplayPort transmitter\n");
		break;
	default:
		DRM_INFO("Analog VGA only\n");
	}
	return 0;
}

static int ast_get_dram_info(struct drm_device *dev)
{
	struct device_node *np = dev->pdev->dev.of_node;
	struct ast_private *ast = dev->dev_private;
	uint32_t mcr_cfg, mcr_scu_mpll, mcr_scu_strap;
	uint32_t denum, num, div, ref_pll, dsel;

	switch (ast->config_mode) {
	case ast_use_dt:
		/*
		 * If some properties are missing, use reasonable
		 * defaults for AST2400
		 */
		if (of_property_read_u32(np, "aspeed,mcr-configuration",
					 &mcr_cfg))
			mcr_cfg = 0x00000577;
		if (of_property_read_u32(np, "aspeed,mcr-scu-mpll",
					 &mcr_scu_mpll))
			mcr_scu_mpll = 0x000050C0;
		if (of_property_read_u32(np, "aspeed,mcr-scu-strap",
					 &mcr_scu_strap))
			mcr_scu_strap = 0;
		break;
	case ast_use_p2a:
		ast_write32(ast, 0xf004, 0x1e6e0000);
		ast_write32(ast, 0xf000, 0x1);
		mcr_cfg = ast_read32(ast, 0x10004);
		mcr_scu_mpll = ast_read32(ast, 0x10120);
		mcr_scu_strap = ast_read32(ast, 0x10170);
		break;
	case ast_use_defaults:
	default:
		ast->dram_bus_width = 16;
		ast->dram_type = AST_DRAM_1Gx16;
		if (ast->chip == AST2500)
			ast->mclk = 800;
		else
			ast->mclk = 396;
		return 0;
	}

	if (mcr_cfg & 0x40)
		ast->dram_bus_width = 16;
	else
		ast->dram_bus_width = 32;

	if (ast->chip == AST2500) {
		switch (mcr_cfg & 0x03) {
		case 0:
			ast->dram_type = AST_DRAM_1Gx16;
			break;
		default:
		case 1:
			ast->dram_type = AST_DRAM_2Gx16;
			break;
		case 2:
			ast->dram_type = AST_DRAM_4Gx16;
			break;
		case 3:
			ast->dram_type = AST_DRAM_8Gx16;
			break;
		}
	} else if (ast->chip == AST2300 || ast->chip == AST2400) {
		switch (mcr_cfg & 0x03) {
		case 0:
			ast->dram_type = AST_DRAM_512Mx16;
			break;
		default:
		case 1:
			ast->dram_type = AST_DRAM_1Gx16;
			break;
		case 2:
			ast->dram_type = AST_DRAM_2Gx16;
			break;
		case 3:
			ast->dram_type = AST_DRAM_4Gx16;
			break;
		}
	} else {
		switch (mcr_cfg & 0x0c) {
		case 0:
		case 4:
			ast->dram_type = AST_DRAM_512Mx16;
			break;
		case 8:
			if (mcr_cfg & 0x40)
				ast->dram_type = AST_DRAM_1Gx16;
			else
				ast->dram_type = AST_DRAM_512Mx32;
			break;
		case 0xc:
			ast->dram_type = AST_DRAM_1Gx32;
			break;
		}
	}

	if (mcr_scu_strap & 0x2000)
		ref_pll = 14318;
	else
		ref_pll = 12000;

	denum = mcr_scu_mpll & 0x1f;
	num = (mcr_scu_mpll & 0x3fe0) >> 5;
	dsel = (mcr_scu_mpll & 0xc000) >> 14;
	switch (dsel) {
	case 3:
		div = 0x4;
		break;
	case 2:
	case 1:
		div = 0x2;
		break;
	default:
		div = 0x1;
		break;
	}
	ast->mclk = ref_pll * (num + 2) / ((denum + 2) * (div * 1000));
	return 0;
}

static const struct drm_mode_config_funcs ast_mode_funcs = {
	.fb_create = drm_gem_fb_create
};

static u32 ast_get_vram_info(struct drm_device *dev)
{
	struct ast_private *ast = dev->dev_private;
	u8 jreg;
	u32 vram_size;
	ast_open_key(ast);

	vram_size = AST_VIDMEM_DEFAULT_SIZE;
	jreg = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xaa, 0xff);
	switch (jreg & 3) {
	case 0: vram_size = AST_VIDMEM_SIZE_8M; break;
	case 1: vram_size = AST_VIDMEM_SIZE_16M; break;
	case 2: vram_size = AST_VIDMEM_SIZE_32M; break;
	case 3: vram_size = AST_VIDMEM_SIZE_64M; break;
	}

	jreg = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0x99, 0xff);
	switch (jreg & 0x03) {
	case 1:
		vram_size -= 0x100000;
		break;
	case 2:
		vram_size -= 0x200000;
		break;
	case 3:
		vram_size -= 0x400000;
		break;
	}

	return vram_size;
}

int ast_driver_load(struct drm_device *dev, unsigned long flags)
{
	struct ast_private *ast;
	bool need_post;
	int ret = 0;

	ast = kzalloc(sizeof(struct ast_private), GFP_KERNEL);
	if (!ast)
		return -ENOMEM;

	dev->dev_private = ast;
	ast->dev = dev;

	ast->regs = pci_iomap(dev->pdev, 1, 0);
	if (!ast->regs) {
		ret = -EIO;
		goto out_free;
	}

	/*
	 * If we don't have IO space at all, use MMIO now and
	 * assume the chip has MMIO enabled by default (rev 0x20
	 * and higher).
	 */
	if (!(pci_resource_flags(dev->pdev, 2) & IORESOURCE_IO)) {
		DRM_INFO("platform has no IO space, trying MMIO\n");
		ast->ioregs = ast->regs + AST_IO_MM_OFFSET;
	}

	/* "map" IO regs if the above hasn't done so already */
	if (!ast->ioregs) {
		ast->ioregs = pci_iomap(dev->pdev, 2, 0);
		if (!ast->ioregs) {
			ret = -EIO;
			goto out_free;
		}
	}

	ast_detect_chip(dev, &need_post);

	if (need_post)
		ast_post_gpu(dev);

	if (ast->chip != AST1180) {
		ret = ast_get_dram_info(dev);
		if (ret)
			goto out_free;
		ast->vram_size = ast_get_vram_info(dev);
		DRM_INFO("dram MCLK=%u Mhz type=%d bus_width=%d size=%08x\n",
			 ast->mclk, ast->dram_type,
			 ast->dram_bus_width, ast->vram_size);
	}

	ret = ast_mm_init(ast);
	if (ret)
		goto out_free;

	drm_mode_config_init(dev);

	dev->mode_config.funcs = (void *)&ast_mode_funcs;
	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	dev->mode_config.preferred_depth = 24;
	dev->mode_config.prefer_shadow = 1;
	dev->mode_config.fb_base = pci_resource_start(ast->dev->pdev, 0);

	if (ast->chip == AST2100 ||
	    ast->chip == AST2200 ||
	    ast->chip == AST2300 ||
	    ast->chip == AST2400 ||
	    ast->chip == AST2500 ||
	    ast->chip == AST1180) {
		dev->mode_config.max_width = 1920;
		dev->mode_config.max_height = 2048;
	} else {
		dev->mode_config.max_width = 1600;
		dev->mode_config.max_height = 1200;
	}

	ret = ast_mode_init(dev);
	if (ret)
		goto out_free;

	ret = drm_fbdev_generic_setup(dev, 32);
	if (ret)
		goto out_free;

	return 0;
out_free:
	kfree(ast);
	dev->dev_private = NULL;
	return ret;
}

void ast_driver_unload(struct drm_device *dev)
{
	struct ast_private *ast = dev->dev_private;

	ast_release_firmware(dev);
	kfree(ast->dp501_fw_addr);
	ast_mode_fini(dev);
	drm_mode_config_cleanup(dev);

	ast_mm_fini(ast);
	if (ast->ioregs != ast->regs + AST_IO_MM_OFFSET)
		pci_iounmap(dev->pdev, ast->ioregs);
	pci_iounmap(dev->pdev, ast->regs);
	kfree(ast);
}

int ast_gem_create(struct drm_device *dev,
		   u32 size, bool iskernel,
		   struct drm_gem_object **obj)
{
	struct drm_gem_vram_object *gbo;
	int ret;

	*obj = NULL;

	size = roundup(size, PAGE_SIZE);
	if (size == 0)
		return -EINVAL;

	gbo = drm_gem_vram_create(dev, &dev->vram_mm->bdev, size, 0, false);
	if (IS_ERR(gbo)) {
		ret = PTR_ERR(gbo);
		if (ret != -ERESTARTSYS)
			DRM_ERROR("failed to allocate GEM object\n");
		return ret;
	}
	*obj = &gbo->gem;
	return 0;
}
