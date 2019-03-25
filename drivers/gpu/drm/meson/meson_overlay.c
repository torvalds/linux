// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/bitfield.h>
#include <linux/platform_device.h>
#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_rect.h>

#include "meson_overlay.h"
#include "meson_vpp.h"
#include "meson_viu.h"
#include "meson_registers.h"

/* VD1_IF0_GEN_REG */
#define VD_URGENT_CHROMA		BIT(28)
#define VD_URGENT_LUMA			BIT(27)
#define VD_HOLD_LINES(lines)		FIELD_PREP(GENMASK(24, 19), lines)
#define VD_DEMUX_MODE_RGB		BIT(16)
#define VD_BYTES_PER_PIXEL(val)		FIELD_PREP(GENMASK(15, 14), val)
#define VD_CHRO_RPT_LASTL_CTRL		BIT(6)
#define VD_LITTLE_ENDIAN		BIT(4)
#define VD_SEPARATE_EN			BIT(1)
#define VD_ENABLE			BIT(0)

/* VD1_IF0_CANVAS0 */
#define CANVAS_ADDR2(addr)		FIELD_PREP(GENMASK(23, 16), addr)
#define CANVAS_ADDR1(addr)		FIELD_PREP(GENMASK(15, 8), addr)
#define CANVAS_ADDR0(addr)		FIELD_PREP(GENMASK(7, 0), addr)

/* VD1_IF0_LUMA_X0 VD1_IF0_CHROMA_X0 */
#define VD_X_START(value)		FIELD_PREP(GENMASK(14, 0), value)
#define VD_X_END(value)			FIELD_PREP(GENMASK(30, 16), value)

/* VD1_IF0_LUMA_Y0 VD1_IF0_CHROMA_Y0 */
#define VD_Y_START(value)		FIELD_PREP(GENMASK(12, 0), value)
#define VD_Y_END(value)			FIELD_PREP(GENMASK(28, 16), value)

/* VD1_IF0_GEN_REG2 */
#define VD_COLOR_MAP(value)		FIELD_PREP(GENMASK(1, 0), value)

/* VIU_VD1_FMT_CTRL */
#define VD_HORZ_Y_C_RATIO(value)	FIELD_PREP(GENMASK(22, 21), value)
#define VD_HORZ_FMT_EN			BIT(20)
#define VD_VERT_RPT_LINE0		BIT(16)
#define VD_VERT_INITIAL_PHASE(value)	FIELD_PREP(GENMASK(11, 8), value)
#define VD_VERT_PHASE_STEP(value)	FIELD_PREP(GENMASK(7, 1), value)
#define VD_VERT_FMT_EN			BIT(0)

/* VPP_POSTBLEND_VD1_H_START_END */
#define VD_H_END(value)			FIELD_PREP(GENMASK(11, 0), value)
#define VD_H_START(value)		FIELD_PREP(GENMASK(27, 16), value)

/* VPP_POSTBLEND_VD1_V_START_END */
#define VD_V_END(value)			FIELD_PREP(GENMASK(11, 0), value)
#define VD_V_START(value)		FIELD_PREP(GENMASK(27, 16), value)

/* VPP_BLEND_VD2_V_START_END */
#define VD2_V_END(value)		FIELD_PREP(GENMASK(11, 0), value)
#define VD2_V_START(value)		FIELD_PREP(GENMASK(27, 16), value)

/* VIU_VD1_FMT_W */
#define VD_V_WIDTH(value)		FIELD_PREP(GENMASK(11, 0), value)
#define VD_H_WIDTH(value)		FIELD_PREP(GENMASK(27, 16), value)

/* VPP_HSC_REGION12_STARTP VPP_HSC_REGION34_STARTP */
#define VD_REGION24_START(value)	FIELD_PREP(GENMASK(11, 0), value)
#define VD_REGION13_END(value)		FIELD_PREP(GENMASK(27, 16), value)

struct meson_overlay {
	struct drm_plane base;
	struct meson_drm *priv;
};
#define to_meson_overlay(x) container_of(x, struct meson_overlay, base)

#define FRAC_16_16(mult, div)    (((mult) << 16) / (div))

