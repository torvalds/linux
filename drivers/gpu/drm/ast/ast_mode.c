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
#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_plane_helper.h>
#include "ast_drv.h"

#include "ast_tables.h"

static struct ast_i2c_chan *ast_i2c_create(struct drm_device *dev);
static void ast_i2c_destroy(struct ast_i2c_chan *i2c);
static int ast_cursor_set(struct drm_crtc *crtc,
			  struct drm_file *file_priv,
			  uint32_t handle,
			  uint32_t width,
			  uint32_t height);
static int ast_cursor_move(struct drm_crtc *crtc,
			   int x, int y);

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

static void ast_crtc_load_lut(struct drm_crtc *crtc)
{
	struct ast_private *ast = crtc->dev->dev_private;
	struct ast_crtc *ast_crtc = to_ast_crtc(crtc);
	int i;

	if (!crtc->enabled)
		return;

	for (i = 0; i < 256; i++)
		ast_load_palette_index(ast, i, ast_crtc->lut_r[i],
				       ast_crtc->lut_g[i], ast_crtc->lut_b[i]);
}

static bool ast_get_vbios_mode_info(struct drm_crtc *crtc, struct drm_display_mode *mode,
				    struct drm_display_mode *adjusted_mode,
				    struct ast_vbios_mode_info *vbios_mode)
{
	struct ast_private *ast = crtc->dev->dev_private;
	u32 refresh_rate_index = 0, mode_id, color_index, refresh_rate;
	u32 hborder, vborder;
	bool check_sync;
	struct ast_vbios_enhtable *best = NULL;

	switch (crtc->primary->fb->bits_per_pixel) {
	case 8:
		vbios_mode->std_table = &vbios_stdtable[VGAModeIndex];
		color_index = VGAModeIndex - 1;
		break;
	case 16:
		vbios_mode->std_table = &vbios_stdtable[HiCModeIndex];
		color_index = HiCModeIndex;
		break;
	case 24:
	case 32:
		vbios_mode->std_table = &vbios_stdtable[TrueCModeIndex];
		color_index = TrueCModeIndex;
		break;
	default:
		return false;
	}

	switch (crtc->mode.crtc_hdisplay) {
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
		if (crtc->mode.crtc_vdisplay == 800)
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
		if (crtc->mode.crtc_vdisplay == 900)
			vbios_mode->enh_table = &res_1600x900[refresh_rate_index];
		else
			vbios_mode->enh_table = &res_1600x1200[refresh_rate_index];
		break;
	case 1680:
		vbios_mode->enh_table = &res_1680x1050[refresh_rate_index];
		break;
	case 1920:
		if (crtc->mode.crtc_vdisplay == 1080)
			vbios_mode->enh_table = &res_1920x1080[refresh_rate_index];
		else
			vbios_mode->enh_table = &res_1920x1200[refresh_rate_index];
		break;
	default:
		return false;
	}

	refresh_rate = drm_mode_vrefresh(mode);
	check_sync = vbios_mode->enh_table->flags & WideScreenMode;
	do {
		struct ast_vbios_enhtable *loop = vbios_mode->enh_table;

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
	} while (1);
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

	refresh_rate_index = vbios_mode->enh_table->refresh_rate_index;
	mode_id = vbios_mode->enh_table->mode_id;

	if (ast->chip == AST1180) {
		/* TODO 1180 */
	} else {
		ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0x8c, (u8)((color_index & 0xf) << 4));
		ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0x8d, refresh_rate_index & 0xff);
		ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0x8e, mode_id & 0xff);

		ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0x91, 0x00);
		if (vbios_mode->enh_table->flags & NewModeInfo) {
			ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0x91, 0xa8);
			ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0x92, crtc->primary->fb->bits_per_pixel);
			ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0x93, adjusted_mode->clock / 1000);
			ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0x94, adjusted_mode->crtc_hdisplay);
			ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0x95, adjusted_mode->crtc_hdisplay >> 8);

			ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0x96, adjusted_mode->crtc_vdisplay);
			ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0x97, adjusted_mode->crtc_vdisplay >> 8);
		}
	}

	return true;


}
static void ast_set_std_reg(struct drm_crtc *crtc, struct drm_display_mode *mode,
			    struct ast_vbios_mode_info *vbios_mode)
{
	struct ast_private *ast = crtc->dev->dev_private;
	struct ast_vbios_stdtable *stdtable;
	u32 i;
	u8 jreg;

