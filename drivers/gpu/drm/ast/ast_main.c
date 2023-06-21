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

#include <linux/pci.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem.h>
#include <drm/drm_managed.h>

#include "ast_drv.h"

static int ast_init_pci_config(struct pci_dev *pdev)
{
	int err;
	u16 pcis04;

	err = pci_read_config_word(pdev, PCI_COMMAND, &pcis04);
	if (err)
		goto out;

	pcis04 |= PCI_COMMAND_MEMORY | PCI_COMMAND_IO;

	err = pci_write_config_word(pdev, PCI_COMMAND, pcis04);

out:
	return pcibios_err_to_errno(err);
}

static bool ast_is_vga_enabled(struct drm_device *dev)
{
	struct ast_device *ast = to_ast_device(dev);
	u8 ch;

	ch = ast_io_read8(ast, AST_IO_VGA_ENABLE_PORT);

	return !!(ch & 0x01);
}

static void ast_enable_vga(struct drm_device *dev)
{
	struct ast_device *ast = to_ast_device(dev);

	ast_io_write8(ast, AST_IO_VGA_ENABLE_PORT, 0x01);
	ast_io_write8(ast, AST_IO_MISC_PORT_WRITE, 0x01);
}

/*
 * Run this function as part of the HW device cleanup; not
 * when the DRM device gets released.
 */
static void ast_enable_mmio_release(void *data)
{
	struct ast_device *ast = data;

	/* enable standard VGA decode */
	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xa1, 0x04);
}

static int ast_enable_mmio(struct ast_device *ast)
{
	struct drm_device *dev = &ast->base;

	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xa1, 0x06);

	return devm_add_action_or_reset(dev->dev, ast_enable_mmio_release, ast);
}

static void ast_open_key(struct ast_device *ast)
{
	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0x80, 0xA8);
}

static int ast_device_config_init(struct ast_device *ast)
{
	struct drm_device *dev = &ast->base;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	struct device_node *np = dev->dev->of_node;
	uint32_t scu_rev = 0xffffffff;
	u32 data;
	u8 jregd0, jregd1;

	/*
	 * Find configuration mode and read SCU revision
	 */

	ast->config_mode = ast_use_defaults;

	/* Check if we have device-tree properties */
	if (np && !of_property_read_u32(np, "aspeed,scu-revision-id", &data)) {
		/* We do, disable P2A access */
		ast->config_mode = ast_use_dt;
		scu_rev = data;
	} else if (pdev->device == PCI_CHIP_AST2000) { // Not all families have a P2A bridge
		/*
		 * The BMC will set SCU 0x40 D[12] to 1 if the P2 bridge
		 * is disabled. We force using P2A if VGA only mode bit
		 * is set D[7]
		 */
		jregd0 = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xd0, 0xff);
		jregd1 = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xd1, 0xff);
		if (!(jregd0 & 0x80) || !(jregd1 & 0x10)) {

			/*
			 * We have a P2A bridge and it is enabled.
			 */

			/* Patch AST2500/AST2510 */
			if ((pdev->revision & 0xf0) == 0x40) {
				if (!(jregd0 & AST_VRAM_INIT_STATUS_MASK))
					ast_patch_ahb_2500(ast);
			}

			/* Double check that it's actually working */
			data = ast_read32(ast, 0xf004);
			if ((data != 0xffffffff) && (data != 0x00)) {
				ast->config_mode = ast_use_p2a;

				/* Read SCU7c (silicon revision register) */
				ast_write32(ast, 0xf004, 0x1e6e0000);
				ast_write32(ast, 0xf000, 0x1);
				scu_rev = ast_read32(ast, 0x1207c);
			}
		}
	}

	switch (ast->config_mode) {
	case ast_use_defaults:
		drm_info(dev, "Using default configuration\n");
		break;
	case ast_use_dt:
		drm_info(dev, "Using device-tree for configuration\n");
		break;
	case ast_use_p2a:
		drm_info(dev, "Using P2A bridge for configuration\n");
		break;
	}

	/*
	 * Identify chipset
	 */

	if (pdev->revision >= 0x50) {
		ast->chip = AST2600;
		drm_info(dev, "AST 2600 detected\n");
	} else if (pdev->revision >= 0x40) {
		switch (scu_rev & 0x300) {
		case 0x0100:
			ast->chip = AST2510;
			drm_info(dev, "AST 2510 detected\n");
			break;
		default:
			ast->chip = AST2500;
			drm_info(dev, "AST 2500 detected\n");
		}
	} else if (pdev->revision >= 0x30) {
		switch (scu_rev & 0x300) {
		case 0x0100:
			ast->chip = AST1400;
			drm_info(dev, "AST 1400 detected\n");
			break;
		default:
			ast->chip = AST2400;
			drm_info(dev, "AST 2400 detected\n");
		}
	} else if (pdev->revision >= 0x20) {
		switch (scu_rev & 0x300) {
		case 0x0000:
			ast->chip = AST1300;
			drm_info(dev, "AST 1300 detected\n");
			break;
		default:
			ast->chip = AST2300;
			drm_info(dev, "AST 2300 detected\n");
			break;
		}
	} else if (pdev->revision >= 0x10) {
		switch (scu_rev & 0x0300) {
		case 0x0200:
			ast->chip = AST1100;
			drm_info(dev, "AST 1100 detected\n");
			break;
		case 0x0100:
			ast->chip = AST2200;
			drm_info(dev, "AST 2200 detected\n");
			break;
		case 0x0000:
			ast->chip = AST2150;
			drm_info(dev, "AST 2150 detected\n");
			break;
		default:
			ast->chip = AST2100;
			drm_info(dev, "AST 2100 detected\n");
			break;
		}
	} else {
		ast->chip = AST2000;
		drm_info(dev, "AST 2000 detected\n");
	}

	return 0;
}

