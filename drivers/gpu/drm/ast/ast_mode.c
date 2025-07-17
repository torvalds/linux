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
 *
 */
/*
 * Authors: Dave Airlie <airlied@redhat.com>
 */

#include <linux/delay.h>
#include <linux/pci.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_format_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_panic.h>
#include <drm/drm_probe_helper.h>

#include "ast_drv.h"
#include "ast_tables.h"
#include "ast_vbios.h"

#define AST_LUT_SIZE 256

#define AST_PRIMARY_PLANE_MAX_OFFSET	(BIT(16) - 1)

static unsigned long ast_fb_vram_offset(void)
{
	return 0; // with shmem, the primary plane is always at offset 0
}

static unsigned long ast_fb_vram_size(struct ast_device *ast)
{
	struct drm_device *dev = &ast->base;
	unsigned long offset = ast_fb_vram_offset(); // starts at offset
	long cursor_offset = ast_cursor_vram_offset(ast); // ends at cursor offset

	if (cursor_offset < 0)
		cursor_offset = ast->vram_size; // no cursor; it's all ours
	if (drm_WARN_ON_ONCE(dev, offset > cursor_offset))
		return 0; // cannot legally happen; signal error
	return cursor_offset - offset;
}

static inline void ast_load_palette_index(struct ast_device *ast,
				     u8 index, u8 red, u8 green,
				     u8 blue)
{
	ast_io_write8(ast, AST_IO_VGADWR, index);
	ast_io_read8(ast, AST_IO_VGASRI);
	ast_io_write8(ast, AST_IO_VGAPDR, red);
	ast_io_read8(ast, AST_IO_VGASRI);
	ast_io_write8(ast, AST_IO_VGAPDR, green);
	ast_io_read8(ast, AST_IO_VGASRI);
	ast_io_write8(ast, AST_IO_VGAPDR, blue);
	ast_io_read8(ast, AST_IO_VGASRI);
}

static void ast_crtc_set_gamma_linear(struct ast_device *ast,
				      const struct drm_format_info *format)
{
	int i;

	switch (format->format) {
	case DRM_FORMAT_C8: /* In this case, gamma table is used as color palette */
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_XRGB8888:
		for (i = 0; i < AST_LUT_SIZE; i++)
			ast_load_palette_index(ast, i, i, i, i);
		break;
	default:
		drm_warn_once(&ast->base, "Unsupported format %p4cc for gamma correction\n",
			      &format->format);
		break;
	}
}

static void ast_crtc_set_gamma(struct ast_device *ast,
			       const struct drm_format_info *format,
			       struct drm_color_lut *lut)
{
	int i;

	switch (format->format) {
	case DRM_FORMAT_C8: /* In this case, gamma table is used as color palette */
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_XRGB8888:
		for (i = 0; i < AST_LUT_SIZE; i++)
			ast_load_palette_index(ast, i,
					       lut[i].red >> 8,
					       lut[i].green >> 8,
					       lut[i].blue >> 8);
		break;
	default:
		drm_warn_once(&ast->base, "Unsupported format %p4cc for gamma correction\n",
			      &format->format);
		break;
	}
}

static void ast_set_vbios_color_reg(struct ast_device *ast,
				    const struct drm_format_info *format,
				    const struct ast_vbios_enhtable *vmode)
{
	u32 color_index;

	switch (format->cpp[0]) {
	case 1:
		color_index = VGAModeIndex - 1;
		break;
	case 2:
		color_index = HiCModeIndex;
		break;
	case 3:
	case 4:
		color_index = TrueCModeIndex;
		break;
	default:
		return;
	}

	ast_set_index_reg(ast, AST_IO_VGACRI, 0x8c, (u8)((color_index & 0x0f) << 4));

	ast_set_index_reg(ast, AST_IO_VGACRI, 0x91, 0x00);

	if (vmode->flags & NewModeInfo) {
		ast_set_index_reg(ast, AST_IO_VGACRI, 0x91, 0xa8);
		ast_set_index_reg(ast, AST_IO_VGACRI, 0x92, format->cpp[0] * 8);
	}
}

