// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 * Copyright (C) 2014 Endless Mobile
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include <linux/bitfield.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_device.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_plane_helper.h>

#include "meson_plane.h"
#include "meson_registers.h"
#include "meson_viu.h"
#include "meson_osd_afbcd.h"

/* OSD_SCI_WH_M1 */
#define SCI_WH_M1_W(w)			FIELD_PREP(GENMASK(28, 16), w)
#define SCI_WH_M1_H(h)			FIELD_PREP(GENMASK(12, 0), h)

/* OSD_SCO_H_START_END */
/* OSD_SCO_V_START_END */
#define SCO_HV_START(start)		FIELD_PREP(GENMASK(27, 16), start)
#define SCO_HV_END(end)			FIELD_PREP(GENMASK(11, 0), end)

/* OSD_SC_CTRL0 */
#define SC_CTRL0_PATH_EN		BIT(3)
#define SC_CTRL0_SEL_OSD1		BIT(2)

/* OSD_VSC_CTRL0 */
#define VSC_BANK_LEN(value)		FIELD_PREP(GENMASK(2, 0), value)
#define VSC_TOP_INI_RCV_NUM(value)	FIELD_PREP(GENMASK(6, 3), value)
#define VSC_TOP_RPT_L0_NUM(value)	FIELD_PREP(GENMASK(9, 8), value)
#define VSC_BOT_INI_RCV_NUM(value)	FIELD_PREP(GENMASK(14, 11), value)
#define VSC_BOT_RPT_L0_NUM(value)	FIELD_PREP(GENMASK(17, 16), value)
#define VSC_PROG_INTERLACE		BIT(23)
#define VSC_VERTICAL_SCALER_EN		BIT(24)

/* OSD_VSC_INI_PHASE */
#define VSC_INI_PHASE_BOT(bottom)	FIELD_PREP(GENMASK(31, 16), bottom)
#define VSC_INI_PHASE_TOP(top)		FIELD_PREP(GENMASK(15, 0), top)

/* OSD_HSC_CTRL0 */
#define HSC_BANK_LENGTH(value)		FIELD_PREP(GENMASK(2, 0), value)
#define HSC_INI_RCV_NUM0(value)		FIELD_PREP(GENMASK(6, 3), value)
#define HSC_RPT_P0_NUM0(value)		FIELD_PREP(GENMASK(9, 8), value)
#define HSC_HORIZ_SCALER_EN		BIT(22)

/* VPP_OSD_VSC_PHASE_STEP */
/* VPP_OSD_HSC_PHASE_STEP */
#define SC_PHASE_STEP(value)		FIELD_PREP(GENMASK(27, 0), value)

struct meson_plane {
	struct drm_plane base;
	struct meson_drm *priv;
	bool enabled;
};
#define to_meson_plane(x) container_of(x, struct meson_plane, base)

#define FRAC_16_16(mult, div)    (((mult) << 16) / (div))

static int meson_plane_atomic_check(struct drm_plane *plane,
				    struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
										 plane);
	struct drm_crtc_state *crtc_state;

	if (!new_plane_state->crtc)
		return 0;

	crtc_state = drm_atomic_get_crtc_state(state,
					       new_plane_state->crtc);
	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);

	/*
	 * Only allow :
	 * - Upscaling up to 5x, vertical and horizontal
	 * - Final coordinates must match crtc size
	 */
	return drm_atomic_helper_check_plane_state(new_plane_state,
						   crtc_state,
						   FRAC_16_16(1, 5),
						   DRM_PLANE_HELPER_NO_SCALING,
						   false, true);
}

#define MESON_MOD_AFBC_VALID_BITS (AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |	\
				   AFBC_FORMAT_MOD_BLOCK_SIZE_32x8 |	\
				   AFBC_FORMAT_MOD_YTR |		\
				   AFBC_FORMAT_MOD_SPARSE |		\
				   AFBC_FORMAT_MOD_SPLIT)

/* Takes a fixed 16.16 number and converts it to integer. */
static inline int64_t fixed16_to_int(int64_t value)
{
	return value >> 16;
}

static u32 meson_g12a_afbcd_line_stride(struct meson_drm *priv)
{
	u32 line_stride = 0;

	switch (priv->afbcd.format) {
	case DRM_FORMAT_RGB565:
		line_stride = ((priv->viu.osd1_width << 4) + 127) >> 7;
		break;
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
		line_stride = ((priv->viu.osd1_width << 5) + 127) >> 7;
		break;
	}

	return ((line_stride + 1) >> 1) << 1;
}

