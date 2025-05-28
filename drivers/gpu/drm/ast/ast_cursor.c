// SPDX-License-Identifier: MIT
/*
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

#include <linux/bits.h>
#include <linux/sizes.h>

#include <drm/drm_atomic.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_format_helper.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_print.h>

#include "ast_drv.h"

/*
 * Hardware cursor
 */

/* define for signature structure */
#define AST_HWC_SIGNATURE_SIZE		SZ_32
#define AST_HWC_SIGNATURE_CHECKSUM	0x00
#define AST_HWC_SIGNATURE_SizeX		0x04
#define AST_HWC_SIGNATURE_SizeY		0x08
#define AST_HWC_SIGNATURE_X		0x0C
#define AST_HWC_SIGNATURE_Y		0x10
#define AST_HWC_SIGNATURE_HOTSPOTX	0x14
#define AST_HWC_SIGNATURE_HOTSPOTY	0x18

static unsigned long ast_cursor_vram_size(void)
{
	return AST_HWC_SIZE + AST_HWC_SIGNATURE_SIZE;
}

long ast_cursor_vram_offset(struct ast_device *ast)
{
	unsigned long size = ast_cursor_vram_size();

	if (size > ast->vram_size)
		return -EINVAL;

	return ALIGN_DOWN(ast->vram_size - size, SZ_8);
}

static u32 ast_cursor_calculate_checksum(const void *src, unsigned int width, unsigned int height)
{
	u32 csum = 0;
	unsigned int one_pixel_copy = width & BIT(0);
	unsigned int two_pixel_copy = width - one_pixel_copy;
	unsigned int trailing_bytes = (AST_MAX_HWC_WIDTH - width) * sizeof(u16);
	unsigned int x, y;

	for (y = 0; y < height; y++) {
		for (x = 0; x < two_pixel_copy; x += 2) {
			const u32 *src32 = (const u32 *)src;

			csum += *src32;
			src += SZ_4;
		}
		if (one_pixel_copy) {
			const u16 *src16 = (const u16 *)src;

			csum += *src16;
			src += SZ_2;
		}
		src += trailing_bytes;
	}

	return csum;
}

static void ast_set_cursor_image(struct ast_device *ast, const u8 *src,
				 unsigned int width, unsigned int height)
{
	u8 __iomem *dst = ast_plane_vaddr(&ast->cursor_plane.base);
	u32 csum;

	csum = ast_cursor_calculate_checksum(src, width, height);

	/* write pixel data */
	memcpy_toio(dst, src, AST_HWC_SIZE);

	/* write checksum + signature */
	dst += AST_HWC_SIZE;
	writel(csum, dst);
	writel(width, dst + AST_HWC_SIGNATURE_SizeX);
	writel(height, dst + AST_HWC_SIGNATURE_SizeY);
	writel(0, dst + AST_HWC_SIGNATURE_HOTSPOTX);
	writel(0, dst + AST_HWC_SIGNATURE_HOTSPOTY);
}

static void ast_set_cursor_base(struct ast_device *ast, u64 address)
{
	u8 addr0 = (address >> 3) & 0xff;
	u8 addr1 = (address >> 11) & 0xff;
	u8 addr2 = (address >> 19) & 0xff;

	ast_set_index_reg(ast, AST_IO_VGACRI, 0xc8, addr0);
	ast_set_index_reg(ast, AST_IO_VGACRI, 0xc9, addr1);
	ast_set_index_reg(ast, AST_IO_VGACRI, 0xca, addr2);
}

static void ast_set_cursor_location(struct ast_device *ast, u16 x, u16 y,
				    u8 x_offset, u8 y_offset)
{
	u8 x0 = (x & 0x00ff);
	u8 x1 = (x & 0x0f00) >> 8;
	u8 y0 = (y & 0x00ff);
	u8 y1 = (y & 0x0700) >> 8;

	ast_set_index_reg(ast, AST_IO_VGACRI, 0xc2, x_offset);
	ast_set_index_reg(ast, AST_IO_VGACRI, 0xc3, y_offset);
	ast_set_index_reg(ast, AST_IO_VGACRI, 0xc4, x0);
	ast_set_index_reg(ast, AST_IO_VGACRI, 0xc5, x1);
	ast_set_index_reg(ast, AST_IO_VGACRI, 0xc6, y0);
	ast_set_index_reg(ast, AST_IO_VGACRI, 0xc7, y1);
}

static void ast_set_cursor_enabled(struct ast_device *ast, bool enabled)
{
	static const u8 mask = (u8)~(AST_IO_VGACRCB_HWC_16BPP |
				     AST_IO_VGACRCB_HWC_ENABLED);

	u8 vgacrcb = AST_IO_VGACRCB_HWC_16BPP;

	if (enabled)
		vgacrcb |= AST_IO_VGACRCB_HWC_ENABLED;

	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xcb, mask, vgacrcb);
}

/*
 * Cursor plane
 */

static const uint32_t ast_cursor_plane_formats[] = {
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_ARGB8888,
};

static int ast_cursor_plane_helper_atomic_check(struct drm_plane *plane,
						struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state, plane);
	struct drm_framebuffer *new_fb = new_plane_state->fb;
	struct drm_crtc_state *new_crtc_state = NULL;
	int ret;

	if (new_plane_state->crtc)
		new_crtc_state = drm_atomic_get_new_crtc_state(state, new_plane_state->crtc);

	ret = drm_atomic_helper_check_plane_state(new_plane_state, new_crtc_state,
						  DRM_PLANE_NO_SCALING,
						  DRM_PLANE_NO_SCALING,
						  true, true);
	if (ret || !new_plane_state->visible)
		return ret;

	if (new_fb->width > AST_MAX_HWC_WIDTH || new_fb->height > AST_MAX_HWC_HEIGHT)
		return -EINVAL;

	return 0;
}