static void ast_set_vbios_mode_reg(struct ast_device *ast,
				   const struct drm_display_mode *adjusted_mode,
				   const struct ast_vbios_enhtable *vmode)
{
	u32 refresh_rate_index, mode_id;

	refresh_rate_index = vmode->refresh_rate_index;
	mode_id = vmode->mode_id;

	ast_set_index_reg(ast, AST_IO_VGACRI, 0x8d, refresh_rate_index & 0xff);
	ast_set_index_reg(ast, AST_IO_VGACRI, 0x8e, mode_id & 0xff);

	ast_set_index_reg(ast, AST_IO_VGACRI, 0x91, 0x00);

	if (vmode->flags & NewModeInfo) {
		ast_set_index_reg(ast, AST_IO_VGACRI, 0x91, 0xa8);
		ast_set_index_reg(ast, AST_IO_VGACRI, 0x93, adjusted_mode->clock / 1000);
		ast_set_index_reg(ast, AST_IO_VGACRI, 0x94, adjusted_mode->crtc_hdisplay);
		ast_set_index_reg(ast, AST_IO_VGACRI, 0x95, adjusted_mode->crtc_hdisplay >> 8);
		ast_set_index_reg(ast, AST_IO_VGACRI, 0x96, adjusted_mode->crtc_vdisplay);
		ast_set_index_reg(ast, AST_IO_VGACRI, 0x97, adjusted_mode->crtc_vdisplay >> 8);
	}
}

static void ast_set_std_reg(struct ast_device *ast,
			    struct drm_display_mode *mode,
			    const struct ast_vbios_stdtable *stdtable)
{
	u32 i;
	u8 jreg;

	jreg = stdtable->misc;
	ast_io_write8(ast, AST_IO_VGAMR_W, jreg);

	/* Set SEQ; except Screen Disable field */
	ast_set_index_reg(ast, AST_IO_VGASRI, 0x00, 0x03);
	ast_set_index_reg_mask(ast, AST_IO_VGASRI, 0x01, 0x20, stdtable->seq[0]);
	for (i = 1; i < 4; i++) {
		jreg = stdtable->seq[i];
		ast_set_index_reg(ast, AST_IO_VGASRI, (i + 1), jreg);
	}

	/* Set CRTC; except base address and offset */
	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0x11, 0x7f, 0x00);
	for (i = 0; i < 12; i++)
		ast_set_index_reg(ast, AST_IO_VGACRI, i, stdtable->crtc[i]);
	for (i = 14; i < 19; i++)
		ast_set_index_reg(ast, AST_IO_VGACRI, i, stdtable->crtc[i]);
	for (i = 20; i < 25; i++)
		ast_set_index_reg(ast, AST_IO_VGACRI, i, stdtable->crtc[i]);

	/* set AR */
	jreg = ast_io_read8(ast, AST_IO_VGAIR1_R);
	for (i = 0; i < 20; i++) {
		jreg = stdtable->ar[i];
		ast_io_write8(ast, AST_IO_VGAARI_W, (u8)i);
		ast_io_write8(ast, AST_IO_VGAARI_W, jreg);
	}
	ast_io_write8(ast, AST_IO_VGAARI_W, 0x14);
	ast_io_write8(ast, AST_IO_VGAARI_W, 0x00);

	jreg = ast_io_read8(ast, AST_IO_VGAIR1_R);
	ast_io_write8(ast, AST_IO_VGAARI_W, 0x20);

	/* Set GR */
	for (i = 0; i < 9; i++)
		ast_set_index_reg(ast, AST_IO_VGAGRI, i, stdtable->gr[i]);
}

