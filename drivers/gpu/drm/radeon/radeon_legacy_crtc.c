/*
 * Copyright 2007-8 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alex Deucher
 */
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/radeon_drm.h>
#include "radeon_fixed.h"
#include "radeon.h"

void radeon_restore_common_regs(struct drm_device *dev)
{
	/* don't need this yet */
}

static void radeon_pll_wait_for_read_update_complete(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;
	int i = 0;

	/* FIXME: Certain revisions of R300 can't recover here.  Not sure of
	   the cause yet, but this workaround will mask the problem for now.
	   Other chips usually will pass at the very first test, so the
	   workaround shouldn't have any effect on them. */
	for (i = 0;
	     (i < 10000 &&
	      RREG32_PLL(RADEON_PPLL_REF_DIV) & RADEON_PPLL_ATOMIC_UPDATE_R);
	     i++);
}

static void radeon_pll_write_update(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;

	while (RREG32_PLL(RADEON_PPLL_REF_DIV) & RADEON_PPLL_ATOMIC_UPDATE_R);

	WREG32_PLL_P(RADEON_PPLL_REF_DIV,
			   RADEON_PPLL_ATOMIC_UPDATE_W,
			   ~(RADEON_PPLL_ATOMIC_UPDATE_W));
}

static void radeon_pll2_wait_for_read_update_complete(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;
	int i = 0;


	/* FIXME: Certain revisions of R300 can't recover here.  Not sure of
	   the cause yet, but this workaround will mask the problem for now.
	   Other chips usually will pass at the very first test, so the
	   workaround shouldn't have any effect on them. */
	for (i = 0;
	     (i < 10000 &&
	      RREG32_PLL(RADEON_P2PLL_REF_DIV) & RADEON_P2PLL_ATOMIC_UPDATE_R);
	     i++);
}

static void radeon_pll2_write_update(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;

	while (RREG32_PLL(RADEON_P2PLL_REF_DIV) & RADEON_P2PLL_ATOMIC_UPDATE_R);

	WREG32_PLL_P(RADEON_P2PLL_REF_DIV,
			   RADEON_P2PLL_ATOMIC_UPDATE_W,
			   ~(RADEON_P2PLL_ATOMIC_UPDATE_W));
}

static uint8_t radeon_compute_pll_gain(uint16_t ref_freq, uint16_t ref_div,
				       uint16_t fb_div)
{
	unsigned int vcoFreq;

	if (!ref_div)
		return 1;

	vcoFreq = ((unsigned)ref_freq & fb_div) / ref_div;

	/*
	 * This is horribly crude: the VCO frequency range is divided into
	 * 3 parts, each part having a fixed PLL gain value.
	 */
	if (vcoFreq >= 30000)
		/*
		 * [300..max] MHz : 7
		 */
		return 7;
	else if (vcoFreq >= 18000)
		/*
		 * [180..300) MHz : 4
		 */
		return 4;
	else
		/*
		 * [0..180) MHz : 1
		 */
		return 1;
}

void radeon_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	uint32_t mask;

	if (radeon_crtc->crtc_id)
		mask = (RADEON_CRTC2_EN |
			RADEON_CRTC2_DISP_DIS |
			RADEON_CRTC2_VSYNC_DIS |
			RADEON_CRTC2_HSYNC_DIS |
			RADEON_CRTC2_DISP_REQ_EN_B);
	else
		mask = (RADEON_CRTC_DISPLAY_DIS |
			RADEON_CRTC_VSYNC_DIS |
			RADEON_CRTC_HSYNC_DIS);

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		if (radeon_crtc->crtc_id)
			WREG32_P(RADEON_CRTC2_GEN_CNTL, RADEON_CRTC2_EN, ~mask);
		else {
			WREG32_P(RADEON_CRTC_GEN_CNTL, RADEON_CRTC_EN, ~(RADEON_CRTC_EN |
									 RADEON_CRTC_DISP_REQ_EN_B));
			WREG32_P(RADEON_CRTC_EXT_CNTL, 0, ~mask);
		}
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		if (radeon_crtc->crtc_id)
			WREG32_P(RADEON_CRTC2_GEN_CNTL, mask, ~mask);
		else {
			WREG32_P(RADEON_CRTC_GEN_CNTL, RADEON_CRTC_DISP_REQ_EN_B, ~(RADEON_CRTC_EN |
										    RADEON_CRTC_DISP_REQ_EN_B));
			WREG32_P(RADEON_CRTC_EXT_CNTL, mask, ~mask);
		}
		break;
	}

	if (mode != DRM_MODE_DPMS_OFF) {
		radeon_crtc_load_lut(crtc);
	}
}

/* properly set crtc bpp when using atombios */
void radeon_legacy_atom_set_surface(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	int format;
	uint32_t crtc_gen_cntl;
	uint32_t disp_merge_cntl;
	uint32_t crtc_pitch;

	switch (crtc->fb->bits_per_pixel) {
	case 15:      /*  555 */
		format = 3;
		break;
	case 16:      /*  565 */
		format = 4;
		break;
	case 24:      /*  RGB */
		format = 5;
		break;
	case 32:      /* xRGB */
		format = 6;
		break;
	default:
		return;
	}

	crtc_pitch  = ((((crtc->fb->pitch / (crtc->fb->bits_per_pixel / 8)) * crtc->fb->bits_per_pixel) +
			((crtc->fb->bits_per_pixel * 8) - 1)) /
		       (crtc->fb->bits_per_pixel * 8));
	crtc_pitch |= crtc_pitch << 16;

	WREG32(RADEON_CRTC_PITCH + radeon_crtc->crtc_offset, crtc_pitch);

	switch (radeon_crtc->crtc_id) {
	case 0:
		disp_merge_cntl = RREG32(RADEON_DISP_MERGE_CNTL);
		disp_merge_cntl &= ~RADEON_DISP_RGB_OFFSET_EN;
		WREG32(RADEON_DISP_MERGE_CNTL, disp_merge_cntl);

		crtc_gen_cntl = RREG32(RADEON_CRTC_GEN_CNTL) & 0xfffff0ff;
		crtc_gen_cntl |= (format << 8);
		crtc_gen_cntl |= RADEON_CRTC_EXT_DISP_EN;
		WREG32(RADEON_CRTC_GEN_CNTL, crtc_gen_cntl);
		break;
	case 1:
		disp_merge_cntl = RREG32(RADEON_DISP2_MERGE_CNTL);
		disp_merge_cntl &= ~RADEON_DISP2_RGB_OFFSET_EN;
		WREG32(RADEON_DISP2_MERGE_CNTL, disp_merge_cntl);

		crtc_gen_cntl = RREG32(RADEON_CRTC2_GEN_CNTL) & 0xfffff0ff;
		crtc_gen_cntl |= (format << 8);
		WREG32(RADEON_CRTC2_GEN_CNTL, crtc_gen_cntl);
		WREG32(RADEON_FP_H2_SYNC_STRT_WID,   RREG32(RADEON_CRTC2_H_SYNC_STRT_WID));
		WREG32(RADEON_FP_V2_SYNC_STRT_WID,   RREG32(RADEON_CRTC2_V_SYNC_STRT_WID));
		break;
	}
}

