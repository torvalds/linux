// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2021 Microsoft
 */

#include <linux/hyperv.h>

#include <drm/drm_damage_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_format_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include "hyperv_drm.h"

static int hyperv_blit_to_vram_rect(struct drm_framebuffer *fb,
				    const struct dma_buf_map *map,
				    struct drm_rect *rect)
{
	struct hyperv_drm_device *hv = to_hv(fb->dev);
	void *vmap = map->vaddr; /* TODO: Use mapping abstraction properly */
	int idx;

	if (!drm_dev_enter(&hv->dev, &idx))
		return -ENODEV;

	drm_fb_memcpy_dstclip(hv->vram, fb->pitches[0], vmap, fb, rect);
	drm_dev_exit(idx);

	return 0;
}

static int hyperv_blit_to_vram_fullscreen(struct drm_framebuffer *fb, const struct dma_buf_map *map)
{
	struct drm_rect fullscreen = {
		.x1 = 0,
		.x2 = fb->width,
		.y1 = 0,
		.y2 = fb->height,
	};
	return hyperv_blit_to_vram_rect(fb, map, &fullscreen);
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

static void hyperv_pipe_enable(struct drm_simple_display_pipe *pipe,
			       struct drm_crtc_state *crtc_state,
			       struct drm_plane_state *plane_state)
{
	struct hyperv_drm_device *hv = to_hv(pipe->crtc.dev);
	struct drm_shadow_plane_state *shadow_plane_state = to_drm_shadow_plane_state(plane_state);

	hyperv_hide_hw_ptr(hv->hdev);
	hyperv_update_situation(hv->hdev, 1,  hv->screen_depth,
				crtc_state->mode.hdisplay,
				crtc_state->mode.vdisplay,
				plane_state->fb->pitches[0]);
	hyperv_blit_to_vram_fullscreen(plane_state->fb, &shadow_plane_state->data[0]);
}

static int hyperv_pipe_check(struct drm_simple_display_pipe *pipe,
			     struct drm_plane_state *plane_state,
			     struct drm_crtc_state *crtc_state)
{
	struct hyperv_drm_device *hv = to_hv(pipe->crtc.dev);
	struct drm_framebuffer *fb = plane_state->fb;

	if (fb->format->format != DRM_FORMAT_XRGB8888)
		return -EINVAL;

	if (fb->pitches[0] * fb->height > hv->fb_size)
		return -EINVAL;

	return 0;
}

static void hyperv_pipe_update(struct drm_simple_display_pipe *pipe,
			       struct drm_plane_state *old_state)
{
	struct hyperv_drm_device *hv = to_hv(pipe->crtc.dev);
	struct drm_plane_state *state = pipe->plane.state;
	struct drm_shadow_plane_state *shadow_plane_state = to_drm_shadow_plane_state(state);
	struct drm_rect rect;

	if (drm_atomic_helper_damage_merged(old_state, state, &rect)) {
		hyperv_blit_to_vram_rect(state->fb, &shadow_plane_state->data[0], &rect);
		hyperv_update_dirt(hv->hdev, &rect);
	}
}

static const struct drm_simple_display_pipe_funcs hyperv_pipe_funcs = {
	.enable	= hyperv_pipe_enable,
	.check = hyperv_pipe_check,
	.update	= hyperv_pipe_update,
	DRM_GEM_SIMPLE_DISPLAY_PIPE_SHADOW_PLANE_FUNCS,
};

static const uint32_t hyperv_formats[] = {
	DRM_FORMAT_XRGB8888,
};

static const uint64_t hyperv_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID
};

static inline int hyperv_pipe_init(struct hyperv_drm_device *hv)
{
	int ret;

	ret = drm_simple_display_pipe_init(&hv->dev,
					   &hv->pipe,
					   &hyperv_pipe_funcs,
					   hyperv_formats,
					   ARRAY_SIZE(hyperv_formats),
					   hyperv_modifiers,
					   &hv->connector);
	if (ret)
		return ret;

	drm_plane_enable_fb_damage_clips(&hv->pipe.plane);

	return 0;
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

	ret = hyperv_conn_init(hv);
	if (ret) {
		drm_err(dev, "Failed to initialized connector.\n");
		return ret;
	}

	ret = hyperv_pipe_init(hv);
	if (ret) {
		drm_err(dev, "Failed to initialized pipe.\n");
		return ret;
	}

	drm_mode_config_reset(dev);

	return 0;
}
