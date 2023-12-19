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

#include <linux/export.h>
#include <linux/pci.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_format_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include "ast_drv.h"
#include "ast_tables.h"

#define AST_LUT_SIZE 256

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

static bool ast_get_vbios_mode_info(const struct drm_format_info *format,
				    const struct drm_display_mode *mode,
				    struct drm_display_mode *adjusted_mode,
				    struct ast_vbios_mode_info *vbios_mode)
{
	u32 refresh_rate_index = 0, refresh_rate;
	const struct ast_vbios_enhtable *best = NULL;
	u32 hborder, vborder;
	bool check_sync;

	switch (format->cpp[0] * 8) {
	case 8:
		vbios_mode->std_table = &vbios_stdtable[VGAModeIndex];
		break;
	case 16:
		vbios_mode->std_table = &vbios_stdtable[HiCModeIndex];
		break;
	case 24:
	case 32:
		vbios_mode->std_table = &vbios_stdtable[TrueCModeIndex];
		break;
	default:
		return false;
	}

	switch (mode->crtc_hdisplay) {
	case 640:
		vbios_mode->enh_table = &res_640x480[refresh_rate_index];
		break;
	case 800:
		vbios_mode->enh_table = &res_800x600[refresh_rate_index];
		break;
	case 1024:
		vbios_mode->enh_table = &res_1024x768[refresh_rate_index];
		break;
	case 1152:
		vbios_mode->enh_table = &res_1152x864[refresh_rate_index];
		break;
	case 1280:
		if (mode->crtc_vdisplay == 800)
			vbios_mode->enh_table = &res_1280x800[refresh_rate_index];
		else
			vbios_mode->enh_table = &res_1280x1024[refresh_rate_index];
		break;
	case 1360:
		vbios_mode->enh_table = &res_1360x768[refresh_rate_index];
		break;
	case 1440:
		vbios_mode->enh_table = &res_1440x900[refresh_rate_index];
		break;
	case 1600:
		if (mode->crtc_vdisplay == 900)
			vbios_mode->enh_table = &res_1600x900[refresh_rate_index];
		else
			vbios_mode->enh_table = &res_1600x1200[refresh_rate_index];
		break;
	case 1680:
		vbios_mode->enh_table = &res_1680x1050[refresh_rate_index];
		break;
	case 1920:
		if (mode->crtc_vdisplay == 1080)
			vbios_mode->enh_table = &res_1920x1080[refresh_rate_index];
		else
			vbios_mode->enh_table = &res_1920x1200[refresh_rate_index];
		break;
	default:
		return false;
	}

	refresh_rate = drm_mode_vrefresh(mode);
	check_sync = vbios_mode->enh_table->flags & WideScreenMode;

	while (1) {
		const struct ast_vbios_enhtable *loop = vbios_mode->enh_table;

		while (loop->refresh_rate != 0xff) {
			if ((check_sync) &&
			    (((mode->flags & DRM_MODE_FLAG_NVSYNC)  &&
			      (loop->flags & PVSync))  ||
			     ((mode->flags & DRM_MODE_FLAG_PVSYNC)  &&
			      (loop->flags & NVSync))  ||
			     ((mode->flags & DRM_MODE_FLAG_NHSYNC)  &&
			      (loop->flags & PHSync))  ||
			     ((mode->flags & DRM_MODE_FLAG_PHSYNC)  &&
			      (loop->flags & NHSync)))) {
				loop++;
				continue;
			}
			if (loop->refresh_rate <= refresh_rate
			    && (!best || loop->refresh_rate > best->refresh_rate))
				best = loop;
			loop++;
		}
		if (best || !check_sync)
			break;
		check_sync = 0;
	}

	if (best)
		vbios_mode->enh_table = best;

	hborder = (vbios_mode->enh_table->flags & HBorder) ? 8 : 0;
	vborder = (vbios_mode->enh_table->flags & VBorder) ? 8 : 0;

	adjusted_mode->crtc_htotal = vbios_mode->enh_table->ht;
	adjusted_mode->crtc_hblank_start = vbios_mode->enh_table->hde + hborder;
	adjusted_mode->crtc_hblank_end = vbios_mode->enh_table->ht - hborder;
	adjusted_mode->crtc_hsync_start = vbios_mode->enh_table->hde + hborder +
		vbios_mode->enh_table->hfp;
	adjusted_mode->crtc_hsync_end = (vbios_mode->enh_table->hde + hborder +
					 vbios_mode->enh_table->hfp +
					 vbios_mode->enh_table->hsync);

	adjusted_mode->crtc_vtotal = vbios_mode->enh_table->vt;
	adjusted_mode->crtc_vblank_start = vbios_mode->enh_table->vde + vborder;
	adjusted_mode->crtc_vblank_end = vbios_mode->enh_table->vt - vborder;
	adjusted_mode->crtc_vsync_start = vbios_mode->enh_table->vde + vborder +
		vbios_mode->enh_table->vfp;
	adjusted_mode->crtc_vsync_end = (vbios_mode->enh_table->vde + vborder +
					 vbios_mode->enh_table->vfp +
					 vbios_mode->enh_table->vsync);

