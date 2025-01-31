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

/* Try to detect WSXGA+ on Gen2+ */
static bool __ast_2100_detect_wsxga_p(struct ast_device *ast)
{
	u8 vgacrd0 = ast_get_index_reg(ast, AST_IO_VGACRI, 0xd0);

	if (!(vgacrd0 & AST_IO_VGACRD0_VRAM_INIT_BY_BMC))
		return true;
	if (vgacrd0 & AST_IO_VGACRD0_IKVM_WIDESCREEN)
		return true;

	return false;
}

/* Try to detect WUXGA on Gen2+ */
static bool __ast_2100_detect_wuxga(struct ast_device *ast)
{
	u8 vgacrd1;

	if (ast->support_fullhd) {
		vgacrd1 = ast_get_index_reg(ast, AST_IO_VGACRI, 0xd1);
		if (!(vgacrd1 & AST_IO_VGACRD1_SUPPORTS_WUXGA))
			return true;
	}

	return false;
}

static void ast_detect_widescreen(struct ast_device *ast)
{
	ast->support_wsxga_p = false;
	ast->support_fullhd = false;
	ast->support_wuxga = false;

	if (AST_GEN(ast) >= 7) {
		ast->support_wsxga_p = true;
		ast->support_fullhd = true;
		if (__ast_2100_detect_wuxga(ast))
			ast->support_wuxga = true;
	} else if (AST_GEN(ast) >= 6) {
		if (__ast_2100_detect_wsxga_p(ast))
			ast->support_wsxga_p = true;
		else if (ast->chip == AST2510)
			ast->support_wsxga_p = true;
		if (ast->support_wsxga_p)
			ast->support_fullhd = true;
		if (__ast_2100_detect_wuxga(ast))
			ast->support_wuxga = true;
	} else if (AST_GEN(ast) >= 5) {
		if (__ast_2100_detect_wsxga_p(ast))
			ast->support_wsxga_p = true;
		else if (ast->chip == AST1400)
			ast->support_wsxga_p = true;
		if (ast->support_wsxga_p)
			ast->support_fullhd = true;
		if (__ast_2100_detect_wuxga(ast))
			ast->support_wuxga = true;
	} else if (AST_GEN(ast) >= 4) {
		if (__ast_2100_detect_wsxga_p(ast))
			ast->support_wsxga_p = true;
		else if (ast->chip == AST1300)
			ast->support_wsxga_p = true;
		if (ast->support_wsxga_p)
			ast->support_fullhd = true;
		if (__ast_2100_detect_wuxga(ast))
			ast->support_wuxga = true;
	} else if (AST_GEN(ast) >= 3) {
		if (__ast_2100_detect_wsxga_p(ast))
			ast->support_wsxga_p = true;
		if (ast->support_wsxga_p) {
			if (ast->chip == AST2200)
				ast->support_fullhd = true;
		}
		if (__ast_2100_detect_wuxga(ast))
			ast->support_wuxga = true;
	} else if (AST_GEN(ast) >= 2) {
		if (__ast_2100_detect_wsxga_p(ast))
			ast->support_wsxga_p = true;
		if (ast->support_wsxga_p) {
			if (ast->chip == AST2100)
				ast->support_fullhd = true;
		}
		if (__ast_2100_detect_wuxga(ast))
			ast->support_wuxga = true;
	}
}