	stdtable = vbios_mode->std_table;

	jreg = stdtable->misc;
	ast_io_write8(ast, AST_IO_MISC_PORT_WRITE, jreg);

	/* Set SEQ */
	ast_set_index_reg(ast, AST_IO_SEQ_PORT, 0x00, 0x03);
	for (i = 0; i < 4; i++) {
		jreg = stdtable->seq[i];
		if (!i)
			jreg |= 0x20;
		ast_set_index_reg(ast, AST_IO_SEQ_PORT, (i + 1) , jreg);
	}

	/* Set CRTC */
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0x11, 0x7f, 0x00);
	for (i = 0; i < 25; i++)
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

static void ast_set_crtc_reg(struct drm_crtc *crtc, struct drm_display_mode *mode,
			     struct ast_vbios_mode_info *vbios_mode)
{
	struct ast_private *ast = crtc->dev->dev_private;
	u8 jreg05 = 0, jreg07 = 0, jreg09 = 0, jregAC = 0, jregAD = 0, jregAE = 0;
	u16 temp;

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

	temp = (mode->crtc_hsync_start >> 3) - 1;
	if (temp & 0x100)
		jregAC |= 0x40; /* HRS D[5] */
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0x04, 0x00, temp);

	temp = ((mode->crtc_hsync_end >> 3) - 1) & 0x3f;
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

	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0x11, 0x7f, 0x80);
}

static void ast_set_offset_reg(struct drm_crtc *crtc)
{
	struct ast_private *ast = crtc->dev->dev_private;

	u16 offset;

	offset = crtc->primary->fb->pitches[0] >> 3;
	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0x13, (offset & 0xff));
	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xb0, (offset >> 8) & 0x3f);
}

static void ast_set_dclk_reg(struct drm_device *dev, struct drm_display_mode *mode,
			     struct ast_vbios_mode_info *vbios_mode)
{
	struct ast_private *ast = dev->dev_private;
	struct ast_vbios_dclk_info *clk_info;

	clk_info = &dclk_table[vbios_mode->enh_table->dclk_index];

	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xc0, 0x00, clk_info->param1);
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xc1, 0x00, clk_info->param2);
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xbb, 0x0f,
			       (clk_info->param3 & 0x80) | ((clk_info->param3 & 0x3) << 4));
}

static void ast_set_ext_reg(struct drm_crtc *crtc, struct drm_display_mode *mode,
			     struct ast_vbios_mode_info *vbios_mode)
{
	struct ast_private *ast = crtc->dev->dev_private;
	u8 jregA0 = 0, jregA3 = 0, jregA8 = 0;

	switch (crtc->primary->fb->bits_per_pixel) {
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

	/* Set Threshold */
	if (ast->chip == AST2300 || ast->chip == AST2400) {
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

static void ast_set_sync_reg(struct drm_device *dev, struct drm_display_mode *mode,
		      struct ast_vbios_mode_info *vbios_mode)
{
	struct ast_private *ast = dev->dev_private;
	u8 jreg;

	jreg  = ast_io_read8(ast, AST_IO_MISC_PORT_READ);
	jreg &= ~0xC0;
	if (vbios_mode->enh_table->flags & NVSync) jreg |= 0x80;
	if (vbios_mode->enh_table->flags & NHSync) jreg |= 0x40;
	ast_io_write8(ast, AST_IO_MISC_PORT_WRITE, jreg);
}

static bool ast_set_dac_reg(struct drm_crtc *crtc, struct drm_display_mode *mode,
		     struct ast_vbios_mode_info *vbios_mode)
{
	switch (crtc->primary->fb->bits_per_pixel) {
	case 8:
		break;
	default:
		return false;
	}
	return true;
}

static void ast_set_start_address_crt1(struct drm_crtc *crtc, unsigned offset)
{
	struct ast_private *ast = crtc->dev->dev_private;
	u32 addr;

	addr = offset >> 2;
	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0x0d, (u8)(addr & 0xff));
	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0x0c, (u8)((addr >> 8) & 0xff));
	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xaf, (u8)((addr >> 16) & 0xff));

}

