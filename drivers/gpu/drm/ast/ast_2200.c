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

#include <linux/pci.h>

#include <drm/drm_drv.h>

#include "ast_drv.h"

static void ast_2200_detect_widescreen(struct ast_device *ast)
{
	if (__ast_2100_detect_wsxga_p(ast)) {
		ast->support_wsxga_p = true;
		if (ast->chip == AST2200)
			ast->support_fullhd = true;
	}
	if (__ast_2100_detect_wuxga(ast))
		ast->support_wuxga = true;
}

static const struct ast_device_quirks ast_2200_device_quirks = {
	.crtc_mem_req_threshold_low = 47,
	.crtc_mem_req_threshold_high = 63,
};

struct drm_device *ast_2200_device_create(struct pci_dev *pdev,
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

	ast_device_init(ast, chip, config_mode, regs, ioregs, &ast_2200_device_quirks);

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

	ast_2200_detect_widescreen(ast);

	ret = ast_mode_config_init(ast);
	if (ret)
		return ERR_PTR(ret);

	return dev;
}