	return true;
}

static void ast_set_vbios_color_reg(struct ast_device *ast,
				    const struct drm_format_info *format,
				    const struct ast_vbios_mode_info *vbios_mode)
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

	if (vbios_mode->enh_table->flags & NewModeInfo) {
		ast_set_index_reg(ast, AST_IO_VGACRI, 0x91, 0xa8);
		ast_set_index_reg(ast, AST_IO_VGACRI, 0x92, format->cpp[0] * 8);
	}
}

static void ast_set_vbios_mode_reg(struct ast_device *ast,
				   const struct drm_display_mode *adjusted_mode,
				   const struct ast_vbios_mode_info *vbios_mode)
{
	u32 refresh_rate_index, mode_id;

	refresh_rate_index = vbios_mode->enh_table->refresh_rate_index;
	mode_id = vbios_mode->enh_table->mode_id;

	ast_set_index_reg(ast, AST_IO_VGACRI, 0x8d, refresh_rate_index & 0xff);
	ast_set_index_reg(ast, AST_IO_VGACRI, 0x8e, mode_id & 0xff);

	ast_set_index_reg(ast, AST_IO_VGACRI, 0x91, 0x00);

	if (vbios_mode->enh_table->flags & NewModeInfo) {
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
			    struct ast_vbios_mode_info *vbios_mode)
{
	const struct ast_vbios_stdtable *stdtable;
	u32 i;
	u8 jreg;

	stdtable = vbios_mode->std_table;

	jreg = stdtable->misc;
	ast_io_write8(ast, AST_IO_VGAMR_W, jreg);

	/* Set SEQ; except Screen Disable field */
	ast_set_index_reg(ast, AST_IO_VGASRI, 0x00, 0x03);
	ast_set_index_reg_mask(ast, AST_IO_VGASRI, 0x01, 0xdf, stdtable->seq[0]);
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
			     struct ast_vbios_mode_info *vbios_mode)
{
	u8 jreg05 = 0, jreg07 = 0, jreg09 = 0, jregAC = 0, jregAD = 0, jregAE = 0;
	u16 temp, precache = 0;

	if ((IS_AST_GEN6(ast) || IS_AST_GEN7(ast)) &&
	    (vbios_mode->enh_table->flags & AST2500PreCatchCRT))
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
			     struct ast_vbios_mode_info *vbios_mode)
{
	const struct ast_vbios_dclk_info *clk_info;

	if (IS_AST_GEN6(ast) || IS_AST_GEN7(ast))
		clk_info = &dclk_table_ast2500[vbios_mode->enh_table->dclk_index];
	else
		clk_info = &dclk_table[vbios_mode->enh_table->dclk_index];

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
			     struct ast_vbios_mode_info *vbios_mode)
{
	u8 jreg;

	jreg  = ast_io_read8(ast, AST_IO_VGAMR_R);
	jreg &= ~0xC0;
	if (vbios_mode->enh_table->flags & NVSync)
		jreg |= 0x80;
	if (vbios_mode->enh_table->flags & NHSync)
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

static int ast_plane_init(struct drm_device *dev, struct ast_plane *ast_plane,
			  void __iomem *vaddr, u64 offset, unsigned long size,
			  uint32_t possible_crtcs,
			  const struct drm_plane_funcs *funcs,
			  const uint32_t *formats, unsigned int format_count,
			  const uint64_t *format_modifiers,
			  enum drm_plane_type type)
{
	struct drm_plane *plane = &ast_plane->base;

	ast_plane->vaddr = vaddr;
	ast_plane->offset = offset;
	ast_plane->size = size;

	return drm_universal_plane_init(dev, plane, possible_crtcs, funcs,
					formats, format_count, format_modifiers,
					type, NULL);
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
	struct iosys_map dst = IOSYS_MAP_INIT_VADDR_IOMEM(ast_plane->vaddr);

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
	struct drm_rect damage;
	struct drm_atomic_helper_damage_iter iter;

	if (!old_fb || (fb->format != old_fb->format)) {
		struct drm_crtc *crtc = plane_state->crtc;
		struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
		struct ast_crtc_state *ast_crtc_state = to_ast_crtc_state(crtc_state);
		struct ast_vbios_mode_info *vbios_mode_info = &ast_crtc_state->vbios_mode_info;

		ast_set_color_reg(ast, fb->format);
		ast_set_vbios_color_reg(ast, fb->format, vbios_mode_info);
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
	ast_set_index_reg_mask(ast, AST_IO_VGASRI, 0x1, 0xdf, 0x00);
}

static void ast_primary_plane_helper_atomic_disable(struct drm_plane *plane,
						    struct drm_atomic_state *state)
{
	struct ast_device *ast = to_ast_device(plane->dev);

	ast_set_index_reg_mask(ast, AST_IO_VGASRI, 0x1, 0xdf, 0x20);
}

static const struct drm_plane_helper_funcs ast_primary_plane_helper_funcs = {
	DRM_GEM_SHADOW_PLANE_HELPER_FUNCS,
	.atomic_check = ast_primary_plane_helper_atomic_check,
	.atomic_update = ast_primary_plane_helper_atomic_update,
	.atomic_enable = ast_primary_plane_helper_atomic_enable,
	.atomic_disable = ast_primary_plane_helper_atomic_disable,
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
	void __iomem *vaddr = ast->vram;
	u64 offset = 0; /* with shmem, the primary plane is always at offset 0 */
	unsigned long cursor_size = roundup(AST_HWC_SIZE + AST_HWC_SIGNATURE_SIZE, PAGE_SIZE);
	unsigned long size = ast->vram_fb_available - cursor_size;
	int ret;

	ret = ast_plane_init(dev, ast_primary_plane, vaddr, offset, size,
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
 * Cursor plane
 */

static void ast_update_cursor_image(u8 __iomem *dst, const u8 *src, int width, int height)
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

static const uint32_t ast_cursor_plane_formats[] = {
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
	struct ast_plane *ast_plane = to_ast_plane(plane);
	struct drm_plane_state *plane_state = drm_atomic_get_new_plane_state(state, plane);
	struct drm_shadow_plane_state *shadow_plane_state = to_drm_shadow_plane_state(plane_state);
	struct drm_framebuffer *fb = plane_state->fb;
	struct drm_plane_state *old_plane_state = drm_atomic_get_old_plane_state(state, plane);
	struct ast_device *ast = to_ast_device(plane->dev);
	struct iosys_map src_map = shadow_plane_state->data[0];
	struct drm_rect damage;
	const u8 *src = src_map.vaddr; /* TODO: Use mapping abstraction properly */
	u64 dst_off = ast_plane->offset;
	u8 __iomem *dst = ast_plane->vaddr; /* TODO: Use mapping abstraction properly */
	u8 __iomem *sig = dst + AST_HWC_SIZE; /* TODO: Use mapping abstraction properly */
	unsigned int offset_x, offset_y;
	u16 x, y;
	u8 x_offset, y_offset;

	/*
	 * Do data transfer to hardware buffer and point the scanout
	 * engine to the offset.
	 */

	if (drm_atomic_helper_damage_merged(old_plane_state, plane_state, &damage)) {
		ast_update_cursor_image(dst, src, fb->width, fb->height);
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

static int ast_cursor_plane_init(struct ast_device *ast)
{
	struct drm_device *dev = &ast->base;
	struct ast_plane *ast_cursor_plane = &ast->cursor_plane;
	struct drm_plane *cursor_plane = &ast_cursor_plane->base;
	size_t size;
	void __iomem *vaddr;
	u64 offset;
	int ret;

	/*
	 * Allocate backing storage for cursors. The BOs are permanently
	 * pinned to the top end of the VRAM.
	 */

	size = roundup(AST_HWC_SIZE + AST_HWC_SIGNATURE_SIZE, PAGE_SIZE);

	if (ast->vram_fb_available < size)
		return -ENOMEM;

	vaddr = ast->vram + ast->vram_fb_available - size;
	offset = ast->vram_fb_available - size;

	ret = ast_plane_init(dev, ast_cursor_plane, vaddr, offset, size,
			     0x01, &ast_cursor_plane_funcs,
			     ast_cursor_plane_formats, ARRAY_SIZE(ast_cursor_plane_formats),
			     NULL, DRM_PLANE_TYPE_CURSOR);
	if (ret) {
		drm_err(dev, "ast_plane_init() failed: %d\n", ret);
		return ret;
	}
	drm_plane_helper_add(cursor_plane, &ast_cursor_plane_helper_funcs);
	drm_plane_enable_fb_damage_clips(cursor_plane);

	ast->vram_fb_available -= size;

	return 0;
}

/*
 * CRTC
 */

static void ast_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct ast_device *ast = to_ast_device(crtc->dev);
	u8 ch = AST_DPMS_VSYNC_OFF | AST_DPMS_HSYNC_OFF;
	struct ast_crtc_state *ast_state;
	const struct drm_format_info *format;
	struct ast_vbios_mode_info *vbios_mode_info;

	/* TODO: Maybe control display signal generation with
	 *       Sync Enable (bit CR17.7).
	 */
	switch (mode) {
	case DRM_MODE_DPMS_ON:
		ast_set_index_reg_mask(ast, AST_IO_VGASRI,  0x01, 0xdf, 0);
		ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xb6, 0xfc, 0);
		if (ast->tx_chip_types & AST_TX_DP501_BIT)
			ast_set_dp501_video_output(crtc->dev, 1);

		if (ast->tx_chip_types & AST_TX_ASTDP_BIT) {
			ast_dp_power_on_off(crtc->dev, AST_DP_POWER_ON);
			ast_wait_for_vretrace(ast);
			ast_dp_set_on_off(crtc->dev, 1);
		}

		ast_state = to_ast_crtc_state(crtc->state);
		format = ast_state->format;

		if (format) {
			vbios_mode_info = &ast_state->vbios_mode_info;

			ast_set_color_reg(ast, format);
			ast_set_vbios_color_reg(ast, format, vbios_mode_info);
			if (crtc->state->gamma_lut)
				ast_crtc_set_gamma(ast, format, crtc->state->gamma_lut->data);
			else
				ast_crtc_set_gamma_linear(ast, format);
		}
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		ch = mode;
		if (ast->tx_chip_types & AST_TX_DP501_BIT)
			ast_set_dp501_video_output(crtc->dev, 0);

		if (ast->tx_chip_types & AST_TX_ASTDP_BIT) {
			ast_dp_set_on_off(crtc->dev, 0);
			ast_dp_power_on_off(crtc->dev, AST_DP_POWER_OFF);
		}

		ast_set_index_reg_mask(ast, AST_IO_VGASRI,  0x01, 0xdf, 0x20);
		ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xb6, 0xfc, ch);
		break;
	}
}

static enum drm_mode_status
ast_crtc_helper_mode_valid(struct drm_crtc *crtc, const struct drm_display_mode *mode)
{
	struct ast_device *ast = to_ast_device(crtc->dev);
	enum drm_mode_status status;
	uint32_t jtemp;

	if (ast->support_wide_screen) {
		if ((mode->hdisplay == 1680) && (mode->vdisplay == 1050))
			return MODE_OK;
		if ((mode->hdisplay == 1280) && (mode->vdisplay == 800))
			return MODE_OK;
		if ((mode->hdisplay == 1440) && (mode->vdisplay == 900))
			return MODE_OK;
		if ((mode->hdisplay == 1360) && (mode->vdisplay == 768))
			return MODE_OK;
		if ((mode->hdisplay == 1600) && (mode->vdisplay == 900))
			return MODE_OK;
		if ((mode->hdisplay == 1152) && (mode->vdisplay == 864))
			return MODE_OK;

		if ((ast->chip == AST2100) || // GEN2, but not AST1100 (?)
		    (ast->chip == AST2200) || // GEN3, but not AST2150 (?)
		    IS_AST_GEN4(ast) || IS_AST_GEN5(ast) ||
		    IS_AST_GEN6(ast) || IS_AST_GEN7(ast)) {
			if ((mode->hdisplay == 1920) && (mode->vdisplay == 1080))
				return MODE_OK;

			if ((mode->hdisplay == 1920) && (mode->vdisplay == 1200)) {
				jtemp = ast_get_index_reg_mask(ast, AST_IO_VGACRI, 0xd1, 0xff);
				if (jtemp & 0x01)
					return MODE_NOMODE;
				else
					return MODE_OK;
			}
		}
	}

	status = MODE_NOMODE;

	switch (mode->hdisplay) {
	case 640:
		if (mode->vdisplay == 480)
			status = MODE_OK;
		break;
	case 800:
		if (mode->vdisplay == 600)
			status = MODE_OK;
		break;
	case 1024:
		if (mode->vdisplay == 768)
			status = MODE_OK;
		break;
	case 1152:
		if (mode->vdisplay == 864)
			status = MODE_OK;
		break;
	case 1280:
		if (mode->vdisplay == 1024)
			status = MODE_OK;
		break;
	case 1600:
		if (mode->vdisplay == 1200)
			status = MODE_OK;
		break;
	default:
		break;
	}

	return status;
}

static int ast_crtc_helper_atomic_check(struct drm_crtc *crtc,
					struct drm_atomic_state *state)
{
	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
	struct drm_crtc_state *old_crtc_state = drm_atomic_get_old_crtc_state(state, crtc);
	struct ast_crtc_state *old_ast_crtc_state = to_ast_crtc_state(old_crtc_state);
	struct drm_device *dev = crtc->dev;
	struct ast_crtc_state *ast_state;
	const struct drm_format_info *format;
	bool succ;
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

	succ = ast_get_vbios_mode_info(format, &crtc_state->mode,
				       &crtc_state->adjusted_mode,
				       &ast_state->vbios_mode_info);
	if (!succ)
		return -EINVAL;

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
	struct ast_vbios_mode_info *vbios_mode_info = &ast_crtc_state->vbios_mode_info;

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

	//Set Aspeed Display-Port
	if (ast->tx_chip_types & AST_TX_ASTDP_BIT)
		ast_dp_set_mode(crtc, vbios_mode_info);
}

static void ast_crtc_helper_atomic_enable(struct drm_crtc *crtc, struct drm_atomic_state *state)
{
	struct drm_device *dev = crtc->dev;
	struct ast_device *ast = to_ast_device(dev);
	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
	struct ast_crtc_state *ast_crtc_state = to_ast_crtc_state(crtc_state);
	struct ast_vbios_mode_info *vbios_mode_info =
		&ast_crtc_state->vbios_mode_info;
	struct drm_display_mode *adjusted_mode = &crtc_state->adjusted_mode;

	ast_set_vbios_mode_reg(ast, adjusted_mode, vbios_mode_info);
	ast_set_index_reg(ast, AST_IO_VGACRI, 0xa1, 0x06);
	ast_set_std_reg(ast, adjusted_mode, vbios_mode_info);
	ast_set_crtc_reg(ast, adjusted_mode, vbios_mode_info);
	ast_set_dclk_reg(ast, adjusted_mode, vbios_mode_info);
	ast_set_crtthd_reg(ast);
	ast_set_sync_reg(ast, adjusted_mode, vbios_mode_info);

	ast_crtc_dpms(crtc, DRM_MODE_DPMS_ON);
}

static void ast_crtc_helper_atomic_disable(struct drm_crtc *crtc, struct drm_atomic_state *state)
{
	struct drm_crtc_state *old_crtc_state = drm_atomic_get_old_crtc_state(state, crtc);
	struct drm_device *dev = crtc->dev;
	struct ast_device *ast = to_ast_device(dev);

	ast_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);

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

	/*
	 * Ensure that no scanout takes place before reprogramming mode
	 * and format registers.
	 */
	ast_wait_for_vretrace(ast);
}

static const struct drm_crtc_helper_funcs ast_crtc_helper_funcs = {
	.mode_valid = ast_crtc_helper_mode_valid,
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
	memcpy(&new_ast_state->vbios_mode_info, &ast_state->vbios_mode_info,
	       sizeof(new_ast_state->vbios_mode_info));

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

static int ast_crtc_init(struct drm_device *dev)
{
	struct ast_device *ast = to_ast_device(dev);
	struct drm_crtc *crtc = &ast->crtc;
	int ret;

	ret = drm_crtc_init_with_planes(dev, crtc, &ast->primary_plane.base,
					&ast->cursor_plane.base, &ast_crtc_funcs,
					NULL);
	if (ret)
		return ret;

	drm_mode_crtc_set_gamma_size(crtc, AST_LUT_SIZE);
	drm_crtc_enable_color_mgmt(crtc, 0, false, AST_LUT_SIZE);

	drm_crtc_helper_add(crtc, &ast_crtc_helper_funcs);

	return 0;
}

/*
 * VGA Connector
 */

static int ast_vga_connector_helper_get_modes(struct drm_connector *connector)
{
	struct ast_vga_connector *ast_vga_connector = to_ast_vga_connector(connector);
	struct drm_device *dev = connector->dev;
	struct ast_device *ast = to_ast_device(dev);
	struct edid *edid;
	int count;

	if (!ast_vga_connector->i2c)
		goto err_drm_connector_update_edid_property;

	/*
	 * Protect access to I/O registers from concurrent modesetting
	 * by acquiring the I/O-register lock.
	 */
	mutex_lock(&ast->ioregs_lock);

	edid = drm_get_edid(connector, &ast_vga_connector->i2c->adapter);
	if (!edid)
		goto err_mutex_unlock;

	mutex_unlock(&ast->ioregs_lock);

	count = drm_add_edid_modes(connector, edid);
	kfree(edid);

	return count;

err_mutex_unlock:
	mutex_unlock(&ast->ioregs_lock);
err_drm_connector_update_edid_property:
	drm_connector_update_edid_property(connector, NULL);
	return 0;
}

static const struct drm_connector_helper_funcs ast_vga_connector_helper_funcs = {
	.get_modes = ast_vga_connector_helper_get_modes,
};

static const struct drm_connector_funcs ast_vga_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int ast_vga_connector_init(struct drm_device *dev,
				  struct ast_vga_connector *ast_vga_connector)
{
	struct drm_connector *connector = &ast_vga_connector->base;
	int ret;

	ast_vga_connector->i2c = ast_i2c_create(dev);
	if (!ast_vga_connector->i2c)
		drm_err(dev, "failed to add ddc bus for connector\n");

	if (ast_vga_connector->i2c)
		ret = drm_connector_init_with_ddc(dev, connector, &ast_vga_connector_funcs,
						  DRM_MODE_CONNECTOR_VGA,
						  &ast_vga_connector->i2c->adapter);
	else
		ret = drm_connector_init(dev, connector, &ast_vga_connector_funcs,
					 DRM_MODE_CONNECTOR_VGA);
	if (ret)
		return ret;

	drm_connector_helper_add(connector, &ast_vga_connector_helper_funcs);

	connector->interlace_allowed = 0;
	connector->doublescan_allowed = 0;

	connector->polled = DRM_CONNECTOR_POLL_CONNECT;

	return 0;
}

static int ast_vga_output_init(struct ast_device *ast)
{
	struct drm_device *dev = &ast->base;
	struct drm_crtc *crtc = &ast->crtc;
	struct drm_encoder *encoder = &ast->output.vga.encoder;
	struct ast_vga_connector *ast_vga_connector = &ast->output.vga.vga_connector;
	struct drm_connector *connector = &ast_vga_connector->base;
	int ret;

	ret = drm_simple_encoder_init(dev, encoder, DRM_MODE_ENCODER_DAC);
	if (ret)
		return ret;
	encoder->possible_crtcs = drm_crtc_mask(crtc);

	ret = ast_vga_connector_init(dev, ast_vga_connector);
	if (ret)
		return ret;

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret)
		return ret;

	return 0;
}

/*
 * SIL164 Connector
 */

static int ast_sil164_connector_helper_get_modes(struct drm_connector *connector)
{
	struct ast_sil164_connector *ast_sil164_connector = to_ast_sil164_connector(connector);
	struct drm_device *dev = connector->dev;
	struct ast_device *ast = to_ast_device(dev);
	struct edid *edid;
	int count;

	if (!ast_sil164_connector->i2c)
		goto err_drm_connector_update_edid_property;

	/*
	 * Protect access to I/O registers from concurrent modesetting
	 * by acquiring the I/O-register lock.
	 */
	mutex_lock(&ast->ioregs_lock);

	edid = drm_get_edid(connector, &ast_sil164_connector->i2c->adapter);
	if (!edid)
		goto err_mutex_unlock;

	mutex_unlock(&ast->ioregs_lock);

	count = drm_add_edid_modes(connector, edid);
	kfree(edid);

	return count;

err_mutex_unlock:
	mutex_unlock(&ast->ioregs_lock);
err_drm_connector_update_edid_property:
	drm_connector_update_edid_property(connector, NULL);
	return 0;
}

static const struct drm_connector_helper_funcs ast_sil164_connector_helper_funcs = {
	.get_modes = ast_sil164_connector_helper_get_modes,
};

static const struct drm_connector_funcs ast_sil164_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int ast_sil164_connector_init(struct drm_device *dev,
				     struct ast_sil164_connector *ast_sil164_connector)
{
	struct drm_connector *connector = &ast_sil164_connector->base;
	int ret;

	ast_sil164_connector->i2c = ast_i2c_create(dev);
	if (!ast_sil164_connector->i2c)
		drm_err(dev, "failed to add ddc bus for connector\n");

	if (ast_sil164_connector->i2c)
		ret = drm_connector_init_with_ddc(dev, connector, &ast_sil164_connector_funcs,
						  DRM_MODE_CONNECTOR_DVII,
						  &ast_sil164_connector->i2c->adapter);
	else
		ret = drm_connector_init(dev, connector, &ast_sil164_connector_funcs,
					 DRM_MODE_CONNECTOR_DVII);
	if (ret)
		return ret;

	drm_connector_helper_add(connector, &ast_sil164_connector_helper_funcs);

	connector->interlace_allowed = 0;
	connector->doublescan_allowed = 0;

	connector->polled = DRM_CONNECTOR_POLL_CONNECT;

	return 0;
}

static int ast_sil164_output_init(struct ast_device *ast)
{
	struct drm_device *dev = &ast->base;
	struct drm_crtc *crtc = &ast->crtc;
	struct drm_encoder *encoder = &ast->output.sil164.encoder;
	struct ast_sil164_connector *ast_sil164_connector = &ast->output.sil164.sil164_connector;
	struct drm_connector *connector = &ast_sil164_connector->base;
	int ret;

	ret = drm_simple_encoder_init(dev, encoder, DRM_MODE_ENCODER_TMDS);
	if (ret)
		return ret;
	encoder->possible_crtcs = drm_crtc_mask(crtc);

	ret = ast_sil164_connector_init(dev, ast_sil164_connector);
	if (ret)
		return ret;

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret)
		return ret;

	return 0;
}

/*
 * DP501 Connector
 */

static int ast_dp501_connector_helper_get_modes(struct drm_connector *connector)
{
	void *edid;
	bool succ;
	int count;

	edid = kmalloc(EDID_LENGTH, GFP_KERNEL);
	if (!edid)
		goto err_drm_connector_update_edid_property;

	succ = ast_dp501_read_edid(connector->dev, edid);
	if (!succ)
		goto err_kfree;

	drm_connector_update_edid_property(connector, edid);
	count = drm_add_edid_modes(connector, edid);
	kfree(edid);

	return count;

err_kfree:
	kfree(edid);
err_drm_connector_update_edid_property:
	drm_connector_update_edid_property(connector, NULL);
	return 0;
}

static int ast_dp501_connector_helper_detect_ctx(struct drm_connector *connector,
						 struct drm_modeset_acquire_ctx *ctx,
						 bool force)
{
	struct ast_device *ast = to_ast_device(connector->dev);

	if (ast_dp501_is_connected(ast))
		return connector_status_connected;
	return connector_status_disconnected;
}

static const struct drm_connector_helper_funcs ast_dp501_connector_helper_funcs = {
	.get_modes = ast_dp501_connector_helper_get_modes,
	.detect_ctx = ast_dp501_connector_helper_detect_ctx,
};

static const struct drm_connector_funcs ast_dp501_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int ast_dp501_connector_init(struct drm_device *dev, struct drm_connector *connector)
{
	int ret;

	ret = drm_connector_init(dev, connector, &ast_dp501_connector_funcs,
				 DRM_MODE_CONNECTOR_DisplayPort);
	if (ret)
		return ret;

	drm_connector_helper_add(connector, &ast_dp501_connector_helper_funcs);

	connector->interlace_allowed = 0;
	connector->doublescan_allowed = 0;

	connector->polled = DRM_CONNECTOR_POLL_CONNECT | DRM_CONNECTOR_POLL_DISCONNECT;

	return 0;
}

static int ast_dp501_output_init(struct ast_device *ast)
{
	struct drm_device *dev = &ast->base;
	struct drm_crtc *crtc = &ast->crtc;
	struct drm_encoder *encoder = &ast->output.dp501.encoder;
	struct drm_connector *connector = &ast->output.dp501.connector;
	int ret;

	ret = drm_simple_encoder_init(dev, encoder, DRM_MODE_ENCODER_TMDS);
	if (ret)
		return ret;
	encoder->possible_crtcs = drm_crtc_mask(crtc);

	ret = ast_dp501_connector_init(dev, connector);
	if (ret)
		return ret;

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret)
		return ret;

	return 0;
}

/*
 * ASPEED Display-Port Connector
 */

static int ast_astdp_connector_helper_get_modes(struct drm_connector *connector)
{
	void *edid;
	struct drm_device *dev = connector->dev;
	struct ast_device *ast = to_ast_device(dev);

	int succ;
	int count;

	edid = kmalloc(EDID_LENGTH, GFP_KERNEL);
	if (!edid)
		goto err_drm_connector_update_edid_property;

	/*
	 * Protect access to I/O registers from concurrent modesetting
	 * by acquiring the I/O-register lock.
	 */
	mutex_lock(&ast->ioregs_lock);

	succ = ast_astdp_read_edid(connector->dev, edid);
	if (succ < 0)
		goto err_mutex_unlock;

	mutex_unlock(&ast->ioregs_lock);

	drm_connector_update_edid_property(connector, edid);
	count = drm_add_edid_modes(connector, edid);
	kfree(edid);

	return count;

err_mutex_unlock:
	mutex_unlock(&ast->ioregs_lock);
	kfree(edid);
err_drm_connector_update_edid_property:
	drm_connector_update_edid_property(connector, NULL);
	return 0;
}

static int ast_astdp_connector_helper_detect_ctx(struct drm_connector *connector,
						 struct drm_modeset_acquire_ctx *ctx,
						 bool force)
{
	struct ast_device *ast = to_ast_device(connector->dev);

	if (ast_astdp_is_connected(ast))
		return connector_status_connected;
	return connector_status_disconnected;
}

static const struct drm_connector_helper_funcs ast_astdp_connector_helper_funcs = {
	.get_modes = ast_astdp_connector_helper_get_modes,
	.detect_ctx = ast_astdp_connector_helper_detect_ctx,
};

static const struct drm_connector_funcs ast_astdp_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int ast_astdp_connector_init(struct drm_device *dev, struct drm_connector *connector)
{
	int ret;

	ret = drm_connector_init(dev, connector, &ast_astdp_connector_funcs,
				 DRM_MODE_CONNECTOR_DisplayPort);
	if (ret)
		return ret;

	drm_connector_helper_add(connector, &ast_astdp_connector_helper_funcs);

	connector->interlace_allowed = 0;
	connector->doublescan_allowed = 0;

	connector->polled = DRM_CONNECTOR_POLL_CONNECT | DRM_CONNECTOR_POLL_DISCONNECT;

	return 0;
}

static int ast_astdp_output_init(struct ast_device *ast)
{
	struct drm_device *dev = &ast->base;
	struct drm_crtc *crtc = &ast->crtc;
	struct drm_encoder *encoder = &ast->output.astdp.encoder;
	struct drm_connector *connector = &ast->output.astdp.connector;
	int ret;

	ret = drm_simple_encoder_init(dev, encoder, DRM_MODE_ENCODER_TMDS);
	if (ret)
		return ret;
	encoder->possible_crtcs = drm_crtc_mask(crtc);

	ret = ast_astdp_connector_init(dev, connector);
	if (ret)
		return ret;

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret)
		return ret;

	return 0;
}

