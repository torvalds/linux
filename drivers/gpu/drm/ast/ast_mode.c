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
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_vram_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include "ast_drv.h"
#include "ast_tables.h"

static struct ast_i2c_chan *ast_i2c_create(struct drm_device *dev);
static void ast_i2c_destroy(struct ast_i2c_chan *i2c);

static inline void ast_load_palette_index(struct ast_private *ast,
				     u8 index, u8 red, u8 green,
				     u8 blue)
{
	ast_io_write8(ast, AST_IO_DAC_INDEX_WRITE, index);
	ast_io_read8(ast, AST_IO_SEQ_PORT);
	ast_io_write8(ast, AST_IO_DAC_DATA, red);
	ast_io_read8(ast, AST_IO_SEQ_PORT);
	ast_io_write8(ast, AST_IO_DAC_DATA, green);
	ast_io_read8(ast, AST_IO_SEQ_PORT);
	ast_io_write8(ast, AST_IO_DAC_DATA, blue);
	ast_io_read8(ast, AST_IO_SEQ_PORT);
}

static void ast_crtc_load_lut(struct ast_private *ast, struct drm_crtc *crtc)
{
	u16 *r, *g, *b;
	int i;

	if (!crtc->enabled)
		return;

	r = crtc->gamma_store;
	g = r + crtc->gamma_size;
	b = g + crtc->gamma_size;

	for (i = 0; i < 256; i++)
		ast_load_palette_index(ast, i, *r++ >> 8, *g++ >> 8, *b++ >> 8);
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

static void ast_set_vbios_color_reg(struct ast_private *ast,
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

	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0x8c, (u8)((color_index & 0x0f) << 4));

	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0x91, 0x00);

	if (vbios_mode->enh_table->flags & NewModeInfo) {
		ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0x91, 0xa8);
		ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0x92, format->cpp[0] * 8);
	}
}

static void ast_set_vbios_mode_reg(struct ast_private *ast,
				   const struct drm_display_mode *adjusted_mode,
				   const struct ast_vbios_mode_info *vbios_mode)
{
	u32 refresh_rate_index, mode_id;

	refresh_rate_index = vbios_mode->enh_table->refresh_rate_index;
	mode_id = vbios_mode->enh_table->mode_id;

	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0x8d, refresh_rate_index & 0xff);
	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0x8e, mode_id & 0xff);

	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0x91, 0x00);

	if (vbios_mode->enh_table->flags & NewModeInfo) {
		ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0x91, 0xa8);
		ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0x93, adjusted_mode->clock / 1000);
		ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0x94, adjusted_mode->crtc_hdisplay);
		ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0x95, adjusted_mode->crtc_hdisplay >> 8);
		ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0x96, adjusted_mode->crtc_vdisplay);
		ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0x97, adjusted_mode->crtc_vdisplay >> 8);
	}
}

static void ast_set_std_reg(struct ast_private *ast,
			    struct drm_display_mode *mode,
			    struct ast_vbios_mode_info *vbios_mode)
{
	const struct ast_vbios_stdtable *stdtable;
	u32 i;
	u8 jreg;

	stdtable = vbios_mode->std_table;

	jreg = stdtable->misc;
	ast_io_write8(ast, AST_IO_MISC_PORT_WRITE, jreg);

	/* Set SEQ; except Screen Disable field */
	ast_set_index_reg(ast, AST_IO_SEQ_PORT, 0x00, 0x03);
	ast_set_index_reg_mask(ast, AST_IO_SEQ_PORT, 0x01, 0xdf, stdtable->seq[0]);
	for (i = 1; i < 4; i++) {
		jreg = stdtable->seq[i];
		ast_set_index_reg(ast, AST_IO_SEQ_PORT, (i + 1) , jreg);
	}

