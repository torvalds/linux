/*
 * Copyright (C) 2017 NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_plane_helper.h>

#include "dc.h"
#include "plane.h"

static void tegra_plane_destroy(struct drm_plane *plane)
{
	struct tegra_plane *p = to_tegra_plane(plane);

	drm_plane_cleanup(plane);
	kfree(p);
}

static void tegra_plane_reset(struct drm_plane *plane)
{
	struct tegra_plane_state *state;

	if (plane->state)
		__drm_atomic_helper_plane_destroy_state(plane->state);

	kfree(plane->state);
	plane->state = NULL;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (state) {
		plane->state = &state->base;
		plane->state->plane = plane;
	}
}

static struct drm_plane_state *
tegra_plane_atomic_duplicate_state(struct drm_plane *plane)
{
	struct tegra_plane_state *state = to_tegra_plane_state(plane->state);
	struct tegra_plane_state *copy;
	unsigned int i;

	copy = kmalloc(sizeof(*copy), GFP_KERNEL);
	if (!copy)
		return NULL;

	__drm_atomic_helper_plane_duplicate_state(plane, &copy->base);
	copy->tiling = state->tiling;
	copy->format = state->format;
	copy->swap = state->swap;
	copy->opaque = state->opaque;

	for (i = 0; i < 3; i++)
		copy->dependent[i] = state->dependent[i];

	return &copy->base;
}

static void tegra_plane_atomic_destroy_state(struct drm_plane *plane,
					     struct drm_plane_state *state)
{
	__drm_atomic_helper_plane_destroy_state(state);
	kfree(state);
}

const struct drm_plane_funcs tegra_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = tegra_plane_destroy,
	.reset = tegra_plane_reset,
	.atomic_duplicate_state = tegra_plane_atomic_duplicate_state,
	.atomic_destroy_state = tegra_plane_atomic_destroy_state,
};

int tegra_plane_state_add(struct tegra_plane *plane,
			  struct drm_plane_state *state)
{
	struct drm_crtc_state *crtc_state;
	struct tegra_dc_state *tegra;
	struct drm_rect clip;
	int err;

	/* Propagate errors from allocation or locking failures. */
	crtc_state = drm_atomic_get_crtc_state(state->state, state->crtc);
	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);

	clip.x1 = 0;
	clip.y1 = 0;
	clip.x2 = crtc_state->mode.hdisplay;
	clip.y2 = crtc_state->mode.vdisplay;

	/* Check plane state for visibility and calculate clipping bounds */
	err = drm_atomic_helper_check_plane_state(state, crtc_state, &clip,
						  0, INT_MAX, true, true);
	if (err < 0)
		return err;

	tegra = to_dc_state(crtc_state);

	tegra->planes |= WIN_A_ACT_REQ << plane->index;

	return 0;
}

