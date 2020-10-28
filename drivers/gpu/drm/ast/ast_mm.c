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

#include <drm/drm_gem_vram_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_print.h>

#include "ast_drv.h"

static u32 ast_get_vram_size(struct ast_private *ast)
{
	u8 jreg;
	u32 vram_size;

	ast_open_key(ast);

	vram_size = AST_VIDMEM_DEFAULT_SIZE;
	jreg = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xaa, 0xff);
	switch (jreg & 3) {
	case 0:
		vram_size = AST_VIDMEM_SIZE_8M;
		break;
	case 1:
		vram_size = AST_VIDMEM_SIZE_16M;
		break;
	case 2:
		vram_size = AST_VIDMEM_SIZE_32M;
		break;
	case 3:
		vram_size = AST_VIDMEM_SIZE_64M;
		break;
	}

	jreg = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0x99, 0xff);
	switch (jreg & 0x03) {
	case 1:
		vram_size -= 0x100000;
		break;
	case 2:
		vram_size -= 0x200000;
		break;
	case 3:
		vram_size -= 0x400000;
		break;
	}

	return vram_size;
}

static void ast_mm_release(struct drm_device *dev, void *ptr)
{
	struct ast_private *ast = to_ast_private(dev);

	arch_phys_wc_del(ast->fb_mtrr);
	arch_io_free_memtype_wc(pci_resource_start(dev->pdev, 0),
				pci_resource_len(dev->pdev, 0));
}

int ast_mm_init(struct ast_private *ast)
{
	struct drm_device *dev = &ast->base;
	u32 vram_size;
	int ret;

	vram_size = ast_get_vram_size(ast);

	ret = drmm_vram_helper_init(dev, pci_resource_start(dev->pdev, 0),
				    vram_size);
	if (ret) {
		drm_err(dev, "Error initializing VRAM MM; %d\n", ret);
		return ret;
	}

	arch_io_reserve_memtype_wc(pci_resource_start(dev->pdev, 0),
				   pci_resource_len(dev->pdev, 0));
	ast->fb_mtrr = arch_phys_wc_add(pci_resource_start(dev->pdev, 0),
					pci_resource_len(dev->pdev, 0));

	return drmm_add_action_or_reset(dev, ast_mm_release, NULL);
}