	/* Set CRTC; except base address and offset */
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0x11, 0x7f, 0x00);
	for (i = 0; i < 12; i++)
		ast_set_index_reg(ast, AST_IO_CRTC_PORT, i, stdtable->crtc[i]);
	for (i = 14; i < 19; i++)
		ast_set_index_reg(ast, AST_IO_CRTC_PORT, i, stdtable->crtc[i]);
	for (i = 20; i < 25; i++)
		ast_set_index_reg(ast, AST_IO_CRTC_PORT, i, stdtable->crtc[i]);

	/* set AR */
	jreg = ast_io_read8(ast, AST_IO_INPUT_STATUS1_READ);
	for (i = 0; i < 20; i++) {
		jreg = stdtable->ar[i];
		ast_io_write8(ast, AST_IO_AR_PORT_WRITE, (u8)i);
		ast_io_write8(ast, AST_IO_AR_PORT_WRITE, jreg);
	}
	ast_io_write8(ast, AST_IO_AR_PORT_WRITE, 0x14);
	ast_io_write8(ast, AST_IO_AR_PORT_WRITE, 0x00);

	jreg = ast_io_read8(ast, AST_IO_INPUT_STATUS1_READ);
	ast_io_write8(ast, AST_IO_AR_PORT_WRITE, 0x20);

	/* Set GR */
	for (i = 0; i < 9; i++)
		ast_set_index_reg(ast, AST_IO_GR_PORT, i, stdtable->gr[i]);
}

static void ast_set_crtc_reg(struct ast_private *ast,
			     struct drm_display_mode *mode,
			     struct ast_vbios_mode_info *vbios_mode)
{
	u8 jreg05 = 0, jreg07 = 0, jreg09 = 0, jregAC = 0, jregAD = 0, jregAE = 0;
	u16 temp, precache = 0;

	if ((ast->chip == AST2500) &&
	    (vbios_mode->enh_table->flags & AST2500PreCatchCRT))
		precache = 40;

	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0x11, 0x7f, 0x00);

	temp = (mode->crtc_htotal >> 3) - 5;
	if (temp & 0x100)
		jregAC |= 0x01; /* HT D[8] */
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0x00, 0x00, temp);

	temp = (mode->crtc_hdisplay >> 3) - 1;
	if (temp & 0x100)
		jregAC |= 0x04; /* HDE D[8] */
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0x01, 0x00, temp);

	temp = (mode->crtc_hblank_start >> 3) - 1;
	if (temp & 0x100)
		jregAC |= 0x10; /* HBS D[8] */
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0x02, 0x00, temp);

	temp = ((mode->crtc_hblank_end >> 3) - 1) & 0x7f;
	if (temp & 0x20)
		jreg05 |= 0x80;  /* HBE D[5] */
	if (temp & 0x40)
		jregAD |= 0x01;  /* HBE D[5] */
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0x03, 0xE0, (temp & 0x1f));

	temp = ((mode->crtc_hsync_start-precache) >> 3) - 1;
	if (temp & 0x100)
		jregAC |= 0x40; /* HRS D[5] */
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0x04, 0x00, temp);

	temp = (((mode->crtc_hsync_end-precache) >> 3) - 1) & 0x3f;
	if (temp & 0x20)
		jregAD |= 0x04; /* HRE D[5] */
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0x05, 0x60, (u8)((temp & 0x1f) | jreg05));

	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xAC, 0x00, jregAC);
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xAD, 0x00, jregAD);

	/* vert timings */
	temp = (mode->crtc_vtotal) - 2;
	if (temp & 0x100)
		jreg07 |= 0x01;
	if (temp & 0x200)
		jreg07 |= 0x20;
	if (temp & 0x400)
		jregAE |= 0x01;
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0x06, 0x00, temp);

	temp = (mode->crtc_vsync_start) - 1;
	if (temp & 0x100)
		jreg07 |= 0x04;
	if (temp & 0x200)
		jreg07 |= 0x80;
	if (temp & 0x400)
		jregAE |= 0x08;
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0x10, 0x00, temp);

	temp = (mode->crtc_vsync_end - 1) & 0x3f;
	if (temp & 0x10)
		jregAE |= 0x20;
	if (temp & 0x20)
		jregAE |= 0x40;
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0x11, 0x70, temp & 0xf);

	temp = mode->crtc_vdisplay - 1;
	if (temp & 0x100)
		jreg07 |= 0x02;
	if (temp & 0x200)
		jreg07 |= 0x40;
	if (temp & 0x400)
		jregAE |= 0x02;
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0x12, 0x00, temp);

	temp = mode->crtc_vblank_start - 1;
	if (temp & 0x100)
		jreg07 |= 0x08;
	if (temp & 0x200)
		jreg09 |= 0x20;
	if (temp & 0x400)
		jregAE |= 0x04;
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0x15, 0x00, temp);

	temp = mode->crtc_vblank_end - 1;
	if (temp & 0x100)
		jregAE |= 0x10;
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0x16, 0x00, temp);

	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0x07, 0x00, jreg07);
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0x09, 0xdf, jreg09);
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xAE, 0x00, (jregAE | 0x80));

	if (precache)
		ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xb6, 0x3f, 0x80);
	else
		ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xb6, 0x3f, 0x00);

	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0x11, 0x7f, 0x80);
}