int radeon_crtc_set_base(struct drm_crtc *crtc, int x, int y,
			 struct drm_framebuffer *old_fb)
{
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct radeon_framebuffer *radeon_fb;
	struct drm_gem_object *obj;
	uint64_t base;
	uint32_t crtc_offset, crtc_offset_cntl, crtc_tile_x0_y0 = 0;
	uint32_t crtc_pitch, pitch_pixels;

	DRM_DEBUG("\n");

	radeon_fb = to_radeon_framebuffer(crtc->fb);

	obj = radeon_fb->obj;
	if (radeon_gem_object_pin(obj, RADEON_GEM_DOMAIN_VRAM, &base)) {
		return -EINVAL;
	}
	crtc_offset = (u32)base;
	crtc_offset_cntl = 0;

	pitch_pixels = crtc->fb->pitch / (crtc->fb->bits_per_pixel / 8);
	crtc_pitch  = (((pitch_pixels * crtc->fb->bits_per_pixel) +
			((crtc->fb->bits_per_pixel * 8) - 1)) /
		       (crtc->fb->bits_per_pixel * 8));
	crtc_pitch |= crtc_pitch << 16;

	/* TODO tiling */
	if (0) {
		if (ASIC_IS_R300(rdev))
			crtc_offset_cntl |= (R300_CRTC_X_Y_MODE_EN |
					     R300_CRTC_MICRO_TILE_BUFFER_DIS |
					     R300_CRTC_MACRO_TILE_EN);
		else
			crtc_offset_cntl |= RADEON_CRTC_TILE_EN;
	} else {
		if (ASIC_IS_R300(rdev))
			crtc_offset_cntl &= ~(R300_CRTC_X_Y_MODE_EN |
					      R300_CRTC_MICRO_TILE_BUFFER_DIS |
					      R300_CRTC_MACRO_TILE_EN);
		else
			crtc_offset_cntl &= ~RADEON_CRTC_TILE_EN;
	}


	/* TODO more tiling */
	if (0) {
		if (ASIC_IS_R300(rdev)) {
			crtc_tile_x0_y0 = x | (y << 16);
			base &= ~0x7ff;
		} else {
			int byteshift = crtc->fb->bits_per_pixel >> 4;
			int tile_addr = (((y >> 3) * crtc->fb->width + x) >> (8 - byteshift)) << 11;
			base += tile_addr + ((x << byteshift) % 256) + ((y % 8) << 8);
			crtc_offset_cntl |= (y % 16);
		}
	} else {
		int offset = y * pitch_pixels + x;
		switch (crtc->fb->bits_per_pixel) {
		case 15:
		case 16:
			offset *= 2;
			break;
		case 24:
			offset *= 3;
			break;
		case 32:
			offset *= 4;
			break;
		default:
			return false;
		}
		base += offset;
	}

	base &= ~7;

	/* update sarea TODO */

	crtc_offset = (u32)base;

	WREG32(RADEON_DISPLAY_BASE_ADDR + radeon_crtc->crtc_offset, rdev->mc.vram_location);

	if (ASIC_IS_R300(rdev)) {
		if (radeon_crtc->crtc_id)
			WREG32(R300_CRTC2_TILE_X0_Y0, crtc_tile_x0_y0);
		else
			WREG32(R300_CRTC_TILE_X0_Y0, crtc_tile_x0_y0);
	}
	WREG32(RADEON_CRTC_OFFSET_CNTL + radeon_crtc->crtc_offset, crtc_offset_cntl);
	WREG32(RADEON_CRTC_OFFSET + radeon_crtc->crtc_offset, crtc_offset);
	WREG32(RADEON_CRTC_PITCH + radeon_crtc->crtc_offset, crtc_pitch);

	if (old_fb && old_fb != crtc->fb) {
		radeon_fb = to_radeon_framebuffer(old_fb);
		radeon_gem_object_unpin(radeon_fb->obj);
	}
	return 0;
}