static void ast_set_crtc_reg(struct ast_device *ast,
			     struct drm_display_mode *mode,
			     const struct ast_vbios_enhtable *vmode)
{
	u8 jreg05 = 0, jreg07 = 0, jreg09 = 0, jregAC = 0, jregAD = 0, jregAE = 0;
	u16 temp, precache = 0;

	if ((IS_AST_GEN6(ast) || IS_AST_GEN7(ast)) &&
	    (vmode->flags & AST2500PreCatchCRT))
		precache = 40;

	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0x11, 0x7f, 0x00);

	temp = (mode->crtc_htotal >> 3) - 5;
	if (temp & 0x100)
		jregAC |= 0x01; /* HT D[8] */
	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0x00, 0x00, temp);

	temp = (mode->crtc_hdisplay >> 3) - 1;
	if (temp & 0x100)
		jregAC |= 0x04; /* HDE D[8] */
	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0x01, 0x00, temp);

	temp = (mode->crtc_hblank_start >> 3) - 1;
	if (temp & 0x100)
		jregAC |= 0x10; /* HBS D[8] */
	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0x02, 0x00, temp);

	temp = ((mode->crtc_hblank_end >> 3) - 1) & 0x7f;
	if (temp & 0x20)
		jreg05 |= 0x80;  /* HBE D[5] */
	if (temp & 0x40)
		jregAD |= 0x01;  /* HBE D[5] */
	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0x03, 0xE0, (temp & 0x1f));

	temp = ((mode->crtc_hsync_start-precache) >> 3) - 1;
	if (temp & 0x100)
		jregAC |= 0x40; /* HRS D[5] */
	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0x04, 0x00, temp);

	temp = (((mode->crtc_hsync_end-precache) >> 3) - 1) & 0x3f;
	if (temp & 0x20)
		jregAD |= 0x04; /* HRE D[5] */
	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0x05, 0x60, (u8)((temp & 0x1f) | jreg05));

	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xAC, 0x00, jregAC);
	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xAD, 0x00, jregAD);

	// Workaround for HSync Time non octave pixels (1920x1080@60Hz HSync 44 pixels);
	if (IS_AST_GEN7(ast) && (mode->crtc_vdisplay == 1080))
		ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xFC, 0xFD, 0x02);
	else
		ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xFC, 0xFD, 0x00);

	/* vert timings */
	temp = (mode->crtc_vtotal) - 2;
	if (temp & 0x100)
		jreg07 |= 0x01;
	if (temp & 0x200)
		jreg07 |= 0x20;
	if (temp & 0x400)
		jregAE |= 0x01;
	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0x06, 0x00, temp);

	temp = (mode->crtc_vsync_start) - 1;
	if (temp & 0x100)
		jreg07 |= 0x04;
	if (temp & 0x200)
		jreg07 |= 0x80;
	if (temp & 0x400)
		jregAE |= 0x08;
	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0x10, 0x00, temp);

	temp = (mode->crtc_vsync_end - 1) & 0x3f;
	if (temp & 0x10)
		jregAE |= 0x20;
	if (temp & 0x20)
		jregAE |= 0x40;
	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0x11, 0x70, temp & 0xf);

	temp = mode->crtc_vdisplay - 1;
	if (temp & 0x100)
		jreg07 |= 0x02;
	if (temp & 0x200)
		jreg07 |= 0x40;
	if (temp & 0x400)
		jregAE |= 0x02;
	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0x12, 0x00, temp);

	temp = mode->crtc_vblank_start - 1;
	if (temp & 0x100)
		jreg07 |= 0x08;
	if (temp & 0x200)
		jreg09 |= 0x20;
	if (temp & 0x400)
		jregAE |= 0x04;
	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0x15, 0x00, temp);

	temp = mode->crtc_vblank_end - 1;
	if (temp & 0x100)
		jregAE |= 0x10;
	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0x16, 0x00, temp);

	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0x07, 0x00, jreg07);
	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0x09, 0xdf, jreg09);
	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xAE, 0x00, (jregAE | 0x80));

	if (precache)
		ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xb6, 0x3f, 0x80);
	else
		ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xb6, 0x3f, 0x00);

	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0x11, 0x7f, 0x80);
}

static void ast_set_offset_reg(struct ast_device *ast,
			       struct drm_framebuffer *fb)
{
	u16 offset;

	offset = fb->pitches[0] >> 3;
	ast_set_index_reg(ast, AST_IO_VGACRI, 0x13, (offset & 0xff));
	ast_set_index_reg(ast, AST_IO_VGACRI, 0xb0, (offset >> 8) & 0x3f);
}

static void ast_set_dclk_reg(struct ast_device *ast,
			     struct drm_display_mode *mode,
			     const struct ast_vbios_enhtable *vmode)
{
	const struct ast_vbios_dclk_info *clk_info;

	if (IS_AST_GEN6(ast) || IS_AST_GEN7(ast))
		clk_info = &dclk_table_ast2500[vmode->dclk_index];
	else
		clk_info = &dclk_table[vmode->dclk_index];

	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xc0, 0x00, clk_info->param1);
	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xc1, 0x00, clk_info->param2);
	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xbb, 0x0f,
			       (clk_info->param3 & 0xc0) |
			       ((clk_info->param3 & 0x3) << 4));
}

static void ast_set_color_reg(struct ast_device *ast,
			      const struct drm_format_info *format)
{
	u8 jregA0 = 0, jregA3 = 0, jregA8 = 0;

	switch (format->cpp[0] * 8) {
	case 8:
		jregA0 = 0x70;
		jregA3 = 0x01;
		jregA8 = 0x00;
		break;
	case 15:
	case 16:
		jregA0 = 0x70;
		jregA3 = 0x04;
		jregA8 = 0x02;
		break;
	case 32:
		jregA0 = 0x70;
		jregA3 = 0x08;
		jregA8 = 0x02;
		break;
	}

	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xa0, 0x8f, jregA0);
	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xa3, 0xf0, jregA3);
	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xa8, 0xfd, jregA8);
}