static void ast_set_offset_reg(struct ast_private *ast,
			       struct drm_framebuffer *fb)
{
	u16 offset;

	offset = fb->pitches[0] >> 3;
	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0x13, (offset & 0xff));
	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xb0, (offset >> 8) & 0x3f);
}

static void ast_set_dclk_reg(struct ast_private *ast,
			     struct drm_display_mode *mode,
			     struct ast_vbios_mode_info *vbios_mode)
{
	const struct ast_vbios_dclk_info *clk_info;

	if (ast->chip == AST2500)
		clk_info = &dclk_table_ast2500[vbios_mode->enh_table->dclk_index];
	else
		clk_info = &dclk_table[vbios_mode->enh_table->dclk_index];

	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xc0, 0x00, clk_info->param1);
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xc1, 0x00, clk_info->param2);
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xbb, 0x0f,
			       (clk_info->param3 & 0xc0) |
			       ((clk_info->param3 & 0x3) << 4));
}

static void ast_set_color_reg(struct ast_private *ast,
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

	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xa0, 0x8f, jregA0);
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xa3, 0xf0, jregA3);
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xa8, 0xfd, jregA8);
}

static void ast_set_crtthd_reg(struct ast_private *ast)
{
	/* Set Threshold */
	if (ast->chip == AST2300 || ast->chip == AST2400 ||
	    ast->chip == AST2500) {
		ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xa7, 0x78);
		ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xa6, 0x60);
	} else if (ast->chip == AST2100 ||
		   ast->chip == AST1100 ||
		   ast->chip == AST2200 ||
		   ast->chip == AST2150) {
		ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xa7, 0x3f);
		ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xa6, 0x2f);
	} else {
		ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xa7, 0x2f);
		ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xa6, 0x1f);
	}
}

static void ast_set_sync_reg(struct ast_private *ast,
			     struct drm_display_mode *mode,
			     struct ast_vbios_mode_info *vbios_mode)
{
	u8 jreg;

	jreg  = ast_io_read8(ast, AST_IO_MISC_PORT_READ);
	jreg &= ~0xC0;
	if (vbios_mode->enh_table->flags & NVSync) jreg |= 0x80;
	if (vbios_mode->enh_table->flags & NHSync) jreg |= 0x40;
	ast_io_write8(ast, AST_IO_MISC_PORT_WRITE, jreg);
}

static void ast_set_start_address_crt1(struct ast_private *ast,
				       unsigned offset)
{
	u32 addr;

	addr = offset >> 2;
	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0x0d, (u8)(addr & 0xff));
	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0x0c, (u8)((addr >> 8) & 0xff));
	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xaf, (u8)((addr >> 16) & 0xff));

}

static void ast_wait_for_vretrace(struct ast_private *ast)
{
	unsigned long timeout = jiffies + HZ;
	u8 vgair1;

	do {
		vgair1 = ast_io_read8(ast, AST_IO_INPUT_STATUS1_READ);
	} while (!(vgair1 & AST_IO_VGAIR1_VREFRESH) && time_before(jiffies, timeout));
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
						 struct drm_plane_state *state)
{
	struct drm_crtc_state *crtc_state;
	struct ast_crtc_state *ast_crtc_state;
	int ret;

	if (!state->crtc)
		return 0;

	crtc_state = drm_atomic_get_new_crtc_state(state->state, state->crtc);

