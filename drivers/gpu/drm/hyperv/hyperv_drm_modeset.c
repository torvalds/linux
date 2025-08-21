// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2021 Microsoft
 */

#include <linux/hyperv.h>

#include <drm/drm_atomic.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>
#include <drm/drm_format_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_panic.h>
#include <drm/drm_plane.h>

#include "hyperv_drm.h"

static int hyperv_blit_to_vram_rect(struct drm_framebuffer *fb,
				    const struct iosys_map *vmap,
				    struct drm_rect *rect)
{
	struct hyperv_drm_device *hv = to_hv(fb->dev);
	struct iosys_map dst = IOSYS_MAP_INIT_VADDR_IOMEM(hv->vram);
	int idx;

	if (!drm_dev_enter(&hv->dev, &idx))
		return -ENODEV;

	iosys_map_incr(&dst, drm_fb_clip_offset(fb->pitches[0], fb->format, rect));
	drm_fb_memcpy(&dst, fb->pitches, vmap, fb, rect);

	drm_dev_exit(idx);

	return 0;
}

static int hyperv_connector_get_modes(struct drm_connector *connector)
{
	struct hyperv_drm_device *hv = to_hv(connector->dev);
	int count;

	count = drm_add_modes_noedid(connector,
				     connector->dev->mode_config.max_width,
				     connector->dev->mode_config.max_height);
	drm_set_preferred_mode(connector, hv->preferred_width,
			       hv->preferred_height);

	return count;
}

static const struct drm_connector_helper_funcs hyperv_connector_helper_funcs = {
	.get_modes = hyperv_connector_get_modes,
};

static const struct drm_connector_funcs hyperv_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static inline int hyperv_conn_init(struct hyperv_drm_device *hv)
{
	drm_connector_helper_add(&hv->connector, &hyperv_connector_helper_funcs);
	return drm_connector_init(&hv->dev, &hv->connector,
				  &hyperv_connector_funcs,
				  DRM_MODE_CONNECTOR_VIRTUAL);
}

static int hyperv_check_size(struct hyperv_drm_device *hv, int w, int h,
			     struct drm_framebuffer *fb)
{
	u32 pitch = w * (hv->screen_depth / 8);

	if (fb)
		pitch = fb->pitches[0];

	if (pitch * h > hv->fb_size)
		return -EINVAL;

	return 0;
}

static const uint32_t hyperv_formats[] = {
	DRM_FORMAT_XRGB8888,
};

static const uint64_t hyperv_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID
};

static void hyperv_crtc_helper_atomic_enable(struct drm_crtc *crtc,
					     struct drm_atomic_state *state)
{
	struct hyperv_drm_device *hv = to_hv(crtc->dev);
	struct drm_plane *plane = &hv->plane;
	struct drm_plane_state *plane_state = plane->state;
	struct drm_crtc_state *crtc_state = crtc->state;

	hyperv_hide_hw_ptr(hv->hdev);
	hyperv_update_situation(hv->hdev, 1,  hv->screen_depth,
				crtc_state->mode.hdisplay,
				crtc_state->mode.vdisplay,
				plane_state->fb->pitches[0]);
}

static const struct drm_crtc_helper_funcs hyperv_crtc_helper_funcs = {
	.atomic_check = drm_crtc_helper_atomic_check,
	.atomic_enable = hyperv_crtc_helper_atomic_enable,
};

static const struct drm_crtc_funcs hyperv_crtc_funcs = {
	.reset = drm_atomic_helper_crtc_reset,
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
};

static int hyperv_plane_atomic_check(struct drm_plane *plane,
				     struct drm_atomic_state *state)
{
	struct drm_plane_state *plane_state = drm_atomic_get_new_plane_state(state, plane);
	struct hyperv_drm_device *hv = to_hv(plane->dev);
	struct drm_framebuffer *fb = plane_state->fb;
	struct drm_crtc *crtc = plane_state->crtc;
	struct drm_crtc_state *crtc_state = NULL;
	int ret;

	if (crtc)
		crtc_state = drm_atomic_get_new_crtc_state(state, crtc);

	ret = drm_atomic_helper_check_plane_state(plane_state, crtc_state,
						  DRM_PLANE_NO_SCALING,
						  DRM_PLANE_NO_SCALING,
						  false, false);
	if (ret)
		return ret;

	if (!plane_state->visible)
		return 0;

	if (fb->pitches[0] * fb->height > hv->fb_size) {
		drm_err(&hv->dev, "fb size requested by %s for %dX%d (pitch %d) greater than %ld\n",
			current->comm, fb->width, fb->height, fb->pitches[0], hv->fb_size);
		return -EINVAL;
	}

	return 0;
}

static void hyperv_plane_atomic_update(struct drm_plane *plane,
				       struct drm_atomic_state *state)
{
	struct hyperv_drm_device *hv = to_hv(plane->dev);
	struct drm_plane_state *old_state = drm_atomic_get_old_plane_state(state, plane);
	struct drm_plane_state *new_state = drm_atomic_get_new_plane_state(state, plane);
	struct drm_shadow_plane_state *shadow_plane_state = to_drm_shadow_plane_state(new_state);
	struct drm_rect damage;
	struct drm_rect dst_clip;
	struct drm_atomic_helper_damage_iter iter;