static void ast_set_crtthd_reg(struct ast_device *ast)
{
	/* Set Threshold */
	if (IS_AST_GEN7(ast)) {
		ast_set_index_reg(ast, AST_IO_VGACRI, 0xa7, 0xe0);
		ast_set_index_reg(ast, AST_IO_VGACRI, 0xa6, 0xa0);
	} else if (IS_AST_GEN6(ast) || IS_AST_GEN5(ast) || IS_AST_GEN4(ast)) {
		ast_set_index_reg(ast, AST_IO_VGACRI, 0xa7, 0x78);
		ast_set_index_reg(ast, AST_IO_VGACRI, 0xa6, 0x60);
	} else if (IS_AST_GEN3(ast) || IS_AST_GEN2(ast)) {
		ast_set_index_reg(ast, AST_IO_VGACRI, 0xa7, 0x3f);
		ast_set_index_reg(ast, AST_IO_VGACRI, 0xa6, 0x2f);
	} else {
		ast_set_index_reg(ast, AST_IO_VGACRI, 0xa7, 0x2f);
		ast_set_index_reg(ast, AST_IO_VGACRI, 0xa6, 0x1f);
	}
}

static void ast_set_sync_reg(struct ast_device *ast,
			     struct drm_display_mode *mode,
			     const struct ast_vbios_enhtable *vmode)
{
	u8 jreg;

	jreg  = ast_io_read8(ast, AST_IO_VGAMR_R);
	jreg &= ~0xC0;
	if (vmode->flags & NVSync)
		jreg |= 0x80;
	if (vmode->flags & NHSync)
		jreg |= 0x40;
	ast_io_write8(ast, AST_IO_VGAMR_W, jreg);
}

static void ast_set_start_address_crt1(struct ast_device *ast,
				       unsigned int offset)
{
	u32 addr;

	addr = offset >> 2;
	ast_set_index_reg(ast, AST_IO_VGACRI, 0x0d, (u8)(addr & 0xff));
	ast_set_index_reg(ast, AST_IO_VGACRI, 0x0c, (u8)((addr >> 8) & 0xff));
	ast_set_index_reg(ast, AST_IO_VGACRI, 0xaf, (u8)((addr >> 16) & 0xff));

}

static void ast_wait_for_vretrace(struct ast_device *ast)
{
	unsigned long timeout = jiffies + HZ;
	u8 vgair1;

	do {
		vgair1 = ast_io_read8(ast, AST_IO_VGAIR1_R);
	} while (!(vgair1 & AST_IO_VGAIR1_VREFRESH) && time_before(jiffies, timeout));
}

/*
 * Planes
 */

int ast_plane_init(struct drm_device *dev, struct ast_plane *ast_plane,
		   u64 offset, unsigned long size,
		   uint32_t possible_crtcs,
		   const struct drm_plane_funcs *funcs,
		   const uint32_t *formats, unsigned int format_count,
		   const uint64_t *format_modifiers,
		   enum drm_plane_type type)
{
	struct drm_plane *plane = &ast_plane->base;

	ast_plane->offset = offset;
	ast_plane->size = size;

	return drm_universal_plane_init(dev, plane, possible_crtcs, funcs,
					formats, format_count, format_modifiers,
					type, NULL);
}

void __iomem *ast_plane_vaddr(struct ast_plane *ast_plane)
{
	struct ast_device *ast = to_ast_device(ast_plane->base.dev);

	return ast->vram + ast_plane->offset;
}

/*
 * Primary plane
 */

static const uint32_t ast_primary_plane_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_C8,
};

static int ast_primary_plane_helper_atomic_check(struct drm_plane *plane,
						 struct drm_atomic_state *state)
{
	struct drm_device *dev = plane->dev;
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state, plane);
	struct drm_crtc_state *new_crtc_state = NULL;
	struct ast_crtc_state *new_ast_crtc_state;
	int ret;

	if (new_plane_state->crtc)
		new_crtc_state = drm_atomic_get_new_crtc_state(state, new_plane_state->crtc);

	ret = drm_atomic_helper_check_plane_state(new_plane_state, new_crtc_state,
						  DRM_PLANE_NO_SCALING,
						  DRM_PLANE_NO_SCALING,
						  false, true);
	if (ret) {
		return ret;
	} else if (!new_plane_state->visible) {
		if (drm_WARN_ON(dev, new_plane_state->crtc)) /* cannot legally happen */
			return -EINVAL;
		else
			return 0;
	}

	new_ast_crtc_state = to_ast_crtc_state(new_crtc_state);

	new_ast_crtc_state->format = new_plane_state->fb->format;

	return 0;
}

static void ast_handle_damage(struct ast_plane *ast_plane, struct iosys_map *src,
			      struct drm_framebuffer *fb,
			      const struct drm_rect *clip)
{
	struct iosys_map dst = IOSYS_MAP_INIT_VADDR_IOMEM(ast_plane_vaddr(ast_plane));

	iosys_map_incr(&dst, drm_fb_clip_offset(fb->pitches[0], fb->format, clip));
	drm_fb_memcpy(&dst, fb->pitches, src, fb, clip);
}

