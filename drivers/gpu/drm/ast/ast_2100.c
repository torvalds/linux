// SPDX-License-Identifier: MIT
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
 */
/*
 * Authors: Dave Airlie <airlied@redhat.com>
 */

#include <linux/delay.h>
#include <linux/pci.h>

#include <drm/drm_drv.h>

#include "ast_drv.h"
#include "ast_post.h"

/*
 * DRAM type
 */

static enum ast_dram_layout ast_2100_get_dram_layout_p2a(struct ast_device *ast)
{
	u32 mcr04;
	enum ast_dram_layout dram_layout;

	mcr04 = ast_mindwm(ast, AST_REG_MCR04);

	switch (mcr04 & GENMASK(3, 2)) {
	case 0:
	case 4:
	default:
		dram_layout = AST_DRAM_512Mx16;
		break;
	case 8:
		if (mcr04 & 0x40)
			dram_layout = AST_DRAM_1Gx16;
		else
			dram_layout = AST_DRAM_512Mx32;
		break;
	case 0xc:
		dram_layout = AST_DRAM_1Gx32;
		break;
	}

	return dram_layout;
}

/*
 * POST
 */

static const struct ast_dramstruct ast1100_dram_table_data[] = {
	AST_DRAMSTRUCT_REG(AST_REG_SCU000, AST_REG_SCU000_PROTECTION_KEY),
	AST_DRAMSTRUCT_REG(AST_REG_SCU020, 0x000041f0),
	AST_DRAMSTRUCT_UDELAY(67u),
	AST_DRAMSTRUCT_REG(AST_REG_MCR00, AST_REG_MCR00_PROTECTION_KEY),
	AST_DRAMSTRUCT_REG(AST_REG_MCR6C, 0x00909090),
	AST_DRAMSTRUCT_REG(AST_REG_MCR64, 0x00050000),
	AST_DRAMSTRUCT_REG(AST_REG_MCR04, 0x00000585), // DRAM type
	AST_DRAMSTRUCT_REG(AST_REG_MCR08, 0x0011030f),
	AST_DRAMSTRUCT_REG(AST_REG_MCR10, 0x22201724),
	AST_DRAMSTRUCT_REG(AST_REG_MCR18, 0x1e29011a),
	AST_DRAMSTRUCT_REG(AST_REG_MCR20, 0x00c82222),
	AST_DRAMSTRUCT_REG(AST_REG_MCR14, 0x01001523),
	AST_DRAMSTRUCT_REG(AST_REG_MCR1C, 0x1024010d),
	AST_DRAMSTRUCT_REG(AST_REG_MCR24, 0x00cb2522),
	AST_DRAMSTRUCT_REG(AST_REG_MCR38, 0xffffff82),
	AST_DRAMSTRUCT_REG(AST_REG_MCR3C, 0x00000000),
	AST_DRAMSTRUCT_REG(AST_REG_MCR40, 0x00000000),
	AST_DRAMSTRUCT_REG(AST_REG_MCR44, 0x00000000),
	AST_DRAMSTRUCT_REG(AST_REG_MCR48, 0x00000000),
	AST_DRAMSTRUCT_REG(AST_REG_MCR4C, 0x00000000),
	AST_DRAMSTRUCT_REG(AST_REG_MCR50, 0x00000000),
	AST_DRAMSTRUCT_REG(AST_REG_MCR54, 0x00000000),
	AST_DRAMSTRUCT_REG(AST_REG_MCR58, 0x00000000),
	AST_DRAMSTRUCT_REG(AST_REG_MCR5C, 0x00000000),
	AST_DRAMSTRUCT_REG(AST_REG_MCR60, 0x032aa02a),
	AST_DRAMSTRUCT_REG(AST_REG_MCR64, 0x002d3000),
	AST_DRAMSTRUCT_REG(AST_REG_MCR68, 0x00000000),
	AST_DRAMSTRUCT_REG(AST_REG_MCR70, 0x00000000),
	AST_DRAMSTRUCT_REG(AST_REG_MCR74, 0x00000000),
	AST_DRAMSTRUCT_REG(AST_REG_MCR78, 0x00000000),
	AST_DRAMSTRUCT_REG(AST_REG_MCR7C, 0x00000000),
	AST_DRAMSTRUCT_REG(AST_REG_MCR34, 0x00000001),
	AST_DRAMSTRUCT_UDELAY(67u),
	AST_DRAMSTRUCT_REG(AST_REG_MCR2C, 0x00000732),
	AST_DRAMSTRUCT_REG(AST_REG_MCR30, 0x00000040),
	AST_DRAMSTRUCT_REG(AST_REG_MCR28, 0x00000005),
	AST_DRAMSTRUCT_REG(AST_REG_MCR28, 0x00000007),
	AST_DRAMSTRUCT_REG(AST_REG_MCR28, 0x00000003),
	AST_DRAMSTRUCT_REG(AST_REG_MCR28, 0x00000001),
	AST_DRAMSTRUCT_REG(AST_REG_MCR0C, 0x00005a08),
	AST_DRAMSTRUCT_REG(AST_REG_MCR2C, 0x00000632),
	AST_DRAMSTRUCT_REG(AST_REG_MCR28, 0x00000001),
	AST_DRAMSTRUCT_REG(AST_REG_MCR30, 0x000003c0),
	AST_DRAMSTRUCT_REG(AST_REG_MCR28, 0x00000003),
	AST_DRAMSTRUCT_REG(AST_REG_MCR30, 0x00000040),
	AST_DRAMSTRUCT_REG(AST_REG_MCR28, 0x00000003),
	AST_DRAMSTRUCT_REG(AST_REG_MCR0C, 0x00005a21),
	AST_DRAMSTRUCT_REG(AST_REG_MCR34, 0x00007c03),
	AST_DRAMSTRUCT_REG(AST_REG_MCR120, 0x00004c41),
	AST_DRAMSTRUCT_INVALID,
};

