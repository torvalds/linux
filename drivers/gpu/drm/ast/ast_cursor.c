/*
 * Copyright 2012 Red Hat Inc.
 * Parts based on xf86-video-ast
 * Copyright (c) 2005 ASPEED Technology Inc.
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

#include <drm/drm_gem_vram_helper.h>

#include "ast_drv.h"

/*
 * Allocate cursor BOs and pins them at the end of VRAM.
 */
int ast_cursor_init(struct drm_device *dev)
{
	struct ast_private *ast = to_ast_private(dev);
	size_t size, i;
	struct drm_gem_vram_object *gbo;
	int ret;

	size = roundup(AST_HWC_SIZE + AST_HWC_SIGNATURE_SIZE, PAGE_SIZE);

	for (i = 0; i < ARRAY_SIZE(ast->cursor.gbo); ++i) {
		gbo = drm_gem_vram_create(dev, size, 0);
		if (IS_ERR(gbo)) {
			ret = PTR_ERR(gbo);
			goto err_drm_gem_vram_put;
		}
		ret = drm_gem_vram_pin(gbo, DRM_GEM_VRAM_PL_FLAG_VRAM |
					    DRM_GEM_VRAM_PL_FLAG_TOPDOWN);
		if (ret) {
			drm_gem_vram_put(gbo);
			goto err_drm_gem_vram_put;
		}

		ast->cursor.gbo[i] = gbo;
	}

	return 0;

err_drm_gem_vram_put:
	while (i) {
		--i;
		gbo = ast->cursor.gbo[i];
		drm_gem_vram_unpin(gbo);
		drm_gem_vram_put(gbo);
		ast->cursor.gbo[i] = NULL;
	}
	return ret;
}

void ast_cursor_fini(struct drm_device *dev)
{
	struct ast_private *ast = to_ast_private(dev);
	size_t i;
	struct drm_gem_vram_object *gbo;

	for (i = 0; i < ARRAY_SIZE(ast->cursor.gbo); ++i) {
		gbo = ast->cursor.gbo[i];
		drm_gem_vram_unpin(gbo);
		drm_gem_vram_put(gbo);
	}
}

static u32 copy_cursor_image(u8 *src, u8 *dst, int width, int height)
{
	union {
		u32 ul;
		u8 b[4];
	} srcdata32[2], data32;
	union {
		u16 us;
		u8 b[2];
	} data16;
	u32 csum = 0;
	s32 alpha_dst_delta, last_alpha_dst_delta;
	u8 *srcxor, *dstxor;
	int i, j;
	u32 per_pixel_copy, two_pixel_copy;

	alpha_dst_delta = AST_MAX_HWC_WIDTH << 1;
	last_alpha_dst_delta = alpha_dst_delta - (width << 1);

	srcxor = src;
	dstxor = (u8 *)dst + last_alpha_dst_delta + (AST_MAX_HWC_HEIGHT - height) * alpha_dst_delta;
	per_pixel_copy = width & 1;
	two_pixel_copy = width >> 1;

	for (j = 0; j < height; j++) {
		for (i = 0; i < two_pixel_copy; i++) {
			srcdata32[0].ul = *((u32 *)srcxor) & 0xf0f0f0f0;
			srcdata32[1].ul = *((u32 *)(srcxor + 4)) & 0xf0f0f0f0;
			data32.b[0] = srcdata32[0].b[1] | (srcdata32[0].b[0] >> 4);
			data32.b[1] = srcdata32[0].b[3] | (srcdata32[0].b[2] >> 4);
			data32.b[2] = srcdata32[1].b[1] | (srcdata32[1].b[0] >> 4);
			data32.b[3] = srcdata32[1].b[3] | (srcdata32[1].b[2] >> 4);

			writel(data32.ul, dstxor);
			csum += data32.ul;

			dstxor += 4;
			srcxor += 8;

		}

		for (i = 0; i < per_pixel_copy; i++) {
			srcdata32[0].ul = *((u32 *)srcxor) & 0xf0f0f0f0;
			data16.b[0] = srcdata32[0].b[1] | (srcdata32[0].b[0] >> 4);
			data16.b[1] = srcdata32[0].b[3] | (srcdata32[0].b[2] >> 4);
			writew(data16.us, dstxor);
			csum += (u32)data16.us;

			dstxor += 2;
			srcxor += 4;
		}
		dstxor += last_alpha_dst_delta;
	}
	return csum;
}

int ast_cursor_update(void *dst, void *src, unsigned int width,
		      unsigned int height)
{
	u32 csum;

	/* do data transfer to cursor cache */
	csum = copy_cursor_image(src, dst, width, height);

	/* write checksum + signature */
	dst += AST_HWC_SIZE;
	writel(csum, dst);
	writel(width, dst + AST_HWC_SIGNATURE_SizeX);
	writel(height, dst + AST_HWC_SIGNATURE_SizeY);
	writel(0, dst + AST_HWC_SIGNATURE_HOTSPOTX);
	writel(0, dst + AST_HWC_SIGNATURE_HOTSPOTY);

	return 0;
}

void ast_cursor_set_base(struct ast_private *ast, u64 address)
{
	u8 addr0 = (address >> 3) & 0xff;
	u8 addr1 = (address >> 11) & 0xff;
	u8 addr2 = (address >> 19) & 0xff;

	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xc8, addr0);
	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xc9, addr1);
	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xca, addr2);
}

int ast_cursor_move(struct drm_crtc *crtc, int x, int y)
{
	struct ast_crtc *ast_crtc = to_ast_crtc(crtc);
	struct ast_private *ast = to_ast_private(crtc->dev);
	struct drm_gem_vram_object *gbo;
	int x_offset, y_offset;
	u8 *dst, *sig;
	u8 jreg;

	gbo = ast->cursor.gbo[ast->cursor.next_index];
	dst = drm_gem_vram_vmap(gbo);
	if (IS_ERR(dst))
		return PTR_ERR(dst);

	sig = dst + AST_HWC_SIZE;
	writel(x, sig + AST_HWC_SIGNATURE_X);
	writel(y, sig + AST_HWC_SIGNATURE_Y);

	x_offset = ast_crtc->offset_x;
	y_offset = ast_crtc->offset_y;
	if (x < 0) {
		x_offset = (-x) + ast_crtc->offset_x;
		x = 0;
	}

	if (y < 0) {
		y_offset = (-y) + ast_crtc->offset_y;
		y = 0;
	}
	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xc2, x_offset);
	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xc3, y_offset);
	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xc4, (x & 0xff));
	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xc5, ((x >> 8) & 0x0f));
	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xc6, (y & 0xff));
	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xc7, ((y >> 8) & 0x07));

	/* dummy write to fire HWC */
	jreg = 0x02 |
	       0x01; /* enable ARGB4444 cursor */
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xcb, 0xfc, jreg);

	drm_gem_vram_vunmap(gbo, dst);

	return 0;
}