static void ast_primary_plane_helper_atomic_update(struct drm_plane *plane,
						   struct drm_atomic_state *state)
{
	struct drm_device *dev = plane->dev;
	struct ast_device *ast = to_ast_device(dev);
	struct drm_plane_state *plane_state = drm_atomic_get_new_plane_state(state, plane);
	struct drm_shadow_plane_state *shadow_plane_state = to_drm_shadow_plane_state(plane_state);
	struct drm_framebuffer *fb = plane_state->fb;
	struct drm_plane_state *old_plane_state = drm_atomic_get_old_plane_state(state, plane);
	struct drm_framebuffer *old_fb = old_plane_state->fb;
	struct ast_plane *ast_plane = to_ast_plane(plane);
	struct drm_crtc *crtc = plane_state->crtc;
	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
	struct drm_rect damage;
	struct drm_atomic_helper_damage_iter iter;

	if (!old_fb || (fb->format != old_fb->format) || crtc_state->mode_changed) {
		struct ast_crtc_state *ast_crtc_state = to_ast_crtc_state(crtc_state);

		ast_set_color_reg(ast, fb->format);
		ast_set_vbios_color_reg(ast, fb->format, ast_crtc_state->vmode);
	}

	drm_atomic_helper_damage_iter_init(&iter, old_plane_state, plane_state);
	drm_atomic_for_each_plane_damage(&iter, &damage) {
		ast_handle_damage(ast_plane, shadow_plane_state->data, fb, &damage);
	}

	/*
	 * Some BMCs stop scanning out the video signal after the driver
	 * reprogrammed the offset. This stalls display output for several
	 * seconds and makes the display unusable. Therefore only update
	 * the offset if it changes.
	 */
	if (!old_fb || old_fb->pitches[0] != fb->pitches[0])
		ast_set_offset_reg(ast, fb);
}

static void ast_primary_plane_helper_atomic_enable(struct drm_plane *plane,
						   struct drm_atomic_state *state)
{
	struct ast_device *ast = to_ast_device(plane->dev);
	struct ast_plane *ast_plane = to_ast_plane(plane);

	/*
	 * Some BMCs stop scanning out the video signal after the driver
	 * reprogrammed the scanout address. This stalls display
	 * output for several seconds and makes the display unusable.
	 * Therefore only reprogram the address after enabling the plane.
	 */
	ast_set_start_address_crt1(ast, (u32)ast_plane->offset);
}

static void ast_primary_plane_helper_atomic_disable(struct drm_plane *plane,
						    struct drm_atomic_state *state)
{
	/*
	 * Keep this empty function to avoid calling
	 * atomic_update when disabling the plane.
	 */
}

static int ast_primary_plane_helper_get_scanout_buffer(struct drm_plane *plane,
						       struct drm_scanout_buffer *sb)
{
	struct ast_plane *ast_plane = to_ast_plane(plane);

	if (plane->state && plane->state->fb) {
		sb->format = plane->state->fb->format;
		sb->width = plane->state->fb->width;
		sb->height = plane->state->fb->height;
		sb->pitch[0] = plane->state->fb->pitches[0];
		iosys_map_set_vaddr_iomem(&sb->map[0], ast_plane_vaddr(ast_plane));
		return 0;
	}
	return -ENODEV;
}

static const struct drm_plane_helper_funcs ast_primary_plane_helper_funcs = {
	DRM_GEM_SHADOW_PLANE_HELPER_FUNCS,
	.atomic_check = ast_primary_plane_helper_atomic_check,
	.atomic_update = ast_primary_plane_helper_atomic_update,
	.atomic_enable = ast_primary_plane_helper_atomic_enable,
	.atomic_disable = ast_primary_plane_helper_atomic_disable,
	.get_scanout_buffer = ast_primary_plane_helper_get_scanout_buffer,
};

static const struct drm_plane_funcs ast_primary_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	DRM_GEM_SHADOW_PLANE_FUNCS,
};

static int ast_primary_plane_init(struct ast_device *ast)
{
	struct drm_device *dev = &ast->base;
	struct ast_plane *ast_primary_plane = &ast->primary_plane;
	struct drm_plane *primary_plane = &ast_primary_plane->base;
	u64 offset = ast_fb_vram_offset();
	unsigned long size = ast_fb_vram_size(ast);
	int ret;

	ret = ast_plane_init(dev, ast_primary_plane, offset, size,
			     0x01, &ast_primary_plane_funcs,
			     ast_primary_plane_formats, ARRAY_SIZE(ast_primary_plane_formats),
			     NULL, DRM_PLANE_TYPE_PRIMARY);
	if (ret) {
		drm_err(dev, "ast_plane_init() failed: %d\n", ret);
		return ret;
	}
	drm_plane_helper_add(primary_plane, &ast_primary_plane_helper_funcs);
	drm_plane_enable_fb_damage_clips(primary_plane);

	return 0;
}

/*
 * CRTC
 */