static void ast_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct ast_private *ast = crtc->dev->dev_private;

	if (ast->chip == AST1180)
		return;

	switch (mode) {
	case DRM_MODE_DPMS_ON:
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
		ast_set_index_reg_mask(ast, AST_IO_SEQ_PORT, 0x1, 0xdf, 0);
		if (ast->tx_chip_type == AST_TX_DP501)
			ast_set_dp501_video_output(crtc->dev, 1);
		ast_crtc_load_lut(crtc);
		break;
	case DRM_MODE_DPMS_OFF:
		if (ast->tx_chip_type == AST_TX_DP501)
			ast_set_dp501_video_output(crtc->dev, 0);
		ast_set_index_reg_mask(ast, AST_IO_SEQ_PORT, 0x1, 0xdf, 0x20);
		break;
	}
}

/* ast is different - we will force move buffers out of VRAM */
static int ast_crtc_do_set_base(struct drm_crtc *crtc,
				struct drm_framebuffer *fb,
				int x, int y, int atomic)
{
	struct ast_private *ast = crtc->dev->dev_private;
	struct drm_gem_object *obj;
	struct ast_framebuffer *ast_fb;
	struct ast_bo *bo;
	int ret;
	u64 gpu_addr;

	/* push the previous fb to system ram */
	if (!atomic && fb) {
		ast_fb = to_ast_framebuffer(fb);
		obj = ast_fb->obj;
		bo = gem_to_ast_bo(obj);
		ret = ast_bo_reserve(bo, false);
		if (ret)
			return ret;
		ast_bo_push_sysram(bo);
		ast_bo_unreserve(bo);
	}

	ast_fb = to_ast_framebuffer(crtc->primary->fb);
	obj = ast_fb->obj;
	bo = gem_to_ast_bo(obj);

	ret = ast_bo_reserve(bo, false);
	if (ret)
		return ret;

	ret = ast_bo_pin(bo, TTM_PL_FLAG_VRAM, &gpu_addr);
	if (ret) {
		ast_bo_unreserve(bo);
		return ret;
	}

	if (&ast->fbdev->afb == ast_fb) {
		/* if pushing console in kmap it */
		ret = ttm_bo_kmap(&bo->bo, 0, bo->bo.num_pages, &bo->kmap);
		if (ret)
			DRM_ERROR("failed to kmap fbcon\n");
		else
			ast_fbdev_set_base(ast, gpu_addr);
	}
	ast_bo_unreserve(bo);

	ast_set_start_address_crt1(crtc, (u32)gpu_addr);

	return 0;
}

static int ast_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
			     struct drm_framebuffer *old_fb)
{
	return ast_crtc_do_set_base(crtc, old_fb, x, y, 0);
}

static int ast_crtc_mode_set(struct drm_crtc *crtc,
			     struct drm_display_mode *mode,
			     struct drm_display_mode *adjusted_mode,
			     int x, int y,
			     struct drm_framebuffer *old_fb)
{
	struct drm_device *dev = crtc->dev;
	struct ast_private *ast = crtc->dev->dev_private;
	struct ast_vbios_mode_info vbios_mode;
	bool ret;
	if (ast->chip == AST1180) {
		DRM_ERROR("AST 1180 modesetting not supported\n");
		return -EINVAL;
	}