static void meson_plane_atomic_update(struct drm_plane *plane,
				      struct drm_atomic_state *state)
{
	struct meson_plane *meson_plane = to_meson_plane(plane);
	struct drm_plane_state *new_state = drm_atomic_get_new_plane_state(state,
									   plane);
	struct drm_rect dest = drm_plane_state_dest(new_state);
	struct meson_drm *priv = meson_plane->priv;
	struct drm_framebuffer *fb = new_state->fb;
	struct drm_gem_cma_object *gem;
	unsigned long flags;
	int vsc_ini_rcv_num, vsc_ini_rpt_p0_num;
	int vsc_bot_rcv_num, vsc_bot_rpt_p0_num;
	int hsc_ini_rcv_num, hsc_ini_rpt_p0_num;
	int hf_phase_step, vf_phase_step;
	int src_w, src_h, dst_w, dst_h;
	int bot_ini_phase;
	int hf_bank_len;
	int vf_bank_len;
	u8 canvas_id_osd1;

	/*
	 * Update Coordinates
	 * Update Formats
	 * Update Buffer
	 * Enable Plane
	 */
	spin_lock_irqsave(&priv->drm->event_lock, flags);

	/* Check if AFBC decoder is required for this buffer */
	if ((meson_vpu_is_compatible(priv, VPU_COMPATIBLE_GXM) ||
	     meson_vpu_is_compatible(priv, VPU_COMPATIBLE_G12A)) &&
	    fb->modifier & DRM_FORMAT_MOD_ARM_AFBC(MESON_MOD_AFBC_VALID_BITS))
		priv->viu.osd1_afbcd = true;
	else
		priv->viu.osd1_afbcd = false;

	/* Enable OSD and BLK0, set max global alpha */
	priv->viu.osd1_ctrl_stat = OSD_ENABLE |
				   (0xFF << OSD_GLOBAL_ALPHA_SHIFT) |
				   OSD_BLK0_ENABLE;

	priv->viu.osd1_ctrl_stat2 = readl(priv->io_base +
					  _REG(VIU_OSD1_CTRL_STAT2));

	canvas_id_osd1 = priv->canvas_id_osd1;

	/* Set up BLK0 to point to the right canvas */
	priv->viu.osd1_blk0_cfg[0] = canvas_id_osd1 << OSD_CANVAS_SEL;

	if (priv->viu.osd1_afbcd) {
		if (meson_vpu_is_compatible(priv, VPU_COMPATIBLE_G12A)) {
			/* This is the internal decoding memory address */
			priv->viu.osd1_blk1_cfg4 = MESON_G12A_AFBCD_OUT_ADDR;
			priv->viu.osd1_blk0_cfg[0] |= OSD_ENDIANNESS_BE;
			priv->viu.osd1_ctrl_stat2 |= OSD_PENDING_STAT_CLEAN;
			priv->viu.osd1_ctrl_stat |= VIU_OSD1_CFG_SYN_EN;
		}

		if (meson_vpu_is_compatible(priv, VPU_COMPATIBLE_GXM)) {
			priv->viu.osd1_blk0_cfg[0] |= OSD_ENDIANNESS_LE;
			priv->viu.osd1_ctrl_stat2 |= OSD_DPATH_MALI_AFBCD;
		}
	} else {
		priv->viu.osd1_blk0_cfg[0] |= OSD_ENDIANNESS_LE;

		if (meson_vpu_is_compatible(priv, VPU_COMPATIBLE_GXM))
			priv->viu.osd1_ctrl_stat2 &= ~OSD_DPATH_MALI_AFBCD;
	}

	/* On GXBB, Use the old non-HDR RGB2YUV converter */
	if (meson_vpu_is_compatible(priv, VPU_COMPATIBLE_GXBB))
		priv->viu.osd1_blk0_cfg[0] |= OSD_OUTPUT_COLOR_RGB;

	if (priv->viu.osd1_afbcd &&
	    meson_vpu_is_compatible(priv, VPU_COMPATIBLE_G12A)) {
		priv->viu.osd1_blk0_cfg[0] |= OSD_MALI_SRC_EN |
			priv->afbcd.ops->fmt_to_blk_mode(fb->modifier,
							  fb->format->format);
	} else {
		switch (fb->format->format) {
		case DRM_FORMAT_XRGB8888:
		case DRM_FORMAT_ARGB8888:
			priv->viu.osd1_blk0_cfg[0] |= OSD_BLK_MODE_32 |
						OSD_COLOR_MATRIX_32_ARGB;
			break;
		case DRM_FORMAT_XBGR8888:
		case DRM_FORMAT_ABGR8888:
			priv->viu.osd1_blk0_cfg[0] |= OSD_BLK_MODE_32 |
						OSD_COLOR_MATRIX_32_ABGR;
			break;
		case DRM_FORMAT_RGB888:
			priv->viu.osd1_blk0_cfg[0] |= OSD_BLK_MODE_24 |
						OSD_COLOR_MATRIX_24_RGB;
			break;
		case DRM_FORMAT_RGB565:
			priv->viu.osd1_blk0_cfg[0] |= OSD_BLK_MODE_16 |
						OSD_COLOR_MATRIX_16_RGB565;
			break;
		}
	}