static bool radeon_set_crtc_timing(struct drm_crtc *crtc, struct drm_display_mode *mode)
{
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	int format;
	int hsync_start;
	int hsync_wid;
	int vsync_wid;
	uint32_t crtc_h_total_disp;
	uint32_t crtc_h_sync_strt_wid;
	uint32_t crtc_v_total_disp;
	uint32_t crtc_v_sync_strt_wid;

	DRM_DEBUG("\n");

	switch (crtc->fb->bits_per_pixel) {
	case 15:      /*  555 */
		format = 3;
		break;
	case 16:      /*  565 */
		format = 4;
		break;
	case 24:      /*  RGB */
		format = 5;
		break;
	case 32:      /* xRGB */
		format = 6;
		break;
	default:
		return false;
	}

	crtc_h_total_disp = ((((mode->crtc_htotal / 8) - 1) & 0x3ff)
			     | ((((mode->crtc_hdisplay / 8) - 1) & 0x1ff) << 16));

	hsync_wid = (mode->crtc_hsync_end - mode->crtc_hsync_start) / 8;
	if (!hsync_wid)
		hsync_wid = 1;
	hsync_start = mode->crtc_hsync_start - 8;

	crtc_h_sync_strt_wid = ((hsync_start & 0x1fff)
				| ((hsync_wid & 0x3f) << 16)
				| ((mode->flags & DRM_MODE_FLAG_NHSYNC)
				   ? RADEON_CRTC_H_SYNC_POL
				   : 0));

	/* This works for double scan mode. */
	crtc_v_total_disp = (((mode->crtc_vtotal - 1) & 0xffff)
			     | ((mode->crtc_vdisplay - 1) << 16));

	vsync_wid = mode->crtc_vsync_end - mode->crtc_vsync_start;
	if (!vsync_wid)
		vsync_wid = 1;

	crtc_v_sync_strt_wid = (((mode->crtc_vsync_start - 1) & 0xfff)
				| ((vsync_wid & 0x1f) << 16)
				| ((mode->flags & DRM_MODE_FLAG_NVSYNC)
				   ? RADEON_CRTC_V_SYNC_POL
				   : 0));

	/* TODO -> Dell Server */
	if (0) {
		uint32_t disp_hw_debug = RREG32(RADEON_DISP_HW_DEBUG);
		uint32_t tv_dac_cntl = RREG32(RADEON_TV_DAC_CNTL);
		uint32_t dac2_cntl = RREG32(RADEON_DAC_CNTL2);
		uint32_t crtc2_gen_cntl = RREG32(RADEON_CRTC2_GEN_CNTL);

		dac2_cntl &= ~RADEON_DAC2_DAC_CLK_SEL;
		dac2_cntl |= RADEON_DAC2_DAC2_CLK_SEL;

		/* For CRT on DAC2, don't turn it on if BIOS didn't
		   enable it, even it's detected.
		*/
		disp_hw_debug |= RADEON_CRT2_DISP1_SEL;
		tv_dac_cntl &= ~((1<<2) | (3<<8) | (7<<24) | (0xff<<16));
		tv_dac_cntl |= (0x03 | (2<<8) | (0x58<<16));

		WREG32(RADEON_TV_DAC_CNTL, tv_dac_cntl);
		WREG32(RADEON_DISP_HW_DEBUG, disp_hw_debug);
		WREG32(RADEON_DAC_CNTL2, dac2_cntl);
		WREG32(RADEON_CRTC2_GEN_CNTL, crtc2_gen_cntl);
	}

	if (radeon_crtc->crtc_id) {
		uint32_t crtc2_gen_cntl;
		uint32_t disp2_merge_cntl;

		/* check to see if TV DAC is enabled for another crtc and keep it enabled */
		if (RREG32(RADEON_CRTC2_GEN_CNTL) & RADEON_CRTC2_CRT2_ON)
			crtc2_gen_cntl = RADEON_CRTC2_CRT2_ON;
		else
			crtc2_gen_cntl = 0;

		crtc2_gen_cntl |= ((format << 8)
				   | RADEON_CRTC2_VSYNC_DIS
				   | RADEON_CRTC2_HSYNC_DIS
				   | RADEON_CRTC2_DISP_DIS
				   | RADEON_CRTC2_DISP_REQ_EN_B
				   | ((mode->flags & DRM_MODE_FLAG_DBLSCAN)
				      ? RADEON_CRTC2_DBL_SCAN_EN
				      : 0)
				   | ((mode->flags & DRM_MODE_FLAG_CSYNC)
				      ? RADEON_CRTC2_CSYNC_EN
				      : 0)
				   | ((mode->flags & DRM_MODE_FLAG_INTERLACE)
				      ? RADEON_CRTC2_INTERLACE_EN
				      : 0));

		disp2_merge_cntl = RREG32(RADEON_DISP2_MERGE_CNTL);
		disp2_merge_cntl &= ~RADEON_DISP2_RGB_OFFSET_EN;

		WREG32(RADEON_DISP2_MERGE_CNTL, disp2_merge_cntl);
		WREG32(RADEON_CRTC2_GEN_CNTL, crtc2_gen_cntl);
	} else {
		uint32_t crtc_gen_cntl;
		uint32_t crtc_ext_cntl;
		uint32_t disp_merge_cntl;

		crtc_gen_cntl = (RADEON_CRTC_EXT_DISP_EN
				 | (format << 8)
				 | RADEON_CRTC_DISP_REQ_EN_B
				 | ((mode->flags & DRM_MODE_FLAG_DBLSCAN)
				    ? RADEON_CRTC_DBL_SCAN_EN
				    : 0)
				 | ((mode->flags & DRM_MODE_FLAG_CSYNC)
				    ? RADEON_CRTC_CSYNC_EN
				    : 0)
				 | ((mode->flags & DRM_MODE_FLAG_INTERLACE)
				    ? RADEON_CRTC_INTERLACE_EN
				    : 0));

		crtc_ext_cntl = RREG32(RADEON_CRTC_EXT_CNTL);
		crtc_ext_cntl |= (RADEON_XCRT_CNT_EN |
				  RADEON_CRTC_VSYNC_DIS |
				  RADEON_CRTC_HSYNC_DIS |
				  RADEON_CRTC_DISPLAY_DIS);

		disp_merge_cntl = RREG32(RADEON_DISP_MERGE_CNTL);
		disp_merge_cntl &= ~RADEON_DISP_RGB_OFFSET_EN;

		WREG32(RADEON_DISP_MERGE_CNTL, disp_merge_cntl);
		WREG32(RADEON_CRTC_GEN_CNTL, crtc_gen_cntl);
		WREG32(RADEON_CRTC_EXT_CNTL, crtc_ext_cntl);
	}

	WREG32(RADEON_CRTC_H_TOTAL_DISP + radeon_crtc->crtc_offset, crtc_h_total_disp);
	WREG32(RADEON_CRTC_H_SYNC_STRT_WID + radeon_crtc->crtc_offset, crtc_h_sync_strt_wid);
	WREG32(RADEON_CRTC_V_TOTAL_DISP + radeon_crtc->crtc_offset, crtc_v_total_disp);
	WREG32(RADEON_CRTC_V_SYNC_STRT_WID + radeon_crtc->crtc_offset, crtc_v_sync_strt_wid);

	return true;
}

