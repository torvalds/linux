/*
 * Copyright 2015 Freescale Semiconductor, Inc.
 *
 * Freescale DCU drm device driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/regmap.h>

#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>

#include "fsl_dcu_drm_crtc.h"
#include "fsl_dcu_drm_drv.h"
#include "fsl_dcu_drm_plane.h"

static void fsl_dcu_drm_crtc_atomic_flush(struct drm_crtc *crtc,
					  struct drm_crtc_state *old_crtc_state)
{
	struct drm_device *dev = crtc->dev;
	struct fsl_dcu_drm_device *fsl_dev = dev->dev_private;
	struct drm_pending_vblank_event *event = crtc->state->event;

	regmap_write(fsl_dev->regmap,
		     DCU_UPDATE_MODE, DCU_UPDATE_MODE_READREG);

	if (event) {
		crtc->state->event = NULL;

		spin_lock_irq(&crtc->dev->event_lock);
		if (drm_crtc_vblank_get(crtc) == 0)
			drm_crtc_arm_vblank_event(crtc, event);
		else
			drm_crtc_send_vblank_event(crtc, event);
		spin_unlock_irq(&crtc->dev->event_lock);
	}
}

static void fsl_dcu_drm_crtc_atomic_disable(struct drm_crtc *crtc,
					struct drm_crtc_state *old_crtc_state)
{
	struct drm_device *dev = crtc->dev;
	struct fsl_dcu_drm_device *fsl_dev = dev->dev_private;

	/* always disable planes on the CRTC */
	drm_atomic_helper_disable_planes_on_crtc(old_crtc_state, true);

	drm_crtc_vblank_off(crtc);

	regmap_update_bits(fsl_dev->regmap, DCU_DCU_MODE,
			   DCU_MODE_DCU_MODE_MASK,
			   DCU_MODE_DCU_MODE(DCU_MODE_OFF));
	regmap_write(fsl_dev->regmap, DCU_UPDATE_MODE,
		     DCU_UPDATE_MODE_READREG);
	clk_disable_unprepare(fsl_dev->pix_clk);
}

static void fsl_dcu_drm_crtc_enable(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct fsl_dcu_drm_device *fsl_dev = dev->dev_private;

	clk_prepare_enable(fsl_dev->pix_clk);
	regmap_update_bits(fsl_dev->regmap, DCU_DCU_MODE,
			   DCU_MODE_DCU_MODE_MASK,
			   DCU_MODE_DCU_MODE(DCU_MODE_NORMAL));
	regmap_write(fsl_dev->regmap, DCU_UPDATE_MODE,
		     DCU_UPDATE_MODE_READREG);

	drm_crtc_vblank_on(crtc);
}