	ret = ast_get_vbios_mode_info(crtc, mode, adjusted_mode, &vbios_mode);
	if (ret == false)
		return -EINVAL;
	ast_open_key(ast);

	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xa1, 0xff, 0x04);

	ast_set_std_reg(crtc, adjusted_mode, &vbios_mode);
	ast_set_crtc_reg(crtc, adjusted_mode, &vbios_mode);
	ast_set_offset_reg(crtc);
	ast_set_dclk_reg(dev, adjusted_mode, &vbios_mode);
	ast_set_ext_reg(crtc, adjusted_mode, &vbios_mode);
	ast_set_sync_reg(dev, adjusted_mode, &vbios_mode);
	ast_set_dac_reg(crtc, adjusted_mode, &vbios_mode);

	ast_crtc_mode_set_base(crtc, x, y, old_fb);

	return 0;
}

static void ast_crtc_disable(struct drm_crtc *crtc)
{

}

static void ast_crtc_prepare(struct drm_crtc *crtc)
{

}

static void ast_crtc_commit(struct drm_crtc *crtc)
{
	struct ast_private *ast = crtc->dev->dev_private;
	ast_set_index_reg_mask(ast, AST_IO_SEQ_PORT, 0x1, 0xdf, 0);
}


static const struct drm_crtc_helper_funcs ast_crtc_helper_funcs = {
	.dpms = ast_crtc_dpms,
	.mode_set = ast_crtc_mode_set,
	.mode_set_base = ast_crtc_mode_set_base,
	.disable = ast_crtc_disable,
	.load_lut = ast_crtc_load_lut,
	.prepare = ast_crtc_prepare,
	.commit = ast_crtc_commit,

};

static void ast_crtc_reset(struct drm_crtc *crtc)
{

}

static void ast_crtc_gamma_set(struct drm_crtc *crtc, u16 *red, u16 *green,
				 u16 *blue, uint32_t start, uint32_t size)
{
	struct ast_crtc *ast_crtc = to_ast_crtc(crtc);
	int end = (start + size > 256) ? 256 : start + size, i;

	/* userspace palettes are always correct as is */
	for (i = start; i < end; i++) {
		ast_crtc->lut_r[i] = red[i] >> 8;
		ast_crtc->lut_g[i] = green[i] >> 8;
		ast_crtc->lut_b[i] = blue[i] >> 8;
	}
	ast_crtc_load_lut(crtc);
}


static void ast_crtc_destroy(struct drm_crtc *crtc)
{
	drm_crtc_cleanup(crtc);
	kfree(crtc);
}

static const struct drm_crtc_funcs ast_crtc_funcs = {
	.cursor_set = ast_cursor_set,
	.cursor_move = ast_cursor_move,
	.reset = ast_crtc_reset,
	.set_config = drm_crtc_helper_set_config,
	.gamma_set = ast_crtc_gamma_set,
	.destroy = ast_crtc_destroy,
};

static int ast_crtc_init(struct drm_device *dev)
{
	struct ast_crtc *crtc;
	int i;

	crtc = kzalloc(sizeof(struct ast_crtc), GFP_KERNEL);
	if (!crtc)
		return -ENOMEM;

	drm_crtc_init(dev, &crtc->base, &ast_crtc_funcs);
	drm_mode_crtc_set_gamma_size(&crtc->base, 256);
	drm_crtc_helper_add(&crtc->base, &ast_crtc_helper_funcs);

	for (i = 0; i < 256; i++) {
		crtc->lut_r[i] = i;
		crtc->lut_g[i] = i;
		crtc->lut_b[i] = i;
	}
	return 0;
}

static void ast_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
	kfree(encoder);
}


static struct drm_encoder *ast_best_single_encoder(struct drm_connector *connector)
{
	int enc_id = connector->encoder_ids[0];
	/* pick the encoder ids */
	if (enc_id)
		return drm_encoder_find(connector->dev, enc_id);
	return NULL;
}


