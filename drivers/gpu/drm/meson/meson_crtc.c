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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/bitfield.h>
#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_flip_work.h>
#include <drm/drm_probe_helper.h>

#include "meson_crtc.h"
#include "meson_plane.h"
#include "meson_venc.h"
#include "meson_vpp.h"
#include "meson_viu.h"
#include "meson_registers.h"

#define MESON_G12A_VIU_OFFSET	0x5ec0

/* CRTC definition */

struct meson_crtc {
	struct drm_crtc base;
	struct drm_pending_vblank_event *event;
	struct meson_drm *priv;
	void (*enable_osd1)(struct meson_drm *priv);
	void (*enable_vd1)(struct meson_drm *priv);
	unsigned int viu_offset;
};
#define to_meson_crtc(x) container_of(x, struct meson_crtc, base)

/* CRTC */

static int meson_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct meson_crtc *meson_crtc = to_meson_crtc(crtc);
	struct meson_drm *priv = meson_crtc->priv;

	meson_venc_enable_vsync(priv);

	return 0;
}

static void meson_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct meson_crtc *meson_crtc = to_meson_crtc(crtc);
	struct meson_drm *priv = meson_crtc->priv;

	meson_venc_disable_vsync(priv);
}

static const struct drm_crtc_funcs meson_crtc_funcs = {
	.atomic_destroy_state	= drm_atomic_helper_crtc_destroy_state,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.destroy		= drm_crtc_cleanup,
	.page_flip		= drm_atomic_helper_page_flip,
	.reset			= drm_atomic_helper_crtc_reset,
	.set_config             = drm_atomic_helper_set_config,
	.enable_vblank		= meson_crtc_enable_vblank,
	.disable_vblank		= meson_crtc_disable_vblank,

};

static void meson_g12a_crtc_atomic_enable(struct drm_crtc *crtc,
					  struct drm_crtc_state *old_state)
{
	struct meson_crtc *meson_crtc = to_meson_crtc(crtc);
	struct drm_crtc_state *crtc_state = crtc->state;
	struct meson_drm *priv = meson_crtc->priv;

	DRM_DEBUG_DRIVER("\n");

	if (!crtc_state) {
		DRM_ERROR("Invalid crtc_state\n");
		return;
	}

	/* VD1 Preblend vertical start/end */
	writel(FIELD_PREP(GENMASK(11, 0), 2303),
	       priv->io_base + _REG(VPP_PREBLEND_VD1_V_START_END));

	/* Setup Blender */
	writel(crtc_state->mode.hdisplay |
	       crtc_state->mode.vdisplay << 16,
	       priv->io_base + _REG(VPP_POSTBLEND_H_SIZE));

	writel_relaxed(0 << 16 |
			(crtc_state->mode.hdisplay - 1),
			priv->io_base + _REG(VPP_OSD1_BLD_H_SCOPE));
	writel_relaxed(0 << 16 |
			(crtc_state->mode.vdisplay - 1),
			priv->io_base + _REG(VPP_OSD1_BLD_V_SCOPE));
	writel_relaxed(crtc_state->mode.hdisplay << 16 |
			crtc_state->mode.vdisplay,
			priv->io_base + _REG(VPP_OUT_H_V_SIZE));

	drm_crtc_vblank_on(crtc);

	priv->viu.osd1_enabled = true;
}

static void meson_crtc_atomic_enable(struct drm_crtc *crtc,
				     struct drm_crtc_state *old_state)
{
	struct meson_crtc *meson_crtc = to_meson_crtc(crtc);
	struct drm_crtc_state *crtc_state = crtc->state;
	struct meson_drm *priv = meson_crtc->priv;

	DRM_DEBUG_DRIVER("\n");

	if (!crtc_state) {
		DRM_ERROR("Invalid crtc_state\n");
		return;
	}

	/* Enable VPP Postblend */
	writel(crtc_state->mode.hdisplay,
	       priv->io_base + _REG(VPP_POSTBLEND_H_SIZE));

	/* VD1 Preblend vertical start/end */
	writel(FIELD_PREP(GENMASK(11, 0), 2303),
			priv->io_base + _REG(VPP_PREBLEND_VD1_V_START_END));