static void ast_cursor_plane_helper_atomic_update(struct drm_plane *plane,
						  struct drm_atomic_state *state)
{
	struct ast_cursor_plane *ast_cursor_plane = to_ast_cursor_plane(plane);
	struct ast_plane *ast_plane = to_ast_plane(plane);
	struct drm_plane_state *plane_state = drm_atomic_get_new_plane_state(state, plane);
	struct drm_shadow_plane_state *shadow_plane_state = to_drm_shadow_plane_state(plane_state);
	struct drm_framebuffer *fb = plane_state->fb;
	struct drm_plane_state *old_plane_state = drm_atomic_get_old_plane_state(state, plane);
	struct ast_device *ast = to_ast_device(plane->dev);
	struct drm_rect damage;
	u64 dst_off = ast_plane->offset;
	u8 __iomem *dst = ast_plane_vaddr(ast_plane); /* TODO: Use mapping abstraction properly */
	u8 __iomem *sig = dst + AST_HWC_SIZE; /* TODO: Use mapping abstraction properly */
	unsigned int offset_x, offset_y;
	u16 x, y;
	u8 x_offset, y_offset;

	/*
	 * Do data transfer to hardware buffer and point the scanout
	 * engine to the offset.
	 */

	if (drm_atomic_helper_damage_merged(old_plane_state, plane_state, &damage)) {
		u8 *argb4444;

		switch (fb->format->format) {
		case DRM_FORMAT_ARGB4444:
			argb4444 = shadow_plane_state->data[0].vaddr;
			break;
		default:
			argb4444 = ast_cursor_plane->argb4444;
			{
				struct iosys_map argb4444_dst[DRM_FORMAT_MAX_PLANES] = {
					IOSYS_MAP_INIT_VADDR(argb4444),
				};
				unsigned int argb4444_dst_pitch[DRM_FORMAT_MAX_PLANES] = {
					AST_HWC_PITCH,
				};

				drm_fb_argb8888_to_argb4444(argb4444_dst, argb4444_dst_pitch,
							    shadow_plane_state->data, fb, &damage,
							    &shadow_plane_state->fmtcnv_state);
			}
			break;
		}
		ast_set_cursor_image(ast, argb4444, fb->width, fb->height);
		ast_set_cursor_base(ast, dst_off);
	}

	/*
	 * Update location in HWC signature and registers.
	 */

	writel(plane_state->crtc_x, sig + AST_HWC_SIGNATURE_X);
	writel(plane_state->crtc_y, sig + AST_HWC_SIGNATURE_Y);

	offset_x = AST_MAX_HWC_WIDTH - fb->width;
	offset_y = AST_MAX_HWC_HEIGHT - fb->height;

	if (plane_state->crtc_x < 0) {
		x_offset = (-plane_state->crtc_x) + offset_x;
		x = 0;
	} else {
		x_offset = offset_x;
		x = plane_state->crtc_x;
	}
	if (plane_state->crtc_y < 0) {
		y_offset = (-plane_state->crtc_y) + offset_y;
		y = 0;
	} else {
		y_offset = offset_y;
		y = plane_state->crtc_y;
	}

	ast_set_cursor_location(ast, x, y, x_offset, y_offset);

	/* Dummy write to enable HWC and make the HW pick-up the changes. */
	ast_set_cursor_enabled(ast, true);
}

static void ast_cursor_plane_helper_atomic_disable(struct drm_plane *plane,
						   struct drm_atomic_state *state)
{
	struct ast_device *ast = to_ast_device(plane->dev);

	ast_set_cursor_enabled(ast, false);
}

static const struct drm_plane_helper_funcs ast_cursor_plane_helper_funcs = {
	DRM_GEM_SHADOW_PLANE_HELPER_FUNCS,
	.atomic_check = ast_cursor_plane_helper_atomic_check,
	.atomic_update = ast_cursor_plane_helper_atomic_update,
	.atomic_disable = ast_cursor_plane_helper_atomic_disable,
};

static const struct drm_plane_funcs ast_cursor_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	DRM_GEM_SHADOW_PLANE_FUNCS,
};

int ast_cursor_plane_init(struct ast_device *ast)
{
	struct drm_device *dev = &ast->base;
	struct ast_cursor_plane *ast_cursor_plane = &ast->cursor_plane;
	struct ast_plane *ast_plane = &ast_cursor_plane->base;
	struct drm_plane *cursor_plane = &ast_plane->base;
	unsigned long size;
	long offset;
	int ret;

	size = ast_cursor_vram_size();
	offset = ast_cursor_vram_offset(ast);
	if (offset < 0)
		return offset;

	ret = ast_plane_init(dev, ast_plane, offset, size,
			     0x01, &ast_cursor_plane_funcs,
			     ast_cursor_plane_formats, ARRAY_SIZE(ast_cursor_plane_formats),
			     NULL, DRM_PLANE_TYPE_CURSOR);
	if (ret) {
		drm_err(dev, "ast_plane_init() failed: %d\n", ret);
		return ret;
	}
	drm_plane_helper_add(cursor_plane, &ast_cursor_plane_helper_funcs);
	drm_plane_enable_fb_damage_clips(cursor_plane);

	return 0;
}