static const struct drm_encoder_funcs ast_enc_funcs = {
	.destroy = ast_encoder_destroy,
};

static void ast_encoder_dpms(struct drm_encoder *encoder, int mode)
{

}

static void ast_encoder_mode_set(struct drm_encoder *encoder,
			       struct drm_display_mode *mode,
			       struct drm_display_mode *adjusted_mode)
{
}

static void ast_encoder_prepare(struct drm_encoder *encoder)
{

}

static void ast_encoder_commit(struct drm_encoder *encoder)
{

}


static const struct drm_encoder_helper_funcs ast_enc_helper_funcs = {
	.dpms = ast_encoder_dpms,
	.prepare = ast_encoder_prepare,
	.commit = ast_encoder_commit,
	.mode_set = ast_encoder_mode_set,
};

static int ast_encoder_init(struct drm_device *dev)
{
	struct ast_encoder *ast_encoder;

	ast_encoder = kzalloc(sizeof(struct ast_encoder), GFP_KERNEL);
	if (!ast_encoder)
		return -ENOMEM;

	drm_encoder_init(dev, &ast_encoder->base, &ast_enc_funcs,
			 DRM_MODE_ENCODER_DAC, NULL);
	drm_encoder_helper_add(&ast_encoder->base, &ast_enc_helper_funcs);

	ast_encoder->base.possible_crtcs = 1;
	return 0;
}

static int ast_get_modes(struct drm_connector *connector)
{
	struct ast_connector *ast_connector = to_ast_connector(connector);
	struct ast_private *ast = connector->dev->dev_private;
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
		drm_mode_connector_update_edid_property(&ast_connector->base, edid);
		ret = drm_add_edid_modes(connector, edid);
		kfree(edid);
		return ret;
	} else
		drm_mode_connector_update_edid_property(&ast_connector->base, NULL);
	return 0;
}

static int ast_mode_valid(struct drm_connector *connector,
			  struct drm_display_mode *mode)
{
	struct ast_private *ast = connector->dev->dev_private;
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

		if ((ast->chip == AST2100) || (ast->chip == AST2200) || (ast->chip == AST2300) || (ast->chip == AST2400) || (ast->chip == AST1180)) {
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
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
	kfree(connector);
}

static enum drm_connector_status
ast_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static const struct drm_connector_helper_funcs ast_connector_helper_funcs = {
	.mode_valid = ast_mode_valid,
	.get_modes = ast_get_modes,
	.best_encoder = ast_best_single_encoder,
};

static const struct drm_connector_funcs ast_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = ast_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = ast_connector_destroy,
};

static int ast_connector_init(struct drm_device *dev)
{
	struct ast_connector *ast_connector;
	struct drm_connector *connector;
	struct drm_encoder *encoder;

	ast_connector = kzalloc(sizeof(struct ast_connector), GFP_KERNEL);
	if (!ast_connector)
		return -ENOMEM;

	connector = &ast_connector->base;
	drm_connector_init(dev, connector, &ast_connector_funcs, DRM_MODE_CONNECTOR_VGA);

	drm_connector_helper_add(connector, &ast_connector_helper_funcs);

	connector->interlace_allowed = 0;
	connector->doublescan_allowed = 0;

	drm_connector_register(connector);

	connector->polled = DRM_CONNECTOR_POLL_CONNECT;

	encoder = list_first_entry(&dev->mode_config.encoder_list, struct drm_encoder, head);
	drm_mode_connector_attach_encoder(connector, encoder);

	ast_connector->i2c = ast_i2c_create(dev);
	if (!ast_connector->i2c)
		DRM_ERROR("failed to add ddc bus for connector\n");

	return 0;
}