	writel_bits_relaxed(VPP_POSTBLEND_ENABLE, VPP_POSTBLEND_ENABLE,
			    priv->io_base + _REG(VPP_MISC));

	drm_crtc_vblank_on(crtc);

	priv->viu.osd1_enabled = true;
}

static void meson_g12a_crtc_atomic_disable(struct drm_crtc *crtc,
					   struct drm_crtc_state *old_state)
{
	struct meson_crtc *meson_crtc = to_meson_crtc(crtc);
	struct meson_drm *priv = meson_crtc->priv;

	DRM_DEBUG_DRIVER("\n");

	drm_crtc_vblank_off(crtc);

	priv->viu.osd1_enabled = false;
	priv->viu.osd1_commit = false;

	priv->viu.vd1_enabled = false;
	priv->viu.vd1_commit = false;

	if (crtc->state->event && !crtc->state->active) {
		spin_lock_irq(&crtc->dev->event_lock);
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		spin_unlock_irq(&crtc->dev->event_lock);

		crtc->state->event = NULL;
	}
}

static void meson_crtc_atomic_disable(struct drm_crtc *crtc,
				      struct drm_crtc_state *old_state)
{
	struct meson_crtc *meson_crtc = to_meson_crtc(crtc);
	struct meson_drm *priv = meson_crtc->priv;

	DRM_DEBUG_DRIVER("\n");

	drm_crtc_vblank_off(crtc);

	priv->viu.osd1_enabled = false;
	priv->viu.osd1_commit = false;

	priv->viu.vd1_enabled = false;
	priv->viu.vd1_commit = false;

	/* Disable VPP Postblend */
	writel_bits_relaxed(VPP_OSD1_POSTBLEND | VPP_VD1_POSTBLEND |
			    VPP_VD1_PREBLEND | VPP_POSTBLEND_ENABLE, 0,
			    priv->io_base + _REG(VPP_MISC));

	if (crtc->state->event && !crtc->state->active) {
		spin_lock_irq(&crtc->dev->event_lock);
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		spin_unlock_irq(&crtc->dev->event_lock);

		crtc->state->event = NULL;
	}
}

static void meson_crtc_atomic_begin(struct drm_crtc *crtc,
				    struct drm_crtc_state *state)
{
	struct meson_crtc *meson_crtc = to_meson_crtc(crtc);
	unsigned long flags;

	if (crtc->state->event) {
		WARN_ON(drm_crtc_vblank_get(crtc) != 0);

		spin_lock_irqsave(&crtc->dev->event_lock, flags);
		meson_crtc->event = crtc->state->event;
		spin_unlock_irqrestore(&crtc->dev->event_lock, flags);
		crtc->state->event = NULL;
	}
}

static void meson_crtc_atomic_flush(struct drm_crtc *crtc,
				    struct drm_crtc_state *old_crtc_state)
{
	struct meson_crtc *meson_crtc = to_meson_crtc(crtc);
	struct meson_drm *priv = meson_crtc->priv;

	priv->viu.osd1_commit = true;
	priv->viu.vd1_commit = true;
}

static const struct drm_crtc_helper_funcs meson_crtc_helper_funcs = {
	.atomic_begin	= meson_crtc_atomic_begin,
	.atomic_flush	= meson_crtc_atomic_flush,
	.atomic_enable	= meson_crtc_atomic_enable,
	.atomic_disable	= meson_crtc_atomic_disable,
};

static const struct drm_crtc_helper_funcs meson_g12a_crtc_helper_funcs = {
	.atomic_begin	= meson_crtc_atomic_begin,
	.atomic_flush	= meson_crtc_atomic_flush,
	.atomic_enable	= meson_g12a_crtc_atomic_enable,
	.atomic_disable	= meson_g12a_crtc_atomic_disable,
};

static void meson_crtc_enable_osd1(struct meson_drm *priv)
{
	writel_bits_relaxed(VPP_OSD1_POSTBLEND, VPP_OSD1_POSTBLEND,
			    priv->io_base + _REG(VPP_MISC));
}