static int meson_overlay_atomic_check(struct drm_plane *plane,
				      struct drm_plane_state *state)
{
	struct drm_crtc_state *crtc_state;

	if (!state->crtc)
		return 0;

	crtc_state = drm_atomic_get_crtc_state(state->state, state->crtc);
	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);

	return drm_atomic_helper_check_plane_state(state, crtc_state,
						   FRAC_16_16(1, 5),
						   FRAC_16_16(5, 1),
						   true, true);
}

/* Takes a fixed 16.16 number and converts it to integer. */
static inline int64_t fixed16_to_int(int64_t value)
{
	return value >> 16;
}

static const uint8_t skip_tab[6] = {
	0x24, 0x04, 0x68, 0x48, 0x28, 0x08,
};

static void meson_overlay_get_vertical_phase(unsigned int ratio_y, int *phase,
					     int *repeat, bool interlace)
{
	int offset_in = 0;
	int offset_out = 0;
	int repeat_skip = 0;

	if (!interlace && ratio_y > (1 << 18))
		offset_out = (1 * ratio_y) >> 10;

	while ((offset_in + (4 << 8)) <= offset_out) {
		repeat_skip++;
		offset_in += 4 << 8;
	}

	*phase = (offset_out - offset_in) >> 2;

	if (*phase > 0x100)
		repeat_skip++;

	*phase = *phase & 0xff;

	if (repeat_skip > 5)
		repeat_skip = 5;

	*repeat = skip_tab[repeat_skip];
}