	switch (fb->format->format) {
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_XBGR8888:
		/* For XRGB, replace the pixel's alpha by 0xFF */
		priv->viu.osd1_ctrl_stat2 |= OSD_REPLACE_EN;
		break;
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_ABGR8888:
		/* For ARGB, use the pixel's alpha */
		priv->viu.osd1_ctrl_stat2 &= ~OSD_REPLACE_EN;
		break;
	}

	/* Default scaler parameters */
	vsc_bot_rcv_num = 0;
	vsc_bot_rpt_p0_num = 0;
	hf_bank_len = 4;
	vf_bank_len = 4;

	if (new_state->crtc->mode.flags & DRM_MODE_FLAG_INTERLACE) {
		vsc_bot_rcv_num = 6;
		vsc_bot_rpt_p0_num = 2;
	}

	hsc_ini_rcv_num = hf_bank_len;
	vsc_ini_rcv_num = vf_bank_len;
	hsc_ini_rpt_p0_num = (hf_bank_len / 2) - 1;
	vsc_ini_rpt_p0_num = (vf_bank_len / 2) - 1;

	src_w = fixed16_to_int(new_state->src_w);
	src_h = fixed16_to_int(new_state->src_h);
	dst_w = new_state->crtc_w;
	dst_h = new_state->crtc_h;

	/*
	 * When the output is interlaced, the OSD must switch between
	 * each field using the INTERLACE_SEL_ODD (0) of VIU_OSD1_BLK0_CFG_W0
	 * at each vsync.
	 * But the vertical scaler can provide such funtionnality if
	 * is configured for 2:1 scaling with interlace options enabled.
	 */
	if (new_state->crtc->mode.flags & DRM_MODE_FLAG_INTERLACE) {
		dest.y1 /= 2;
		dest.y2 /= 2;
		dst_h /= 2;
	}

	hf_phase_step = ((src_w << 18) / dst_w) << 6;
	vf_phase_step = (src_h << 20) / dst_h;

	if (new_state->crtc->mode.flags & DRM_MODE_FLAG_INTERLACE)
		bot_ini_phase = ((vf_phase_step / 2) >> 4);
	else
		bot_ini_phase = 0;

	vf_phase_step = (vf_phase_step << 4);

	/* In interlaced mode, scaler is always active */
	if (src_h != dst_h || src_w != dst_w) {
		priv->viu.osd_sc_i_wh_m1 = SCI_WH_M1_W(src_w - 1) |
					   SCI_WH_M1_H(src_h - 1);
		priv->viu.osd_sc_o_h_start_end = SCO_HV_START(dest.x1) |
						 SCO_HV_END(dest.x2 - 1);
		priv->viu.osd_sc_o_v_start_end = SCO_HV_START(dest.y1) |
						 SCO_HV_END(dest.y2 - 1);
		/* Enable OSD Scaler */
		priv->viu.osd_sc_ctrl0 = SC_CTRL0_PATH_EN | SC_CTRL0_SEL_OSD1;
	} else {
		priv->viu.osd_sc_i_wh_m1 = 0;
		priv->viu.osd_sc_o_h_start_end = 0;
		priv->viu.osd_sc_o_v_start_end = 0;
		priv->viu.osd_sc_ctrl0 = 0;
	}

	/* In interlaced mode, vertical scaler is always active */
	if (src_h != dst_h) {
		priv->viu.osd_sc_v_ctrl0 =
					VSC_BANK_LEN(vf_bank_len) |
					VSC_TOP_INI_RCV_NUM(vsc_ini_rcv_num) |
					VSC_TOP_RPT_L0_NUM(vsc_ini_rpt_p0_num) |
					VSC_VERTICAL_SCALER_EN;

		if (new_state->crtc->mode.flags & DRM_MODE_FLAG_INTERLACE)
			priv->viu.osd_sc_v_ctrl0 |=
					VSC_BOT_INI_RCV_NUM(vsc_bot_rcv_num) |
					VSC_BOT_RPT_L0_NUM(vsc_bot_rpt_p0_num) |
					VSC_PROG_INTERLACE;

		priv->viu.osd_sc_v_phase_step = SC_PHASE_STEP(vf_phase_step);
		priv->viu.osd_sc_v_ini_phase = VSC_INI_PHASE_BOT(bot_ini_phase);
	} else {
		priv->viu.osd_sc_v_ctrl0 = 0;
		priv->viu.osd_sc_v_phase_step = 0;
		priv->viu.osd_sc_v_ini_phase = 0;
	}