static void meson_g12a_crtc_enable_osd1(struct meson_drm *priv)
{
	writel_relaxed(priv->viu.osd_blend_din0_scope_h,
		       priv->io_base +
		       _REG(VIU_OSD_BLEND_DIN0_SCOPE_H));
	writel_relaxed(priv->viu.osd_blend_din0_scope_v,
		       priv->io_base +
		       _REG(VIU_OSD_BLEND_DIN0_SCOPE_V));
	writel_relaxed(priv->viu.osb_blend0_size,
		       priv->io_base +
		       _REG(VIU_OSD_BLEND_BLEND0_SIZE));
	writel_relaxed(priv->viu.osb_blend1_size,
		       priv->io_base +
		       _REG(VIU_OSD_BLEND_BLEND1_SIZE));
}

static void meson_crtc_enable_vd1(struct meson_drm *priv)
{
	writel_bits_relaxed(VPP_VD1_PREBLEND | VPP_VD1_POSTBLEND |
			    VPP_COLOR_MNG_ENABLE,
			    VPP_VD1_PREBLEND | VPP_VD1_POSTBLEND |
			    VPP_COLOR_MNG_ENABLE,
			    priv->io_base + _REG(VPP_MISC));
}

static void meson_g12a_crtc_enable_vd1(struct meson_drm *priv)
{
	writel_relaxed(((1 << 16) | /* post bld premult*/
			(1 << 8) | /* post src */
			(1 << 4) | /* pre bld premult*/
			(1 << 0)),
			priv->io_base + _REG(VD1_BLEND_SRC_CTRL));
}

