// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Icenowy Zheng <uwu@icenowy.me>
 */

#include <linux/clk.h>
#include <linux/regmap.h>
#include <linux/units.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_managed.h>
#include <drm/drm_vblank_helper.h>

#include "vs_crtc_regs.h"
#include "vs_crtc.h"
#include "vs_dc.h"
#include "vs_dc_top_regs.h"
#include "vs_drm.h"
#include "vs_plane.h"

static void vs_crtc_atomic_disable(struct drm_crtc *crtc,
				   struct drm_atomic_state *state)
{
	struct vs_crtc *vcrtc = drm_crtc_to_vs_crtc(crtc);
	struct vs_dc *dc = vcrtc->dc;
	unsigned int output = vcrtc->id;

	drm_crtc_vblank_off(crtc);

	clk_disable_unprepare(dc->pix_clk[output]);
}

static void vs_crtc_atomic_enable(struct drm_crtc *crtc,
				  struct drm_atomic_state *state)
{
	struct vs_crtc *vcrtc = drm_crtc_to_vs_crtc(crtc);
	struct vs_dc *dc = vcrtc->dc;
	unsigned int output = vcrtc->id;

	drm_WARN_ON(&dc->drm_dev->base,
		    clk_prepare_enable(dc->pix_clk[output]));

	drm_crtc_vblank_on(crtc);
}

static void vs_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	struct vs_crtc *vcrtc = drm_crtc_to_vs_crtc(crtc);
	struct vs_dc *dc = vcrtc->dc;
	unsigned int output = vcrtc->id;

	regmap_write(dc->regs, VSDC_DISP_HSIZE(output),
		     VSDC_DISP_HSIZE_DISP(mode->hdisplay) |
		     VSDC_DISP_HSIZE_TOTAL(mode->htotal));
	regmap_write(dc->regs, VSDC_DISP_VSIZE(output),
		     VSDC_DISP_VSIZE_DISP(mode->vdisplay) |
		     VSDC_DISP_VSIZE_TOTAL(mode->vtotal));
	regmap_write(dc->regs, VSDC_DISP_HSYNC(output),
		     VSDC_DISP_HSYNC_START(mode->hsync_start) |
		     VSDC_DISP_HSYNC_END(mode->hsync_end) |
		     VSDC_DISP_HSYNC_EN);
	if (!(mode->flags & DRM_MODE_FLAG_PHSYNC))
		regmap_set_bits(dc->regs, VSDC_DISP_HSYNC(output),
				VSDC_DISP_HSYNC_POL);
	regmap_write(dc->regs, VSDC_DISP_VSYNC(output),
		     VSDC_DISP_VSYNC_START(mode->vsync_start) |
		     VSDC_DISP_VSYNC_END(mode->vsync_end) |
		     VSDC_DISP_VSYNC_EN);
	if (!(mode->flags & DRM_MODE_FLAG_PVSYNC))
		regmap_set_bits(dc->regs, VSDC_DISP_VSYNC(output),
				VSDC_DISP_VSYNC_POL);

	WARN_ON(clk_set_rate(dc->pix_clk[output], mode->crtc_clock * 1000));
}

static enum drm_mode_status
vs_crtc_mode_valid(struct drm_crtc *crtc, const struct drm_display_mode *mode)
{
	struct vs_crtc *vcrtc = drm_crtc_to_vs_crtc(crtc);
	struct vs_dc *dc = vcrtc->dc;
	unsigned int output = vcrtc->id;
	long rate;

	if (mode->htotal > VSDC_DISP_TIMING_VALUE_MAX)
		return MODE_BAD_HVALUE;
	if (mode->vtotal > VSDC_DISP_TIMING_VALUE_MAX)
		return MODE_BAD_VVALUE;

	rate = clk_round_rate(dc->pix_clk[output], mode->clock * HZ_PER_KHZ);
	if (rate <= 0)
		return MODE_CLOCK_RANGE;

	return MODE_OK;
}

static bool vs_crtc_mode_fixup(struct drm_crtc *crtc,
			       const struct drm_display_mode *m,
			       struct drm_display_mode *adjusted_mode)
{
	struct vs_crtc *vcrtc = drm_crtc_to_vs_crtc(crtc);
	struct vs_dc *dc = vcrtc->dc;
	unsigned int output = vcrtc->id;
	long clk_rate;

	drm_mode_set_crtcinfo(adjusted_mode, 0);

	/* Feedback the pixel clock to crtc_clock */
	clk_rate = adjusted_mode->crtc_clock * HZ_PER_KHZ;
	clk_rate = clk_round_rate(dc->pix_clk[output], clk_rate);
	if (clk_rate <= 0)
		return false;

	adjusted_mode->crtc_clock = clk_rate / HZ_PER_KHZ;

	return true;
}

static const struct drm_crtc_helper_funcs vs_crtc_helper_funcs = {
	.atomic_flush	= drm_crtc_vblank_atomic_flush,
	.atomic_enable	= vs_crtc_atomic_enable,
	.atomic_disable	= vs_crtc_atomic_disable,
	.mode_set_nofb	= vs_crtc_mode_set_nofb,
	.mode_valid	= vs_crtc_mode_valid,
	.mode_fixup	= vs_crtc_mode_fixup,
};

static int vs_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct vs_crtc *vcrtc = drm_crtc_to_vs_crtc(crtc);
	struct vs_dc *dc = vcrtc->dc;

	regmap_set_bits(dc->regs, VSDC_TOP_IRQ_EN, VSDC_TOP_IRQ_VSYNC(vcrtc->id));

	return 0;
}

static void vs_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct vs_crtc *vcrtc = drm_crtc_to_vs_crtc(crtc);
	struct vs_dc *dc = vcrtc->dc;

	regmap_clear_bits(dc->regs, VSDC_TOP_IRQ_EN, VSDC_TOP_IRQ_VSYNC(vcrtc->id));
}

static const struct drm_crtc_funcs vs_crtc_funcs = {
	.atomic_destroy_state	= drm_atomic_helper_crtc_destroy_state,
	.atomic_duplicate_state	= drm_atomic_helper_crtc_duplicate_state,
	.page_flip		= drm_atomic_helper_page_flip,
	.reset			= drm_atomic_helper_crtc_reset,
	.set_config		= drm_atomic_helper_set_config,
	.enable_vblank		= vs_crtc_enable_vblank,
	.disable_vblank		= vs_crtc_disable_vblank,
};

struct vs_crtc *vs_crtc_init(struct drm_device *drm_dev, struct vs_dc *dc,
			     unsigned int output)
{
	struct vs_crtc *vcrtc;
	struct drm_plane *primary;
	int ret;

	vcrtc = drmm_kzalloc(drm_dev, sizeof(*vcrtc), GFP_KERNEL);
	if (!vcrtc)
		return ERR_PTR(-ENOMEM);
	vcrtc->dc = dc;
	vcrtc->id = output;

	/* Create our primary plane */
	primary = vs_primary_plane_init(drm_dev, dc);
	if (IS_ERR(primary)) {
		drm_err(drm_dev, "Couldn't create the primary plane\n");
		return ERR_PTR(PTR_ERR(primary));
	}

	ret = drmm_crtc_init_with_planes(drm_dev, &vcrtc->base,
					 primary,
					 NULL,
					 &vs_crtc_funcs,
					 NULL);
	if (ret) {
		drm_err(drm_dev, "Couldn't initialize CRTC\n");
		return ERR_PTR(ret);
	}

	drm_crtc_helper_add(&vcrtc->base, &vs_crtc_helper_funcs);

	return vcrtc;
}
