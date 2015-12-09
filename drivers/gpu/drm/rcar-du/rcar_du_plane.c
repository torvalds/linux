/*
 * rcar_du_plane.c  --  R-Car Display Unit Planes
 *
 * Copyright (C) 2013-2014 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_plane_helper.h>

#include "rcar_du_drv.h"
#include "rcar_du_kms.h"
#include "rcar_du_plane.h"
#include "rcar_du_regs.h"

#define RCAR_DU_COLORKEY_NONE		(0 << 24)
#define RCAR_DU_COLORKEY_SOURCE		(1 << 24)
#define RCAR_DU_COLORKEY_MASK		(1 << 24)

static u32 rcar_du_plane_read(struct rcar_du_group *rgrp,
			      unsigned int index, u32 reg)
{
	return rcar_du_read(rgrp->dev,
			    rgrp->mmio_offset + index * PLANE_OFF + reg);
}

static void rcar_du_plane_write(struct rcar_du_group *rgrp,
				unsigned int index, u32 reg, u32 data)
{
	rcar_du_write(rgrp->dev, rgrp->mmio_offset + index * PLANE_OFF + reg,
		      data);
}

static void rcar_du_plane_setup_fb(struct rcar_du_plane *plane)
{
	struct rcar_du_plane_state *state =
		to_rcar_plane_state(plane->plane.state);
	struct drm_framebuffer *fb = plane->plane.state->fb;
	struct rcar_du_group *rgrp = plane->group;
	unsigned int src_x = state->state.src_x >> 16;
	unsigned int src_y = state->state.src_y >> 16;
	unsigned int index = state->hwindex;
	struct drm_gem_cma_object *gem;
	bool interlaced;
	u32 mwr;

	interlaced = state->state.crtc->state->adjusted_mode.flags
		   & DRM_MODE_FLAG_INTERLACE;

	/* Memory pitch (expressed in pixels). Must be doubled for interlaced
	 * operation with 32bpp formats.
	 */
	if (state->format->planes == 2)
		mwr = fb->pitches[0];
	else
		mwr = fb->pitches[0] * 8 / state->format->bpp;

	if (interlaced && state->format->bpp == 32)
		mwr *= 2;

	rcar_du_plane_write(rgrp, index, PnMWR, mwr);

	/* The Y position is expressed in raster line units and must be doubled
	 * for 32bpp formats, according to the R8A7790 datasheet. No mention of
	 * doubling the Y position is found in the R8A7779 datasheet, but the
	 * rule seems to apply there as well.
	 *
	 * Despite not being documented, doubling seem not to be needed when
	 * operating in interlaced mode.
	 *
	 * Similarly, for the second plane, NV12 and NV21 formats seem to
	 * require a halved Y position value, in both progressive and interlaced
	 * modes.
	 */
	rcar_du_plane_write(rgrp, index, PnSPXR, src_x);
	rcar_du_plane_write(rgrp, index, PnSPYR, src_y *
			    (!interlaced && state->format->bpp == 32 ? 2 : 1));

	gem = drm_fb_cma_get_gem_obj(fb, 0);
	rcar_du_plane_write(rgrp, index, PnDSA0R, gem->paddr + fb->offsets[0]);

	if (state->format->planes == 2) {
		index = (index + 1) % 8;

		rcar_du_plane_write(rgrp, index, PnMWR, fb->pitches[0]);

		rcar_du_plane_write(rgrp, index, PnSPXR, src_x);
		rcar_du_plane_write(rgrp, index, PnSPYR, src_y *
				    (state->format->bpp == 16 ? 2 : 1) / 2);

		gem = drm_fb_cma_get_gem_obj(fb, 1);
		rcar_du_plane_write(rgrp, index, PnDSA0R,
				    gem->paddr + fb->offsets[1]);
	}
}