void meson_crtc_irq(struct meson_drm *priv)
{
	struct meson_crtc *meson_crtc = to_meson_crtc(priv->crtc);
	unsigned long flags;

	/* Update the OSD registers */
	if (priv->viu.osd1_enabled && priv->viu.osd1_commit) {
		writel_relaxed(priv->viu.osd1_ctrl_stat,
				priv->io_base + _REG(VIU_OSD1_CTRL_STAT));
		writel_relaxed(priv->viu.osd1_blk0_cfg[0],
				priv->io_base + _REG(VIU_OSD1_BLK0_CFG_W0));
		writel_relaxed(priv->viu.osd1_blk0_cfg[1],
				priv->io_base + _REG(VIU_OSD1_BLK0_CFG_W1));
		writel_relaxed(priv->viu.osd1_blk0_cfg[2],
				priv->io_base + _REG(VIU_OSD1_BLK0_CFG_W2));
		writel_relaxed(priv->viu.osd1_blk0_cfg[3],
				priv->io_base + _REG(VIU_OSD1_BLK0_CFG_W3));
		writel_relaxed(priv->viu.osd1_blk0_cfg[4],
				priv->io_base + _REG(VIU_OSD1_BLK0_CFG_W4));
		writel_relaxed(priv->viu.osd_sc_ctrl0,
				priv->io_base + _REG(VPP_OSD_SC_CTRL0));
		writel_relaxed(priv->viu.osd_sc_i_wh_m1,
				priv->io_base + _REG(VPP_OSD_SCI_WH_M1));
		writel_relaxed(priv->viu.osd_sc_o_h_start_end,
				priv->io_base + _REG(VPP_OSD_SCO_H_START_END));
		writel_relaxed(priv->viu.osd_sc_o_v_start_end,
				priv->io_base + _REG(VPP_OSD_SCO_V_START_END));
		writel_relaxed(priv->viu.osd_sc_v_ini_phase,
				priv->io_base + _REG(VPP_OSD_VSC_INI_PHASE));
		writel_relaxed(priv->viu.osd_sc_v_phase_step,
				priv->io_base + _REG(VPP_OSD_VSC_PHASE_STEP));
		writel_relaxed(priv->viu.osd_sc_h_ini_phase,
				priv->io_base + _REG(VPP_OSD_HSC_INI_PHASE));
		writel_relaxed(priv->viu.osd_sc_h_phase_step,
				priv->io_base + _REG(VPP_OSD_HSC_PHASE_STEP));
		writel_relaxed(priv->viu.osd_sc_h_ctrl0,
				priv->io_base + _REG(VPP_OSD_HSC_CTRL0));
		writel_relaxed(priv->viu.osd_sc_v_ctrl0,
				priv->io_base + _REG(VPP_OSD_VSC_CTRL0));

		meson_canvas_config(priv->canvas, priv->canvas_id_osd1,
				priv->viu.osd1_addr, priv->viu.osd1_stride,
				priv->viu.osd1_height, MESON_CANVAS_WRAP_NONE,
				MESON_CANVAS_BLKMODE_LINEAR, 0);

		/* Enable OSD1 */
		if (meson_crtc->enable_osd1)
			meson_crtc->enable_osd1(priv);

		priv->viu.osd1_commit = false;
	}

	/* Update the VD1 registers */
	if (priv->viu.vd1_enabled && priv->viu.vd1_commit) {

		switch (priv->viu.vd1_planes) {
		case 3:
			meson_canvas_config(priv->canvas,
					    priv->canvas_id_vd1_2,
					    priv->viu.vd1_addr2,
					    priv->viu.vd1_stride2,
					    priv->viu.vd1_height2,
					    MESON_CANVAS_WRAP_NONE,
					    MESON_CANVAS_BLKMODE_LINEAR,
					    MESON_CANVAS_ENDIAN_SWAP64);
		/* fallthrough */
		case 2:
			meson_canvas_config(priv->canvas,
					    priv->canvas_id_vd1_1,
					    priv->viu.vd1_addr1,
					    priv->viu.vd1_stride1,
					    priv->viu.vd1_height1,
					    MESON_CANVAS_WRAP_NONE,
					    MESON_CANVAS_BLKMODE_LINEAR,
					    MESON_CANVAS_ENDIAN_SWAP64);
		/* fallthrough */
		case 1:
			meson_canvas_config(priv->canvas,
					    priv->canvas_id_vd1_0,
					    priv->viu.vd1_addr0,
					    priv->viu.vd1_stride0,
					    priv->viu.vd1_height0,
					    MESON_CANVAS_WRAP_NONE,
					    MESON_CANVAS_BLKMODE_LINEAR,
					    MESON_CANVAS_ENDIAN_SWAP64);
		};

		writel_relaxed(priv->viu.vd1_if0_gen_reg,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD1_IF0_GEN_REG));
		writel_relaxed(priv->viu.vd1_if0_gen_reg,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD2_IF0_GEN_REG));
		writel_relaxed(priv->viu.vd1_if0_gen_reg2,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD1_IF0_GEN_REG2));
		writel_relaxed(priv->viu.viu_vd1_fmt_ctrl,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VIU_VD1_FMT_CTRL));
		writel_relaxed(priv->viu.viu_vd1_fmt_ctrl,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VIU_VD2_FMT_CTRL));
		writel_relaxed(priv->viu.viu_vd1_fmt_w,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VIU_VD1_FMT_W));
		writel_relaxed(priv->viu.viu_vd1_fmt_w,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VIU_VD2_FMT_W));
		writel_relaxed(priv->viu.vd1_if0_canvas0,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD1_IF0_CANVAS0));
		writel_relaxed(priv->viu.vd1_if0_canvas0,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD1_IF0_CANVAS1));
		writel_relaxed(priv->viu.vd1_if0_canvas0,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD2_IF0_CANVAS0));
		writel_relaxed(priv->viu.vd1_if0_canvas0,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD2_IF0_CANVAS1));
		writel_relaxed(priv->viu.vd1_if0_luma_x0,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD1_IF0_LUMA_X0));
		writel_relaxed(priv->viu.vd1_if0_luma_x0,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD1_IF0_LUMA_X1));
		writel_relaxed(priv->viu.vd1_if0_luma_x0,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD2_IF0_LUMA_X0));
		writel_relaxed(priv->viu.vd1_if0_luma_x0,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD2_IF0_LUMA_X1));
		writel_relaxed(priv->viu.vd1_if0_luma_y0,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD1_IF0_LUMA_Y0));
		writel_relaxed(priv->viu.vd1_if0_luma_y0,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD1_IF0_LUMA_Y1));
		writel_relaxed(priv->viu.vd1_if0_luma_y0,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD2_IF0_LUMA_Y0));
		writel_relaxed(priv->viu.vd1_if0_luma_y0,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD2_IF0_LUMA_Y1));
		writel_relaxed(priv->viu.vd1_if0_chroma_x0,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD1_IF0_CHROMA_X0));
		writel_relaxed(priv->viu.vd1_if0_chroma_x0,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD1_IF0_CHROMA_X1));
		writel_relaxed(priv->viu.vd1_if0_chroma_x0,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD2_IF0_CHROMA_X0));
		writel_relaxed(priv->viu.vd1_if0_chroma_x0,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD2_IF0_CHROMA_X1));
		writel_relaxed(priv->viu.vd1_if0_chroma_y0,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD1_IF0_CHROMA_Y0));
		writel_relaxed(priv->viu.vd1_if0_chroma_y0,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD1_IF0_CHROMA_Y1));
		writel_relaxed(priv->viu.vd1_if0_chroma_y0,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD2_IF0_CHROMA_Y0));
		writel_relaxed(priv->viu.vd1_if0_chroma_y0,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD2_IF0_CHROMA_Y1));
		writel_relaxed(priv->viu.vd1_if0_repeat_loop,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD1_IF0_RPT_LOOP));
		writel_relaxed(priv->viu.vd1_if0_repeat_loop,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD2_IF0_RPT_LOOP));
		writel_relaxed(priv->viu.vd1_if0_luma0_rpt_pat,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD1_IF0_LUMA0_RPT_PAT));
		writel_relaxed(priv->viu.vd1_if0_luma0_rpt_pat,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD2_IF0_LUMA0_RPT_PAT));
		writel_relaxed(priv->viu.vd1_if0_luma0_rpt_pat,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD1_IF0_LUMA1_RPT_PAT));
		writel_relaxed(priv->viu.vd1_if0_luma0_rpt_pat,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD2_IF0_LUMA1_RPT_PAT));
		writel_relaxed(priv->viu.vd1_if0_chroma0_rpt_pat,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD1_IF0_CHROMA0_RPT_PAT));
		writel_relaxed(priv->viu.vd1_if0_chroma0_rpt_pat,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD2_IF0_CHROMA0_RPT_PAT));
		writel_relaxed(priv->viu.vd1_if0_chroma0_rpt_pat,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD1_IF0_CHROMA1_RPT_PAT));
		writel_relaxed(priv->viu.vd1_if0_chroma0_rpt_pat,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD2_IF0_CHROMA1_RPT_PAT));
		writel_relaxed(0, priv->io_base + meson_crtc->viu_offset +
				_REG(VD1_IF0_LUMA_PSEL));
		writel_relaxed(0, priv->io_base + meson_crtc->viu_offset +
				_REG(VD1_IF0_CHROMA_PSEL));
		writel_relaxed(0, priv->io_base + meson_crtc->viu_offset +
				_REG(VD2_IF0_LUMA_PSEL));
		writel_relaxed(0, priv->io_base + meson_crtc->viu_offset +
				_REG(VD2_IF0_CHROMA_PSEL));
		writel_relaxed(priv->viu.vd1_range_map_y,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD1_IF0_RANGE_MAP_Y));
		writel_relaxed(priv->viu.vd1_range_map_cb,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD1_IF0_RANGE_MAP_CB));
		writel_relaxed(priv->viu.vd1_range_map_cr,
				priv->io_base + meson_crtc->viu_offset +
				_REG(VD1_IF0_RANGE_MAP_CR));
		writel_relaxed(0x78404,
				priv->io_base + _REG(VPP_SC_MISC));
		writel_relaxed(priv->viu.vpp_pic_in_height,
				priv->io_base + _REG(VPP_PIC_IN_HEIGHT));
		writel_relaxed(priv->viu.vpp_postblend_vd1_h_start_end,
			priv->io_base + _REG(VPP_POSTBLEND_VD1_H_START_END));
		writel_relaxed(priv->viu.vpp_blend_vd2_h_start_end,
			priv->io_base + _REG(VPP_BLEND_VD2_H_START_END));
		writel_relaxed(priv->viu.vpp_postblend_vd1_v_start_end,
			priv->io_base + _REG(VPP_POSTBLEND_VD1_V_START_END));
		writel_relaxed(priv->viu.vpp_blend_vd2_v_start_end,
			priv->io_base + _REG(VPP_BLEND_VD2_V_START_END));
		writel_relaxed(priv->viu.vpp_hsc_region12_startp,
				priv->io_base + _REG(VPP_HSC_REGION12_STARTP));
		writel_relaxed(priv->viu.vpp_hsc_region34_startp,
				priv->io_base + _REG(VPP_HSC_REGION34_STARTP));
		writel_relaxed(priv->viu.vpp_hsc_region4_endp,
				priv->io_base + _REG(VPP_HSC_REGION4_ENDP));
		writel_relaxed(priv->viu.vpp_hsc_start_phase_step,
				priv->io_base + _REG(VPP_HSC_START_PHASE_STEP));
		writel_relaxed(priv->viu.vpp_hsc_region1_phase_slope,
			priv->io_base + _REG(VPP_HSC_REGION1_PHASE_SLOPE));
		writel_relaxed(priv->viu.vpp_hsc_region3_phase_slope,
			priv->io_base + _REG(VPP_HSC_REGION3_PHASE_SLOPE));
		writel_relaxed(priv->viu.vpp_line_in_length,
				priv->io_base + _REG(VPP_LINE_IN_LENGTH));
		writel_relaxed(priv->viu.vpp_preblend_h_size,
				priv->io_base + _REG(VPP_PREBLEND_H_SIZE));
		writel_relaxed(priv->viu.vpp_vsc_region12_startp,
				priv->io_base + _REG(VPP_VSC_REGION12_STARTP));
		writel_relaxed(priv->viu.vpp_vsc_region34_startp,
				priv->io_base + _REG(VPP_VSC_REGION34_STARTP));
		writel_relaxed(priv->viu.vpp_vsc_region4_endp,
				priv->io_base + _REG(VPP_VSC_REGION4_ENDP));
		writel_relaxed(priv->viu.vpp_vsc_start_phase_step,
				priv->io_base + _REG(VPP_VSC_START_PHASE_STEP));
		writel_relaxed(priv->viu.vpp_vsc_ini_phase,
				priv->io_base + _REG(VPP_VSC_INI_PHASE));
		writel_relaxed(priv->viu.vpp_vsc_phase_ctrl,
				priv->io_base + _REG(VPP_VSC_PHASE_CTRL));
		writel_relaxed(priv->viu.vpp_hsc_phase_ctrl,
				priv->io_base + _REG(VPP_HSC_PHASE_CTRL));
		writel_relaxed(0x42, priv->io_base + _REG(VPP_SCALE_COEF_IDX));

		/* Enable VD1 */
		if (meson_crtc->enable_vd1)
			meson_crtc->enable_vd1(priv);

		priv->viu.vd1_commit = false;
	}

	drm_crtc_handle_vblank(priv->crtc);

	spin_lock_irqsave(&priv->drm->event_lock, flags);
	if (meson_crtc->event) {
		drm_crtc_send_vblank_event(priv->crtc, meson_crtc->event);
		drm_crtc_vblank_put(priv->crtc);
		meson_crtc->event = NULL;
	}
	spin_unlock_irqrestore(&priv->drm->event_lock, flags);
}