	drm_atomic_helper_damage_iter_init(&iter, old_state, new_state);
	drm_atomic_for_each_plane_damage(&iter, &damage) {
		dst_clip = new_state->dst;

		if (!drm_rect_intersect(&dst_clip, &damage))
			continue;

		hyperv_blit_to_vram_rect(new_state->fb, &shadow_plane_state->data[0], &damage);
		hyperv_update_dirt(hv->hdev, &damage);
	}
}

static int hyperv_plane_get_scanout_buffer(struct drm_plane *plane,
					   struct drm_scanout_buffer *sb)
{
	struct hyperv_drm_device *hv = to_hv(plane->dev);
	struct iosys_map map = IOSYS_MAP_INIT_VADDR_IOMEM(hv->vram);

	if (plane->state && plane->state->fb) {
		sb->format = plane->state->fb->format;
		sb->width = plane->state->fb->width;
		sb->height = plane->state->fb->height;
		sb->pitch[0] = plane->state->fb->pitches[0];
		sb->map[0] = map;
		return 0;
	}
	return -ENODEV;
}

static void hyperv_plane_panic_flush(struct drm_plane *plane)
{
	struct hyperv_drm_device *hv = to_hv(plane->dev);
	struct drm_rect rect;

	if (!plane->state || !plane->state->fb)
		return;

	rect.x1 = 0;
	rect.y1 = 0;
	rect.x2 = plane->state->fb->width;
	rect.y2 = plane->state->fb->height;

	hyperv_update_dirt(hv->hdev, &rect);
}

static const struct drm_plane_helper_funcs hyperv_plane_helper_funcs = {
	DRM_GEM_SHADOW_PLANE_HELPER_FUNCS,
	.atomic_check = hyperv_plane_atomic_check,
	.atomic_update = hyperv_plane_atomic_update,
	.get_scanout_buffer = hyperv_plane_get_scanout_buffer,
	.panic_flush = hyperv_plane_panic_flush,
};

static const struct drm_plane_funcs hyperv_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= drm_plane_cleanup,
	DRM_GEM_SHADOW_PLANE_FUNCS,
};

static const struct drm_encoder_funcs hyperv_drm_simple_encoder_funcs_cleanup = {
	.destroy = drm_encoder_cleanup,
};

static inline int hyperv_pipe_init(struct hyperv_drm_device *hv)
{
	struct drm_device *dev = &hv->dev;
	struct drm_encoder *encoder = &hv->encoder;
	struct drm_plane *plane = &hv->plane;
	struct drm_crtc *crtc = &hv->crtc;
	struct drm_connector *connector = &hv->connector;
	int ret;

	ret = drm_universal_plane_init(dev, plane, 0,
				       &hyperv_plane_funcs,
				       hyperv_formats, ARRAY_SIZE(hyperv_formats),
				       hyperv_modifiers,
				       DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret)
		return ret;
	drm_plane_helper_add(plane, &hyperv_plane_helper_funcs);
	drm_plane_enable_fb_damage_clips(plane);

	ret = drm_crtc_init_with_planes(dev, crtc, plane, NULL,
					&hyperv_crtc_funcs, NULL);
	if (ret)
		return ret;
	drm_crtc_helper_add(crtc, &hyperv_crtc_helper_funcs);

	encoder->possible_crtcs = drm_crtc_mask(crtc);
	ret = drm_encoder_init(dev, encoder,
			       &hyperv_drm_simple_encoder_funcs_cleanup,
			       DRM_MODE_ENCODER_NONE, NULL);
	if (ret)
		return ret;

	ret = hyperv_conn_init(hv);
	if (ret) {
		drm_err(dev, "Failed to initialized connector.\n");
		return ret;
	}

	return drm_connector_attach_encoder(connector, encoder);
}

static enum drm_mode_status
hyperv_mode_valid(struct drm_device *dev,
		  const struct drm_display_mode *mode)
{
	struct hyperv_drm_device *hv = to_hv(dev);

	if (hyperv_check_size(hv, mode->hdisplay, mode->vdisplay, NULL))
		return MODE_BAD;

	return MODE_OK;
}

static const struct drm_mode_config_funcs hyperv_mode_config_funcs = {
	.fb_create = drm_gem_fb_create_with_dirty,
	.mode_valid = hyperv_mode_valid,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

int hyperv_mode_config_init(struct hyperv_drm_device *hv)
{
	struct drm_device *dev = &hv->dev;
	int ret;

	ret = drmm_mode_config_init(dev);
	if (ret) {
		drm_err(dev, "Failed to initialized mode setting.\n");
		return ret;
	}

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	dev->mode_config.max_width = hv->screen_width_max;
	dev->mode_config.max_height = hv->screen_height_max;

	dev->mode_config.preferred_depth = hv->screen_depth;
	dev->mode_config.prefer_shadow = 0;

	dev->mode_config.funcs = &hyperv_mode_config_funcs;

	ret = hyperv_pipe_init(hv);
	if (ret) {
		drm_err(dev, "Failed to initialized pipe.\n");
		return ret;
	}

	drm_mode_config_reset(dev);

	return 0;
}