static void rcar_du_plane_setup_mode(struct rcar_du_plane *plane,
				     unsigned int index)
{
	struct rcar_du_plane_state *state =
		to_rcar_plane_state(plane->plane.state);
	struct rcar_du_group *rgrp = plane->group;
	u32 colorkey;
	u32 pnmr;

	/* The PnALPHAR register controls alpha-blending in 16bpp formats
	 * (ARGB1555 and XRGB1555).
	 *
	 * For ARGB, set the alpha value to 0, and enable alpha-blending when
	 * the A bit is 0. This maps A=0 to alpha=0 and A=1 to alpha=255.
	 *
	 * For XRGB, set the alpha value to the plane-wide alpha value and
	 * enable alpha-blending regardless of the X bit value.
	 */
	if (state->format->fourcc != DRM_FORMAT_XRGB1555)
		rcar_du_plane_write(rgrp, index, PnALPHAR, PnALPHAR_ABIT_0);
	else
		rcar_du_plane_write(rgrp, index, PnALPHAR,
				    PnALPHAR_ABIT_X | state->alpha);

	pnmr = PnMR_BM_MD | state->format->pnmr;

	/* Disable color keying when requested. YUV formats have the
	 * PnMR_SPIM_TP_OFF bit set in their pnmr field, disabling color keying
	 * automatically.
	 */
	if ((state->colorkey & RCAR_DU_COLORKEY_MASK) == RCAR_DU_COLORKEY_NONE)
		pnmr |= PnMR_SPIM_TP_OFF;

	/* For packed YUV formats we need to select the U/V order. */
	if (state->format->fourcc == DRM_FORMAT_YUYV)
		pnmr |= PnMR_YCDF_YUYV;

	rcar_du_plane_write(rgrp, index, PnMR, pnmr);

	switch (state->format->fourcc) {
	case DRM_FORMAT_RGB565:
		colorkey = ((state->colorkey & 0xf80000) >> 8)
			 | ((state->colorkey & 0x00fc00) >> 5)
			 | ((state->colorkey & 0x0000f8) >> 3);
		rcar_du_plane_write(rgrp, index, PnTC2R, colorkey);
		break;

	case DRM_FORMAT_ARGB1555:
	case DRM_FORMAT_XRGB1555:
		colorkey = ((state->colorkey & 0xf80000) >> 9)
			 | ((state->colorkey & 0x00f800) >> 6)
			 | ((state->colorkey & 0x0000f8) >> 3);
		rcar_du_plane_write(rgrp, index, PnTC2R, colorkey);
		break;

	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
		rcar_du_plane_write(rgrp, index, PnTC3R,
				    PnTC3R_CODE | (state->colorkey & 0xffffff));
		break;
	}
}

static void __rcar_du_plane_setup(struct rcar_du_plane *plane,
				  unsigned int index)
{
	struct rcar_du_plane_state *state =
		to_rcar_plane_state(plane->plane.state);
	struct rcar_du_group *rgrp = plane->group;
	u32 ddcr2 = PnDDCR2_CODE;
	u32 ddcr4;

	/* Data format
	 *
	 * The data format is selected by the DDDF field in PnMR and the EDF
	 * field in DDCR4.
	 */
	ddcr4 = rcar_du_plane_read(rgrp, index, PnDDCR4);
	ddcr4 &= ~PnDDCR4_EDF_MASK;
	ddcr4 |= state->format->edf | PnDDCR4_CODE;

	rcar_du_plane_setup_mode(plane, index);

	if (state->format->planes == 2) {
		if (state->hwindex != index) {
			if (state->format->fourcc == DRM_FORMAT_NV12 ||
			    state->format->fourcc == DRM_FORMAT_NV21)
				ddcr2 |= PnDDCR2_Y420;

			if (state->format->fourcc == DRM_FORMAT_NV21)
				ddcr2 |= PnDDCR2_NV21;

			ddcr2 |= PnDDCR2_DIVU;
		} else {
			ddcr2 |= PnDDCR2_DIVY;
		}
	}

	rcar_du_plane_write(rgrp, index, PnDDCR2, ddcr2);
	rcar_du_plane_write(rgrp, index, PnDDCR4, ddcr4);

	/* Destination position and size */
	rcar_du_plane_write(rgrp, index, PnDSXR, plane->plane.state->crtc_w);
	rcar_du_plane_write(rgrp, index, PnDSYR, plane->plane.state->crtc_h);
	rcar_du_plane_write(rgrp, index, PnDPXR, plane->plane.state->crtc_x);
	rcar_du_plane_write(rgrp, index, PnDPYR, plane->plane.state->crtc_y);

	/* Wrap-around and blinking, disabled */
	rcar_du_plane_write(rgrp, index, PnWASPR, 0);
	rcar_du_plane_write(rgrp, index, PnWAMWR, 4095);
	rcar_du_plane_write(rgrp, index, PnBTR, 0);
	rcar_du_plane_write(rgrp, index, PnMLR, 0);
}

