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

#include <linux/of.h>
#include <linux/pci.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem.h>
#include <drm/drm_managed.h>

#include "ast_drv.h"

static void ast_detect_widescreen(struct ast_device *ast)
{
	u8 jreg;

	/* Check if we support wide screen */
	switch (AST_GEN(ast)) {
	case 1:
		ast->support_wide_screen = false;
		break;
	default:
		jreg = ast_get_index_reg_mask(ast, AST_IO_VGACRI, 0xd0, 0xff);
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
		jreg = ast_get_index_reg_mask(ast, AST_IO_VGACRI, 0xa3, 0xff);
		if (jreg & 0x80)
			ast->tx_chip_types = AST_TX_SIL164_BIT;
	}

	if (IS_AST_GEN4(ast) || IS_AST_GEN5(ast) || IS_AST_GEN6(ast)) {
		/*
		 * On AST GEN4+, look the configuration set by the SoC in
		 * the SOC scratch register #1 bits 11:8 (interestingly marked
		 * as "reserved" in the spec)
		 */
		jreg = ast_get_index_reg_mask(ast, AST_IO_VGACRI, 0xd1, 0xff);
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
		if (ast_get_index_reg_mask(ast, AST_IO_VGACRI, 0xD1, TX_TYPE_MASK) ==
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

struct drm_device *ast_device_create(struct pci_dev *pdev,
				     const struct drm_driver *drv,
				     enum ast_chip chip,
				     enum ast_config_mode config_mode,
				     void __iomem *regs,
				     void __iomem *ioregs,
				     bool need_post)
{
	struct drm_device *dev;
	struct ast_device *ast;
	int ret;

	ast = devm_drm_dev_alloc(&pdev->dev, drv, struct ast_device, base);
	if (IS_ERR(ast))
		return ERR_CAST(ast);
	dev = &ast->base;

	ast->chip = chip;
	ast->config_mode = config_mode;
	ast->regs = regs;
	ast->ioregs = ioregs;

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

	return dev;
}