static void ast_detect_tx_chip(struct ast_device *ast, bool need_post)
{
	static const char * const info_str[] = {
		"analog VGA",
		"Sil164 TMDS transmitter",
		"DP501 DisplayPort transmitter",
		"ASPEED DisplayPort transmitter",
	};

	struct drm_device *dev = &ast->base;
	u8 vgacra3, vgacrd1;

	/* Check 3rd Tx option (digital output afaik) */
	ast->tx_chip = AST_TX_NONE;

	if (AST_GEN(ast) <= 3) {
		/*
		 * VGACRA3 Enhanced Color Mode Register, check if DVO is already
		 * enabled, in that case, assume we have a SIL164 TMDS transmitter
		 *
		 * Don't make that assumption if we the chip wasn't enabled and
		 * is at power-on reset, otherwise we'll incorrectly "detect" a
		 * SIL164 when there is none.
		 */
		if (!need_post) {
			vgacra3 = ast_get_index_reg_mask(ast, AST_IO_VGACRI, 0xa3, 0xff);
			if (vgacra3 & AST_IO_VGACRA3_DVO_ENABLED)
				ast->tx_chip = AST_TX_SIL164;
		}
	} else {
		/*
		 * On AST GEN4+, look at the configuration set by the SoC in
		 * the SOC scratch register #1 bits 11:8 (interestingly marked
		 * as "reserved" in the spec)
		 */
		vgacrd1 = ast_get_index_reg_mask(ast, AST_IO_VGACRI, 0xd1,
						 AST_IO_VGACRD1_TX_TYPE_MASK);
		switch (vgacrd1) {
		/*
		 * GEN4 to GEN6
		 */
		case AST_IO_VGACRD1_TX_SIL164_VBIOS:
			ast->tx_chip = AST_TX_SIL164;
			break;
		case AST_IO_VGACRD1_TX_DP501_VBIOS:
			ast->dp501_fw_addr = drmm_kzalloc(dev, 32*1024, GFP_KERNEL);
			if (ast->dp501_fw_addr) {
				/* backup firmware */
				if (ast_backup_fw(ast, ast->dp501_fw_addr, 32*1024)) {
					drmm_kfree(dev, ast->dp501_fw_addr);
					ast->dp501_fw_addr = NULL;
				}
			}
			fallthrough;
		case AST_IO_VGACRD1_TX_FW_EMBEDDED_FW:
			ast->tx_chip = AST_TX_DP501;
			break;
		/*
		 * GEN7+
		 */
		case AST_IO_VGACRD1_TX_ASTDP:
			ast->tx_chip = AST_TX_ASTDP;
			break;
		/*
		 * Several of the listed TX chips are not explicitly supported
		 * by the ast driver. If these exist in real-world devices, they
		 * are most likely reported as VGA or SIL164 outputs. We warn here
		 * to get bug reports for these devices. If none come in for some
		 * time, we can begin to fail device probing on these values.
		 */
		case AST_IO_VGACRD1_TX_ITE66121_VBIOS:
			drm_warn(dev, "ITE IT66121 detected, 0x%x, Gen%lu\n",
				 vgacrd1, AST_GEN(ast));
			break;
		case AST_IO_VGACRD1_TX_CH7003_VBIOS:
			drm_warn(dev, "Chrontel CH7003 detected, 0x%x, Gen%lu\n",
				 vgacrd1, AST_GEN(ast));
			break;
		case AST_IO_VGACRD1_TX_ANX9807_VBIOS:
			drm_warn(dev, "Analogix ANX9807 detected, 0x%x, Gen%lu\n",
				 vgacrd1, AST_GEN(ast));
			break;
		}
	}

	drm_info(dev, "Using %s\n", info_str[ast->tx_chip]);
}

static int ast_get_dram_info(struct ast_device *ast)
{
	struct drm_device *dev = &ast->base;
	struct device_node *np = dev->dev->of_node;
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

	ret = ast_get_dram_info(ast);
	if (ret)
		return ERR_PTR(ret);
	drm_info(dev, "dram MCLK=%u Mhz type=%d bus_width=%d\n",
		 ast->mclk, ast->dram_type, ast->dram_bus_width);

	ast_detect_tx_chip(ast, need_post);
	switch (ast->tx_chip) {
	case AST_TX_ASTDP:
		ret = ast_post_gpu(ast);
		break;
	default:
		ret = 0;
		if (need_post)
			ret = ast_post_gpu(ast);
		break;
	}
	if (ret)
		return ERR_PTR(ret);

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

	ast_detect_widescreen(ast);

	ret = ast_mode_config_init(ast);
	if (ret)
		return ERR_PTR(ret);

	return dev;
}