static enum drm_mode_status
ast_crtc_helper_mode_valid(struct drm_crtc *crtc, const struct drm_display_mode *mode)
{
	struct ast_device *ast = to_ast_device(crtc->dev);
	const struct ast_vbios_enhtable *vmode;

	vmode = ast_vbios_find_mode(ast, mode);
	if (!vmode)
		return MODE_NOMODE;

	return MODE_OK;
}

static void ast_crtc_helper_mode_set_nofb(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct ast_device *ast = to_ast_device(dev);
	struct drm_crtc_state *crtc_state = crtc->state;
	struct ast_crtc_state *ast_crtc_state = to_ast_crtc_state(crtc_state);
	const struct ast_vbios_stdtable *std_table = ast_crtc_state->std_table;
	const struct ast_vbios_enhtable *vmode = ast_crtc_state->vmode;
	struct drm_display_mode *adjusted_mode = &crtc_state->adjusted_mode;

	/*
	 * Ensure that no scanout takes place before reprogramming mode
	 * and format registers.
	 *
	 * TODO: Get vblank interrupts working and remove this line.
	 */
	ast_wait_for_vretrace(ast);

	ast_set_vbios_mode_reg(ast, adjusted_mode, vmode);
	ast_set_index_reg(ast, AST_IO_VGACRI, 0xa1, 0x06);
	ast_set_std_reg(ast, adjusted_mode, std_table);
	ast_set_crtc_reg(ast, adjusted_mode, vmode);
	ast_set_dclk_reg(ast, adjusted_mode, vmode);
	ast_set_crtthd_reg(ast);
	ast_set_sync_reg(ast, adjusted_mode, vmode);
}

static int ast_crtc_helper_atomic_check(struct drm_crtc *crtc,
					struct drm_atomic_state *state)
{
	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
	struct drm_display_mode *adjusted_mode = &crtc_state->adjusted_mode;
	struct drm_crtc_state *old_crtc_state = drm_atomic_get_old_crtc_state(state, crtc);
	struct ast_crtc_state *old_ast_crtc_state = to_ast_crtc_state(old_crtc_state);
	struct drm_device *dev = crtc->dev;
	struct ast_device *ast = to_ast_device(dev);
	struct ast_crtc_state *ast_state;
	const struct drm_format_info *format;
	const struct ast_vbios_enhtable *vmode;
	unsigned int hborder = 0;
	unsigned int vborder = 0;
	int ret;

	if (!crtc_state->enable)
		return 0;

	ret = drm_atomic_helper_check_crtc_primary_plane(crtc_state);
	if (ret)
		return ret;

	ast_state = to_ast_crtc_state(crtc_state);

	format = ast_state->format;
	if (drm_WARN_ON_ONCE(dev, !format))
		return -EINVAL; /* BUG: We didn't set format in primary check(). */

	/*
	 * The gamma LUT has to be reloaded after changing the primary
	 * plane's color format.
	 */
	if (old_ast_crtc_state->format != format)
		crtc_state->color_mgmt_changed = true;

	if (crtc_state->color_mgmt_changed && crtc_state->gamma_lut) {
		if (crtc_state->gamma_lut->length !=
		    AST_LUT_SIZE * sizeof(struct drm_color_lut)) {
			drm_err(dev, "Wrong size for gamma_lut %zu\n",
				crtc_state->gamma_lut->length);
			return -EINVAL;
		}
	}

	/*
	 * Set register tables.
	 *
	 * TODO: These tables mix all kinds of fields and should
	 *       probably be resolved into various helper functions.
	 */
	switch (format->format) {
	case DRM_FORMAT_C8:
		ast_state->std_table = &vbios_stdtable[VGAModeIndex];
		break;
	case DRM_FORMAT_RGB565:
		ast_state->std_table = &vbios_stdtable[HiCModeIndex];
		break;
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_XRGB8888:
		ast_state->std_table = &vbios_stdtable[TrueCModeIndex];
		break;
	default:
		return -EINVAL;
	}

