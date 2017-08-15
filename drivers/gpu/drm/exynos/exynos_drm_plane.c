/*
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 * Authors: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <drm/drmP.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/exynos_drm.h>
#include "exynos_drm_drv.h"
#include "exynos_drm_crtc.h"
#include "exynos_drm_fb.h"
#include "exynos_drm_gem.h"
#include "exynos_drm_plane.h"

/*
 * This function is to get X or Y size shown via screen. This needs length and
 * start position of CRTC.
 *
 *      <--- length --->
 * CRTC ----------------
 *      ^ start        ^ end
 *
 * There are six cases from a to f.
 *
 *             <----- SCREEN ----->
 *             0                 last
 *   ----------|------------------|----------
 * CRTCs
 * a -------
 *        b -------
 *        c --------------------------
 *                 d --------
 *                           e -------
 *                                  f -------
 */
static int exynos_plane_get_size(int start, unsigned length, unsigned last)
{
	int end = start + length;
	int size = 0;

	if (start <= 0) {
		if (end > 0)
			size = min_t(unsigned, end, last);
	} else if (start <= last) {
		size = min_t(unsigned, last - start, length);
	}

	return size;
}

static void exynos_plane_mode_set(struct exynos_drm_plane_state *exynos_state)
{
	struct drm_plane_state *state = &exynos_state->base;
	struct drm_crtc *crtc = state->crtc;
	struct drm_crtc_state *crtc_state =
			drm_atomic_get_existing_crtc_state(state->state, crtc);
	struct drm_display_mode *mode = &crtc_state->adjusted_mode;
	int crtc_x, crtc_y;
	unsigned int crtc_w, crtc_h;
	unsigned int src_x, src_y;
	unsigned int src_w, src_h;
	unsigned int actual_w;
	unsigned int actual_h;

	/*
	 * The original src/dest coordinates are stored in exynos_state->base,
	 * but we want to keep another copy internal to our driver that we can
	 * clip/modify ourselves.
	 */

	crtc_x = state->crtc_x;
	crtc_y = state->crtc_y;
	crtc_w = state->crtc_w;
	crtc_h = state->crtc_h;

	src_x = state->src_x >> 16;
	src_y = state->src_y >> 16;
	src_w = state->src_w >> 16;
	src_h = state->src_h >> 16;

	/* set ratio */
	exynos_state->h_ratio = (src_w << 16) / crtc_w;
	exynos_state->v_ratio = (src_h << 16) / crtc_h;

	/* clip to visible area */
	actual_w = exynos_plane_get_size(crtc_x, crtc_w, mode->hdisplay);
	actual_h = exynos_plane_get_size(crtc_y, crtc_h, mode->vdisplay);

	if (crtc_x < 0) {
		if (actual_w)
			src_x += ((-crtc_x) * exynos_state->h_ratio) >> 16;
		crtc_x = 0;
	}

	if (crtc_y < 0) {
		if (actual_h)
			src_y += ((-crtc_y) * exynos_state->v_ratio) >> 16;
		crtc_y = 0;
	}

	/* set drm framebuffer data. */
	exynos_state->src.x = src_x;
	exynos_state->src.y = src_y;
	exynos_state->src.w = (actual_w * exynos_state->h_ratio) >> 16;
	exynos_state->src.h = (actual_h * exynos_state->v_ratio) >> 16;

	/* set plane range to be displayed. */
	exynos_state->crtc.x = crtc_x;
	exynos_state->crtc.y = crtc_y;
	exynos_state->crtc.w = actual_w;
	exynos_state->crtc.h = actual_h;

	DRM_DEBUG_KMS("plane : offset_x/y(%d,%d), width/height(%d,%d)",
			exynos_state->crtc.x, exynos_state->crtc.y,
			exynos_state->crtc.w, exynos_state->crtc.h);
}

static void exynos_drm_plane_reset(struct drm_plane *plane)
{
	struct exynos_drm_plane *exynos_plane = to_exynos_plane(plane);
	struct exynos_drm_plane_state *exynos_state;

	if (plane->state) {
		exynos_state = to_exynos_plane_state(plane->state);
		if (exynos_state->base.fb)
			drm_framebuffer_unreference(exynos_state->base.fb);
		kfree(exynos_state);
		plane->state = NULL;
	}

	exynos_state = kzalloc(sizeof(*exynos_state), GFP_KERNEL);
	if (exynos_state) {
		plane->state = &exynos_state->base;
		plane->state->plane = plane;
		plane->state->zpos = exynos_plane->config->zpos;
	}
}

static struct drm_plane_state *
exynos_drm_plane_duplicate_state(struct drm_plane *plane)
{
	struct exynos_drm_plane_state *exynos_state;
	struct exynos_drm_plane_state *copy;

	exynos_state = to_exynos_plane_state(plane->state);
	copy = kzalloc(sizeof(*exynos_state), GFP_KERNEL);
	if (!copy)
		return NULL;

	__drm_atomic_helper_plane_duplicate_state(plane, &copy->base);
	return &copy->base;
}