/*
 * BMC virtual Connector
 */

static const struct drm_encoder_funcs ast_bmc_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int ast_bmc_connector_helper_detect_ctx(struct drm_connector *connector,
					       struct drm_modeset_acquire_ctx *ctx,
					       bool force)
{
	struct ast_bmc_connector *bmc_connector = to_ast_bmc_connector(connector);
	struct drm_connector *physical_connector = bmc_connector->physical_connector;

	/*
	 * Most user-space compositors cannot handle more than one connected
	 * connector per CRTC. Hence, we only mark the BMC as connected if the
	 * physical connector is disconnected. If the physical connector's status
	 * is connected or unknown, the BMC remains disconnected. This has no
	 * effect on the output of the BMC.
	 *
	 * FIXME: Remove this logic once user-space compositors can handle more
	 *        than one connector per CRTC. The BMC should always be connected.
	 */

	if (physical_connector && physical_connector->status == connector_status_disconnected)
		return connector_status_connected;

	return connector_status_disconnected;
}

static int ast_bmc_connector_helper_get_modes(struct drm_connector *connector)
{
	return drm_add_modes_noedid(connector, 4096, 4096);
}

static const struct drm_connector_helper_funcs ast_bmc_connector_helper_funcs = {
	.get_modes = ast_bmc_connector_helper_get_modes,
	.detect_ctx = ast_bmc_connector_helper_detect_ctx,
};

