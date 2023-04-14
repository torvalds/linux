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

#include "ast_drv.h"

void ast_post_chip_2500(struct drm_device *dev);

void ast_enable_vga(struct drm_device *dev)
{
	struct ast_private *ast = to_ast_private(dev);
	u8 ch;

	ast_io_write8(ast, AST_IO_VGA_ENABLE_PORT, 0x01);
	ch = ast_io_read8(ast, AST_IO_MISC_PORT_READ);
	ast_io_write8(ast, AST_IO_MISC_PORT_WRITE, ch | 0x01);
}

void ast_disable_vga(struct drm_device *dev)
{
	struct ast_private *ast = to_ast_private(dev);

	ast_io_write8(ast, AST_IO_VGA_ENABLE_PORT, 0x0);
}

void ast_enable_mmio(struct drm_device *dev)
{
	struct ast_private *ast = to_ast_private(dev);

	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xa1, 0x06);
}


bool ast_is_vga_enabled(struct drm_device *dev)
{
	struct ast_private *ast = to_ast_private(dev);
	u8 ch;

	ch = ast_io_read8(ast, AST_IO_VGA_ENABLE_PORT);

	return !!(ch & 0x01);
}

static const u8 extreginfo[] = { 0x0f, 0x04, 0x1c, 0xff };
static const u8 extreginfo_ast2300a0[] = { 0x0f, 0x04, 0x1c, 0xff };
static const u8 extreginfo_ast2300[] = { 0x0f, 0x04, 0x1f, 0xff };

static void
ast_set_def_ext_reg(struct drm_device *dev)
{
	struct ast_private *ast = to_ast_private(dev);
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	u8 i, index, reg;
	const u8 *ext_reg_info;

	/* reset scratch */
	for (i = 0x81; i <= 0x9f; i++)
		ast_set_index_reg(ast, AST_IO_CRTC_PORT, i, 0x00);

	if (ast->chip == AST2300 || ast->chip == AST2400 ||
	    ast->chip == AST2500 || ast->chip == AST2600) {
		if (pdev->revision >= 0x20)
			ext_reg_info = extreginfo_ast2300;
		else
			ext_reg_info = extreginfo_ast2300a0;
	} else
		ext_reg_info = extreginfo;

	index = 0xa0;
	while (*ext_reg_info != 0xff) {
		ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, index, 0x00, *ext_reg_info);
		index++;
		ext_reg_info++;
	}

	/* disable standard IO/MEM decode if secondary */
	/* ast_set_index_reg-mask(ast, AST_IO_CRTC_PORT, 0xa1, 0xff, 0x3); */

	/* Set Ext. Default */
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0x8c, 0x00, 0x01);
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xb7, 0x00, 0x00);

	/* Enable RAMDAC for A1 */
	reg = 0x04;
	if (ast->chip == AST2300 || ast->chip == AST2400 ||
	    ast->chip == AST2500 || ast->chip == AST2600)
		reg |= 0x20;
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xb6, 0xff, reg);
}

void ast_post_gpu(struct drm_device *dev)
{
	struct ast_private *ast = to_ast_private(dev);
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	u32 reg;

	pci_read_config_dword(pdev, 0x04, &reg);
	reg |= 0x3;
	pci_write_config_dword(pdev, 0x04, reg);

	ast_enable_vga(dev);
	ast_open_key(ast);
	ast_enable_mmio(dev);
	ast_set_def_ext_reg(dev);

	if (ast->chip == AST2600) {
		if (ast->tx_chip_type == AST_TX_ASTDP)
			ast_dp_launch(dev);
	} else if (ast->config_mode == ast_use_p2a) {
		if (ast->chip == AST2500)
			ast_post_chip_2500(dev);

		ast_init_3rdtx(dev);
	} else {
		if (ast->tx_chip_type != AST_TX_NONE)
			ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xa3, 0xcf, 0x80);	/* Enable DVO */
	}
}