static void meson_overlay_setup_scaler_params(struct meson_drm *priv,
					      struct drm_plane *plane,
					      bool interlace_mode)
{
	struct drm_crtc_state *crtc_state = priv->crtc->state;
	int video_top, video_left, video_width, video_height;
	struct drm_plane_state *state = plane->state;
	unsigned int vd_start_lines, vd_end_lines;
	unsigned int hd_start_lines, hd_end_lines;
	unsigned int crtc_height, crtc_width;
	unsigned int vsc_startp, vsc_endp;
	unsigned int hsc_startp, hsc_endp;
	unsigned int crop_top, crop_left;
	int vphase, vphase_repeat_skip;
	unsigned int ratio_x, ratio_y;
	int temp_height, temp_width;
	unsigned int w_in, h_in;
	int temp, start, end;

	if (!crtc_state) {
		DRM_ERROR("Invalid crtc_state\n");
		return;
	}

	crtc_height = crtc_state->mode.vdisplay;
	crtc_width = crtc_state->mode.hdisplay;

	w_in = fixed16_to_int(state->src_w);
	h_in = fixed16_to_int(state->src_h);
	crop_top = fixed16_to_int(state->src_x);
	crop_left = fixed16_to_int(state->src_x);

	video_top = state->crtc_y;
	video_left = state->crtc_x;
	video_width = state->crtc_w;
	video_height = state->crtc_h;

	DRM_DEBUG("crtc_width %d crtc_height %d interlace %d\n",
		  crtc_width, crtc_height, interlace_mode);
	DRM_DEBUG("w_in %d h_in %d crop_top %d crop_left %d\n",
		  w_in, h_in, crop_top, crop_left);
	DRM_DEBUG("video top %d left %d width %d height %d\n",
		  video_top, video_left, video_width, video_height);

	ratio_x = (w_in << 18) / video_width;
	ratio_y = (h_in << 18) / video_height;

	if (ratio_x * video_width < (w_in << 18))
		ratio_x++;

	DRM_DEBUG("ratio x 0x%x y 0x%x\n", ratio_x, ratio_y);

	meson_overlay_get_vertical_phase(ratio_y, &vphase, &vphase_repeat_skip,
					 interlace_mode);

	DRM_DEBUG("vphase 0x%x skip %d\n", vphase, vphase_repeat_skip);

	/* Vertical */

	start = video_top + video_height / 2 - ((h_in << 17) / ratio_y);
	end = (h_in << 18) / ratio_y + start - 1;

	if (video_top < 0 && start < 0)
		vd_start_lines = (-(start) * ratio_y) >> 18;
	else if (start < video_top)
		vd_start_lines = ((video_top - start) * ratio_y) >> 18;
	else
		vd_start_lines = 0;

	if (video_top < 0)
		temp_height = min_t(unsigned int,
				    video_top + video_height - 1,
				    crtc_height - 1);
	else
		temp_height = min_t(unsigned int,
				    video_top + video_height - 1,
				    crtc_height - 1) - video_top + 1;

	temp = vd_start_lines + (temp_height * ratio_y >> 18);
	vd_end_lines = (temp <= (h_in - 1)) ? temp : (h_in - 1);

	vd_start_lines += crop_left;
	vd_end_lines += crop_left;

	/*
	 * TOFIX: Input frames are handled and scaled like progressive frames,
	 * proper handling of interlaced field input frames need to be figured
	 * out using the proper framebuffer flags set by userspace.
	 */
	if (interlace_mode) {
		start >>= 1;
		end >>= 1;
	}

	vsc_startp = max_t(int, start,
			   max_t(int, 0, video_top));
	vsc_endp = min_t(int, end,
			 min_t(int, crtc_height - 1,
			       video_top + video_height - 1));

	DRM_DEBUG("vsc startp %d endp %d start_lines %d end_lines %d\n",
		 vsc_startp, vsc_endp, vd_start_lines, vd_end_lines);

	/* Horizontal */

	start = video_left + video_width / 2 - ((w_in << 17) / ratio_x);
	end = (w_in << 18) / ratio_x + start - 1;

	if (video_left < 0 && start < 0)
		hd_start_lines = (-(start) * ratio_x) >> 18;
	else if (start < video_left)
		hd_start_lines = ((video_left - start) * ratio_x) >> 18;
	else
		hd_start_lines = 0;

	if (video_left < 0)
		temp_width = min_t(unsigned int,
				   video_left + video_width - 1,
				   crtc_width - 1);
	else
		temp_width = min_t(unsigned int,
				   video_left + video_width - 1,
				   crtc_width - 1) - video_left + 1;

	temp = hd_start_lines + (temp_width * ratio_x >> 18);
	hd_end_lines = (temp <= (w_in - 1)) ? temp : (w_in - 1);

	priv->viu.vpp_line_in_length = hd_end_lines - hd_start_lines + 1;
	hsc_startp = max_t(int, start, max_t(int, 0, video_left));
	hsc_endp = min_t(int, end, min_t(int, crtc_width - 1,
					 video_left + video_width - 1));

	hd_start_lines += crop_top;
	hd_end_lines += crop_top;

	DRM_DEBUG("hsc startp %d endp %d start_lines %d end_lines %d\n",
		 hsc_startp, hsc_endp, hd_start_lines, hd_end_lines);

	priv->viu.vpp_vsc_start_phase_step = ratio_y << 6;

	priv->viu.vpp_vsc_ini_phase = vphase << 8;
	priv->viu.vpp_vsc_phase_ctrl = (1 << 13) | (4 << 8) |
				       vphase_repeat_skip;

	priv->viu.vd1_if0_luma_x0 = VD_X_START(hd_start_lines) |
				    VD_X_END(hd_end_lines);
	priv->viu.vd1_if0_chroma_x0 = VD_X_START(hd_start_lines >> 1) |
				      VD_X_END(hd_end_lines >> 1);

	priv->viu.viu_vd1_fmt_w =
			VD_H_WIDTH(hd_end_lines - hd_start_lines + 1) |
			VD_V_WIDTH(hd_end_lines/2 - hd_start_lines/2 + 1);

	priv->viu.vd1_if0_luma_y0 = VD_Y_START(vd_start_lines) |
				    VD_Y_END(vd_end_lines);

	priv->viu.vd1_if0_chroma_y0 = VD_Y_START(vd_start_lines >> 1) |
				      VD_Y_END(vd_end_lines >> 1);

	priv->viu.vpp_pic_in_height = h_in;

	priv->viu.vpp_postblend_vd1_h_start_end = VD_H_START(hsc_startp) |
						  VD_H_END(hsc_endp);
	priv->viu.vpp_blend_vd2_h_start_end = VD_H_START(hd_start_lines) |
					      VD_H_END(hd_end_lines);
	priv->viu.vpp_hsc_region12_startp = VD_REGION13_END(0) |
					    VD_REGION24_START(hsc_startp);
	priv->viu.vpp_hsc_region34_startp =
				VD_REGION13_END(hsc_startp) |
				VD_REGION24_START(hsc_endp - hsc_startp);
	priv->viu.vpp_hsc_region4_endp = hsc_endp - hsc_startp;
	priv->viu.vpp_hsc_start_phase_step = ratio_x << 6;
	priv->viu.vpp_hsc_region1_phase_slope = 0;
	priv->viu.vpp_hsc_region3_phase_slope = 0;
	priv->viu.vpp_hsc_phase_ctrl = (1 << 21) | (4 << 16);

	priv->viu.vpp_line_in_length = hd_end_lines - hd_start_lines + 1;
	priv->viu.vpp_preblend_h_size = hd_end_lines - hd_start_lines + 1;

	priv->viu.vpp_postblend_vd1_v_start_end = VD_V_START(vsc_startp) |
						  VD_V_END(vsc_endp);
	priv->viu.vpp_blend_vd2_v_start_end =
				VD2_V_START((vd_end_lines + 1) >> 1) |
				VD2_V_END(vd_end_lines);

	priv->viu.vpp_vsc_region12_startp = 0;
	priv->viu.vpp_vsc_region34_startp =
				VD_REGION13_END(vsc_endp - vsc_startp) |
				VD_REGION24_START(vsc_endp - vsc_startp);
	priv->viu.vpp_vsc_region4_endp = vsc_endp - vsc_startp;
	priv->viu.vpp_vsc_start_phase_step = ratio_y << 6;
}

