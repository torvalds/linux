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

static void fsl_dcu_drm_crtc_atomic_begin(struct drm_crtc *crtc,
					  struct drm_crtc_state *old_crtc_state)
{
}

static int fsl_dcu_drm_crtc_atomic_check(struct drm_crtc *crtc,
					 struct drm_crtc_state *state)
{
	return 0;
}

static void fsl_dcu_drm_crtc_atomic_flush(struct drm_crtc *crtc,
					  struct drm_crtc_state *old_crtc_state)
{
}

static void fsl_dcu_drm_disable_crtc(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct fsl_dcu_drm_device *fsl_dev = dev->dev_private;

	regmap_update_bits(fsl_dev->regmap, DCU_DCU_MODE,
			   DCU_MODE_DCU_MODE_MASK,
			   DCU_MODE_DCU_MODE(DCU_MODE_OFF));
	regmap_write(fsl_dev->regmap, DCU_UPDATE_MODE,
		     DCU_UPDATE_MODE_READREG);
}

static void fsl_dcu_drm_crtc_enable(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct fsl_dcu_drm_device *fsl_dev = dev->dev_private;

	regmap_update_bits(fsl_dev->regmap, DCU_DCU_MODE,
			   DCU_MODE_DCU_MODE_MASK,
			   DCU_MODE_DCU_MODE(DCU_MODE_NORMAL));
	regmap_write(fsl_dev->regmap, DCU_UPDATE_MODE,
		     DCU_UPDATE_MODE_READREG);
}

static bool fsl_dcu_drm_crtc_mode_fixup(struct drm_crtc *crtc,
					const struct drm_display_mode *mode,
					struct drm_display_mode *adjusted_mode)
{
	return true;
}

static void fsl_dcu_drm_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct fsl_dcu_drm_device *fsl_dev = dev->dev_private;
	struct drm_display_mode *mode = &crtc->state->mode;
	unsigned int hbp, hfp, hsw, vbp, vfp, vsw, div, index, pol = 0;
	unsigned long dcuclk;

	index = drm_crtc_index(crtc);
	dcuclk = clk_get_rate(fsl_dev->clk);
	div = dcuclk / mode->clock / 1000;

	/* Configure timings: */
	hbp = mode->htotal - mode->hsync_end;
	hfp = mode->hsync_start - mode->hdisplay;
	hsw = mode->hsync_end - mode->hsync_start;
	vbp = mode->vtotal - mode->vsync_end;
	vfp = mode->vsync_start - mode->vdisplay;
	vsw = mode->vsync_end - mode->vsync_start;

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
	regmap_write(fsl_dev->regmap, DCU_DIV_RATIO, div);
	regmap_write(fsl_dev->regmap, DCU_SYN_POL, pol);
	regmap_write(fsl_dev->regmap, DCU_BGND, DCU_BGND_R(0) |
		     DCU_BGND_G(0) | DCU_BGND_B(0));
	regmap_write(fsl_dev->regmap, DCU_DCU_MODE,
		     DCU_MODE_BLEND_ITER(1) | DCU_MODE_RASTER_EN);
	regmap_write(fsl_dev->regmap, DCU_THRESHOLD,
		     DCU_THRESHOLD_LS_BF_VS(BF_VS_VAL) |
		     DCU_THRESHOLD_OUT_BUF_HIGH(BUF_MAX_VAL) |
		     DCU_THRESHOLD_OUT_BUF_LOW(BUF_MIN_VAL));
	regmap_write(fsl_dev->regmap, DCU_UPDATE_MODE,
		     DCU_UPDATE_MODE_READREG);
	return;
}

static const struct drm_crtc_helper_funcs fsl_dcu_drm_crtc_helper_funcs = {
	.atomic_begin = fsl_dcu_drm_crtc_atomic_begin,
	.atomic_check = fsl_dcu_drm_crtc_atomic_check,
	.atomic_flush = fsl_dcu_drm_crtc_atomic_flush,
	.disable = fsl_dcu_drm_disable_crtc,
	.enable = fsl_dcu_drm_crtc_enable,
	.mode_fixup = fsl_dcu_drm_crtc_mode_fixup,
	.mode_set_nofb = fsl_dcu_drm_crtc_mode_set_nofb,
};

static const struct drm_crtc_funcs fsl_dcu_drm_crtc_funcs = {
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.destroy = drm_crtc_cleanup,
	.page_flip = drm_atomic_helper_page_flip,
	.reset = drm_atomic_helper_crtc_reset,
	.set_config = drm_atomic_helper_set_config,
};

int fsl_dcu_drm_crtc_create(struct fsl_dcu_drm_device *fsl_dev)
{
	struct drm_plane *primary;
	struct drm_crtc *crtc = &fsl_dev->crtc;
	unsigned int i, j, reg_num;
	int ret;

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

	if (!strcmp(fsl_dev->soc->name, "ls1021a"))
		reg_num = LS1021A_LAYER_REG_NUM;
	else
		reg_num = VF610_LAYER_REG_NUM;
	for (i = 0; i < fsl_dev->soc->total_layer; i++) {
		for (j = 1; j <= reg_num; j++)
			regmap_write(fsl_dev->regmap, DCU_CTRLDESCLN(i, j), 0);
	}
	regmap_update_bits(fsl_dev->regmap, DCU_DCU_MODE,
			   DCU_MODE_DCU_MODE_MASK,
			   DCU_MODE_DCU_MODE(DCU_MODE_OFF));
	regmap_write(fsl_dev->regmap, DCU_UPDATE_MODE,
		     DCU_UPDATE_MODE_READREG);

	return 0;
}