/* allocate cursor cache and pin at start of VRAM */
static int ast_cursor_init(struct drm_device *dev)
{
	struct ast_private *ast = dev->dev_private;
	int size;
	int ret;
	struct drm_gem_object *obj;
	struct ast_bo *bo;
	uint64_t gpu_addr;

	size = (AST_HWC_SIZE + AST_HWC_SIGNATURE_SIZE) * AST_DEFAULT_HWC_NUM;

	ret = ast_gem_create(dev, size, true, &obj);
	if (ret)
		return ret;
	bo = gem_to_ast_bo(obj);
	ret = ast_bo_reserve(bo, false);
	if (unlikely(ret != 0))
		goto fail;

	ret = ast_bo_pin(bo, TTM_PL_FLAG_VRAM, &gpu_addr);
	ast_bo_unreserve(bo);
	if (ret)
		goto fail;

	/* kmap the object */
	ret = ttm_bo_kmap(&bo->bo, 0, bo->bo.num_pages, &ast->cache_kmap);
	if (ret)
		goto fail;

	ast->cursor_cache = obj;
	ast->cursor_cache_gpu_addr = gpu_addr;
	DRM_DEBUG_KMS("pinned cursor cache at %llx\n", ast->cursor_cache_gpu_addr);
	return 0;
fail:
	return ret;
}

static void ast_cursor_fini(struct drm_device *dev)
{
	struct ast_private *ast = dev->dev_private;
	ttm_bo_kunmap(&ast->cache_kmap);
	drm_gem_object_unreference_unlocked(ast->cursor_cache);
}

int ast_mode_init(struct drm_device *dev)
{
	ast_cursor_init(dev);
	ast_crtc_init(dev);
	ast_encoder_init(dev);
	ast_connector_init(dev);
	return 0;
}

void ast_mode_fini(struct drm_device *dev)
{
	ast_cursor_fini(dev);
}

static int get_clock(void *i2c_priv)
{
	struct ast_i2c_chan *i2c = i2c_priv;
	struct ast_private *ast = i2c->dev->dev_private;
	uint32_t val;

	val = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xb7, 0x10) >> 4;
	return val & 1 ? 1 : 0;
}

static int get_data(void *i2c_priv)
{
	struct ast_i2c_chan *i2c = i2c_priv;
	struct ast_private *ast = i2c->dev->dev_private;
	uint32_t val;

	val = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xb7, 0x20) >> 5;
	return val & 1 ? 1 : 0;
}

static void set_clock(void *i2c_priv, int clock)
{
	struct ast_i2c_chan *i2c = i2c_priv;
	struct ast_private *ast = i2c->dev->dev_private;
	int i;
	u8 ujcrb7, jtemp;

	for (i = 0; i < 0x10000; i++) {
		ujcrb7 = ((clock & 0x01) ? 0 : 1);
		ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xb7, 0xfe, ujcrb7);
		jtemp = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xb7, 0x01);
		if (ujcrb7 == jtemp)
			break;
	}
}

static void set_data(void *i2c_priv, int data)
{
	struct ast_i2c_chan *i2c = i2c_priv;
	struct ast_private *ast = i2c->dev->dev_private;
	int i;
	u8 ujcrb7, jtemp;

	for (i = 0; i < 0x10000; i++) {
		ujcrb7 = ((data & 0x01) ? 0 : 1) << 2;
		ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xb7, 0xfb, ujcrb7);
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
		DRM_ERROR("Failed to register bit i2c\n");
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

static void ast_show_cursor(struct drm_crtc *crtc)
{
	struct ast_private *ast = crtc->dev->dev_private;
	u8 jreg;

	jreg = 0x2;
	/* enable ARGB cursor */
	jreg |= 1;
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xcb, 0xfc, jreg);
}

static void ast_hide_cursor(struct drm_crtc *crtc)
{
	struct ast_private *ast = crtc->dev->dev_private;
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xcb, 0xfc, 0x00);
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