void rcar_du_plane_setup(struct rcar_du_plane *plane)
{
	struct rcar_du_plane_state *state =
		to_rcar_plane_state(plane->plane.state);

	__rcar_du_plane_setup(plane, state->hwindex);
	if (state->format->planes == 2)
		__rcar_du_plane_setup(plane, (state->hwindex + 1) % 8);

	rcar_du_plane_setup_fb(plane);
}

static int rcar_du_plane_atomic_check(struct drm_plane *plane,
				      struct drm_plane_state *state)
{
	struct rcar_du_plane_state *rstate = to_rcar_plane_state(state);
	struct rcar_du_plane *rplane = to_rcar_plane(plane);
	struct rcar_du_device *rcdu = rplane->group->dev;

	if (!state->fb || !state->crtc) {
		rstate->format = NULL;
		return 0;
	}

	if (state->src_w >> 16 != state->crtc_w ||
	    state->src_h >> 16 != state->crtc_h) {
		dev_dbg(rcdu->dev, "%s: scaling not supported\n", __func__);
		return -EINVAL;
	}

	rstate->format = rcar_du_format_info(state->fb->pixel_format);
	if (rstate->format == NULL) {
		dev_dbg(rcdu->dev, "%s: unsupported format %08x\n", __func__,
			state->fb->pixel_format);
		return -EINVAL;
	}

	return 0;
}

static void rcar_du_plane_atomic_update(struct drm_plane *plane,
					struct drm_plane_state *old_state)
{
	struct rcar_du_plane *rplane = to_rcar_plane(plane);

	if (plane->state->crtc)
		rcar_du_plane_setup(rplane);
}

static const struct drm_plane_helper_funcs rcar_du_plane_helper_funcs = {
	.atomic_check = rcar_du_plane_atomic_check,
	.atomic_update = rcar_du_plane_atomic_update,
};

static struct drm_plane_state *
rcar_du_plane_atomic_duplicate_state(struct drm_plane *plane)
{
	struct rcar_du_plane_state *state;
	struct rcar_du_plane_state *copy;

	if (WARN_ON(!plane->state))
		return NULL;

	state = to_rcar_plane_state(plane->state);
	copy = kmemdup(state, sizeof(*state), GFP_KERNEL);
	if (copy == NULL)
		return NULL;

	__drm_atomic_helper_plane_duplicate_state(plane, &copy->state);

	return &copy->state;
}

static void rcar_du_plane_atomic_destroy_state(struct drm_plane *plane,
					       struct drm_plane_state *state)
{
	__drm_atomic_helper_plane_destroy_state(plane, state);
	kfree(to_rcar_plane_state(state));
}