	/* Horizontal scaler is only used if width does not match */
	if (src_w != dst_w) {
		priv->viu.osd_sc_h_ctrl0 =
					HSC_BANK_LENGTH(hf_bank_len) |
					HSC_INI_RCV_NUM0(hsc_ini_rcv_num) |
					HSC_RPT_P0_NUM0(hsc_ini_rpt_p0_num) |
					HSC_HORIZ_SCALER_EN;
		priv->viu.osd_sc_h_phase_step = SC_PHASE_STEP(hf_phase_step);
		priv->viu.osd_sc_h_ini_phase = 0;
	} else {
		priv->viu.osd_sc_h_ctrl0 = 0;
		priv->viu.osd_sc_h_phase_step = 0;
		priv->viu.osd_sc_h_ini_phase = 0;
	}

	/*
	 * The format of these registers is (x2 << 16 | x1),
	 * where x2 is exclusive.
	 * e.g. +30x1920 would be (1919 << 16) | 30
	 */
	priv->viu.osd1_blk0_cfg[1] =
				((fixed16_to_int(new_state->src.x2) - 1) << 16) |
				fixed16_to_int(new_state->src.x1);
	priv->viu.osd1_blk0_cfg[2] =
				((fixed16_to_int(new_state->src.y2) - 1) << 16) |
				fixed16_to_int(new_state->src.y1);
	priv->viu.osd1_blk0_cfg[3] = ((dest.x2 - 1) << 16) | dest.x1;
	priv->viu.osd1_blk0_cfg[4] = ((dest.y2 - 1) << 16) | dest.y1;

	if (meson_vpu_is_compatible(priv, VPU_COMPATIBLE_G12A)) {
		priv->viu.osd_blend_din0_scope_h = ((dest.x2 - 1) << 16) | dest.x1;
		priv->viu.osd_blend_din0_scope_v = ((dest.y2 - 1) << 16) | dest.y1;
		priv->viu.osb_blend0_size = dst_h << 16 | dst_w;
		priv->viu.osb_blend1_size = dst_h << 16 | dst_w;
	}

	/* Update Canvas with buffer address */
	gem = drm_fb_cma_get_gem_obj(fb, 0);

	priv->viu.osd1_addr = gem->paddr;
	priv->viu.osd1_stride = fb->pitches[0];
	priv->viu.osd1_height = fb->height;
	priv->viu.osd1_width = fb->width;

	if (priv->viu.osd1_afbcd) {
		priv->afbcd.modifier = fb->modifier;
		priv->afbcd.format = fb->format->format;

		/* Calculate decoder write stride */
		if (meson_vpu_is_compatible(priv, VPU_COMPATIBLE_G12A))
			priv->viu.osd1_blk2_cfg4 =
				meson_g12a_afbcd_line_stride(priv);
	}

	if (!meson_plane->enabled) {
		/* Reset OSD1 before enabling it on GXL+ SoCs */
		if (meson_vpu_is_compatible(priv, VPU_COMPATIBLE_GXM) ||
		    meson_vpu_is_compatible(priv, VPU_COMPATIBLE_GXL))
			meson_viu_osd1_reset(priv);

		meson_plane->enabled = true;
	}

	priv->viu.osd1_enabled = true;

	spin_unlock_irqrestore(&priv->drm->event_lock, flags);
}

static void meson_plane_atomic_disable(struct drm_plane *plane,
				       struct drm_atomic_state *state)
{
	struct meson_plane *meson_plane = to_meson_plane(plane);
	struct meson_drm *priv = meson_plane->priv;

	if (priv->afbcd.ops) {
		priv->afbcd.ops->reset(priv);
		priv->afbcd.ops->disable(priv);
	}

	/* Disable OSD1 */
	if (meson_vpu_is_compatible(priv, VPU_COMPATIBLE_G12A))
		writel_bits_relaxed(VIU_OSD1_POSTBLD_SRC_OSD1, 0,
				    priv->io_base + _REG(OSD1_BLEND_SRC_CTRL));
	else
		writel_bits_relaxed(VPP_OSD1_POSTBLEND, 0,
				    priv->io_base + _REG(VPP_MISC));

	meson_plane->enabled = false;
	priv->viu.osd1_enabled = false;
}