int meson_crtc_create(struct meson_drm *priv)
{
	struct meson_crtc *meson_crtc;
	struct drm_crtc *crtc;
	int ret;

	meson_crtc = devm_kzalloc(priv->drm->dev, sizeof(*meson_crtc),
				  GFP_KERNEL);
	if (!meson_crtc)
		return -ENOMEM;

	meson_crtc->priv = priv;
	crtc = &meson_crtc->base;
	ret = drm_crtc_init_with_planes(priv->drm, crtc,
					priv->primary_plane, NULL,
					&meson_crtc_funcs, "meson_crtc");
	if (ret) {
		dev_err(priv->drm->dev, "Failed to init CRTC\n");
		return ret;
	}

	if (meson_vpu_is_compatible(priv, "amlogic,meson-g12a-vpu")) {
		meson_crtc->enable_osd1 = meson_g12a_crtc_enable_osd1;
		meson_crtc->enable_vd1 = meson_g12a_crtc_enable_vd1;
		meson_crtc->viu_offset = MESON_G12A_VIU_OFFSET;
		drm_crtc_helper_add(crtc, &meson_g12a_crtc_helper_funcs);
	} else {
		meson_crtc->enable_osd1 = meson_crtc_enable_osd1;
		meson_crtc->enable_vd1 = meson_crtc_enable_vd1;
		drm_crtc_helper_add(crtc, &meson_crtc_helper_funcs);
	}

	priv->crtc = crtc;

	return 0;
}