static void meson_overlay_atomic_update(struct drm_plane *plane,
					struct drm_plane_state *old_state)
{
	struct meson_overlay *meson_overlay = to_meson_overlay(plane);
	struct drm_plane_state *state = plane->state;
	struct drm_framebuffer *fb = state->fb;
	struct meson_drm *priv = meson_overlay->priv;
	struct drm_gem_cma_object *gem;
	unsigned long flags;
	bool interlace_mode;

	DRM_DEBUG_DRIVER("\n");

	interlace_mode = state->crtc->mode.flags & DRM_MODE_FLAG_INTERLACE;

	spin_lock_irqsave(&priv->drm->event_lock, flags);

	priv->viu.vd1_if0_gen_reg = VD_URGENT_CHROMA |
				    VD_URGENT_LUMA |
				    VD_HOLD_LINES(9) |
				    VD_CHRO_RPT_LASTL_CTRL |
				    VD_ENABLE;

	/* Setup scaler params */
	meson_overlay_setup_scaler_params(priv, plane, interlace_mode);

	priv->viu.vd1_if0_repeat_loop = 0;
	priv->viu.vd1_if0_luma0_rpt_pat = interlace_mode ? 8 : 0;
	priv->viu.vd1_if0_chroma0_rpt_pat = interlace_mode ? 8 : 0;
	priv->viu.vd1_range_map_y = 0;
	priv->viu.vd1_range_map_cb = 0;
	priv->viu.vd1_range_map_cr = 0;

	/* Default values for RGB888/YUV444 */
	priv->viu.vd1_if0_gen_reg2 = 0;
	priv->viu.viu_vd1_fmt_ctrl = 0;