int tegra_plane_format(u32 fourcc, u32 *format, u32 *swap)
{
	/* assume no swapping of fetched data */
	if (swap)
		*swap = BYTE_SWAP_NOSWAP;

	switch (fourcc) {
	case DRM_FORMAT_ARGB4444:
		*format = WIN_COLOR_DEPTH_B4G4R4A4;
		break;

	case DRM_FORMAT_ARGB1555:
		*format = WIN_COLOR_DEPTH_B5G5R5A1;
		break;

	case DRM_FORMAT_RGB565:
		*format = WIN_COLOR_DEPTH_B5G6R5;
		break;

	case DRM_FORMAT_RGBA5551:
		*format = WIN_COLOR_DEPTH_A1B5G5R5;
		break;

	case DRM_FORMAT_ARGB8888:
		*format = WIN_COLOR_DEPTH_B8G8R8A8;
		break;

	case DRM_FORMAT_ABGR8888:
		*format = WIN_COLOR_DEPTH_R8G8B8A8;
		break;

	case DRM_FORMAT_ABGR4444:
		*format = WIN_COLOR_DEPTH_R4G4B4A4;
		break;

	case DRM_FORMAT_ABGR1555:
		*format = WIN_COLOR_DEPTH_R5G5B5A;
		break;

	case DRM_FORMAT_BGRA5551:
		*format = WIN_COLOR_DEPTH_AR5G5B5;
		break;

	case DRM_FORMAT_XRGB1555:
		*format = WIN_COLOR_DEPTH_B5G5R5X1;
		break;

	case DRM_FORMAT_RGBX5551:
		*format = WIN_COLOR_DEPTH_X1B5G5R5;
		break;

	case DRM_FORMAT_XBGR1555:
		*format = WIN_COLOR_DEPTH_R5G5B5X1;
		break;

	case DRM_FORMAT_BGRX5551:
		*format = WIN_COLOR_DEPTH_X1R5G5B5;
		break;

	case DRM_FORMAT_BGR565:
		*format = WIN_COLOR_DEPTH_R5G6B5;
		break;

	case DRM_FORMAT_BGRA8888:
		*format = WIN_COLOR_DEPTH_A8R8G8B8;
		break;

	case DRM_FORMAT_RGBA8888:
		*format = WIN_COLOR_DEPTH_A8B8G8R8;
		break;

	case DRM_FORMAT_XRGB8888:
		*format = WIN_COLOR_DEPTH_B8G8R8X8;
		break;

	case DRM_FORMAT_XBGR8888:
		*format = WIN_COLOR_DEPTH_R8G8B8X8;
		break;

	case DRM_FORMAT_UYVY:
		*format = WIN_COLOR_DEPTH_YCbCr422;
		break;

	case DRM_FORMAT_YUYV:
		if (!swap)
			return -EINVAL;

		*format = WIN_COLOR_DEPTH_YCbCr422;
		*swap = BYTE_SWAP_SWAP2;
		break;

	case DRM_FORMAT_YUV420:
		*format = WIN_COLOR_DEPTH_YCbCr420P;
		break;

	case DRM_FORMAT_YUV422:
		*format = WIN_COLOR_DEPTH_YCbCr422P;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

bool tegra_plane_format_is_yuv(unsigned int format, bool *planar)
{
	switch (format) {
	case WIN_COLOR_DEPTH_YCbCr422:
	case WIN_COLOR_DEPTH_YUV422:
		if (planar)
			*planar = false;

		return true;

	case WIN_COLOR_DEPTH_YCbCr420P:
	case WIN_COLOR_DEPTH_YUV420P:
	case WIN_COLOR_DEPTH_YCbCr422P:
	case WIN_COLOR_DEPTH_YUV422P:
	case WIN_COLOR_DEPTH_YCbCr422R:
	case WIN_COLOR_DEPTH_YUV422R:
	case WIN_COLOR_DEPTH_YCbCr422RA:
	case WIN_COLOR_DEPTH_YUV422RA:
		if (planar)
			*planar = true;

		return true;
	}

	if (planar)
		*planar = false;

	return false;
}

static bool __drm_format_has_alpha(u32 format)
{
	switch (format) {
	case DRM_FORMAT_ARGB1555:
	case DRM_FORMAT_RGBA5551:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_ARGB8888:
		return true;
	}

	return false;
}

/*
 * This is applicable to Tegra20 and Tegra30 only where the opaque formats can
 * be emulated using the alpha formats and alpha blending disabled.
 */
bool tegra_plane_format_has_alpha(unsigned int format)
{
	switch (format) {
	case WIN_COLOR_DEPTH_B5G5R5A1:
	case WIN_COLOR_DEPTH_A1B5G5R5:
	case WIN_COLOR_DEPTH_R8G8B8A8:
	case WIN_COLOR_DEPTH_B8G8R8A8:
		return true;
	}

	return false;
}

int tegra_plane_format_get_alpha(unsigned int opaque, unsigned int *alpha)
{
	if (tegra_plane_format_is_yuv(opaque, NULL)) {
		*alpha = opaque;
		return 0;
	}

	switch (opaque) {
	case WIN_COLOR_DEPTH_B5G5R5X1:
		*alpha = WIN_COLOR_DEPTH_B5G5R5A1;
		return 0;

	case WIN_COLOR_DEPTH_X1B5G5R5:
		*alpha = WIN_COLOR_DEPTH_A1B5G5R5;
		return 0;

	case WIN_COLOR_DEPTH_R8G8B8X8:
		*alpha = WIN_COLOR_DEPTH_R8G8B8A8;
		return 0;

	case WIN_COLOR_DEPTH_B8G8R8X8:
		*alpha = WIN_COLOR_DEPTH_B8G8R8A8;
		return 0;

	case WIN_COLOR_DEPTH_B5G6R5:
		*alpha = opaque;
		return 0;
	}

	return -EINVAL;
}

unsigned int tegra_plane_get_overlap_index(struct tegra_plane *plane,
					   struct tegra_plane *other)
{
	unsigned int index = 0, i;

	WARN_ON(plane == other);

	for (i = 0; i < 3; i++) {
		if (i == plane->index)
			continue;

		if (i == other->index)
			break;

		index++;
	}

	return index;
}

void tegra_plane_check_dependent(struct tegra_plane *tegra,
				 struct tegra_plane_state *state)
{
	struct drm_plane_state *old, *new;
	struct drm_plane *plane;
	unsigned int zpos[2];
	unsigned int i;

	for (i = 0; i < 2; i++)
		zpos[i] = 0;

	for_each_oldnew_plane_in_state(state->base.state, plane, old, new, i) {
		struct tegra_plane *p = to_tegra_plane(plane);
		unsigned index;

		/* skip this plane and planes on different CRTCs */
		if (p == tegra || new->crtc != state->base.crtc)
			continue;

		index = tegra_plane_get_overlap_index(tegra, p);

		state->dependent[index] = false;

		/*
		 * If any of the other planes is on top of this plane and uses
		 * a format with an alpha component, mark this plane as being
		 * dependent, meaning it's alpha value will be 1 minus the sum
		 * of alpha components of the overlapping planes.
		 */
		if (p->index > tegra->index) {
			if (__drm_format_has_alpha(new->fb->format->format))
				state->dependent[index] = true;

			/* keep track of the Z position */
			zpos[index] = p->index;
		}
	}

	/*
	 * The region where three windows overlap is the intersection of the
	 * two regions where two windows overlap. It contributes to the area
	 * if any of the windows on top of it have an alpha component.
	 */
	for (i = 0; i < 2; i++)
		state->dependent[2] = state->dependent[2] ||
				      state->dependent[i];

	/*
	 * However, if any of the windows on top of this window is opaque, it
	 * will completely conceal this window within that area, so avoid the
	 * window from contributing to the area.
	 */
	for (i = 0; i < 2; i++) {
		if (zpos[i] > tegra->index)
			state->dependent[2] = state->dependent[2] &&
					      state->dependent[i];
	}
}