static void exynos_drm_plane_destroy_state(struct drm_plane *plane,
					   struct drm_plane_state *old_state)
{
	struct exynos_drm_plane_state *old_exynos_state =
					to_exynos_plane_state(old_state);
	__drm_atomic_helper_plane_destroy_state(old_state);
	kfree(old_exynos_state);
}

static struct drm_plane_funcs exynos_plane_funcs = {
	.update_plane	= drm_atomic_helper_update_plane,
	.disable_plane	= drm_atomic_helper_disable_plane,
	.destroy	= drm_plane_cleanup,
	.set_property	= drm_atomic_helper_plane_set_property,
	.reset		= exynos_drm_plane_reset,
	.atomic_duplicate_state = exynos_drm_plane_duplicate_state,
	.atomic_destroy_state = exynos_drm_plane_destroy_state,
};

static int
exynos_drm_plane_check_size(const struct exynos_drm_plane_config *config,
			    struct exynos_drm_plane_state *state)
{
	bool width_ok = false, height_ok = false;

	if (config->capabilities & EXYNOS_DRM_PLANE_CAP_SCALE)
		return 0;

	if (state->src.w == state->crtc.w)
		width_ok = true;

	if (state->src.h == state->crtc.h)
		height_ok = true;

	if ((config->capabilities & EXYNOS_DRM_PLANE_CAP_DOUBLE) &&
	    state->h_ratio == (1 << 15))
		width_ok = true;

	if ((config->capabilities & EXYNOS_DRM_PLANE_CAP_DOUBLE) &&
	    state->v_ratio == (1 << 15))
		height_ok = true;

	if (width_ok && height_ok)
		return 0;

	DRM_DEBUG_KMS("scaling mode is not supported");
	return -ENOTSUPP;
}

static int exynos_plane_atomic_check(struct drm_plane *plane,
				     struct drm_plane_state *state)
{
	struct exynos_drm_plane *exynos_plane = to_exynos_plane(plane);
	struct exynos_drm_plane_state *exynos_state =
						to_exynos_plane_state(state);
	int ret = 0;

	if (!state->crtc || !state->fb)
		return 0;

	/* translate state into exynos_state */
	exynos_plane_mode_set(exynos_state);

	ret = exynos_drm_plane_check_size(exynos_plane->config, exynos_state);
	return ret;
}

static void exynos_plane_atomic_update(struct drm_plane *plane,
				       struct drm_plane_state *old_state)
{
	struct drm_plane_state *state = plane->state;
	struct exynos_drm_crtc *exynos_crtc = to_exynos_crtc(state->crtc);
	struct exynos_drm_plane *exynos_plane = to_exynos_plane(plane);

	if (!state->crtc)
		return;

	plane->crtc = state->crtc;

	if (exynos_crtc->ops->update_plane)
		exynos_crtc->ops->update_plane(exynos_crtc, exynos_plane);
}

static void exynos_plane_atomic_disable(struct drm_plane *plane,
					struct drm_plane_state *old_state)
{
	struct exynos_drm_plane *exynos_plane = to_exynos_plane(plane);
	struct exynos_drm_crtc *exynos_crtc = to_exynos_crtc(old_state->crtc);

	if (!old_state->crtc)
		return;

	if (exynos_crtc->ops->disable_plane)
		exynos_crtc->ops->disable_plane(exynos_crtc, exynos_plane);
}

static const struct drm_plane_helper_funcs plane_helper_funcs = {
	.atomic_check = exynos_plane_atomic_check,
	.atomic_update = exynos_plane_atomic_update,
	.atomic_disable = exynos_plane_atomic_disable,
};

static void exynos_plane_attach_zpos_property(struct drm_plane *plane,
					      bool immutable)
{
	/* FIXME */
	if (immutable)
		drm_plane_create_zpos_immutable_property(plane, 0);
	else
		drm_plane_create_zpos_property(plane, 0, 0, MAX_PLANE - 1);
}

int exynos_plane_init(struct drm_device *dev,
		      struct exynos_drm_plane *exynos_plane, unsigned int index,
		      const struct exynos_drm_plane_config *config)
{
	int err;

	err = drm_universal_plane_init(dev, &exynos_plane->base,
				       1 << dev->mode_config.num_crtc,
				       &exynos_plane_funcs,
				       config->pixel_formats,
				       config->num_pixel_formats,
				       config->type, NULL);
	if (err) {
		DRM_ERROR("failed to initialize plane\n");
		return err;
	}

	drm_plane_helper_add(&exynos_plane->base, &plane_helper_funcs);

	exynos_plane->index = index;
	exynos_plane->config = config;

	exynos_plane_attach_zpos_property(&exynos_plane->base,
			   !(config->capabilities & EXYNOS_DRM_PLANE_CAP_ZPOS));

	return 0;
}