	ret = drm_atomic_helper_check_plane_state(state, crtc_state,
						  DRM_PLANE_HELPER_NO_SCALING,
						  DRM_PLANE_HELPER_NO_SCALING,
						  false, true);
	if (ret)
		return ret;

	if (!state->visible)
		return 0;

	ast_crtc_state = to_ast_crtc_state(crtc_state);

	ast_crtc_state->format = state->fb->format;

	return 0;
}

static void
ast_primary_plane_helper_atomic_update(struct drm_plane *plane,
				       struct drm_plane_state *old_state)
{
	struct drm_device *dev = plane->dev;
	struct ast_private *ast = to_ast_private(dev);
	struct drm_plane_state *state = plane->state;
	struct drm_gem_vram_object *gbo;
	s64 gpu_addr;
	struct drm_framebuffer *fb = state->fb;
	struct drm_framebuffer *old_fb = old_state->fb;

	if (!old_fb || (fb->format != old_fb->format)) {
		struct drm_crtc_state *crtc_state = state->crtc->state;
		struct ast_crtc_state *ast_crtc_state = to_ast_crtc_state(crtc_state);
		struct ast_vbios_mode_info *vbios_mode_info = &ast_crtc_state->vbios_mode_info;

		ast_set_color_reg(ast, fb->format);
		ast_set_vbios_color_reg(ast, fb->format, vbios_mode_info);
	}

	gbo = drm_gem_vram_of_gem(fb->obj[0]);
	gpu_addr = drm_gem_vram_offset(gbo);
	if (drm_WARN_ON_ONCE(dev, gpu_addr < 0))
		return; /* Bug: we didn't pin the BO to VRAM in prepare_fb. */

	ast_set_offset_reg(ast, fb);
	ast_set_start_address_crt1(ast, (u32)gpu_addr);

	ast_set_index_reg_mask(ast, AST_IO_SEQ_PORT, 0x1, 0xdf, 0x00);
}

static void
ast_primary_plane_helper_atomic_disable(struct drm_plane *plane,
					struct drm_plane_state *old_state)
{
	struct ast_private *ast = to_ast_private(plane->dev);

	ast_set_index_reg_mask(ast, AST_IO_SEQ_PORT, 0x1, 0xdf, 0x20);
}

static const struct drm_plane_helper_funcs ast_primary_plane_helper_funcs = {
	.prepare_fb = drm_gem_vram_plane_helper_prepare_fb,
	.cleanup_fb = drm_gem_vram_plane_helper_cleanup_fb,
	.atomic_check = ast_primary_plane_helper_atomic_check,
	.atomic_update = ast_primary_plane_helper_atomic_update,
	.atomic_disable = ast_primary_plane_helper_atomic_disable,
};

static const struct drm_plane_funcs ast_primary_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	.reset = drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
};

/*
 * Cursor plane
 */

static const uint32_t ast_cursor_plane_formats[] = {
	DRM_FORMAT_ARGB8888,
};

static int
ast_cursor_plane_helper_prepare_fb(struct drm_plane *plane,
				   struct drm_plane_state *new_state)
{
	struct drm_framebuffer *fb = new_state->fb;
	struct drm_crtc *crtc = new_state->crtc;
	struct ast_private *ast;
	int ret;

	if (!crtc || !fb)
		return 0;

	ast = to_ast_private(plane->dev);

	ret = ast_cursor_blit(ast, fb);
	if (ret)
		return ret;

	return 0;
}

static int ast_cursor_plane_helper_atomic_check(struct drm_plane *plane,
						struct drm_plane_state *state)
{
	struct drm_framebuffer *fb = state->fb;
	struct drm_crtc_state *crtc_state;
	int ret;

	if (!state->crtc)
		return 0;

	crtc_state = drm_atomic_get_new_crtc_state(state->state, state->crtc);

	ret = drm_atomic_helper_check_plane_state(state, crtc_state,
						  DRM_PLANE_HELPER_NO_SCALING,
						  DRM_PLANE_HELPER_NO_SCALING,
						  true, true);
	if (ret)
		return ret;

	if (!state->visible)
		return 0;

	if (fb->width > AST_MAX_HWC_WIDTH || fb->height > AST_MAX_HWC_HEIGHT)
		return -EINVAL;