static const struct drm_connector_funcs ast_bmc_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int ast_bmc_connector_init(struct drm_device *dev,
				  struct ast_bmc_connector *bmc_connector,
				  struct drm_connector *physical_connector)
{
	struct drm_connector *connector = &bmc_connector->base;
	int ret;

	ret = drm_connector_init(dev, connector, &ast_bmc_connector_funcs,
				 DRM_MODE_CONNECTOR_VIRTUAL);
	if (ret)
		return ret;

	drm_connector_helper_add(connector, &ast_bmc_connector_helper_funcs);

	bmc_connector->physical_connector = physical_connector;

	return 0;
}

static int ast_bmc_output_init(struct ast_device *ast,
			       struct drm_connector *physical_connector)
{
	struct drm_device *dev = &ast->base;
	struct drm_crtc *crtc = &ast->crtc;
	struct drm_encoder *encoder = &ast->output.bmc.encoder;
	struct ast_bmc_connector *bmc_connector = &ast->output.bmc.bmc_connector;
	struct drm_connector *connector = &bmc_connector->base;
	int ret;

	ret = drm_encoder_init(dev, encoder,
			       &ast_bmc_encoder_funcs,
			       DRM_MODE_ENCODER_VIRTUAL, "ast_bmc");
	if (ret)
		return ret;
	encoder->possible_crtcs = drm_crtc_mask(crtc);

