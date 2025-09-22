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

#include "ast_drv.h"
#include "ast_post.h"

/*
 * DRAM type
 */

static enum ast_dram_layout ast_2100_get_dram_layout_p2a(struct ast_device *ast)
{
	u32 mcr_cfg;
	enum ast_dram_layout dram_layout;

	ast_write32(ast, 0xf004, 0x1e6e0000);
	ast_write32(ast, 0xf000, 0x1);
	mcr_cfg = ast_read32(ast, 0x10004);

	switch (mcr_cfg & 0x0c) {
	case 0:
	case 4:
	default:
		dram_layout = AST_DRAM_512Mx16;
		break;
	case 8:
		if (mcr_cfg & 0x40)
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
	{ 0x2000, 0x1688a8a8 },
	{ 0x2020, 0x000041f0 },
	AST_DRAMSTRUCT_UDELAY(67u),
	{ 0x0000, 0xfc600309 },
	{ 0x006C, 0x00909090 },
	{ 0x0064, 0x00050000 },
	AST_DRAMSTRUCT_INIT(DRAM_TYPE, 0x00000585),
	{ 0x0008, 0x0011030f },
	{ 0x0010, 0x22201724 },
	{ 0x0018, 0x1e29011a },
	{ 0x0020, 0x00c82222 },
	{ 0x0014, 0x01001523 },
	{ 0x001C, 0x1024010d },
	{ 0x0024, 0x00cb2522 },
	{ 0x0038, 0xffffff82 },
	{ 0x003C, 0x00000000 },
	{ 0x0040, 0x00000000 },
	{ 0x0044, 0x00000000 },
	{ 0x0048, 0x00000000 },
	{ 0x004C, 0x00000000 },
	{ 0x0050, 0x00000000 },
	{ 0x0054, 0x00000000 },
	{ 0x0058, 0x00000000 },
	{ 0x005C, 0x00000000 },
	{ 0x0060, 0x032aa02a },
	{ 0x0064, 0x002d3000 },
	{ 0x0068, 0x00000000 },
	{ 0x0070, 0x00000000 },
	{ 0x0074, 0x00000000 },
	{ 0x0078, 0x00000000 },
	{ 0x007C, 0x00000000 },
	{ 0x0034, 0x00000001 },
	AST_DRAMSTRUCT_UDELAY(67u),
	{ 0x002C, 0x00000732 },
	{ 0x0030, 0x00000040 },
	{ 0x0028, 0x00000005 },
	{ 0x0028, 0x00000007 },
	{ 0x0028, 0x00000003 },
	{ 0x0028, 0x00000001 },
	{ 0x000C, 0x00005a08 },
	{ 0x002C, 0x00000632 },
	{ 0x0028, 0x00000001 },
	{ 0x0030, 0x000003c0 },
	{ 0x0028, 0x00000003 },
	{ 0x0030, 0x00000040 },
	{ 0x0028, 0x00000003 },
	{ 0x000C, 0x00005a21 },
	{ 0x0034, 0x00007c03 },
	{ 0x0120, 0x00004c41 },
	AST_DRAMSTRUCT_INVALID,
};

static const struct ast_dramstruct ast2100_dram_table_data[] = {
	{ 0x2000, 0x1688a8a8 },
	{ 0x2020, 0x00004120 },
	AST_DRAMSTRUCT_UDELAY(67u),
	{ 0x0000, 0xfc600309 },
	{ 0x006C, 0x00909090 },
	{ 0x0064, 0x00070000 },
	AST_DRAMSTRUCT_INIT(DRAM_TYPE, 0x00000489),
	{ 0x0008, 0x0011030f },
	{ 0x0010, 0x32302926 },
	{ 0x0018, 0x274c0122 },
	{ 0x0020, 0x00ce2222 },
	{ 0x0014, 0x01001523 },
	{ 0x001C, 0x1024010d },
	{ 0x0024, 0x00cb2522 },
	{ 0x0038, 0xffffff82 },
	{ 0x003C, 0x00000000 },
	{ 0x0040, 0x00000000 },
	{ 0x0044, 0x00000000 },
	{ 0x0048, 0x00000000 },
	{ 0x004C, 0x00000000 },
	{ 0x0050, 0x00000000 },
	{ 0x0054, 0x00000000 },
	{ 0x0058, 0x00000000 },
	{ 0x005C, 0x00000000 },
	{ 0x0060, 0x0f2aa02a },
	{ 0x0064, 0x003f3005 },
	{ 0x0068, 0x02020202 },
	{ 0x0070, 0x00000000 },
	{ 0x0074, 0x00000000 },
	{ 0x0078, 0x00000000 },
	{ 0x007C, 0x00000000 },
	{ 0x0034, 0x00000001 },
	AST_DRAMSTRUCT_UDELAY(67u),
	{ 0x002C, 0x00000942 },
	{ 0x0030, 0x00000040 },
	{ 0x0028, 0x00000005 },
	{ 0x0028, 0x00000007 },
	{ 0x0028, 0x00000003 },
	{ 0x0028, 0x00000001 },
	{ 0x000C, 0x00005a08 },
	{ 0x002C, 0x00000842 },
	{ 0x0028, 0x00000001 },
	{ 0x0030, 0x000003c0 },
	{ 0x0028, 0x00000003 },
	{ 0x0030, 0x00000040 },
	{ 0x0028, 0x00000003 },
	{ 0x000C, 0x00005a21 },
	{ 0x0034, 0x00007c03 },
	{ 0x0120, 0x00005061 },
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

	ast_moutdwm(ast, 0x1e6e0070, 0x00000000);
	ast_moutdwm(ast, 0x1e6e0070, 0x00000001 | (datagen << 3));
	timeout = 0;
	do {
		data = ast_mindwm(ast, 0x1e6e0070) & 0x40;
		if (++timeout > TIMEOUT_AST2150) {
			ast_moutdwm(ast, 0x1e6e0070, 0x00000000);
			return 0xffffffff;
		}
	} while (!data);
	ast_moutdwm(ast, 0x1e6e0070, 0x00000000);
	ast_moutdwm(ast, 0x1e6e0070, 0x00000003 | (datagen << 3));
	timeout = 0;
	do {
		data = ast_mindwm(ast, 0x1e6e0070) & 0x40;
		if (++timeout > TIMEOUT_AST2150) {
			ast_moutdwm(ast, 0x1e6e0070, 0x00000000);
			return 0xffffffff;
		}
	} while (!data);
	data = (ast_mindwm(ast, 0x1e6e0070) & 0x80) >> 7;
	ast_moutdwm(ast, 0x1e6e0070, 0x00000000);
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
		ast_moutdwm(ast, 0x1e6e007c, pattern_AST2150[patcnt]);
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
		ast_moutdwm(ast, 0x1e6e0068, dlli | (dlli << 8) | (dlli << 16) | (dlli << 24));
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
	ast_moutdwm(ast, 0x1e6e0068, dlli | (dlli << 8) | (dlli << 16) | (dlli << 24));
}

static void ast_post_chip_2100(struct ast_device *ast)
{
	u8 j;
	u32 data, temp, i;
	const struct ast_dramstruct *dram_reg_info;
	enum ast_dram_layout dram_layout  = ast_2100_get_dram_layout_p2a(ast);

	j = ast_get_index_reg_mask(ast, AST_IO_VGACRI, 0xd0, 0xff);

	if ((j & 0x80) == 0) { /* VGA only */
		if (ast->chip == AST2100 || ast->chip == AST2200)
			dram_reg_info = ast2100_dram_table_data;
		else
			dram_reg_info = ast1100_dram_table_data;

		ast_write32(ast, 0xf004, 0x1e6e0000);
		ast_write32(ast, 0xf000, 0x1);
		ast_write32(ast, 0x12000, 0x1688A8A8);
		do {
			;
		} while (ast_read32(ast, 0x12000) != 0x01);

		ast_write32(ast, 0x10000, 0xfc600309);
		do {
			;
		} while (ast_read32(ast, 0x10000) != 0x01);

		while (!AST_DRAMSTRUCT_IS(dram_reg_info, INVALID)) {
			if (AST_DRAMSTRUCT_IS(dram_reg_info, UDELAY)) {
				for (i = 0; i < 15; i++)
					udelay(dram_reg_info->data);
			} else if (AST_DRAMSTRUCT_IS(dram_reg_info, DRAM_TYPE)) {
				switch (dram_layout) {
				case AST_DRAM_1Gx16:
					data = 0x00000d89;
					break;
				case AST_DRAM_1Gx32:
					data = 0x00000c8d;
					break;
				default:
					data = dram_reg_info->data;
					break;
				}

				temp = ast_read32(ast, 0x12070);
				temp &= 0xc;
				temp <<= 2;
				ast_write32(ast, 0x10000 + dram_reg_info->index, data | temp);
			} else {
				ast_write32(ast, 0x10000 + dram_reg_info->index,
					    dram_reg_info->data);
			}
			dram_reg_info++;
		}

		/* AST 2100/2150 DRAM calibration */
		data = ast_read32(ast, 0x10120);
		if (data == 0x5061) { /* 266Mhz */
			data = ast_read32(ast, 0x10004);
			if (data & 0x40)
				cbrdlli_ast2150(ast, 16); /* 16 bits */
			else
				cbrdlli_ast2150(ast, 32); /* 32 bits */
		}

		temp = ast_read32(ast, 0x1200c);
		ast_write32(ast, 0x1200c, temp & 0xfffffffd);
		temp = ast_read32(ast, 0x12040);
		ast_write32(ast, 0x12040, temp | 0x40);
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