	return 0;
}

static void
ast_cursor_plane_helper_atomic_update(struct drm_plane *plane,
				      struct drm_plane_state *old_state)
{
	struct drm_plane_state *state = plane->state;
	struct drm_framebuffer *fb = state->fb;
	struct ast_private *ast = to_ast_private(plane->dev);
	unsigned int offset_x, offset_y;

	offset_x = AST_MAX_HWC_WIDTH - fb->width;
	offset_y = AST_MAX_HWC_WIDTH - fb->height;

	if (state->fb != old_state->fb) {
		/* A new cursor image was installed. */
		ast_cursor_page_flip(ast);
	}

	ast_cursor_show(ast, state->crtc_x, state->crtc_y,
			offset_x, offset_y);
}

static void
ast_cursor_plane_helper_atomic_disable(struct drm_plane *plane,
				       struct drm_plane_state *old_state)
{
	struct ast_private *ast = to_ast_private(plane->dev);

	ast_cursor_hide(ast);
}

static const struct drm_plane_helper_funcs ast_cursor_plane_helper_funcs = {
	.prepare_fb = ast_cursor_plane_helper_prepare_fb,
	.cleanup_fb = NULL, /* not required for cursor plane */
	.atomic_check = ast_cursor_plane_helper_atomic_check,
	.atomic_update = ast_cursor_plane_helper_atomic_update,
	.atomic_disable = ast_cursor_plane_helper_atomic_disable,
};

static const struct drm_plane_funcs ast_cursor_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	.reset = drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
};

/*
 * CRTC
 */

static void ast_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct ast_private *ast = to_ast_private(crtc->dev);

	/* TODO: Maybe control display signal generation with
	 *       Sync Enable (bit CR17.7).
	 */
	switch (mode) {
	case DRM_MODE_DPMS_ON:
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
		if (ast->tx_chip_type == AST_TX_DP501)
			ast_set_dp501_video_output(crtc->dev, 1);
		ast_crtc_load_lut(ast, crtc);
		break;
	case DRM_MODE_DPMS_OFF:
		if (ast->tx_chip_type == AST_TX_DP501)
			ast_set_dp501_video_output(crtc->dev, 0);
		break;
	}
}

static int ast_crtc_helper_atomic_check(struct drm_crtc *crtc,
					struct drm_crtc_state *state)
{
	struct drm_device *dev = crtc->dev;
	struct ast_crtc_state *ast_state;
	const struct drm_format_info *format;
	bool succ;

	if (!state->enable)
		return 0; /* no mode checks if CRTC is being disabled */

	ast_state = to_ast_crtc_state(state);

	format = ast_state->format;
	if (drm_WARN_ON_ONCE(dev, !format))
		return -EINVAL; /* BUG: We didn't set format in primary check(). */

	succ = ast_get_vbios_mode_info(format, &state->mode,
				       &state->adjusted_mode,
				       &ast_state->vbios_mode_info);
	if (!succ)
		return -EINVAL;

	return 0;
}

static void
ast_crtc_helper_atomic_enable(struct drm_crtc *crtc,
			      struct drm_crtc_state *old_crtc_state)
{
	struct drm_device *dev = crtc->dev;
	struct ast_private *ast = to_ast_private(dev);
	struct drm_crtc_state *crtc_state = crtc->state;
	struct ast_crtc_state *ast_crtc_state = to_ast_crtc_state(crtc_state);
	struct ast_vbios_mode_info *vbios_mode_info =
		&ast_crtc_state->vbios_mode_info;
	struct drm_display_mode *adjusted_mode = &crtc_state->adjusted_mode;

	ast_set_vbios_mode_reg(ast, adjusted_mode, vbios_mode_info);
	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xa1, 0x06);
	ast_set_std_reg(ast, adjusted_mode, vbios_mode_info);
	ast_set_crtc_reg(ast, adjusted_mode, vbios_mode_info);
	ast_set_dclk_reg(ast, adjusted_mode, vbios_mode_info);
	ast_set_crtthd_reg(ast);
	ast_set_sync_reg(ast, adjusted_mode, vbios_mode_info);

	ast_crtc_dpms(crtc, DRM_MODE_DPMS_ON);
}

