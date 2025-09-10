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
 * POST
 */

void ast_2000_set_def_ext_reg(struct ast_device *ast)
{
	static const u8 extreginfo[] = { 0x0f, 0x04, 0x1c, 0xff };
	u8 i, index, reg;
	const u8 *ext_reg_info;

	/* reset scratch */
	for (i = 0x81; i <= 0x9f; i++)
		ast_set_index_reg(ast, AST_IO_VGACRI, i, 0x00);

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
	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xb6, 0xff, reg);
}

static const struct ast_dramstruct ast2000_dram_table_data[] = {
	{ 0x0108, 0x00000000 },
	{ 0x0120, 0x00004a21 },
	AST_DRAMSTRUCT_UDELAY(67u),
	{ 0x0000, 0xFFFFFFFF },
	AST_DRAMSTRUCT_INIT(DRAM_TYPE, 0x00000089),
	{ 0x0008, 0x22331353 },
	{ 0x000C, 0x0d07000b },
	{ 0x0010, 0x11113333 },
	{ 0x0020, 0x00110350 },
	{ 0x0028, 0x1e0828f0 },
	{ 0x0024, 0x00000001 },
	{ 0x001C, 0x00000000 },
	{ 0x0014, 0x00000003 },
	AST_DRAMSTRUCT_UDELAY(67u),
	{ 0x0018, 0x00000131 },
	{ 0x0014, 0x00000001 },
	AST_DRAMSTRUCT_UDELAY(67u),
	{ 0x0018, 0x00000031 },
	{ 0x0014, 0x00000001 },
	AST_DRAMSTRUCT_UDELAY(67u),
	{ 0x0028, 0x1e0828f1 },
	{ 0x0024, 0x00000003 },
	{ 0x002C, 0x1f0f28fb },
	{ 0x0030, 0xFFFFFE01 },
	AST_DRAMSTRUCT_INVALID,
};

static void ast_post_chip_2000(struct ast_device *ast)
{
	u8 j;
	u32 temp, i;
	const struct ast_dramstruct *dram_reg_info;

	j = ast_get_index_reg_mask(ast, AST_IO_VGACRI, 0xd0, 0xff);

	if ((j & 0x80) == 0) { /* VGA only */
		dram_reg_info = ast2000_dram_table_data;
		ast_write32(ast, 0xf004, 0x1e6e0000);
		ast_write32(ast, 0xf000, 0x1);
		ast_write32(ast, 0x10100, 0xa8);

		do {
			;
		} while (ast_read32(ast, 0x10100) != 0xa8);

		while (!AST_DRAMSTRUCT_IS(dram_reg_info, INVALID)) {
			if (AST_DRAMSTRUCT_IS(dram_reg_info, UDELAY)) {
				for (i = 0; i < 15; i++)
					udelay(dram_reg_info->data);
			} else {
				ast_write32(ast, 0x10000 + dram_reg_info->index,
					    dram_reg_info->data);
			}
			dram_reg_info++;
		}

		temp = ast_read32(ast, 0x10140);
		ast_write32(ast, 0x10140, temp | 0x40);
	}

	/* wait ready */
	do {
		j = ast_get_index_reg_mask(ast, AST_IO_VGACRI, 0xd0, 0xff);
	} while ((j & 0x40) == 0);
}

int ast_2000_post(struct ast_device *ast)
{
	ast_2000_set_def_ext_reg(ast);

	if (ast->config_mode == ast_use_p2a) {
		ast_post_chip_2000(ast);
	} else {
		if (ast->tx_chip == AST_TX_SIL164) {
			/* Enable DVO */
			ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xa3, 0xcf, 0x80);
		}
	}

	return 0;
}