static const struct ast_dramstruct ast2100_dram_table_data[] = {
	AST_DRAMSTRUCT_REG(AST_REG_SCU000, AST_REG_SCU000_PROTECTION_KEY),
	AST_DRAMSTRUCT_REG(AST_REG_SCU020, 0x00004120),
	AST_DRAMSTRUCT_UDELAY(67u),
	AST_DRAMSTRUCT_REG(AST_REG_MCR00, AST_REG_MCR00_PROTECTION_KEY),
	AST_DRAMSTRUCT_REG(AST_REG_MCR6C, 0x00909090),
	AST_DRAMSTRUCT_REG(AST_REG_MCR64, 0x00070000),
	AST_DRAMSTRUCT_REG(AST_REG_MCR04, 0x00000489), // DRAM type
	AST_DRAMSTRUCT_REG(AST_REG_MCR08, 0x0011030f),
	AST_DRAMSTRUCT_REG(AST_REG_MCR10, 0x32302926),
	AST_DRAMSTRUCT_REG(AST_REG_MCR18, 0x274c0122),
	AST_DRAMSTRUCT_REG(AST_REG_MCR20, 0x00ce2222),
	AST_DRAMSTRUCT_REG(AST_REG_MCR14, 0x01001523),
	AST_DRAMSTRUCT_REG(AST_REG_MCR1C, 0x1024010d),
	AST_DRAMSTRUCT_REG(AST_REG_MCR24, 0x00cb2522),
	AST_DRAMSTRUCT_REG(AST_REG_MCR38, 0xffffff82),
	AST_DRAMSTRUCT_REG(AST_REG_MCR3C, 0x00000000),
	AST_DRAMSTRUCT_REG(AST_REG_MCR40, 0x00000000),
	AST_DRAMSTRUCT_REG(AST_REG_MCR44, 0x00000000),
	AST_DRAMSTRUCT_REG(AST_REG_MCR48, 0x00000000),
	AST_DRAMSTRUCT_REG(AST_REG_MCR4C, 0x00000000),
	AST_DRAMSTRUCT_REG(AST_REG_MCR50, 0x00000000),
	AST_DRAMSTRUCT_REG(AST_REG_MCR54, 0x00000000),
	AST_DRAMSTRUCT_REG(AST_REG_MCR58, 0x00000000),
	AST_DRAMSTRUCT_REG(AST_REG_MCR5C, 0x00000000),
	AST_DRAMSTRUCT_REG(AST_REG_MCR60, 0x0f2aa02a),
	AST_DRAMSTRUCT_REG(AST_REG_MCR64, 0x003f3005),
	AST_DRAMSTRUCT_REG(AST_REG_MCR68, 0x02020202),
	AST_DRAMSTRUCT_REG(AST_REG_MCR70, 0x00000000),
	AST_DRAMSTRUCT_REG(AST_REG_MCR74, 0x00000000),
	AST_DRAMSTRUCT_REG(AST_REG_MCR78, 0x00000000),
	AST_DRAMSTRUCT_REG(AST_REG_MCR7C, 0x00000000),
	AST_DRAMSTRUCT_REG(AST_REG_MCR34, 0x00000001),
	AST_DRAMSTRUCT_UDELAY(67u),
	AST_DRAMSTRUCT_REG(AST_REG_MCR2C, 0x00000942),
	AST_DRAMSTRUCT_REG(AST_REG_MCR30, 0x00000040),
	AST_DRAMSTRUCT_REG(AST_REG_MCR28, 0x00000005),
	AST_DRAMSTRUCT_REG(AST_REG_MCR28, 0x00000007),
	AST_DRAMSTRUCT_REG(AST_REG_MCR28, 0x00000003),
	AST_DRAMSTRUCT_REG(AST_REG_MCR28, 0x00000001),
	AST_DRAMSTRUCT_REG(AST_REG_MCR0C, 0x00005a08),
	AST_DRAMSTRUCT_REG(AST_REG_MCR2C, 0x00000842),
	AST_DRAMSTRUCT_REG(AST_REG_MCR28, 0x00000001),
	AST_DRAMSTRUCT_REG(AST_REG_MCR30, 0x000003c0),
	AST_DRAMSTRUCT_REG(AST_REG_MCR28, 0x00000003),
	AST_DRAMSTRUCT_REG(AST_REG_MCR30, 0x00000040),
	AST_DRAMSTRUCT_REG(AST_REG_MCR28, 0x00000003),
	AST_DRAMSTRUCT_REG(AST_REG_MCR0C, 0x00005a21),
	AST_DRAMSTRUCT_REG(AST_REG_MCR34, 0x00007c03),
	AST_DRAMSTRUCT_REG(AST_REG_MCR120, 0x00005061),
	AST_DRAMSTRUCT_INVALID,
};

