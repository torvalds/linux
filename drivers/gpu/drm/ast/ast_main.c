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

	if (AST_GEN(ast) >= 4)
		ast_2300_detect_tx_chip(ast);
	else
		ast_2000_detect_tx_chip(ast, need_post);

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