static void rcar_du_plane_reset(struct drm_plane *plane)
{
	struct rcar_du_plane_state *state;

	if (plane->state) {
		rcar_du_plane_atomic_destroy_state(plane, plane->state);
		plane->state = NULL;
	}

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (state == NULL)
		return;

	state->hwindex = -1;
	state->alpha = 255;
	state->colorkey = RCAR_DU_COLORKEY_NONE;
	state->zpos = plane->type == DRM_PLANE_TYPE_PRIMARY ? 0 : 1;

	plane->state = &state->state;
	plane->state->plane = plane;
}

static int rcar_du_plane_atomic_set_property(struct drm_plane *plane,
					     struct drm_plane_state *state,
					     struct drm_property *property,
					     uint64_t val)
{
	struct rcar_du_plane_state *rstate = to_rcar_plane_state(state);
	struct rcar_du_device *rcdu = to_rcar_plane(plane)->group->dev;

	if (property == rcdu->props.alpha)
		rstate->alpha = val;
	else if (property == rcdu->props.colorkey)
		rstate->colorkey = val;
	else if (property == rcdu->props.zpos)
		rstate->zpos = val;
	else
		return -EINVAL;

	return 0;
}

static int rcar_du_plane_atomic_get_property(struct drm_plane *plane,
	const struct drm_plane_state *state, struct drm_property *property,
	uint64_t *val)
{
	const struct rcar_du_plane_state *rstate =
		container_of(state, const struct rcar_du_plane_state, state);
	struct rcar_du_device *rcdu = to_rcar_plane(plane)->group->dev;

	if (property == rcdu->props.alpha)
		*val = rstate->alpha;
	else if (property == rcdu->props.colorkey)
		*val = rstate->colorkey;
	else if (property == rcdu->props.zpos)
		*val = rstate->zpos;
	else
		return -EINVAL;

	return 0;
}

static const struct drm_plane_funcs rcar_du_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.reset = rcar_du_plane_reset,
	.set_property = drm_atomic_helper_plane_set_property,
	.destroy = drm_plane_cleanup,
	.atomic_duplicate_state = rcar_du_plane_atomic_duplicate_state,
	.atomic_destroy_state = rcar_du_plane_atomic_destroy_state,
	.atomic_set_property = rcar_du_plane_atomic_set_property,
	.atomic_get_property = rcar_du_plane_atomic_get_property,
};

static const uint32_t formats[] = {
	DRM_FORMAT_RGB565,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_NV12,
	DRM_FORMAT_NV21,
	DRM_FORMAT_NV16,
};

int rcar_du_planes_init(struct rcar_du_group *rgrp)
{
	struct rcar_du_device *rcdu = rgrp->dev;
	unsigned int crtcs;
	unsigned int i;
	int ret;

	 /* Create one primary plane per CRTC in this group and seven overlay
	  * planes.
	  */
	rgrp->num_planes = rgrp->num_crtcs + 7;

	crtcs = ((1 << rcdu->num_crtcs) - 1) & (3 << (2 * rgrp->index));

	for (i = 0; i < rgrp->num_planes; ++i) {
		enum drm_plane_type type = i < rgrp->num_crtcs
					 ? DRM_PLANE_TYPE_PRIMARY
					 : DRM_PLANE_TYPE_OVERLAY;
		struct rcar_du_plane *plane = &rgrp->planes[i];

		plane->group = rgrp;

		ret = drm_universal_plane_init(rcdu->ddev, &plane->plane, crtcs,
					       &rcar_du_plane_funcs, formats,
					       ARRAY_SIZE(formats), type,
					       NULL);
		if (ret < 0)
			return ret;

		drm_plane_helper_add(&plane->plane,
				     &rcar_du_plane_helper_funcs);

		if (type == DRM_PLANE_TYPE_PRIMARY)
			continue;

		drm_object_attach_property(&plane->plane.base,
					   rcdu->props.alpha, 255);
		drm_object_attach_property(&plane->plane.base,
					   rcdu->props.colorkey,
					   RCAR_DU_COLORKEY_NONE);
		drm_object_attach_property(&plane->plane.base,
					   rcdu->props.zpos, 1);
	}

	return 0;
}