/*
 * AST2100/2150 DLL CBR Setting
 */
#define CBR_SIZE_AST2150	     ((16 << 10) - 1)
#define CBR_PASSNUM_AST2150          5
#define CBR_THRESHOLD_AST2150        10
#define CBR_THRESHOLD2_AST2150       10
#define TIMEOUT_AST2150              5000000

#define CBR_PATNUM_AST2150           8

static const u32 pattern_AST2150[14] = {
	0xFF00FF00,
	0xCC33CC33,
	0xAA55AA55,
	0xFFFE0001,
	0x683501FE,
	0x0F1929B0,
	0x2D0B4346,
	0x60767F02,
	0x6FBE36A6,
	0x3A253035,
	0x3019686D,
	0x41C6167E,
	0x620152BF,
	0x20F050E0
};

static u32 mmctestburst2_ast2150(struct ast_device *ast, u32 datagen)
{
	u32 data, timeout;

	ast_moutdwm(ast, AST_REG_MCR70, 0x00000000);
	ast_moutdwm(ast, AST_REG_MCR70, 0x00000001 | (datagen << 3));
	timeout = 0;
	do {
		data = ast_mindwm(ast, AST_REG_MCR70) & 0x40;
		if (++timeout > TIMEOUT_AST2150) {
			ast_moutdwm(ast, AST_REG_MCR70, 0x00000000);
			return 0xffffffff;
		}
	} while (!data);
	ast_moutdwm(ast, AST_REG_MCR70, 0x00000000);
	ast_moutdwm(ast, AST_REG_MCR70, 0x00000003 | (datagen << 3));
	timeout = 0;
	do {
		data = ast_mindwm(ast, AST_REG_MCR70) & 0x40;
		if (++timeout > TIMEOUT_AST2150) {
			ast_moutdwm(ast, AST_REG_MCR70, 0x00000000);
			return 0xffffffff;
		}
	} while (!data);
	data = (ast_mindwm(ast, AST_REG_MCR70) & 0x80) >> 7;
	ast_moutdwm(ast, AST_REG_MCR70, 0x00000000);
	return data;
}