	/*
	 * Find the VBIOS mode and adjust the DRM display mode accordingly
	 * if a full modeset is required. Otherwise keep the existing values.
	 */
	if (drm_atomic_crtc_needs_modeset(crtc_state)) {
		vmode = ast_vbios_find_mode(ast, &crtc_state->mode);
		if (!vmode)
			return -EINVAL;
		ast_state->vmode = vmode;

		if (vmode->flags & HBorder)
			hborder = 8;
		if (vmode->flags & VBorder)
			vborder = 8;

		adjusted_mode->crtc_hdisplay = vmode->hde;
		adjusted_mode->crtc_hblank_start = vmode->hde + hborder;
		adjusted_mode->crtc_hblank_end = vmode->ht - hborder;
		adjusted_mode->crtc_hsync_start = vmode->hde + hborder + vmode->hfp;
		adjusted_mode->crtc_hsync_end = vmode->hde + hborder + vmode->hfp + vmode->hsync;
		adjusted_mode->crtc_htotal = vmode->ht;

		adjusted_mode->crtc_vdisplay = vmode->vde;
		adjusted_mode->crtc_vblank_start = vmode->vde + vborder;
		adjusted_mode->crtc_vblank_end = vmode->vt - vborder;
		adjusted_mode->crtc_vsync_start = vmode->vde + vborder + vmode->vfp;
		adjusted_mode->crtc_vsync_end = vmode->vde + vborder + vmode->vfp + vmode->vsync;
		adjusted_mode->crtc_vtotal = vmode->vt;
	}

	return 0;
}

static void
ast_crtc_helper_atomic_flush(struct drm_crtc *crtc,
			     struct drm_atomic_state *state)
{
	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state,
									  crtc);
	struct drm_device *dev = crtc->dev;
	struct ast_device *ast = to_ast_device(dev);
	struct ast_crtc_state *ast_crtc_state = to_ast_crtc_state(crtc_state);

	/*
	 * The gamma LUT has to be reloaded after changing the primary
	 * plane's color format.
	 */
	if (crtc_state->enable && crtc_state->color_mgmt_changed) {
		if (crtc_state->gamma_lut)
			ast_crtc_set_gamma(ast,
					   ast_crtc_state->format,
					   crtc_state->gamma_lut->data);
		else
			ast_crtc_set_gamma_linear(ast, ast_crtc_state->format);
	}
}

static void ast_crtc_helper_atomic_enable(struct drm_crtc *crtc, struct drm_atomic_state *state)
{
	struct ast_device *ast = to_ast_device(crtc->dev);

	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xb6, 0xfc, 0x00);
	ast_set_index_reg_mask(ast, AST_IO_VGASRI, 0x01, 0xdf, 0x00);
}

static void ast_crtc_helper_atomic_disable(struct drm_crtc *crtc, struct drm_atomic_state *state)
{
	struct drm_crtc_state *old_crtc_state = drm_atomic_get_old_crtc_state(state, crtc);
	struct ast_device *ast = to_ast_device(crtc->dev);
	u8 vgacrb6;

	ast_set_index_reg_mask(ast, AST_IO_VGASRI, 0x01, 0xdf, AST_IO_VGASR1_SD);

	vgacrb6 = AST_IO_VGACRB6_VSYNC_OFF |
		  AST_IO_VGACRB6_HSYNC_OFF;
	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xb6, 0xfc, vgacrb6);

	/*
	 * HW cursors require the underlying primary plane and CRTC to
	 * display a valid mode and image. This is not the case during
	 * full modeset operations. So we temporarily disable any active
	 * plane, including the HW cursor. Each plane's atomic_update()
	 * helper will re-enable it if necessary.
	 *
	 * We only do this during *full* modesets. It does not affect
	 * simple pageflips on the planes.
	 */
	drm_atomic_helper_disable_planes_on_crtc(old_crtc_state, false);
}

static const struct drm_crtc_helper_funcs ast_crtc_helper_funcs = {
	.mode_valid = ast_crtc_helper_mode_valid,
	.mode_set_nofb = ast_crtc_helper_mode_set_nofb,
	.atomic_check = ast_crtc_helper_atomic_check,
	.atomic_flush = ast_crtc_helper_atomic_flush,
	.atomic_enable = ast_crtc_helper_atomic_enable,
	.atomic_disable = ast_crtc_helper_atomic_disable,
};

static void ast_crtc_reset(struct drm_crtc *crtc)
{
	struct ast_crtc_state *ast_state =
		kzalloc(sizeof(*ast_state), GFP_KERNEL);

	if (crtc->state)
		crtc->funcs->atomic_destroy_state(crtc, crtc->state);

	if (ast_state)
		__drm_atomic_helper_crtc_reset(crtc, &ast_state->base);
	else
		__drm_atomic_helper_crtc_reset(crtc, NULL);
}

static struct drm_crtc_state *
ast_crtc_atomic_duplicate_state(struct drm_crtc *crtc)
{
	struct ast_crtc_state *new_ast_state, *ast_state;
	struct drm_device *dev = crtc->dev;

	if (drm_WARN_ON(dev, !crtc->state))
		return NULL;

	new_ast_state = kmalloc(sizeof(*new_ast_state), GFP_KERNEL);
	if (!new_ast_state)
		return NULL;
	__drm_atomic_helper_crtc_duplicate_state(crtc, &new_ast_state->base);

	ast_state = to_ast_crtc_state(crtc->state);

	new_ast_state->format = ast_state->format;
	new_ast_state->std_table = ast_state->std_table;
	new_ast_state->vmode = ast_state->vmode;

	return &new_ast_state->base;
}