static void radeon_set_pll(struct drm_crtc *crtc, struct drm_display_mode *mode)
{
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_encoder *encoder;
	uint32_t feedback_div = 0;
	uint32_t frac_fb_div = 0;
	uint32_t reference_div = 0;
	uint32_t post_divider = 0;
	uint32_t freq = 0;
	uint8_t pll_gain;
	int pll_flags = RADEON_PLL_LEGACY;
	bool use_bios_divs = false;
	/* PLL registers */
	uint32_t pll_ref_div = 0;
	uint32_t pll_fb_post_div = 0;
	uint32_t htotal_cntl = 0;

	struct radeon_pll *pll;

	struct {
		int divider;
		int bitvalue;
	} *post_div, post_divs[]   = {
		/* From RAGE 128 VR/RAGE 128 GL Register
		 * Reference Manual (Technical Reference
		 * Manual P/N RRG-G04100-C Rev. 0.04), page
		 * 3-17 (PLL_DIV_[3:0]).
		 */
		{  1, 0 },              /* VCLK_SRC                 */
		{  2, 1 },              /* VCLK_SRC/2               */
		{  4, 2 },              /* VCLK_SRC/4               */
		{  8, 3 },              /* VCLK_SRC/8               */
		{  3, 4 },              /* VCLK_SRC/3               */
		{ 16, 5 },              /* VCLK_SRC/16              */
		{  6, 6 },              /* VCLK_SRC/6               */
		{ 12, 7 },              /* VCLK_SRC/12              */
		{  0, 0 }
	};

	if (radeon_crtc->crtc_id)
		pll = &rdev->clock.p2pll;
	else
		pll = &rdev->clock.p1pll;

	if (mode->clock > 200000) /* range limits??? */
		pll_flags |= RADEON_PLL_PREFER_HIGH_FB_DIV;
	else
		pll_flags |= RADEON_PLL_PREFER_LOW_REF_DIV;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		if (encoder->crtc == crtc) {
			if (encoder->encoder_type != DRM_MODE_ENCODER_DAC)
				pll_flags |= RADEON_PLL_NO_ODD_POST_DIV;
			if (encoder->encoder_type == DRM_MODE_ENCODER_LVDS) {
				struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
				struct radeon_encoder_lvds *lvds = (struct radeon_encoder_lvds *)radeon_encoder->enc_priv;
				if (lvds) {
					if (lvds->use_bios_dividers) {
						pll_ref_div = lvds->panel_ref_divider;
						pll_fb_post_div   = (lvds->panel_fb_divider |
								     (lvds->panel_post_divider << 16));
						htotal_cntl  = 0;
						use_bios_divs = true;
					}
				}
				pll_flags |= RADEON_PLL_USE_REF_DIV;
			}
		}
	}

	DRM_DEBUG("\n");

	if (!use_bios_divs) {
		radeon_compute_pll(pll, mode->clock,
				   &freq, &feedback_div, &frac_fb_div,
				   &reference_div, &post_divider,
				   pll_flags);

		for (post_div = &post_divs[0]; post_div->divider; ++post_div) {
			if (post_div->divider == post_divider)
				break;
		}

		if (!post_div->divider)
			post_div = &post_divs[0];

		DRM_DEBUG("dc=%u, fd=%d, rd=%d, pd=%d\n",
			  (unsigned)freq,
			  feedback_div,
			  reference_div,
			  post_divider);

		pll_ref_div   = reference_div;
#if defined(__powerpc__) && (0) /* TODO */
		/* apparently programming this otherwise causes a hang??? */
		if (info->MacModel == RADEON_MAC_IBOOK)
			pll_fb_post_div = 0x000600ad;
		else
#endif
			pll_fb_post_div     = (feedback_div | (post_div->bitvalue << 16));

		htotal_cntl    = mode->htotal & 0x7;

	}

	pll_gain = radeon_compute_pll_gain(pll->reference_freq,
					   pll_ref_div & 0x3ff,
					   pll_fb_post_div & 0x7ff);

	if (radeon_crtc->crtc_id) {
		uint32_t pixclks_cntl = ((RREG32_PLL(RADEON_PIXCLKS_CNTL) &
					  ~(RADEON_PIX2CLK_SRC_SEL_MASK)) |
					 RADEON_PIX2CLK_SRC_SEL_P2PLLCLK);

		WREG32_PLL_P(RADEON_PIXCLKS_CNTL,
			     RADEON_PIX2CLK_SRC_SEL_CPUCLK,
			     ~(RADEON_PIX2CLK_SRC_SEL_MASK));

		WREG32_PLL_P(RADEON_P2PLL_CNTL,
			     RADEON_P2PLL_RESET
			     | RADEON_P2PLL_ATOMIC_UPDATE_EN
			     | ((uint32_t)pll_gain << RADEON_P2PLL_PVG_SHIFT),
			     ~(RADEON_P2PLL_RESET
			       | RADEON_P2PLL_ATOMIC_UPDATE_EN
			       | RADEON_P2PLL_PVG_MASK));

		WREG32_PLL_P(RADEON_P2PLL_REF_DIV,
			     pll_ref_div,
			     ~RADEON_P2PLL_REF_DIV_MASK);

		WREG32_PLL_P(RADEON_P2PLL_DIV_0,
			     pll_fb_post_div,
			     ~RADEON_P2PLL_FB0_DIV_MASK);

		WREG32_PLL_P(RADEON_P2PLL_DIV_0,
			     pll_fb_post_div,
			     ~RADEON_P2PLL_POST0_DIV_MASK);

		radeon_pll2_write_update(dev);
		radeon_pll2_wait_for_read_update_complete(dev);

		WREG32_PLL(RADEON_HTOTAL2_CNTL, htotal_cntl);

		WREG32_PLL_P(RADEON_P2PLL_CNTL,
			     0,
			     ~(RADEON_P2PLL_RESET
			       | RADEON_P2PLL_SLEEP
			       | RADEON_P2PLL_ATOMIC_UPDATE_EN));

		DRM_DEBUG("Wrote2: 0x%08x 0x%08x 0x%08x (0x%08x)\n",
			  (unsigned)pll_ref_div,
			  (unsigned)pll_fb_post_div,
			  (unsigned)htotal_cntl,
			  RREG32_PLL(RADEON_P2PLL_CNTL));
		DRM_DEBUG("Wrote2: rd=%u, fd=%u, pd=%u\n",
			  (unsigned)pll_ref_div & RADEON_P2PLL_REF_DIV_MASK,
			  (unsigned)pll_fb_post_div & RADEON_P2PLL_FB0_DIV_MASK,
			  (unsigned)((pll_fb_post_div &
				      RADEON_P2PLL_POST0_DIV_MASK) >> 16));

		mdelay(50); /* Let the clock to lock */

		WREG32_PLL_P(RADEON_PIXCLKS_CNTL,
			     RADEON_PIX2CLK_SRC_SEL_P2PLLCLK,
			     ~(RADEON_PIX2CLK_SRC_SEL_MASK));

		WREG32_PLL(RADEON_PIXCLKS_CNTL, pixclks_cntl);
	} else {
		if (rdev->flags & RADEON_IS_MOBILITY) {
			/* A temporal workaround for the occational blanking on certain laptop panels.
			   This appears to related to the PLL divider registers (fail to lock?).
			   It occurs even when all dividers are the same with their old settings.
			   In this case we really don't need to fiddle with PLL registers.
			   By doing this we can avoid the blanking problem with some panels.
			*/
			if ((pll_ref_div == (RREG32_PLL(RADEON_PPLL_REF_DIV) & RADEON_PPLL_REF_DIV_MASK)) &&
			    (pll_fb_post_div == (RREG32_PLL(RADEON_PPLL_DIV_3) &
						 (RADEON_PPLL_POST3_DIV_MASK | RADEON_PPLL_FB3_DIV_MASK)))) {
				WREG32_P(RADEON_CLOCK_CNTL_INDEX,
					 RADEON_PLL_DIV_SEL,
					 ~(RADEON_PLL_DIV_SEL));
				r100_pll_errata_after_index(rdev);
				return;
			}
		}

		WREG32_PLL_P(RADEON_VCLK_ECP_CNTL,
			     RADEON_VCLK_SRC_SEL_CPUCLK,
			     ~(RADEON_VCLK_SRC_SEL_MASK));
		WREG32_PLL_P(RADEON_PPLL_CNTL,
			     RADEON_PPLL_RESET
			     | RADEON_PPLL_ATOMIC_UPDATE_EN
			     | RADEON_PPLL_VGA_ATOMIC_UPDATE_EN
			     | ((uint32_t)pll_gain << RADEON_PPLL_PVG_SHIFT),
			     ~(RADEON_PPLL_RESET
			       | RADEON_PPLL_ATOMIC_UPDATE_EN
			       | RADEON_PPLL_VGA_ATOMIC_UPDATE_EN
			       | RADEON_PPLL_PVG_MASK));

		WREG32_P(RADEON_CLOCK_CNTL_INDEX,
			 RADEON_PLL_DIV_SEL,
			 ~(RADEON_PLL_DIV_SEL));
		r100_pll_errata_after_index(rdev);

		if (ASIC_IS_R300(rdev) ||
		    (rdev->family == CHIP_RS300) ||
		    (rdev->family == CHIP_RS400) ||
		    (rdev->family == CHIP_RS480)) {
			if (pll_ref_div & R300_PPLL_REF_DIV_ACC_MASK) {
				/* When restoring console mode, use saved PPLL_REF_DIV
				 * setting.
				 */
				WREG32_PLL_P(RADEON_PPLL_REF_DIV,
					     pll_ref_div,
					     0);
			} else {
				/* R300 uses ref_div_acc field as real ref divider */
				WREG32_PLL_P(RADEON_PPLL_REF_DIV,
					     (pll_ref_div << R300_PPLL_REF_DIV_ACC_SHIFT),
					     ~R300_PPLL_REF_DIV_ACC_MASK);
			}
		} else
			WREG32_PLL_P(RADEON_PPLL_REF_DIV,
				     pll_ref_div,
				     ~RADEON_PPLL_REF_DIV_MASK);

		WREG32_PLL_P(RADEON_PPLL_DIV_3,
			     pll_fb_post_div,
			     ~RADEON_PPLL_FB3_DIV_MASK);

		WREG32_PLL_P(RADEON_PPLL_DIV_3,
			     pll_fb_post_div,
			     ~RADEON_PPLL_POST3_DIV_MASK);

		radeon_pll_write_update(dev);
		radeon_pll_wait_for_read_update_complete(dev);

		WREG32_PLL(RADEON_HTOTAL_CNTL, htotal_cntl);

		WREG32_PLL_P(RADEON_PPLL_CNTL,
			     0,
			     ~(RADEON_PPLL_RESET
			       | RADEON_PPLL_SLEEP
			       | RADEON_PPLL_ATOMIC_UPDATE_EN
			       | RADEON_PPLL_VGA_ATOMIC_UPDATE_EN));

		DRM_DEBUG("Wrote: 0x%08x 0x%08x 0x%08x (0x%08x)\n",
			  pll_ref_div,
			  pll_fb_post_div,
			  (unsigned)htotal_cntl,
			  RREG32_PLL(RADEON_PPLL_CNTL));
		DRM_DEBUG("Wrote: rd=%d, fd=%d, pd=%d\n",
			  pll_ref_div & RADEON_PPLL_REF_DIV_MASK,
			  pll_fb_post_div & RADEON_PPLL_FB3_DIV_MASK,
			  (pll_fb_post_div & RADEON_PPLL_POST3_DIV_MASK) >> 16);

		mdelay(50); /* Let the clock to lock */

		WREG32_PLL_P(RADEON_VCLK_ECP_CNTL,
			     RADEON_VCLK_SRC_SEL_PPLLCLK,
			     ~(RADEON_VCLK_SRC_SEL_MASK));

	}
}