static int cbrtest_ast2150(struct ast_device *ast)
{
	int i;

	for (i = 0; i < 8; i++)
		if (mmctestburst2_ast2150(ast, i))
			return 0;
	return 1;
}

static int cbrscan_ast2150(struct ast_device *ast, int busw)
{
	u32 patcnt, loop;

	for (patcnt = 0; patcnt < CBR_PATNUM_AST2150; patcnt++) {
		ast_moutdwm(ast, AST_REG_MCR7C, pattern_AST2150[patcnt]);
		for (loop = 0; loop < CBR_PASSNUM_AST2150; loop++) {
			if (cbrtest_ast2150(ast))
				break;
		}
		if (loop == CBR_PASSNUM_AST2150)
			return 0;
	}
	return 1;
}

static void cbrdlli_ast2150(struct ast_device *ast, int busw)
{
	u32 dll_min[4], dll_max[4], dlli, data, passcnt;

cbr_start:
	dll_min[0] = 0xff;
	dll_min[1] = 0xff;
	dll_min[2] = 0xff;
	dll_min[3] = 0xff;
	dll_max[0] = 0x00;
	dll_max[1] = 0x00;
	dll_max[2] = 0x00;
	dll_max[3] = 0x00;
	passcnt = 0;

	for (dlli = 0; dlli < 100; dlli++) {
		ast_moutdwm(ast, AST_REG_MCR68, dlli | (dlli << 8) | (dlli << 16) | (dlli << 24));
		data = cbrscan_ast2150(ast, busw);
		if (data != 0) {
			if (data & 0x1) {
				if (dll_min[0] > dlli)
					dll_min[0] = dlli;
				if (dll_max[0] < dlli)
					dll_max[0] = dlli;
			}
			passcnt++;
		} else if (passcnt >= CBR_THRESHOLD_AST2150) {
			goto cbr_start;
		}
	}
	if (dll_max[0] == 0 || (dll_max[0] - dll_min[0]) < CBR_THRESHOLD_AST2150)
		goto cbr_start;

	dlli = dll_min[0] + (((dll_max[0] - dll_min[0]) * 7) >> 4);
	ast_moutdwm(ast, AST_REG_MCR68, dlli | (dlli << 8) | (dlli << 16) | (dlli << 24));
}

static void ast_post_chip_2100(struct ast_device *ast)
{
	u8 j;
	u32 i;
	enum ast_dram_layout dram_layout = ast_2100_get_dram_layout_p2a(ast);
	u32 mcr120;
	u32 scu00c, scu040;

	j = ast_get_index_reg_mask(ast, AST_IO_VGACRI, 0xd0, 0xff);

	if ((j & 0x80) == 0) { /* VGA only */
		const struct ast_dramstruct *dram_reg_info;

		if (ast->chip == AST2100 || ast->chip == AST2200)
			dram_reg_info = ast2100_dram_table_data;
		else
			dram_reg_info = ast1100_dram_table_data;

		ast_moutdwm_poll(ast, AST_REG_SCU000, AST_REG_SCU000_PROTECTION_KEY, 0x01);
		ast_moutdwm_poll(ast, AST_REG_MCR00, AST_REG_MCR00_PROTECTION_KEY, 0x01);

		while (!AST_DRAMSTRUCT_IS(dram_reg_info, INVALID)) {
			if (AST_DRAMSTRUCT_IS(dram_reg_info, UDELAY)) {
				for (i = 0; i < 15; i++)
					udelay(dram_reg_info->data);
			} else if (AST_DRAMSTRUCT_IS_REG(dram_reg_info, AST_REG_MCR04)) {
				u32 mcr04;
				u32 scu070;

				switch (dram_layout) {
				case AST_DRAM_1Gx16:
					mcr04 = 0x00000d89;
					break;
				case AST_DRAM_1Gx32:
					mcr04 = 0x00000c8d;
					break;
				default:
					mcr04 = dram_reg_info->data;
					break;
				}

				/*
				 * FIXME: There might be bits already in MCR04[5:4]. Should
				 *        we only do this in the default case?
				 */
				scu070 = ast_mindwm(ast, AST_REG_SCU070);
				mcr04 |= (scu070 & GENMASK(3, 2)) << 2;

				ast_moutdwm(ast, dram_reg_info->index, mcr04);
			} else {
				ast_moutdwm(ast, dram_reg_info->index, dram_reg_info->data);
			}
			dram_reg_info++;
		}

		/* AST 2100/2150 DRAM calibration */
		mcr120 = ast_mindwm(ast, AST_REG_MCR120);
		if (mcr120 == 0x5061) { /* 266Mhz */
			u32 mcr04 = ast_mindwm(ast, AST_REG_MCR04);

			if (mcr04 & 0x40)
				cbrdlli_ast2150(ast, 16); /* 16 bits */
			else
				cbrdlli_ast2150(ast, 32); /* 32 bits */
		}

		scu00c = ast_mindwm(ast, AST_REG_SCU00C);
		scu00c &= 0xfffffffd;
		ast_moutdwm(ast, AST_REG_SCU00C, scu00c);

		scu040 = ast_mindwm(ast, AST_REG_SCU040);
		scu040 |= 0x00000040;
		ast_moutdwm(ast, AST_REG_SCU040, scu040);
	}

	/* wait ready */
	do {
		j = ast_get_index_reg_mask(ast, AST_IO_VGACRI, 0xd0, 0xff);
	} while ((j & 0x40) == 0);
}

