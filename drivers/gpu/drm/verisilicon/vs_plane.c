// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Icenowy Zheng <uwu@icenowy.me>
 */

#include <linux/errno.h>
#include <linux/printk.h>

#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_print.h>

#include "vs_plane.h"

int drm_format_to_vs_format(u32 drm_format, struct vs_format *vs_format)
{
	switch (drm_format) {
	case DRM_FORMAT_XRGB4444:
	case DRM_FORMAT_RGBX4444:
	case DRM_FORMAT_XBGR4444:
	case DRM_FORMAT_BGRX4444:
		vs_format->color = VSDC_COLOR_FORMAT_X4R4G4B4;
		break;
	case DRM_FORMAT_ARGB4444:
	case DRM_FORMAT_RGBA4444:
	case DRM_FORMAT_ABGR4444:
	case DRM_FORMAT_BGRA4444:
		vs_format->color = VSDC_COLOR_FORMAT_A4R4G4B4;
		break;
	case DRM_FORMAT_XRGB1555:
	case DRM_FORMAT_RGBX5551:
	case DRM_FORMAT_XBGR1555:
	case DRM_FORMAT_BGRX5551:
		vs_format->color = VSDC_COLOR_FORMAT_X1R5G5B5;
		break;
	case DRM_FORMAT_ARGB1555:
	case DRM_FORMAT_RGBA5551:
	case DRM_FORMAT_ABGR1555:
	case DRM_FORMAT_BGRA5551:
		vs_format->color = VSDC_COLOR_FORMAT_A1R5G5B5;
		break;
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_BGR565:
		vs_format->color = VSDC_COLOR_FORMAT_R5G6B5;
		break;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_RGBX8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_BGRX8888:
		vs_format->color = VSDC_COLOR_FORMAT_X8R8G8B8;
		break;
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_RGBA8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_BGRA8888:
		vs_format->color = VSDC_COLOR_FORMAT_A8R8G8B8;
		break;
	case DRM_FORMAT_ARGB2101010:
	case DRM_FORMAT_RGBA1010102:
	case DRM_FORMAT_ABGR2101010:
	case DRM_FORMAT_BGRA1010102:
		vs_format->color = VSDC_COLOR_FORMAT_A2R10G10B10;
		break;
	default:
		return -EINVAL;
	}

	switch (drm_format) {
	case DRM_FORMAT_RGBX4444:
	case DRM_FORMAT_RGBA4444:
	case DRM_FORMAT_RGBX5551:
	case DRM_FORMAT_RGBA5551:
	case DRM_FORMAT_RGBX8888:
	case DRM_FORMAT_RGBA8888:
	case DRM_FORMAT_RGBA1010102:
		vs_format->swizzle = VSDC_SWIZZLE_RGBA;
		break;
	case DRM_FORMAT_XBGR4444:
	case DRM_FORMAT_ABGR4444:
	case DRM_FORMAT_XBGR1555:
	case DRM_FORMAT_ABGR1555:
	case DRM_FORMAT_BGR565:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_ABGR2101010:
		vs_format->swizzle = VSDC_SWIZZLE_ABGR;
		break;
	case DRM_FORMAT_BGRX4444:
	case DRM_FORMAT_BGRA4444:
	case DRM_FORMAT_BGRX5551:
	case DRM_FORMAT_BGRA5551:
	case DRM_FORMAT_BGRX8888:
	case DRM_FORMAT_BGRA8888:
	case DRM_FORMAT_BGRA1010102:
		vs_format->swizzle = VSDC_SWIZZLE_BGRA;
		break;
	default:
		/* N/A for YUV formats */
		vs_format->swizzle = VSDC_SWIZZLE_ARGB;
	}

	/* N/A for non-YUV formats */
	vs_format->uv_swizzle = false;

	return 0;
}

dma_addr_t vs_fb_get_dma_addr(struct drm_framebuffer *fb,
			      const struct drm_rect *src_rect)
{
	struct drm_gem_dma_object *gem;
	dma_addr_t dma_addr;

	/* Get the physical address of the buffer in memory */
	gem = drm_fb_dma_get_gem_obj(fb, 0);

	/* Compute the start of the displayed memory */
	dma_addr = gem->dma_addr + fb->offsets[0];

	/* Fixup framebuffer address for src coordinates */
	dma_addr += drm_format_info_min_pitch(fb->format, 0,
					      src_rect->x1 >> 16);
	dma_addr += (src_rect->y1 >> 16) * fb->pitches[0];

	return dma_addr;
}

struct drm_plane_state *vs_plane_duplicate_state(struct drm_plane *plane)
{
	struct vs_plane_state *vs_state, *vs_state_old;

	if (drm_WARN_ON(plane->dev, !plane->state))
		return NULL;

	vs_state_old = to_vs_plane_state(plane->state);

	vs_state = kzalloc_obj(*vs_state, GFP_KERNEL);
	if (!vs_state)
		return NULL;

	__drm_atomic_helper_plane_duplicate_state(plane, &vs_state->base);

	memcpy(&vs_state->format, &vs_state_old->format,
	       sizeof(struct vs_format));

	return &vs_state->base;
}

void vs_plane_destroy_state(struct drm_plane *plane,
			    struct drm_plane_state *state)
{
	__drm_atomic_helper_plane_destroy_state(state);
	kfree(state);
}

/* Called during init to allocate the plane's atomic state. */
void vs_plane_reset(struct drm_plane *plane)
{
	struct vs_plane_state *vs_state;

	if (plane->state) {
		__drm_atomic_helper_plane_destroy_state(plane->state);
		kfree(plane->state);
		plane->state = NULL;
	}

	vs_state = kzalloc_obj(*vs_state, GFP_KERNEL);
	if (!vs_state)
		return;

	__drm_atomic_helper_plane_reset(plane, &vs_state->base);
}