static void fsl_dcu_drm_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct fsl_dcu_drm_device *fsl_dev = dev->dev_private;
	struct drm_connector *con = &fsl_dev->connector.base;
	struct drm_display_mode *mode = &crtc->state->mode;
	unsigned int hbp, hfp, hsw, vbp, vfp, vsw, index, pol = 0;

	index = drm_crtc_index(crtc);
	clk_set_rate(fsl_dev->pix_clk, mode->clock * 1000);

	/* Configure timings: */
	hbp = mode->htotal - mode->hsync_end;
	hfp = mode->hsync_start - mode->hdisplay;
	hsw = mode->hsync_end - mode->hsync_start;
	vbp = mode->vtotal - mode->vsync_end;
	vfp = mode->vsync_start - mode->vdisplay;
	vsw = mode->vsync_end - mode->vsync_start;

	/* INV_PXCK as default (most display sample data on rising edge) */
	if (!(con->display_info.bus_flags & DRM_BUS_FLAG_PIXDATA_POSEDGE))
		pol |= DCU_SYN_POL_INV_PXCK;

	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		pol |= DCU_SYN_POL_INV_HS_LOW;

	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		pol |= DCU_SYN_POL_INV_VS_LOW;

	regmap_write(fsl_dev->regmap, DCU_HSYN_PARA,
		     DCU_HSYN_PARA_BP(hbp) |
		     DCU_HSYN_PARA_PW(hsw) |
		     DCU_HSYN_PARA_FP(hfp));
	regmap_write(fsl_dev->regmap, DCU_VSYN_PARA,
		     DCU_VSYN_PARA_BP(vbp) |
		     DCU_VSYN_PARA_PW(vsw) |
		     DCU_VSYN_PARA_FP(vfp));
	regmap_write(fsl_dev->regmap, DCU_DISP_SIZE,
		     DCU_DISP_SIZE_DELTA_Y(mode->vdisplay) |
		     DCU_DISP_SIZE_DELTA_X(mode->hdisplay));
	regmap_write(fsl_dev->regmap, DCU_SYN_POL, pol);
	regmap_write(fsl_dev->regmap, DCU_BGND, DCU_BGND_R(0) |
		     DCU_BGND_G(0) | DCU_BGND_B(0));
	regmap_write(fsl_dev->regmap, DCU_DCU_MODE,
		     DCU_MODE_BLEND_ITER(1) | DCU_MODE_RASTER_EN);
	regmap_write(fsl_dev->regmap, DCU_THRESHOLD,
		     DCU_THRESHOLD_LS_BF_VS(BF_VS_VAL) |
		     DCU_THRESHOLD_OUT_BUF_HIGH(BUF_MAX_VAL) |
		     DCU_THRESHOLD_OUT_BUF_LOW(BUF_MIN_VAL));
	return;
}

static const struct drm_crtc_helper_funcs fsl_dcu_drm_crtc_helper_funcs = {
	.atomic_disable = fsl_dcu_drm_crtc_atomic_disable,
	.atomic_flush = fsl_dcu_drm_crtc_atomic_flush,
	.enable = fsl_dcu_drm_crtc_enable,
	.mode_set_nofb = fsl_dcu_drm_crtc_mode_set_nofb,
};

static int fsl_dcu_drm_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct fsl_dcu_drm_device *fsl_dev = dev->dev_private;
	unsigned int value;

	regmap_read(fsl_dev->regmap, DCU_INT_MASK, &value);
	value &= ~DCU_INT_MASK_VBLANK;
	regmap_write(fsl_dev->regmap, DCU_INT_MASK, value);

	return 0;
}

static void fsl_dcu_drm_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct fsl_dcu_drm_device *fsl_dev = dev->dev_private;
	unsigned int value;

	regmap_read(fsl_dev->regmap, DCU_INT_MASK, &value);
	value |= DCU_INT_MASK_VBLANK;
	regmap_write(fsl_dev->regmap, DCU_INT_MASK, value);
}

static const struct drm_crtc_funcs fsl_dcu_drm_crtc_funcs = {
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.destroy = drm_crtc_cleanup,
	.page_flip = drm_atomic_helper_page_flip,
	.reset = drm_atomic_helper_crtc_reset,
	.set_config = drm_atomic_helper_set_config,
	.enable_vblank = fsl_dcu_drm_crtc_enable_vblank,
	.disable_vblank = fsl_dcu_drm_crtc_disable_vblank,
};

int fsl_dcu_drm_crtc_create(struct fsl_dcu_drm_device *fsl_dev)
{
	struct drm_plane *primary;
	struct drm_crtc *crtc = &fsl_dev->crtc;
	int ret;

	fsl_dcu_drm_init_planes(fsl_dev->drm);

	primary = fsl_dcu_drm_primary_create_plane(fsl_dev->drm);
	if (!primary)
		return -ENOMEM;

	ret = drm_crtc_init_with_planes(fsl_dev->drm, crtc, primary, NULL,
					&fsl_dcu_drm_crtc_funcs, NULL);
	if (ret) {
		primary->funcs->destroy(primary);
		return ret;
	}

	drm_crtc_helper_add(crtc, &fsl_dcu_drm_crtc_helper_funcs);

	return 0;
}