static void ast_detect_widescreen(struct ast_device *ast)
{
	u8 jreg;

	/* Check if we support wide screen */
	switch (AST_GEN(ast)) {
	case 1:
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
			if (ast->chip == AST1300)
				ast->support_wide_screen = true;
			if (ast->chip == AST1400)
				ast->support_wide_screen = true;
			if (ast->chip == AST2510)
				ast->support_wide_screen = true;
			if (IS_AST_GEN7(ast))
				ast->support_wide_screen = true;
		}
		break;
	}
}

static void ast_detect_tx_chip(struct ast_device *ast, bool need_post)
{
	struct drm_device *dev = &ast->base;
	u8 jreg;

	/* Check 3rd Tx option (digital output afaik) */
	ast->tx_chip_types |= AST_TX_NONE_BIT;

	/*
	 * VGACRA3 Enhanced Color Mode Register, check if DVO is already
	 * enabled, in that case, assume we have a SIL164 TMDS transmitter
	 *
	 * Don't make that assumption if we the chip wasn't enabled and
	 * is at power-on reset, otherwise we'll incorrectly "detect" a
	 * SIL164 when there is none.
	 */
	if (!need_post) {
		jreg = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xa3, 0xff);
		if (jreg & 0x80)
			ast->tx_chip_types = AST_TX_SIL164_BIT;
	}

	if (IS_AST_GEN4(ast) || IS_AST_GEN5(ast) || IS_AST_GEN6(ast)) {
		/*
		 * On AST GEN4+, look the configuration set by the SoC in
		 * the SOC scratch register #1 bits 11:8 (interestingly marked
		 * as "reserved" in the spec)
		 */
		jreg = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xd1, 0xff);
		switch (jreg) {
		case 0x04:
			ast->tx_chip_types = AST_TX_SIL164_BIT;
			break;
		case 0x08:
			ast->dp501_fw_addr = drmm_kzalloc(dev, 32*1024, GFP_KERNEL);
			if (ast->dp501_fw_addr) {
				/* backup firmware */
				if (ast_backup_fw(dev, ast->dp501_fw_addr, 32*1024)) {
					drmm_kfree(dev, ast->dp501_fw_addr);
					ast->dp501_fw_addr = NULL;
				}
			}
			fallthrough;
		case 0x0c:
			ast->tx_chip_types = AST_TX_DP501_BIT;
		}
	} else if (IS_AST_GEN7(ast)) {
		if (ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xD1, TX_TYPE_MASK) ==
		    ASTDP_DPMCU_TX) {
			ast->tx_chip_types = AST_TX_ASTDP_BIT;
			ast_dp_launch(&ast->base);
		}
	}

	/* Print stuff for diagnostic purposes */
	if (ast->tx_chip_types & AST_TX_NONE_BIT)
		drm_info(dev, "Using analog VGA\n");
	if (ast->tx_chip_types & AST_TX_SIL164_BIT)
		drm_info(dev, "Using Sil164 TMDS transmitter\n");
	if (ast->tx_chip_types & AST_TX_DP501_BIT)
		drm_info(dev, "Using DP501 DisplayPort transmitter\n");
	if (ast->tx_chip_types & AST_TX_ASTDP_BIT)
		drm_info(dev, "Using ASPEED DisplayPort transmitter\n");
}