static int ast_cursor_set(struct drm_crtc *crtc,
			  struct drm_file *file_priv,
			  uint32_t handle,
			  uint32_t width,
			  uint32_t height)
{
	struct ast_private *ast = crtc->dev->dev_private;
	struct ast_crtc *ast_crtc = to_ast_crtc(crtc);
	struct drm_gem_object *obj;
	struct ast_bo *bo;
	uint64_t gpu_addr;
	u32 csum;
	int ret;
	struct ttm_bo_kmap_obj uobj_map;
	u8 *src, *dst;
	bool src_isiomem, dst_isiomem;
	if (!handle) {
		ast_hide_cursor(crtc);
		return 0;
	}

	if (width > AST_MAX_HWC_WIDTH || height > AST_MAX_HWC_HEIGHT)
		return -EINVAL;

	obj = drm_gem_object_lookup(crtc->dev, file_priv, handle);
	if (!obj) {
		DRM_ERROR("Cannot find cursor object %x for crtc\n", handle);
		return -ENOENT;
	}
	bo = gem_to_ast_bo(obj);

	ret = ast_bo_reserve(bo, false);
	if (ret)
		goto fail;

	ret = ttm_bo_kmap(&bo->bo, 0, bo->bo.num_pages, &uobj_map);

	src = ttm_kmap_obj_virtual(&uobj_map, &src_isiomem);
	dst = ttm_kmap_obj_virtual(&ast->cache_kmap, &dst_isiomem);

	if (src_isiomem == true)
		DRM_ERROR("src cursor bo should be in main memory\n");
	if (dst_isiomem == false)
		DRM_ERROR("dst bo should be in VRAM\n");

	dst += (AST_HWC_SIZE + AST_HWC_SIGNATURE_SIZE)*ast->next_cursor;

	/* do data transfer to cursor cache */
	csum = copy_cursor_image(src, dst, width, height);

	/* write checksum + signature */
	ttm_bo_kunmap(&uobj_map);
	ast_bo_unreserve(bo);
	{
		u8 *dst = (u8 *)ast->cache_kmap.virtual + (AST_HWC_SIZE + AST_HWC_SIGNATURE_SIZE)*ast->next_cursor + AST_HWC_SIZE;
		writel(csum, dst);
		writel(width, dst + AST_HWC_SIGNATURE_SizeX);
		writel(height, dst + AST_HWC_SIGNATURE_SizeY);
		writel(0, dst + AST_HWC_SIGNATURE_HOTSPOTX);
		writel(0, dst + AST_HWC_SIGNATURE_HOTSPOTY);

		/* set pattern offset */
		gpu_addr = ast->cursor_cache_gpu_addr;
		gpu_addr += (AST_HWC_SIZE + AST_HWC_SIGNATURE_SIZE)*ast->next_cursor;
		gpu_addr >>= 3;
		ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xc8, gpu_addr & 0xff);
		ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xc9, (gpu_addr >> 8) & 0xff);
		ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xca, (gpu_addr >> 16) & 0xff);
	}
	ast_crtc->cursor_width = width;
	ast_crtc->cursor_height = height;
	ast_crtc->offset_x = AST_MAX_HWC_WIDTH - width;
	ast_crtc->offset_y = AST_MAX_HWC_WIDTH - height;

	ast->next_cursor = (ast->next_cursor + 1) % AST_DEFAULT_HWC_NUM;

	ast_show_cursor(crtc);

	drm_gem_object_unreference_unlocked(obj);
	return 0;
fail:
	drm_gem_object_unreference_unlocked(obj);
	return ret;
}

static int ast_cursor_move(struct drm_crtc *crtc,
			   int x, int y)
{
	struct ast_crtc *ast_crtc = to_ast_crtc(crtc);
	struct ast_private *ast = crtc->dev->dev_private;
	int x_offset, y_offset;
	u8 *sig;

	sig = (u8 *)ast->cache_kmap.virtual + (AST_HWC_SIZE + AST_HWC_SIGNATURE_SIZE)*ast->next_cursor + AST_HWC_SIZE;
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
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xCB, 0xFF, 0x00);

	return 0;
}