int ast_2100_post(struct ast_device *ast)
{
	ast_2000_set_def_ext_reg(ast);

	if (ast->config_mode == ast_use_p2a) {
		ast_post_chip_2100(ast);
	} else {
		if (ast->tx_chip == AST_TX_SIL164) {
			/* Enable DVO */
			ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xa3, 0xcf, 0x80);
		}
	}

	return 0;
}

/*
 * Widescreen detection
 */

/* Try to detect WSXGA+ on Gen2+ */
bool __ast_2100_detect_wsxga_p(struct ast_device *ast)
{
	u8 vgacrd0 = ast_get_index_reg(ast, AST_IO_VGACRI, 0xd0);

	if (!(vgacrd0 & AST_IO_VGACRD0_VRAM_INIT_BY_BMC))
		return true;
	if (vgacrd0 & AST_IO_VGACRD0_IKVM_WIDESCREEN)
		return true;

	return false;
}

/* Try to detect WUXGA on Gen2+ */
bool __ast_2100_detect_wuxga(struct ast_device *ast)
{
	u8 vgacrd1;

	if (ast->support_fullhd) {
		vgacrd1 = ast_get_index_reg(ast, AST_IO_VGACRI, 0xd1);
		if (!(vgacrd1 & AST_IO_VGACRD1_SUPPORTS_WUXGA))
			return true;
	}

	return false;
}

static void ast_2100_detect_widescreen(struct ast_device *ast)
{
	if (__ast_2100_detect_wsxga_p(ast)) {
		ast->support_wsxga_p = true;
		if (ast->chip == AST2100)
			ast->support_fullhd = true;
	}
	if (__ast_2100_detect_wuxga(ast))
		ast->support_wuxga = true;
}

static const struct ast_device_quirks ast_2100_device_quirks = {
	.crtc_mem_req_threshold_low = 47,
	.crtc_mem_req_threshold_high = 63,
};

struct drm_device *ast_2100_device_create(struct pci_dev *pdev,
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

	ast_device_init(ast, chip, config_mode, regs, ioregs, &ast_2100_device_quirks);

	ast->dclk_table = ast_2000_dclk_table;

	ast_2000_detect_tx_chip(ast, need_post);

	if (need_post) {
		ret = ast_post_gpu(ast);
		if (ret)
			return ERR_PTR(ret);
	}

	ret = ast_mm_init(ast);
	if (ret)
		return ERR_PTR(ret);

	ast_2100_detect_widescreen(ast);

	ret = ast_mode_config_init(ast);
	if (ret)
		return ERR_PTR(ret);

	return dev;
}