static bool radeon_crtc_mode_fixup(struct drm_crtc *crtc,
				   struct drm_display_mode *mode,
				   struct drm_display_mode *adjusted_mode)
{
	return true;
}

static int radeon_crtc_mode_set(struct drm_crtc *crtc,
				 struct drm_display_mode *mode,
				 struct drm_display_mode *adjusted_mode,
				 int x, int y, struct drm_framebuffer *old_fb)
{

	DRM_DEBUG("\n");

	/* TODO TV */

	radeon_crtc_set_base(crtc, x, y, old_fb);
	radeon_set_crtc_timing(crtc, adjusted_mode);
	radeon_set_pll(crtc, adjusted_mode);
	radeon_init_disp_bandwidth(crtc->dev);

	return 0;
}

static void radeon_crtc_prepare(struct drm_crtc *crtc)
{
	radeon_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);
}

static void radeon_crtc_commit(struct drm_crtc *crtc)
{
	radeon_crtc_dpms(crtc, DRM_MODE_DPMS_ON);
}

static const struct drm_crtc_helper_funcs legacy_helper_funcs = {
	.dpms = radeon_crtc_dpms,
	.mode_fixup = radeon_crtc_mode_fixup,
	.mode_set = radeon_crtc_mode_set,
	.mode_set_base = radeon_crtc_set_base,
	.prepare = radeon_crtc_prepare,
	.commit = radeon_crtc_commit,
};


void radeon_legacy_init_crtc(struct drm_device *dev,
			       struct radeon_crtc *radeon_crtc)
{
	if (radeon_crtc->crtc_id == 1)
		radeon_crtc->crtc_offset = RADEON_CRTC2_H_TOTAL_DISP - RADEON_CRTC_H_TOTAL_DISP;
	drm_crtc_helper_add(&radeon_crtc->base, &legacy_helper_funcs);
}