static void
ast_crtc_helper_atomic_disable(struct drm_crtc *crtc,
			       struct drm_crtc_state *old_crtc_state)
{
	struct drm_device *dev = crtc->dev;
	struct ast_private *ast = to_ast_private(dev);

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
	.atomic_check = ast_crtc_helper_atomic_check,
	.atomic_enable = ast_crtc_helper_atomic_enable,
	.atomic_disable = ast_crtc_helper_atomic_disable,
};

static void ast_crtc_reset(struct drm_crtc *crtc)
{
	struct ast_crtc_state *ast_state =
		kzalloc(sizeof(*ast_state), GFP_KERNEL);

	if (crtc->state)
		crtc->funcs->atomic_destroy_state(crtc, crtc->state);

	__drm_atomic_helper_crtc_reset(crtc, &ast_state->base);
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
	.gamma_set = drm_atomic_helper_legacy_gamma_set,
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = ast_crtc_atomic_duplicate_state,
	.atomic_destroy_state = ast_crtc_atomic_destroy_state,
};

static int ast_crtc_init(struct drm_device *dev)
{
	struct ast_private *ast = to_ast_private(dev);
	struct drm_crtc *crtc = &ast->crtc;
	int ret;

	ret = drm_crtc_init_with_planes(dev, crtc, &ast->primary_plane,
					&ast->cursor_plane, &ast_crtc_funcs,
					NULL);
	if (ret)
		return ret;

	drm_mode_crtc_set_gamma_size(crtc, 256);
	drm_crtc_helper_add(crtc, &ast_crtc_helper_funcs);

	return 0;
}

/*
 * Encoder
 */

static int ast_encoder_init(struct drm_device *dev)
{
	struct ast_private *ast = to_ast_private(dev);
	struct drm_encoder *encoder = &ast->encoder;
	int ret;

	ret = drm_simple_encoder_init(dev, encoder, DRM_MODE_ENCODER_DAC);
	if (ret)
		return ret;

	encoder->possible_crtcs = 1;

	return 0;
}

/*
 * Connector
 */

static int ast_get_modes(struct drm_connector *connector)
{
	struct ast_connector *ast_connector = to_ast_connector(connector);
	struct ast_private *ast = to_ast_private(connector->dev);
	struct edid *edid;
	int ret;
	bool flags = false;
	if (ast->tx_chip_type == AST_TX_DP501) {
		ast->dp501_maxclk = 0xff;
		edid = kmalloc(128, GFP_KERNEL);
		if (!edid)
			return -ENOMEM;

		flags = ast_dp501_read_edid(connector->dev, (u8 *)edid);
		if (flags)
			ast->dp501_maxclk = ast_get_dp501_max_clk(connector->dev);
		else
			kfree(edid);
	}
	if (!flags)
		edid = drm_get_edid(connector, &ast_connector->i2c->adapter);
	if (edid) {
		drm_connector_update_edid_property(&ast_connector->base, edid);
		ret = drm_add_edid_modes(connector, edid);
		kfree(edid);
		return ret;
	} else
		drm_connector_update_edid_property(&ast_connector->base, NULL);
	return 0;
}

static enum drm_mode_status ast_mode_valid(struct drm_connector *connector,
			  struct drm_display_mode *mode)
{
	struct ast_private *ast = to_ast_private(connector->dev);
	int flags = MODE_NOMODE;
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

		if ((ast->chip == AST2100) || (ast->chip == AST2200) ||
		    (ast->chip == AST2300) || (ast->chip == AST2400) ||
		    (ast->chip == AST2500)) {
			if ((mode->hdisplay == 1920) && (mode->vdisplay == 1080))
				return MODE_OK;

			if ((mode->hdisplay == 1920) && (mode->vdisplay == 1200)) {
				jtemp = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xd1, 0xff);
				if (jtemp & 0x01)
					return MODE_NOMODE;
				else
					return MODE_OK;
			}
		}
	}
	switch (mode->hdisplay) {
	case 640:
		if (mode->vdisplay == 480) flags = MODE_OK;
		break;
	case 800:
		if (mode->vdisplay == 600) flags = MODE_OK;
		break;
	case 1024:
		if (mode->vdisplay == 768) flags = MODE_OK;
		break;
	case 1280:
		if (mode->vdisplay == 1024) flags = MODE_OK;
		break;
	case 1600:
		if (mode->vdisplay == 1200) flags = MODE_OK;
		break;
	default:
		return flags;
	}

	return flags;
}