static int ast_get_dram_info(struct drm_device *dev)
{
	struct device_node *np = dev->dev->of_node;
	struct ast_device *ast = to_ast_device(dev);
	uint32_t mcr_cfg, mcr_scu_mpll, mcr_scu_strap;
	uint32_t denum, num, div, ref_pll, dsel;

	switch (ast->config_mode) {
	case ast_use_dt:
		/*
		 * If some properties are missing, use reasonable
		 * defaults for GEN5
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
		if (IS_AST_GEN6(ast))
			ast->mclk = 800;
		else
			ast->mclk = 396;
		return 0;
	}

	if (mcr_cfg & 0x40)
		ast->dram_bus_width = 16;
	else
		ast->dram_bus_width = 32;

	if (IS_AST_GEN6(ast)) {
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
	} else if (IS_AST_GEN4(ast) || IS_AST_GEN5(ast)) {
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

struct ast_device *ast_device_create(const struct drm_driver *drv,
				     struct pci_dev *pdev,
				     unsigned long flags)
{
	struct drm_device *dev;
	struct ast_device *ast;
	bool need_post = false;
	int ret = 0;

	ast = devm_drm_dev_alloc(&pdev->dev, drv, struct ast_device, base);
	if (IS_ERR(ast))
		return ast;
	dev = &ast->base;

	pci_set_drvdata(pdev, dev);

	ret = drmm_mutex_init(dev, &ast->ioregs_lock);
	if (ret)
		return ERR_PTR(ret);

	ast->regs = pcim_iomap(pdev, 1, 0);
	if (!ast->regs)
		return ERR_PTR(-EIO);

	/*
	 * After AST2500, MMIO is enabled by default, and it should be adopted
	 * to be compatible with Arm.
	 */
	if (pdev->revision >= 0x40) {
		ast->ioregs = ast->regs + AST_IO_MM_OFFSET;
	} else if (!(pci_resource_flags(pdev, 2) & IORESOURCE_IO)) {
		drm_info(dev, "platform has no IO space, trying MMIO\n");
		ast->ioregs = ast->regs + AST_IO_MM_OFFSET;
	}

	/* "map" IO regs if the above hasn't done so already */
	if (!ast->ioregs) {
		ast->ioregs = pcim_iomap(pdev, 2, 0);
		if (!ast->ioregs)
			return ERR_PTR(-EIO);
	}

	ret = ast_init_pci_config(pdev);
	if (ret)
		return ERR_PTR(ret);

	if (!ast_is_vga_enabled(dev)) {
		drm_info(dev, "VGA not enabled on entry, requesting chip POST\n");
		need_post = true;
	}

	/*
	 * If VGA isn't enabled, we need to enable now or subsequent
	 * access to the scratch registers will fail.
	 */
	if (need_post)
		ast_enable_vga(dev);

	/* Enable extended register access */
	ast_open_key(ast);
	ret = ast_enable_mmio(ast);
	if (ret)
		return ERR_PTR(ret);

	ret = ast_device_config_init(ast);
	if (ret)
		return ERR_PTR(ret);

	ast_detect_widescreen(ast);
	ast_detect_tx_chip(ast, need_post);

	ret = ast_get_dram_info(dev);
	if (ret)
		return ERR_PTR(ret);

	drm_info(dev, "dram MCLK=%u Mhz type=%d bus_width=%d\n",
		 ast->mclk, ast->dram_type, ast->dram_bus_width);

	if (need_post)
		ast_post_gpu(dev);

	ret = ast_mm_init(ast);
	if (ret)
		return ERR_PTR(ret);

	/* map reserved buffer */
	ast->dp501_fw_buf = NULL;
	if (ast->vram_size < pci_resource_len(pdev, 0)) {
		ast->dp501_fw_buf = pci_iomap_range(pdev, 0, ast->vram_size, 0);
		if (!ast->dp501_fw_buf)
			drm_info(dev, "failed to map reserved buffer!\n");
	}

	ret = ast_mode_config_init(ast);
	if (ret)
		return ERR_PTR(ret);

	return ast;
}