	switch (fb->format->format) {
	/* TOFIX DRM_FORMAT_RGB888 should be supported */
	case DRM_FORMAT_YUYV:
		priv->viu.vd1_if0_gen_reg |= VD_BYTES_PER_PIXEL(1);
		priv->viu.vd1_if0_canvas0 =
					CANVAS_ADDR2(priv->canvas_id_vd1_0) |
					CANVAS_ADDR1(priv->canvas_id_vd1_0) |
					CANVAS_ADDR0(priv->canvas_id_vd1_0);
		priv->viu.viu_vd1_fmt_ctrl = VD_HORZ_Y_C_RATIO(1) | /* /2 */
					     VD_HORZ_FMT_EN |
					     VD_VERT_RPT_LINE0 |
					     VD_VERT_INITIAL_PHASE(12) |
					     VD_VERT_PHASE_STEP(16) | /* /2 */
					     VD_VERT_FMT_EN;
		break;
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
		priv->viu.vd1_if0_gen_reg |= VD_SEPARATE_EN;
		priv->viu.vd1_if0_canvas0 =
					CANVAS_ADDR2(priv->canvas_id_vd1_1) |
					CANVAS_ADDR1(priv->canvas_id_vd1_1) |
					CANVAS_ADDR0(priv->canvas_id_vd1_0);
		if (fb->format->format == DRM_FORMAT_NV12)
			priv->viu.vd1_if0_gen_reg2 = VD_COLOR_MAP(1);
		else
			priv->viu.vd1_if0_gen_reg2 = VD_COLOR_MAP(2);
		priv->viu.viu_vd1_fmt_ctrl = VD_HORZ_Y_C_RATIO(1) | /* /2 */
					     VD_HORZ_FMT_EN |
					     VD_VERT_RPT_LINE0 |
					     VD_VERT_INITIAL_PHASE(12) |
					     VD_VERT_PHASE_STEP(8) | /* /4 */
					     VD_VERT_FMT_EN;
		break;
	case DRM_FORMAT_YUV444:
	case DRM_FORMAT_YUV422:
	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YUV411:
	case DRM_FORMAT_YUV410:
		priv->viu.vd1_if0_gen_reg |= VD_SEPARATE_EN;
		priv->viu.vd1_if0_canvas0 =
					CANVAS_ADDR2(priv->canvas_id_vd1_2) |
					CANVAS_ADDR1(priv->canvas_id_vd1_1) |
					CANVAS_ADDR0(priv->canvas_id_vd1_0);
		switch (fb->format->format) {
		case DRM_FORMAT_YUV422:
			priv->viu.viu_vd1_fmt_ctrl =
					VD_HORZ_Y_C_RATIO(1) | /* /2 */
					VD_HORZ_FMT_EN |
					VD_VERT_RPT_LINE0 |
					VD_VERT_INITIAL_PHASE(12) |
					VD_VERT_PHASE_STEP(16) | /* /2 */
					VD_VERT_FMT_EN;
			break;
		case DRM_FORMAT_YUV420:
			priv->viu.viu_vd1_fmt_ctrl =
					VD_HORZ_Y_C_RATIO(1) | /* /2 */
					VD_HORZ_FMT_EN |
					VD_VERT_RPT_LINE0 |
					VD_VERT_INITIAL_PHASE(12) |
					VD_VERT_PHASE_STEP(8) | /* /4 */
					VD_VERT_FMT_EN;
			break;
		case DRM_FORMAT_YUV411:
			priv->viu.viu_vd1_fmt_ctrl =
					VD_HORZ_Y_C_RATIO(2) | /* /4 */
					VD_HORZ_FMT_EN |
					VD_VERT_RPT_LINE0 |
					VD_VERT_INITIAL_PHASE(12) |
					VD_VERT_PHASE_STEP(16) | /* /2 */
					VD_VERT_FMT_EN;
			break;
		case DRM_FORMAT_YUV410:
			priv->viu.viu_vd1_fmt_ctrl =
					VD_HORZ_Y_C_RATIO(2) | /* /4 */
					VD_HORZ_FMT_EN |
					VD_VERT_RPT_LINE0 |
					VD_VERT_INITIAL_PHASE(12) |
					VD_VERT_PHASE_STEP(8) | /* /4 */
					VD_VERT_FMT_EN;
			break;
		}
		break;
	}

	/* Update Canvas with buffer address */
	priv->viu.vd1_planes = drm_format_num_planes(fb->format->format);