static void ast_connector_destroy(struct drm_connector *connector)
{
	struct ast_connector *ast_connector = to_ast_connector(connector);
	ast_i2c_destroy(ast_connector->i2c);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_helper_funcs ast_connector_helper_funcs = {
	.get_modes = ast_get_modes,
	.mode_valid = ast_mode_valid,
};

static const struct drm_connector_funcs ast_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = ast_connector_destroy,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int ast_connector_init(struct drm_device *dev)
{
	struct ast_private *ast = to_ast_private(dev);
	struct ast_connector *ast_connector = &ast->connector;
	struct drm_connector *connector = &ast_connector->base;
	struct drm_encoder *encoder = &ast->encoder;

	ast_connector->i2c = ast_i2c_create(dev);
	if (!ast_connector->i2c)
		drm_err(dev, "failed to add ddc bus for connector\n");

	drm_connector_init_with_ddc(dev, connector,
				    &ast_connector_funcs,
				    DRM_MODE_CONNECTOR_VGA,
				    &ast_connector->i2c->adapter);

	drm_connector_helper_add(connector, &ast_connector_helper_funcs);

	connector->interlace_allowed = 0;
	connector->doublescan_allowed = 0;

	connector->polled = DRM_CONNECTOR_POLL_CONNECT;

	drm_connector_attach_encoder(connector, encoder);

	return 0;
}

/*
 * Mode config
 */

static const struct drm_mode_config_helper_funcs
ast_mode_config_helper_funcs = {
	.atomic_commit_tail = drm_atomic_helper_commit_tail_rpm,
};

static const struct drm_mode_config_funcs ast_mode_config_funcs = {
	.fb_create = drm_gem_fb_create,
	.mode_valid = drm_vram_helper_mode_valid,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

int ast_mode_config_init(struct ast_private *ast)
{
	struct drm_device *dev = &ast->base;
	int ret;

	ret = ast_cursor_init(ast);
	if (ret)
		return ret;

	ret = drmm_mode_config_init(dev);
	if (ret)
		return ret;

	dev->mode_config.funcs = &ast_mode_config_funcs;
	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	dev->mode_config.preferred_depth = 24;
	dev->mode_config.prefer_shadow = 1;
	dev->mode_config.fb_base = pci_resource_start(dev->pdev, 0);

	if (ast->chip == AST2100 ||
	    ast->chip == AST2200 ||
	    ast->chip == AST2300 ||
	    ast->chip == AST2400 ||
	    ast->chip == AST2500) {
		dev->mode_config.max_width = 1920;
		dev->mode_config.max_height = 2048;
	} else {
		dev->mode_config.max_width = 1600;
		dev->mode_config.max_height = 1200;
	}

	dev->mode_config.helper_private = &ast_mode_config_helper_funcs;

	memset(&ast->primary_plane, 0, sizeof(ast->primary_plane));
	ret = drm_universal_plane_init(dev, &ast->primary_plane, 0x01,
				       &ast_primary_plane_funcs,
				       ast_primary_plane_formats,
				       ARRAY_SIZE(ast_primary_plane_formats),
				       NULL, DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret) {
		drm_err(dev, "ast: drm_universal_plane_init() failed: %d\n", ret);
		return ret;
	}
	drm_plane_helper_add(&ast->primary_plane,
			     &ast_primary_plane_helper_funcs);

	ret = drm_universal_plane_init(dev, &ast->cursor_plane, 0x01,
				       &ast_cursor_plane_funcs,
				       ast_cursor_plane_formats,
				       ARRAY_SIZE(ast_cursor_plane_formats),
				       NULL, DRM_PLANE_TYPE_CURSOR, NULL);
	if (ret) {
		drm_err(dev, "drm_universal_plane_failed(): %d\n", ret);
		return ret;
	}
	drm_plane_helper_add(&ast->cursor_plane,
			     &ast_cursor_plane_helper_funcs);

	ast_crtc_init(dev);
	ast_encoder_init(dev);
	ast_connector_init(dev);

	drm_mode_config_reset(dev);

	return 0;
}

static int get_clock(void *i2c_priv)
{
	struct ast_i2c_chan *i2c = i2c_priv;
	struct ast_private *ast = to_ast_private(i2c->dev);
	uint32_t val, val2, count, pass;

	count = 0;
	pass = 0;
	val = (ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xb7, 0x10) >> 4) & 0x01;
	do {
		val2 = (ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xb7, 0x10) >> 4) & 0x01;
		if (val == val2) {
			pass++;
		} else {
			pass = 0;
			val = (ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xb7, 0x10) >> 4) & 0x01;
		}
	} while ((pass < 5) && (count++ < 0x10000));

	return val & 1 ? 1 : 0;
}