	ret = ast_bmc_connector_init(dev, bmc_connector, physical_connector);
	if (ret)
		return ret;

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret)
		return ret;

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
	 * drm_connector_helper_funcs.get_modes by trying to read the
	 * display modes. Protect access to I/O registers by acquiring
	 * the I/O-register lock. Released in atomic_flush().
	 */
	mutex_lock(&ast->ioregs_lock);
	drm_atomic_helper_commit_tail_rpm(state);
	mutex_unlock(&ast->ioregs_lock);
}

static const struct drm_mode_config_helper_funcs ast_mode_config_helper_funcs = {
	.atomic_commit_tail = ast_mode_config_helper_atomic_commit_tail,
};

static enum drm_mode_status ast_mode_config_mode_valid(struct drm_device *dev,
						       const struct drm_display_mode *mode)
{
	static const unsigned long max_bpp = 4; /* DRM_FORMAT_XRGB8888 */
	struct ast_device *ast = to_ast_device(dev);
	unsigned long fbsize, fbpages, max_fbpages;

	max_fbpages = (ast->vram_fb_available) >> PAGE_SHIFT;

	fbsize = mode->hdisplay * mode->vdisplay * max_bpp;
	fbpages = DIV_ROUND_UP(fbsize, PAGE_SIZE);

	if (fbpages > max_fbpages)
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
	struct drm_connector *physical_connector = NULL;
	int ret;

	ret = drmm_mode_config_init(dev);
	if (ret)
		return ret;

	dev->mode_config.funcs = &ast_mode_config_funcs;
	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	dev->mode_config.preferred_depth = 24;

	if (ast->chip == AST2100 || // GEN2, but not AST1100 (?)
	    ast->chip == AST2200 || // GEN3, but not AST2150 (?)
	    IS_AST_GEN7(ast) ||
	    IS_AST_GEN6(ast) ||
	    IS_AST_GEN5(ast) ||
	    IS_AST_GEN4(ast)) {
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

	ast_crtc_init(dev);

	if (ast->tx_chip_types & AST_TX_NONE_BIT) {
		ret = ast_vga_output_init(ast);
		if (ret)
			return ret;
		physical_connector = &ast->output.vga.vga_connector.base;
	}
	if (ast->tx_chip_types & AST_TX_SIL164_BIT) {
		ret = ast_sil164_output_init(ast);
		if (ret)
			return ret;
		physical_connector = &ast->output.sil164.sil164_connector.base;
	}
	if (ast->tx_chip_types & AST_TX_DP501_BIT) {
		ret = ast_dp501_output_init(ast);
		if (ret)
			return ret;
		physical_connector = &ast->output.dp501.connector;
	}
	if (ast->tx_chip_types & AST_TX_ASTDP_BIT) {
		ret = ast_astdp_output_init(ast);
		if (ret)
			return ret;
		physical_connector = &ast->output.astdp.connector;
	}
	ret = ast_bmc_output_init(ast, physical_connector);
	if (ret)
		return ret;

	drm_mode_config_reset(dev);

	drm_kms_helper_poll_init(dev);

	return 0;
}
