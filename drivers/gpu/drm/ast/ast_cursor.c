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
#include <drm/drm_managed.h>

#include "ast_drv.h"

static void ast_cursor_fini(struct ast_private *ast)
{
	size_t i;
	struct drm_gem_vram_object *gbo;

	for (i = 0; i < ARRAY_SIZE(ast->cursor.gbo); ++i) {
		gbo = ast->cursor.gbo[i];
		drm_gem_vram_unpin(gbo);
		drm_gem_vram_put(gbo);
	}
}

static void ast_cursor_release(struct drm_device *dev, void *ptr)
{
	struct ast_private *ast = to_ast_private(dev);

	ast_cursor_fini(ast);
}

/*
 * Allocate cursor BOs and pin them at the end of VRAM.
 */
int ast_cursor_init(struct ast_private *ast)
{
	struct drm_device *dev = &ast->base;
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

	return drmm_add_action_or_reset(dev, ast_cursor_release, NULL);

err_drm_gem_vram_put:
	while (i) {
		--i;
		gbo = ast->cursor.gbo[i];
		drm_gem_vram_unpin(gbo);
		drm_gem_vram_put(gbo);
	}
	return ret;
}

static void update_cursor_image(u8 __iomem *dst, const u8 *src, int width, int height)
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
	u8 __iomem *dstxor;
	const u8 *srcxor;
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

	/* write checksum + signature */
	dst += AST_HWC_SIZE;
	writel(csum, dst);
	writel(width, dst + AST_HWC_SIGNATURE_SizeX);
	writel(height, dst + AST_HWC_SIGNATURE_SizeY);
	writel(0, dst + AST_HWC_SIGNATURE_HOTSPOTX);
	writel(0, dst + AST_HWC_SIGNATURE_HOTSPOTY);
}

int ast_cursor_blit(struct ast_private *ast, struct drm_framebuffer *fb)
{
	struct drm_device *dev = &ast->base;
	struct drm_gem_vram_object *dst_gbo = ast->cursor.gbo[ast->cursor.next_index];
	struct drm_gem_vram_object *src_gbo = drm_gem_vram_of_gem(fb->obj[0]);
	struct dma_buf_map src_map, dst_map;
	void __iomem *dst;
	void *src;
	int ret;

	if (drm_WARN_ON_ONCE(dev, fb->width > AST_MAX_HWC_WIDTH) ||
	    drm_WARN_ON_ONCE(dev, fb->height > AST_MAX_HWC_HEIGHT))
		return -EINVAL;

	ret = drm_gem_vram_vmap(src_gbo, &src_map);
	if (ret)
		return ret;
	src = src_map.vaddr; /* TODO: Use mapping abstraction properly */

	ret = drm_gem_vram_vmap(dst_gbo, &dst_map);
	if (ret)
		goto err_drm_gem_vram_vunmap;
	dst = dst_map.vaddr_iomem; /* TODO: Use mapping abstraction properly */

	/* do data transfer to cursor BO */
	update_cursor_image(dst, src, fb->width, fb->height);

	drm_gem_vram_vunmap(dst_gbo, &dst_map);
	drm_gem_vram_vunmap(src_gbo, &src_map);

	return 0;

err_drm_gem_vram_vunmap:
	drm_gem_vram_vunmap(src_gbo, &src_map);
	return ret;
}

static void ast_cursor_set_base(struct ast_private *ast, u64 address)
{
	u8 addr0 = (address >> 3) & 0xff;
	u8 addr1 = (address >> 11) & 0xff;
	u8 addr2 = (address >> 19) & 0xff;

	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xc8, addr0);
	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xc9, addr1);
	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xca, addr2);
}

void ast_cursor_page_flip(struct ast_private *ast)
{
	struct drm_device *dev = &ast->base;
	struct drm_gem_vram_object *gbo;
	s64 off;

	gbo = ast->cursor.gbo[ast->cursor.next_index];

	off = drm_gem_vram_offset(gbo);
	if (drm_WARN_ON_ONCE(dev, off < 0))
		return; /* Bug: we didn't pin the cursor HW BO to VRAM. */

	ast_cursor_set_base(ast, off);

	++ast->cursor.next_index;
	ast->cursor.next_index %= ARRAY_SIZE(ast->cursor.gbo);
}

static void ast_cursor_set_location(struct ast_private *ast, u16 x, u16 y,
				    u8 x_offset, u8 y_offset)
{
	u8 x0 = (x & 0x00ff);
	u8 x1 = (x & 0x0f00) >> 8;
	u8 y0 = (y & 0x00ff);
	u8 y1 = (y & 0x0700) >> 8;

	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xc2, x_offset);
	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xc3, y_offset);
	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xc4, x0);
	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xc5, x1);
	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xc6, y0);
	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xc7, y1);
}

void ast_cursor_show(struct ast_private *ast, int x, int y,
		     unsigned int offset_x, unsigned int offset_y)
{
	struct drm_device *dev = &ast->base;
	struct drm_gem_vram_object *gbo = ast->cursor.gbo[ast->cursor.next_index];
	struct dma_buf_map map;
	u8 x_offset, y_offset;
	u8 __iomem *dst;
	u8 __iomem *sig;
	u8 jreg;
	int ret;

	ret = drm_gem_vram_vmap(gbo, &map);
	if (drm_WARN_ONCE(dev, ret, "drm_gem_vram_vmap() failed, ret=%d\n", ret))
		return;
	dst = map.vaddr_iomem; /* TODO: Use mapping abstraction properly */

	sig = dst + AST_HWC_SIZE;
	writel(x, sig + AST_HWC_SIGNATURE_X);
	writel(y, sig + AST_HWC_SIGNATURE_Y);

	drm_gem_vram_vunmap(gbo, &map);

	if (x < 0) {
		x_offset = (-x) + offset_x;
		x = 0;
	} else {
		x_offset = offset_x;
	}
	if (y < 0) {
		y_offset = (-y) + offset_y;
		y = 0;
	} else {
		y_offset = offset_y;
	}

	ast_cursor_set_location(ast, x, y, x_offset, y_offset);

	/* dummy write to fire HWC */
	jreg = 0x02 |
	       0x01; /* enable ARGB4444 cursor */
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xcb, 0xfc, jreg);
}

void ast_cursor_hide(struct ast_private *ast)
{
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xcb, 0xfc, 0x00);
}