static int get_data(void *i2c_priv)
{
	struct ast_i2c_chan *i2c = i2c_priv;
	struct ast_private *ast = to_ast_private(i2c->dev);
	uint32_t val, val2, count, pass;

	count = 0;
	pass = 0;
	val = (ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xb7, 0x20) >> 5) & 0x01;
	do {
		val2 = (ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xb7, 0x20) >> 5) & 0x01;
		if (val == val2) {
			pass++;
		} else {
			pass = 0;
			val = (ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xb7, 0x20) >> 5) & 0x01;
		}
	} while ((pass < 5) && (count++ < 0x10000));

	return val & 1 ? 1 : 0;
}

static void set_clock(void *i2c_priv, int clock)
{
	struct ast_i2c_chan *i2c = i2c_priv;
	struct ast_private *ast = to_ast_private(i2c->dev);
	int i;
	u8 ujcrb7, jtemp;

	for (i = 0; i < 0x10000; i++) {
		ujcrb7 = ((clock & 0x01) ? 0 : 1);
		ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xb7, 0xf4, ujcrb7);
		jtemp = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xb7, 0x01);
		if (ujcrb7 == jtemp)
			break;
	}
}

static void set_data(void *i2c_priv, int data)
{
	struct ast_i2c_chan *i2c = i2c_priv;
	struct ast_private *ast = to_ast_private(i2c->dev);
	int i;
	u8 ujcrb7, jtemp;

	for (i = 0; i < 0x10000; i++) {
		ujcrb7 = ((data & 0x01) ? 0 : 1) << 2;
		ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xb7, 0xf1, ujcrb7);
		jtemp = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xb7, 0x04);
		if (ujcrb7 == jtemp)
			break;
	}
}

static struct ast_i2c_chan *ast_i2c_create(struct drm_device *dev)
{
	struct ast_i2c_chan *i2c;
	int ret;

	i2c = kzalloc(sizeof(struct ast_i2c_chan), GFP_KERNEL);
	if (!i2c)
		return NULL;

	i2c->adapter.owner = THIS_MODULE;
	i2c->adapter.class = I2C_CLASS_DDC;
	i2c->adapter.dev.parent = &dev->pdev->dev;
	i2c->dev = dev;
	i2c_set_adapdata(&i2c->adapter, i2c);
	snprintf(i2c->adapter.name, sizeof(i2c->adapter.name),
		 "AST i2c bit bus");
	i2c->adapter.algo_data = &i2c->bit;

	i2c->bit.udelay = 20;
	i2c->bit.timeout = 2;
	i2c->bit.data = i2c;
	i2c->bit.setsda = set_data;
	i2c->bit.setscl = set_clock;
	i2c->bit.getsda = get_data;
	i2c->bit.getscl = get_clock;
	ret = i2c_bit_add_bus(&i2c->adapter);
	if (ret) {
		drm_err(dev, "Failed to register bit i2c\n");
		goto out_free;
	}

	return i2c;
out_free:
	kfree(i2c);
	return NULL;
}

static void ast_i2c_destroy(struct ast_i2c_chan *i2c)
{
	if (!i2c)
		return;
	i2c_del_adapter(&i2c->adapter);
	kfree(i2c);
}