static void ast_crtc_atomic_destroy_state(struct drm_crtc *crtc,
					  struct drm_crtc_state *state)
{
	struct ast_crtc_state *ast_state = to_ast_crtc_state(state);

	__drm_atomic_helper_crtc_destroy_state(&ast_state->base);
	kfree(ast_state);
}

static const struct drm_crtc_funcs ast_crtc_funcs = {
	.reset = ast_crtc_reset,
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = ast_crtc_atomic_duplicate_state,
	.atomic_destroy_state = ast_crtc_atomic_destroy_state,
};

static int ast_crtc_init(struct ast_device *ast)
{
	struct drm_device *dev = &ast->base;
	struct drm_crtc *crtc = &ast->crtc;
	int ret;

	ret = drm_crtc_init_with_planes(dev, crtc, &ast->primary_plane.base,
					&ast->cursor_plane.base.base, &ast_crtc_funcs,
					NULL);
	if (ret)
		return ret;

	drm_mode_crtc_set_gamma_size(crtc, AST_LUT_SIZE);
	drm_crtc_enable_color_mgmt(crtc, 0, false, AST_LUT_SIZE);

	drm_crtc_helper_add(crtc, &ast_crtc_helper_funcs);

	return 0;
}

/*
 * Mode config
 */

static void ast_mode_config_helper_atomic_commit_tail(struct drm_atomic_state *state)
{
	struct ast_device *ast = to_ast_device(state->dev);

	/*
	 * Concurrent operations could possibly trigger a call to
	 * drm_connector_helper_funcs.get_modes by reading the display
	 * modes. Protect access to registers by acquiring the modeset
	 * lock.
	 */
	mutex_lock(&ast->modeset_lock);
	drm_atomic_helper_commit_tail(state);
	mutex_unlock(&ast->modeset_lock);
}

static const struct drm_mode_config_helper_funcs ast_mode_config_helper_funcs = {
	.atomic_commit_tail = ast_mode_config_helper_atomic_commit_tail,
};

static enum drm_mode_status ast_mode_config_mode_valid(struct drm_device *dev,
						       const struct drm_display_mode *mode)
{
	const struct drm_format_info *info = drm_format_info(DRM_FORMAT_XRGB8888);
	struct ast_device *ast = to_ast_device(dev);
	unsigned long max_fb_size = ast_fb_vram_size(ast);
	u64 pitch;

	if (drm_WARN_ON_ONCE(dev, !info))
		return MODE_ERROR; /* driver bug */

	pitch = drm_format_info_min_pitch(info, 0, mode->hdisplay);
	if (!pitch)
		return MODE_BAD_WIDTH;
	if (pitch > AST_PRIMARY_PLANE_MAX_OFFSET)
		return MODE_BAD_WIDTH; /* maximum programmable pitch */
	if (pitch > max_fb_size / mode->vdisplay)
		return MODE_MEM;

	return MODE_OK;
}

static const struct drm_mode_config_funcs ast_mode_config_funcs = {
	.fb_create = drm_gem_fb_create_with_dirty,
	.mode_valid = ast_mode_config_mode_valid,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

int ast_mode_config_init(struct ast_device *ast)
{
	struct drm_device *dev = &ast->base;
	int ret;

	ret = drmm_mutex_init(dev, &ast->modeset_lock);
	if (ret)
		return ret;

	ret = drmm_mode_config_init(dev);
	if (ret)
		return ret;

	dev->mode_config.funcs = &ast_mode_config_funcs;
	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	dev->mode_config.preferred_depth = 24;

	if (ast->support_fullhd) {
		dev->mode_config.max_width = 1920;
		dev->mode_config.max_height = 2048;
	} else {
		dev->mode_config.max_width = 1600;
		dev->mode_config.max_height = 1200;
	}

	dev->mode_config.helper_private = &ast_mode_config_helper_funcs;

	ret = ast_primary_plane_init(ast);
	if (ret)
		return ret;

	ret = ast_cursor_plane_init(ast);
	if (ret)
		return ret;

	ret = ast_crtc_init(ast);
	if (ret)
		return ret;

	switch (ast->tx_chip) {
	case AST_TX_NONE:
		ret = ast_vga_output_init(ast);
		break;
	case AST_TX_SIL164:
		ret = ast_sil164_output_init(ast);
		break;
	case AST_TX_DP501:
		ret = ast_dp501_output_init(ast);
		break;
	case AST_TX_ASTDP:
		ret = ast_astdp_output_init(ast);
		break;
	}
	if (ret)
		return ret;

	drm_mode_config_reset(dev);
	drmm_kms_helper_poll_init(dev);

	return 0;
}