	switch (priv->viu.vd1_planes) {
	case 3:
		gem = drm_fb_cma_get_gem_obj(fb, 2);
		priv->viu.vd1_addr2 = gem->paddr + fb->offsets[2];
		priv->viu.vd1_stride2 = fb->pitches[2];
		priv->viu.vd1_height2 =
			drm_format_plane_height(fb->height,
						fb->format->format, 2);
		DRM_DEBUG("plane 2 addr 0x%x stride %d height %d\n",
			 priv->viu.vd1_addr2,
			 priv->viu.vd1_stride2,
			 priv->viu.vd1_height2);
	/* fallthrough */
	case 2:
		gem = drm_fb_cma_get_gem_obj(fb, 1);
		priv->viu.vd1_addr1 = gem->paddr + fb->offsets[1];
		priv->viu.vd1_stride1 = fb->pitches[1];
		priv->viu.vd1_height1 =
			drm_format_plane_height(fb->height,
						fb->format->format, 1);
		DRM_DEBUG("plane 1 addr 0x%x stride %d height %d\n",
			 priv->viu.vd1_addr1,
			 priv->viu.vd1_stride1,
			 priv->viu.vd1_height1);
	/* fallthrough */
	case 1:
		gem = drm_fb_cma_get_gem_obj(fb, 0);
		priv->viu.vd1_addr0 = gem->paddr + fb->offsets[0];
		priv->viu.vd1_stride0 = fb->pitches[0];
		priv->viu.vd1_height0 =
			drm_format_plane_height(fb->height,
						fb->format->format, 0);
		DRM_DEBUG("plane 0 addr 0x%x stride %d height %d\n",
			 priv->viu.vd1_addr0,
			 priv->viu.vd1_stride0,
			 priv->viu.vd1_height0);
	}

	priv->viu.vd1_enabled = true;

	spin_unlock_irqrestore(&priv->drm->event_lock, flags);

	DRM_DEBUG_DRIVER("\n");
}

static void meson_overlay_atomic_disable(struct drm_plane *plane,
				       struct drm_plane_state *old_state)
{
	struct meson_overlay *meson_overlay = to_meson_overlay(plane);
	struct meson_drm *priv = meson_overlay->priv;

	DRM_DEBUG_DRIVER("\n");

	priv->viu.vd1_enabled = false;

	/* Disable VD1 */
	if (meson_vpu_is_compatible(priv, "amlogic,meson-g12a-vpu")) {
		writel_relaxed(0, priv->io_base + _REG(VD1_BLEND_SRC_CTRL));
		writel_relaxed(0, priv->io_base + _REG(VD2_BLEND_SRC_CTRL));
		writel_relaxed(0, priv->io_base + _REG(VD1_IF0_GEN_REG + 0x17b0));
		writel_relaxed(0, priv->io_base + _REG(VD2_IF0_GEN_REG + 0x17b0));
	} else
		writel_bits_relaxed(VPP_VD1_POSTBLEND | VPP_VD1_PREBLEND, 0,
				    priv->io_base + _REG(VPP_MISC));

}

static const struct drm_plane_helper_funcs meson_overlay_helper_funcs = {
	.atomic_check	= meson_overlay_atomic_check,
	.atomic_disable	= meson_overlay_atomic_disable,
	.atomic_update	= meson_overlay_atomic_update,
	.prepare_fb	= drm_gem_fb_prepare_fb,
};

static const struct drm_plane_funcs meson_overlay_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= drm_plane_cleanup,
	.reset			= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
};

static const uint32_t supported_drm_formats[] = {
	DRM_FORMAT_YUYV,
	DRM_FORMAT_NV12,
	DRM_FORMAT_NV21,
	DRM_FORMAT_YUV444,
	DRM_FORMAT_YUV422,
	DRM_FORMAT_YUV420,
	DRM_FORMAT_YUV411,
	DRM_FORMAT_YUV410,
};

int meson_overlay_create(struct meson_drm *priv)
{
	struct meson_overlay *meson_overlay;
	struct drm_plane *plane;

	DRM_DEBUG_DRIVER("\n");

	meson_overlay = devm_kzalloc(priv->drm->dev, sizeof(*meson_overlay),
				   GFP_KERNEL);
	if (!meson_overlay)
		return -ENOMEM;

	meson_overlay->priv = priv;
	plane = &meson_overlay->base;

	drm_universal_plane_init(priv->drm, plane, 0xFF,
				 &meson_overlay_funcs,
				 supported_drm_formats,
				 ARRAY_SIZE(supported_drm_formats),
				 NULL,
				 DRM_PLANE_TYPE_OVERLAY, "meson_overlay_plane");

	drm_plane_helper_add(plane, &meson_overlay_helper_funcs);

	priv->overlay_plane = plane;

	DRM_DEBUG_DRIVER("\n");

	return 0;
}