void radeon_init_disp_bw_legacy(struct drm_device *dev,
				struct drm_display_mode *mode1,
				uint32_t pixel_bytes1,
				struct drm_display_mode *mode2,
				uint32_t pixel_bytes2)
{
	struct radeon_device *rdev = dev->dev_private;
	fixed20_12 trcd_ff, trp_ff, tras_ff, trbs_ff, tcas_ff;
	fixed20_12 sclk_ff, mclk_ff, sclk_eff_ff, sclk_delay_ff;
	fixed20_12 peak_disp_bw, mem_bw, pix_clk, pix_clk2, temp_ff, crit_point_ff;
	uint32_t temp, data, mem_trcd, mem_trp, mem_tras;
	fixed20_12 memtcas_ff[8] = {
		fixed_init(1),
		fixed_init(2),
		fixed_init(3),
		fixed_init(0),
		fixed_init_half(1),
		fixed_init_half(2),
		fixed_init(0),
	};
	fixed20_12 memtcas_rs480_ff[8] = {
		fixed_init(0),
		fixed_init(1),
		fixed_init(2),
		fixed_init(3),
		fixed_init(0),
		fixed_init_half(1),
		fixed_init_half(2),
		fixed_init_half(3),
	};
	fixed20_12 memtcas2_ff[8] = {
		fixed_init(0),
		fixed_init(1),
		fixed_init(2),
		fixed_init(3),
		fixed_init(4),
		fixed_init(5),
		fixed_init(6),
		fixed_init(7),
	};
	fixed20_12 memtrbs[8] = {
		fixed_init(1),
		fixed_init_half(1),
		fixed_init(2),
		fixed_init_half(2),
		fixed_init(3),
		fixed_init_half(3),
		fixed_init(4),
		fixed_init_half(4)
	};
	fixed20_12 memtrbs_r4xx[8] = {
		fixed_init(4),
		fixed_init(5),
		fixed_init(6),
		fixed_init(7),
		fixed_init(8),
		fixed_init(9),
		fixed_init(10),
		fixed_init(11)
	};
	fixed20_12 min_mem_eff;
	fixed20_12 mc_latency_sclk, mc_latency_mclk, k1;
	fixed20_12 cur_latency_mclk, cur_latency_sclk;
	fixed20_12 disp_latency, disp_latency_overhead, disp_drain_rate,
		disp_drain_rate2, read_return_rate;
	fixed20_12 time_disp1_drop_priority;
	int c;
	int cur_size = 16;       /* in octawords */
	int critical_point = 0, critical_point2;
/* 	uint32_t read_return_rate, time_disp1_drop_priority; */
	int stop_req, max_stop_req;

	min_mem_eff.full = rfixed_const_8(0);
	/* get modes */
	if ((rdev->disp_priority == 2) && ASIC_IS_R300(rdev)) {
		uint32_t mc_init_misc_lat_timer = RREG32(R300_MC_INIT_MISC_LAT_TIMER);
		mc_init_misc_lat_timer &= ~(R300_MC_DISP1R_INIT_LAT_MASK << R300_MC_DISP1R_INIT_LAT_SHIFT);
		mc_init_misc_lat_timer &= ~(R300_MC_DISP0R_INIT_LAT_MASK << R300_MC_DISP0R_INIT_LAT_SHIFT);
		/* check crtc enables */
		if (mode2)
			mc_init_misc_lat_timer |= (1 << R300_MC_DISP1R_INIT_LAT_SHIFT);
		if (mode1)
			mc_init_misc_lat_timer |= (1 << R300_MC_DISP0R_INIT_LAT_SHIFT);
		WREG32(R300_MC_INIT_MISC_LAT_TIMER, mc_init_misc_lat_timer);
	}

	/*
	 * determine is there is enough bw for current mode
	 */
	mclk_ff.full = rfixed_const(rdev->clock.default_mclk);
	temp_ff.full = rfixed_const(100);
	mclk_ff.full = rfixed_div(mclk_ff, temp_ff);
	sclk_ff.full = rfixed_const(rdev->clock.default_sclk);
	sclk_ff.full = rfixed_div(sclk_ff, temp_ff);

	temp = (rdev->mc.vram_width / 8) * (rdev->mc.vram_is_ddr ? 2 : 1);
	temp_ff.full = rfixed_const(temp);
	mem_bw.full = rfixed_mul(mclk_ff, temp_ff);

	pix_clk.full = 0;
	pix_clk2.full = 0;
	peak_disp_bw.full = 0;
	if (mode1) {
		temp_ff.full = rfixed_const(1000);
		pix_clk.full = rfixed_const(mode1->clock); /* convert to fixed point */
		pix_clk.full = rfixed_div(pix_clk, temp_ff);
		temp_ff.full = rfixed_const(pixel_bytes1);
		peak_disp_bw.full += rfixed_mul(pix_clk, temp_ff);
	}
	if (mode2) {
		temp_ff.full = rfixed_const(1000);
		pix_clk2.full = rfixed_const(mode2->clock); /* convert to fixed point */
		pix_clk2.full = rfixed_div(pix_clk2, temp_ff);
		temp_ff.full = rfixed_const(pixel_bytes2);
		peak_disp_bw.full += rfixed_mul(pix_clk2, temp_ff);
	}

	mem_bw.full = rfixed_mul(mem_bw, min_mem_eff);
	if (peak_disp_bw.full >= mem_bw.full) {
		DRM_ERROR("You may not have enough display bandwidth for current mode\n"
			  "If you have flickering problem, try to lower resolution, refresh rate, or color depth\n");
	}

	/*  Get values from the EXT_MEM_CNTL register...converting its contents. */
	temp = RREG32(RADEON_MEM_TIMING_CNTL);
	if ((rdev->family == CHIP_RV100) || (rdev->flags & RADEON_IS_IGP)) { /* RV100, M6, IGPs */
		mem_trcd = ((temp >> 2) & 0x3) + 1;
		mem_trp  = ((temp & 0x3)) + 1;
		mem_tras = ((temp & 0x70) >> 4) + 1;
	} else if (rdev->family == CHIP_R300 ||
		   rdev->family == CHIP_R350) { /* r300, r350 */
		mem_trcd = (temp & 0x7) + 1;
		mem_trp = ((temp >> 8) & 0x7) + 1;
		mem_tras = ((temp >> 11) & 0xf) + 4;
	} else if (rdev->family == CHIP_RV350 ||
		   rdev->family <= CHIP_RV380) {
		/* rv3x0 */
		mem_trcd = (temp & 0x7) + 3;
		mem_trp = ((temp >> 8) & 0x7) + 3;
		mem_tras = ((temp >> 11) & 0xf) + 6;
	} else if (rdev->family == CHIP_R420 ||
		   rdev->family == CHIP_R423 ||
		   rdev->family == CHIP_RV410) {
		/* r4xx */
		mem_trcd = (temp & 0xf) + 3;
		if (mem_trcd > 15)
			mem_trcd = 15;
		mem_trp = ((temp >> 8) & 0xf) + 3;
		if (mem_trp > 15)
			mem_trp = 15;
		mem_tras = ((temp >> 12) & 0x1f) + 6;
		if (mem_tras > 31)
			mem_tras = 31;
	} else { /* RV200, R200 */
		mem_trcd = (temp & 0x7) + 1;
		mem_trp = ((temp >> 8) & 0x7) + 1;
		mem_tras = ((temp >> 12) & 0xf) + 4;
	}
	/* convert to FF */
	trcd_ff.full = rfixed_const(mem_trcd);
	trp_ff.full = rfixed_const(mem_trp);
	tras_ff.full = rfixed_const(mem_tras);

	/* Get values from the MEM_SDRAM_MODE_REG register...converting its */
	temp = RREG32(RADEON_MEM_SDRAM_MODE_REG);
	data = (temp & (7 << 20)) >> 20;
	if ((rdev->family == CHIP_RV100) || rdev->flags & RADEON_IS_IGP) {
		if (rdev->family == CHIP_RS480) /* don't think rs400 */
			tcas_ff = memtcas_rs480_ff[data];
		else
			tcas_ff = memtcas_ff[data];
	} else
		tcas_ff = memtcas2_ff[data];

	if (rdev->family == CHIP_RS400 ||
	    rdev->family == CHIP_RS480) {
		/* extra cas latency stored in bits 23-25 0-4 clocks */
		data = (temp >> 23) & 0x7;
		if (data < 5)
			tcas_ff.full += rfixed_const(data);
	}

	if (ASIC_IS_R300(rdev) && !(rdev->flags & RADEON_IS_IGP)) {
		/* on the R300, Tcas is included in Trbs.
		 */
		temp = RREG32(RADEON_MEM_CNTL);
		data = (R300_MEM_NUM_CHANNELS_MASK & temp);
		if (data == 1) {
			if (R300_MEM_USE_CD_CH_ONLY & temp) {
				temp = RREG32(R300_MC_IND_INDEX);
				temp &= ~R300_MC_IND_ADDR_MASK;
				temp |= R300_MC_READ_CNTL_CD_mcind;
				WREG32(R300_MC_IND_INDEX, temp);
				temp = RREG32(R300_MC_IND_DATA);
				data = (R300_MEM_RBS_POSITION_C_MASK & temp);
			} else {
				temp = RREG32(R300_MC_READ_CNTL_AB);
				data = (R300_MEM_RBS_POSITION_A_MASK & temp);
			}
		} else {
			temp = RREG32(R300_MC_READ_CNTL_AB);
			data = (R300_MEM_RBS_POSITION_A_MASK & temp);
		}
		if (rdev->family == CHIP_RV410 ||
		    rdev->family == CHIP_R420 ||
		    rdev->family == CHIP_R423)
			trbs_ff = memtrbs_r4xx[data];
		else
			trbs_ff = memtrbs[data];
		tcas_ff.full += trbs_ff.full;
	}

	sclk_eff_ff.full = sclk_ff.full;

	if (rdev->flags & RADEON_IS_AGP) {
		fixed20_12 agpmode_ff;
		agpmode_ff.full = rfixed_const(radeon_agpmode);
		temp_ff.full = rfixed_const_666(16);
		sclk_eff_ff.full -= rfixed_mul(agpmode_ff, temp_ff);
	}
	/* TODO PCIE lanes may affect this - agpmode == 16?? */

	if (ASIC_IS_R300(rdev)) {
		sclk_delay_ff.full = rfixed_const(250);
	} else {
		if ((rdev->family == CHIP_RV100) ||
		    rdev->flags & RADEON_IS_IGP) {
			if (rdev->mc.vram_is_ddr)
				sclk_delay_ff.full = rfixed_const(41);
			else
				sclk_delay_ff.full = rfixed_const(33);
		} else {
			if (rdev->mc.vram_width == 128)
				sclk_delay_ff.full = rfixed_const(57);
			else
				sclk_delay_ff.full = rfixed_const(41);
		}
	}

	mc_latency_sclk.full = rfixed_div(sclk_delay_ff, sclk_eff_ff);

	if (rdev->mc.vram_is_ddr) {
		if (rdev->mc.vram_width == 32) {
			k1.full = rfixed_const(40);
			c  = 3;
		} else {
			k1.full = rfixed_const(20);
			c  = 1;
		}
	} else {
		k1.full = rfixed_const(40);
		c  = 3;
	}

	temp_ff.full = rfixed_const(2);
	mc_latency_mclk.full = rfixed_mul(trcd_ff, temp_ff);
	temp_ff.full = rfixed_const(c);
	mc_latency_mclk.full += rfixed_mul(tcas_ff, temp_ff);
	temp_ff.full = rfixed_const(4);
	mc_latency_mclk.full += rfixed_mul(tras_ff, temp_ff);
	mc_latency_mclk.full += rfixed_mul(trp_ff, temp_ff);
	mc_latency_mclk.full += k1.full;

	mc_latency_mclk.full = rfixed_div(mc_latency_mclk, mclk_ff);
	mc_latency_mclk.full += rfixed_div(temp_ff, sclk_eff_ff);

	/*
	  HW cursor time assuming worst case of full size colour cursor.
	*/
	temp_ff.full = rfixed_const((2 * (cur_size - (rdev->mc.vram_is_ddr + 1))));
	temp_ff.full += trcd_ff.full;
	if (temp_ff.full < tras_ff.full)
		temp_ff.full = tras_ff.full;
	cur_latency_mclk.full = rfixed_div(temp_ff, mclk_ff);

	temp_ff.full = rfixed_const(cur_size);
	cur_latency_sclk.full = rfixed_div(temp_ff, sclk_eff_ff);
	/*
	  Find the total latency for the display data.
	*/
	disp_latency_overhead.full = rfixed_const(80);
	disp_latency_overhead.full = rfixed_div(disp_latency_overhead, sclk_ff);
	mc_latency_mclk.full += disp_latency_overhead.full + cur_latency_mclk.full;
	mc_latency_sclk.full += disp_latency_overhead.full + cur_latency_sclk.full;

	if (mc_latency_mclk.full > mc_latency_sclk.full)
		disp_latency.full = mc_latency_mclk.full;
	else
		disp_latency.full = mc_latency_sclk.full;

	/* setup Max GRPH_STOP_REQ default value */
	if (ASIC_IS_RV100(rdev))
		max_stop_req = 0x5c;
	else
		max_stop_req = 0x7c;

	if (mode1) {
		/*  CRTC1
		    Set GRPH_BUFFER_CNTL register using h/w defined optimal values.
		    GRPH_STOP_REQ <= MIN[ 0x7C, (CRTC_H_DISP + 1) * (bit depth) / 0x10 ]
		*/
		stop_req = mode1->hdisplay * pixel_bytes1 / 16;

		if (stop_req > max_stop_req)
			stop_req = max_stop_req;

		/*
		  Find the drain rate of the display buffer.
		*/
		temp_ff.full = rfixed_const((16/pixel_bytes1));
		disp_drain_rate.full = rfixed_div(pix_clk, temp_ff);

		/*
		  Find the critical point of the display buffer.
		*/
		crit_point_ff.full = rfixed_mul(disp_drain_rate, disp_latency);
		crit_point_ff.full += rfixed_const_half(0);

		critical_point = rfixed_trunc(crit_point_ff);

		if (rdev->disp_priority == 2) {
			critical_point = 0;
		}

		/*
		  The critical point should never be above max_stop_req-4.  Setting
		  GRPH_CRITICAL_CNTL = 0 will thus force high priority all the time.
		*/
		if (max_stop_req - critical_point < 4)
			critical_point = 0;

		if (critical_point == 0 && mode2 && rdev->family == CHIP_R300) {
			/* some R300 cards have problem with this set to 0, when CRTC2 is enabled.*/
			critical_point = 0x10;
		}

		temp = RREG32(RADEON_GRPH_BUFFER_CNTL);
		temp &= ~(RADEON_GRPH_STOP_REQ_MASK);
		temp |= (stop_req << RADEON_GRPH_STOP_REQ_SHIFT);
		temp &= ~(RADEON_GRPH_START_REQ_MASK);
		if ((rdev->family == CHIP_R350) &&
		    (stop_req > 0x15)) {
			stop_req -= 0x10;
		}
		temp |= (stop_req << RADEON_GRPH_START_REQ_SHIFT);
		temp |= RADEON_GRPH_BUFFER_SIZE;
		temp &= ~(RADEON_GRPH_CRITICAL_CNTL   |
			  RADEON_GRPH_CRITICAL_AT_SOF |
			  RADEON_GRPH_STOP_CNTL);
		/*
		  Write the result into the register.
		*/
		WREG32(RADEON_GRPH_BUFFER_CNTL, ((temp & ~RADEON_GRPH_CRITICAL_POINT_MASK) |
						       (critical_point << RADEON_GRPH_CRITICAL_POINT_SHIFT)));

#if 0
		if ((rdev->family == CHIP_RS400) ||
		    (rdev->family == CHIP_RS480)) {
			/* attempt to program RS400 disp regs correctly ??? */
			temp = RREG32(RS400_DISP1_REG_CNTL);
			temp &= ~(RS400_DISP1_START_REQ_LEVEL_MASK |
				  RS400_DISP1_STOP_REQ_LEVEL_MASK);
			WREG32(RS400_DISP1_REQ_CNTL1, (temp |
						       (critical_point << RS400_DISP1_START_REQ_LEVEL_SHIFT) |
						       (critical_point << RS400_DISP1_STOP_REQ_LEVEL_SHIFT)));
			temp = RREG32(RS400_DMIF_MEM_CNTL1);
			temp &= ~(RS400_DISP1_CRITICAL_POINT_START_MASK |
				  RS400_DISP1_CRITICAL_POINT_STOP_MASK);
			WREG32(RS400_DMIF_MEM_CNTL1, (temp |
						      (critical_point << RS400_DISP1_CRITICAL_POINT_START_SHIFT) |
						      (critical_point << RS400_DISP1_CRITICAL_POINT_STOP_SHIFT)));
		}
#endif

		DRM_DEBUG("GRPH_BUFFER_CNTL from to %x\n",
			  /* 	  (unsigned int)info->SavedReg->grph_buffer_cntl, */
			  (unsigned int)RREG32(RADEON_GRPH_BUFFER_CNTL));
	}

	if (mode2) {
		u32 grph2_cntl;
		stop_req = mode2->hdisplay * pixel_bytes2 / 16;

		if (stop_req > max_stop_req)
			stop_req = max_stop_req;

		/*
		  Find the drain rate of the display buffer.
		*/
		temp_ff.full = rfixed_const((16/pixel_bytes2));
		disp_drain_rate2.full = rfixed_div(pix_clk2, temp_ff);

		grph2_cntl = RREG32(RADEON_GRPH2_BUFFER_CNTL);
		grph2_cntl &= ~(RADEON_GRPH_STOP_REQ_MASK);
		grph2_cntl |= (stop_req << RADEON_GRPH_STOP_REQ_SHIFT);
		grph2_cntl &= ~(RADEON_GRPH_START_REQ_MASK);
		if ((rdev->family == CHIP_R350) &&
		    (stop_req > 0x15)) {
			stop_req -= 0x10;
		}
		grph2_cntl |= (stop_req << RADEON_GRPH_START_REQ_SHIFT);
		grph2_cntl |= RADEON_GRPH_BUFFER_SIZE;
		grph2_cntl &= ~(RADEON_GRPH_CRITICAL_CNTL   |
			  RADEON_GRPH_CRITICAL_AT_SOF |
			  RADEON_GRPH_STOP_CNTL);

		if ((rdev->family == CHIP_RS100) ||
		    (rdev->family == CHIP_RS200))
			critical_point2 = 0;
		else {
			temp = (rdev->mc.vram_width * rdev->mc.vram_is_ddr + 1)/128;
			temp_ff.full = rfixed_const(temp);
			temp_ff.full = rfixed_mul(mclk_ff, temp_ff);
			if (sclk_ff.full < temp_ff.full)
				temp_ff.full = sclk_ff.full;

			read_return_rate.full = temp_ff.full;

			if (mode1) {
				temp_ff.full = read_return_rate.full - disp_drain_rate.full;
				time_disp1_drop_priority.full = rfixed_div(crit_point_ff, temp_ff);
			} else {
				time_disp1_drop_priority.full = 0;
			}
			crit_point_ff.full = disp_latency.full + time_disp1_drop_priority.full + disp_latency.full;
			crit_point_ff.full = rfixed_mul(crit_point_ff, disp_drain_rate2);
			crit_point_ff.full += rfixed_const_half(0);

			critical_point2 = rfixed_trunc(crit_point_ff);

			if (rdev->disp_priority == 2) {
				critical_point2 = 0;
			}

			if (max_stop_req - critical_point2 < 4)
				critical_point2 = 0;

		}

		if (critical_point2 == 0 && rdev->family == CHIP_R300) {
			/* some R300 cards have problem with this set to 0 */
			critical_point2 = 0x10;
		}

		WREG32(RADEON_GRPH2_BUFFER_CNTL, ((grph2_cntl & ~RADEON_GRPH_CRITICAL_POINT_MASK) |
						  (critical_point2 << RADEON_GRPH_CRITICAL_POINT_SHIFT)));

		if ((rdev->family == CHIP_RS400) ||
		    (rdev->family == CHIP_RS480)) {
#if 0
			/* attempt to program RS400 disp2 regs correctly ??? */
			temp = RREG32(RS400_DISP2_REQ_CNTL1);
			temp &= ~(RS400_DISP2_START_REQ_LEVEL_MASK |
				  RS400_DISP2_STOP_REQ_LEVEL_MASK);
			WREG32(RS400_DISP2_REQ_CNTL1, (temp |
						       (critical_point2 << RS400_DISP1_START_REQ_LEVEL_SHIFT) |
						       (critical_point2 << RS400_DISP1_STOP_REQ_LEVEL_SHIFT)));
			temp = RREG32(RS400_DISP2_REQ_CNTL2);
			temp &= ~(RS400_DISP2_CRITICAL_POINT_START_MASK |
				  RS400_DISP2_CRITICAL_POINT_STOP_MASK);
			WREG32(RS400_DISP2_REQ_CNTL2, (temp |
						       (critical_point2 << RS400_DISP2_CRITICAL_POINT_START_SHIFT) |
						       (critical_point2 << RS400_DISP2_CRITICAL_POINT_STOP_SHIFT)));
#endif
			WREG32(RS400_DISP2_REQ_CNTL1, 0x105DC1CC);
			WREG32(RS400_DISP2_REQ_CNTL2, 0x2749D000);
			WREG32(RS400_DMIF_MEM_CNTL1,  0x29CA71DC);
			WREG32(RS400_DISP1_REQ_CNTL1, 0x28FBC3AC);
		}

		DRM_DEBUG("GRPH2_BUFFER_CNTL from to %x\n",
			  (unsigned int)RREG32(RADEON_GRPH2_BUFFER_CNTL));
	}
}
