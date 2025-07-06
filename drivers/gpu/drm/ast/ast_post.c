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

#include <linux/delay.h>
#include <linux/pci.h>

#include <drm/drm_print.h>

#include "ast_dram_tables.h"
#include "ast_drv.h"
#include "ast_post.h"

static const u8 extreginfo[] = { 0x0f, 0x04, 0x1c, 0xff };
static const u8 extreginfo_ast2300[] = { 0x0f, 0x04, 0x1f, 0xff };

static void ast_set_def_ext_reg(struct ast_device *ast)
{
	u8 i, index, reg;
	const u8 *ext_reg_info;

	/* reset scratch */
	for (i = 0x81; i <= 0x9f; i++)
		ast_set_index_reg(ast, AST_IO_VGACRI, i, 0x00);

	if (IS_AST_GEN4(ast) || IS_AST_GEN5(ast) || IS_AST_GEN6(ast))
		ext_reg_info = extreginfo_ast2300;
	else
		ext_reg_info = extreginfo;

	index = 0xa0;
	while (*ext_reg_info != 0xff) {
		ast_set_index_reg_mask(ast, AST_IO_VGACRI, index, 0x00, *ext_reg_info);
		index++;
		ext_reg_info++;
	}

	/* disable standard IO/MEM decode if secondary */
	/* ast_set_index_reg-mask(ast, AST_IO_VGACRI, 0xa1, 0xff, 0x3); */

	/* Set Ext. Default */
	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0x8c, 0x00, 0x01);
	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xb7, 0x00, 0x00);

	/* Enable RAMDAC for A1 */
	reg = 0x04;
	if (IS_AST_GEN4(ast) || IS_AST_GEN5(ast) || IS_AST_GEN6(ast))
		reg |= 0x20;
	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xb6, 0xff, reg);
}

u32 __ast_mindwm(void __iomem *regs, u32 r)
{
	u32 data;

	__ast_write32(regs, 0xf004, r & 0xffff0000);
	__ast_write32(regs, 0xf000, 0x1);

	do {
		data = __ast_read32(regs, 0xf004) & 0xffff0000;
	} while (data != (r & 0xffff0000));

	return __ast_read32(regs, 0x10000 + (r & 0x0000ffff));
}

void __ast_moutdwm(void __iomem *regs, u32 r, u32 v)
{
	u32 data;

	__ast_write32(regs, 0xf004, r & 0xffff0000);
	__ast_write32(regs, 0xf000, 0x1);

	do {
		data = __ast_read32(regs, 0xf004) & 0xffff0000;
	} while (data != (r & 0xffff0000));

	__ast_write32(regs, 0x10000 + (r & 0x0000ffff), v);
}

u32 ast_mindwm(struct ast_device *ast, u32 r)
{
	return __ast_mindwm(ast->regs, r);
}

void ast_moutdwm(struct ast_device *ast, u32 r, u32 v)
{
	__ast_moutdwm(ast->regs, r, v);
}

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

#if 0 /* unused in DDX driver - here for completeness */
static u32 mmctestsingle2_ast2150(struct ast_device *ast, u32 datagen)
{
	u32 data, timeout;

	ast_moutdwm(ast, 0x1e6e0070, 0x00000000);
	ast_moutdwm(ast, 0x1e6e0070, 0x00000005 | (datagen << 3));
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
#endif

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
	dll_min[0] = dll_min[1] = dll_min[2] = dll_min[3] = 0xff;
	dll_max[0] = dll_max[1] = dll_max[2] = dll_max[3] = 0x0;
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
		} else if (passcnt >= CBR_THRESHOLD_AST2150)
			goto cbr_start;
	}
	if (dll_max[0] == 0 || (dll_max[0]-dll_min[0]) < CBR_THRESHOLD_AST2150)
		goto cbr_start;

	dlli = dll_min[0] + (((dll_max[0] - dll_min[0]) * 7) >> 4);
	ast_moutdwm(ast, 0x1e6e0068, dlli | (dlli << 8) | (dlli << 16) | (dlli << 24));
}



