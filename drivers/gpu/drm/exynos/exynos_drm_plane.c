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

#include <drm/exynos_drm.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_atomic_helper.h>
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

static void exynos_plane_mode_set(struct drm_plane *plane,
				  struct drm_crtc *crtc,
				  struct drm_framebuffer *fb,
				  int crtc_x, int crtc_y,
				  unsigned int crtc_w, unsigned int crtc_h,
				  uint32_t src_x, uint32_t src_y,
				  uint32_t src_w, uint32_t src_h)
{
	struct exynos_drm_plane *exynos_plane = to_exynos_plane(plane);
	struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	unsigned int actual_w;
	unsigned int actual_h;

	actual_w = exynos_plane_get_size(crtc_x, crtc_w, mode->hdisplay);
	actual_h = exynos_plane_get_size(crtc_y, crtc_h, mode->vdisplay);

	if (crtc_x < 0) {
		if (actual_w)
			src_x -= crtc_x;
		crtc_x = 0;
	}

	if (crtc_y < 0) {
		if (actual_h)
			src_y -= crtc_y;
		crtc_y = 0;
	}

	/* set ratio */
	exynos_plane->h_ratio = (src_w << 16) / crtc_w;
	exynos_plane->v_ratio = (src_h << 16) / crtc_h;

	/* set drm framebuffer data. */
	exynos_plane->src_x = src_x;
	exynos_plane->src_y = src_y;
	exynos_plane->src_w = (actual_w * exynos_plane->h_ratio) >> 16;
	exynos_plane->src_h = (actual_h * exynos_plane->v_ratio) >> 16;

	/* set plane range to be displayed. */
	exynos_plane->crtc_x = crtc_x;
	exynos_plane->crtc_y = crtc_y;
	exynos_plane->crtc_w = actual_w;
	exynos_plane->crtc_h = actual_h;

	DRM_DEBUG_KMS("plane : offset_x/y(%d,%d), width/height(%d,%d)",
			exynos_plane->crtc_x, exynos_plane->crtc_y,
			exynos_plane->crtc_w, exynos_plane->crtc_h);

	plane->crtc = crtc;
}

static struct drm_plane_funcs exynos_plane_funcs = {
	.update_plane	= drm_atomic_helper_update_plane,
	.disable_plane	= drm_atomic_helper_disable_plane,
	.destroy	= drm_plane_cleanup,
	.reset = drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
};

static int exynos_plane_atomic_check(struct drm_plane *plane,
				     struct drm_plane_state *state)
{
	struct exynos_drm_plane *exynos_plane = to_exynos_plane(plane);
	int nr;
	int i;

	if (!state->fb)
		return 0;

	nr = drm_format_num_planes(state->fb->pixel_format);
	for (i = 0; i < nr; i++) {
		struct exynos_drm_gem_obj *obj =
					exynos_drm_fb_gem_obj(state->fb, i);

		if (!obj) {
			DRM_DEBUG_KMS("gem object is null\n");
			return -EFAULT;
		}

		exynos_plane->dma_addr[i] = obj->dma_addr +
					    state->fb->offsets[i];

		DRM_DEBUG_KMS("buffer: %d, dma_addr = 0x%lx\n",
				i, (unsigned long)exynos_plane->dma_addr[i]);
	}

	return 0;
}

static void exynos_plane_atomic_update(struct drm_plane *plane,
				       struct drm_plane_state *old_state)
{
	struct drm_plane_state *state = plane->state;
	struct exynos_drm_crtc *exynos_crtc = to_exynos_crtc(state->crtc);
	struct exynos_drm_plane *exynos_plane = to_exynos_plane(plane);

	if (!state->crtc)
		return;

	exynos_plane_mode_set(plane, state->crtc, state->fb,
			      state->crtc_x, state->crtc_y,
			      state->crtc_w, state->crtc_h,
			      state->src_x >> 16, state->src_y >> 16,
			      state->src_w >> 16, state->src_h >> 16);

	exynos_plane->pending_fb = state->fb;

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
		exynos_crtc->ops->disable_plane(exynos_crtc,
						exynos_plane);
}

static const struct drm_plane_helper_funcs plane_helper_funcs = {
	.atomic_check = exynos_plane_atomic_check,
	.atomic_update = exynos_plane_atomic_update,
	.atomic_disable = exynos_plane_atomic_disable,
};

static void exynos_plane_attach_zpos_property(struct drm_plane *plane,
					      unsigned int zpos)
{
	struct drm_device *dev = plane->dev;
	struct exynos_drm_private *dev_priv = dev->dev_private;
	struct drm_property *prop;

	prop = dev_priv->plane_zpos_property;
	if (!prop) {
		prop = drm_property_create_range(dev, DRM_MODE_PROP_IMMUTABLE,
						 "zpos", 0, MAX_PLANE - 1);
		if (!prop)
			return;

		dev_priv->plane_zpos_property = prop;
	}

	drm_object_attach_property(&plane->base, prop, zpos);
}

int exynos_plane_init(struct drm_device *dev,
		      struct exynos_drm_plane *exynos_plane,
		      unsigned long possible_crtcs, enum drm_plane_type type,
		      const uint32_t *formats, unsigned int fcount,
		      unsigned int zpos)
{
	int err;

	err = drm_universal_plane_init(dev, &exynos_plane->base, possible_crtcs,
				       &exynos_plane_funcs, formats, fcount,
				       type);
	if (err) {
		DRM_ERROR("failed to initialize plane\n");
		return err;
	}

	drm_plane_helper_add(&exynos_plane->base, &plane_helper_funcs);

	exynos_plane->zpos = zpos;

	if (type == DRM_PLANE_TYPE_OVERLAY)
		exynos_plane_attach_zpos_property(&exynos_plane->base, zpos);

	return 0;
}