static const struct drm_plane_helper_funcs meson_plane_helper_funcs = {
	.atomic_check	= meson_plane_atomic_check,
	.atomic_disable	= meson_plane_atomic_disable,
	.atomic_update	= meson_plane_atomic_update,
};

static bool meson_plane_format_mod_supported(struct drm_plane *plane,
					     u32 format, u64 modifier)
{
	struct meson_plane *meson_plane = to_meson_plane(plane);
	struct meson_drm *priv = meson_plane->priv;
	int i;

	if (modifier == DRM_FORMAT_MOD_INVALID)
		return false;

	if (modifier == DRM_FORMAT_MOD_LINEAR)
		return true;

	if (!meson_vpu_is_compatible(priv, VPU_COMPATIBLE_GXM) &&
	    !meson_vpu_is_compatible(priv, VPU_COMPATIBLE_G12A))
		return false;

	if (modifier & ~DRM_FORMAT_MOD_ARM_AFBC(MESON_MOD_AFBC_VALID_BITS))
		return false;

	for (i = 0 ; i < plane->modifier_count ; ++i)
		if (plane->modifiers[i] == modifier)
			break;

	if (i == plane->modifier_count) {
		DRM_DEBUG_KMS("Unsupported modifier\n");
		return false;
	}

	if (priv->afbcd.ops && priv->afbcd.ops->supported_fmt)
		return priv->afbcd.ops->supported_fmt(modifier, format);

	DRM_DEBUG_KMS("AFBC Unsupported\n");
	return false;
}

static const struct drm_plane_funcs meson_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= drm_plane_cleanup,
	.reset			= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
	.format_mod_supported   = meson_plane_format_mod_supported,
};

static const uint32_t supported_drm_formats[] = {
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_RGB565,
};

static const uint64_t format_modifiers_afbc_gxm[] = {
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_SPARSE |
				AFBC_FORMAT_MOD_YTR),
	/* SPLIT mandates SPARSE, RGB modes mandates YTR */
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_YTR |
				AFBC_FORMAT_MOD_SPARSE |
				AFBC_FORMAT_MOD_SPLIT),
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID,
};

static const uint64_t format_modifiers_afbc_g12a[] = {
	/*
	 * - TOFIX Support AFBC modifiers for YUV formats (16x16 + TILED)
	 * - SPLIT is mandatory for performances reasons when in 16x16
	 *   block size
	 * - 32x8 block size + SPLIT is mandatory with 4K frame size
	 *   for performances reasons
	 */
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_SPARSE |
				AFBC_FORMAT_MOD_SPLIT),
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_YTR |
				AFBC_FORMAT_MOD_SPARSE |
				AFBC_FORMAT_MOD_SPLIT),
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_32x8 |
				AFBC_FORMAT_MOD_SPARSE),
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_32x8 |
				AFBC_FORMAT_MOD_YTR |
				AFBC_FORMAT_MOD_SPARSE),
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_32x8 |
				AFBC_FORMAT_MOD_SPARSE |
				AFBC_FORMAT_MOD_SPLIT),
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_32x8 |
				AFBC_FORMAT_MOD_YTR |
				AFBC_FORMAT_MOD_SPARSE |
				AFBC_FORMAT_MOD_SPLIT),
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID,
};

static const uint64_t format_modifiers_default[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID,
};

int meson_plane_create(struct meson_drm *priv)
{
	struct meson_plane *meson_plane;
	struct drm_plane *plane;
	const uint64_t *format_modifiers = format_modifiers_default;

	meson_plane = devm_kzalloc(priv->drm->dev, sizeof(*meson_plane),
				   GFP_KERNEL);
	if (!meson_plane)
		return -ENOMEM;

	meson_plane->priv = priv;
	plane = &meson_plane->base;

	if (meson_vpu_is_compatible(priv, VPU_COMPATIBLE_GXM))
		format_modifiers = format_modifiers_afbc_gxm;
	else if (meson_vpu_is_compatible(priv, VPU_COMPATIBLE_G12A))
		format_modifiers = format_modifiers_afbc_g12a;

	drm_universal_plane_init(priv->drm, plane, 0xFF,
				 &meson_plane_funcs,
				 supported_drm_formats,
				 ARRAY_SIZE(supported_drm_formats),
				 format_modifiers,
				 DRM_PLANE_TYPE_PRIMARY, "meson_primary_plane");

	drm_plane_helper_add(plane, &meson_plane_helper_funcs);

	/* For now, OSD Primary plane is always on the front */
	drm_plane_create_zpos_immutable_property(plane, 1);

	priv->primary_plane = plane;

	return 0;
}