static void ast_init_dram_reg(struct ast_device *ast)
{
	u8 j;
	u32 data, temp, i;
	const struct ast_dramstruct *dram_reg_info;

	j = ast_get_index_reg_mask(ast, AST_IO_VGACRI, 0xd0, 0xff);

	if ((j & 0x80) == 0) { /* VGA only */
		if (IS_AST_GEN1(ast)) {
			dram_reg_info = ast2000_dram_table_data;
			ast_write32(ast, 0xf004, 0x1e6e0000);
			ast_write32(ast, 0xf000, 0x1);
			ast_write32(ast, 0x10100, 0xa8);

			do {
				;
			} while (ast_read32(ast, 0x10100) != 0xa8);
		} else { /* GEN2/GEN3 */
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
		}

		while (dram_reg_info->index != 0xffff) {
			if (dram_reg_info->index == 0xff00) {/* delay fn */
				for (i = 0; i < 15; i++)
					udelay(dram_reg_info->data);
			} else if (dram_reg_info->index == 0x4 && !IS_AST_GEN1(ast)) {
				data = dram_reg_info->data;
				if (ast->dram_type == AST_DRAM_1Gx16)
					data = 0x00000d89;
				else if (ast->dram_type == AST_DRAM_1Gx32)
					data = 0x00000c8d;

				temp = ast_read32(ast, 0x12070);
				temp &= 0xc;
				temp <<= 2;
				ast_write32(ast, 0x10000 + dram_reg_info->index, data | temp);
			} else
				ast_write32(ast, 0x10000 + dram_reg_info->index, dram_reg_info->data);
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

		switch (AST_GEN(ast)) {
		case 1:
			temp = ast_read32(ast, 0x10140);
			ast_write32(ast, 0x10140, temp | 0x40);
			break;
		case 2:
		case 3:
			temp = ast_read32(ast, 0x1200c);
			ast_write32(ast, 0x1200c, temp & 0xfffffffd);
			temp = ast_read32(ast, 0x12040);
			ast_write32(ast, 0x12040, temp | 0x40);
			break;
		default:
			break;
		}
	}

	/* wait ready */
	do {
		j = ast_get_index_reg_mask(ast, AST_IO_VGACRI, 0xd0, 0xff);
	} while ((j & 0x40) == 0);
}

int ast_post_gpu(struct ast_device *ast)
{
	int ret;

	ast_set_def_ext_reg(ast);

	if (AST_GEN(ast) >= 7) {
		ret = ast_2600_post(ast);
		if (ret)
			return ret;
	} else if (AST_GEN(ast) >= 6) {
		ret = ast_2500_post(ast);
		if (ret)
			return ret;
	} else if (AST_GEN(ast) >= 4) {
		ret = ast_2300_post(ast);
		if (ret)
			return ret;
	} else  {
		if (ast->config_mode == ast_use_p2a) {
			ast_init_dram_reg(ast);
		} else {
			if (ast->tx_chip == AST_TX_SIL164) {
				/* Enable DVO */
				ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xa3, 0xcf, 0x80);
			}
		}
	}

	return 0;
}

#define TIMEOUT              5000000

bool mmc_test(struct ast_device *ast, u32 datagen, u8 test_ctl)
{
	u32 data, timeout;

	ast_moutdwm(ast, 0x1e6e0070, 0x00000000);
	ast_moutdwm(ast, 0x1e6e0070, (datagen << 3) | test_ctl);
	timeout = 0;
	do {
		data = ast_mindwm(ast, 0x1e6e0070) & 0x3000;
		if (data & 0x2000)
			return false;
		if (++timeout > TIMEOUT) {
			ast_moutdwm(ast, 0x1e6e0070, 0x00000000);
			return false;
		}
	} while (!data);
	ast_moutdwm(ast, 0x1e6e0070, 0x0);
	return true;
}

bool mmc_test_burst(struct ast_device *ast, u32 datagen)
{
	return mmc_test(ast, datagen, 0xc1);
}
