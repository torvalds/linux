/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "bochs.h"
#include <drm/drm_atomic_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_probe_helper.h>

static int defx = 1024;
static int defy = 768;

module_param(defx, int, 0444);
module_param(defy, int, 0444);
MODULE_PARM_DESC(defx, "default x resolution");
MODULE_PARM_DESC(defy, "default y resolution");

/* ---------------------------------------------------------------------- */

static void bochs_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	struct bochs_device *bochs =
		container_of(crtc, struct bochs_device, crtc);

	bochs_hw_setmode(bochs, &crtc->mode);
}

static void bochs_crtc_atomic_enable(struct drm_crtc *crtc,
				     struct drm_crtc_state *old_crtc_state)
{
}

static void bochs_crtc_atomic_flush(struct drm_crtc *crtc,
				    struct drm_crtc_state *old_crtc_state)
{
	struct drm_device *dev = crtc->dev;
	struct drm_pending_vblank_event *event;

	if (crtc->state && crtc->state->event) {
		unsigned long irqflags;

		spin_lock_irqsave(&dev->event_lock, irqflags);
		event = crtc->state->event;
		crtc->state->event = NULL;
		drm_crtc_send_vblank_event(crtc, event);
		spin_unlock_irqrestore(&dev->event_lock, irqflags);
	}
}


/* These provide the minimum set of functions required to handle a CRTC */
static const struct drm_crtc_funcs bochs_crtc_funcs = {
	.set_config = drm_atomic_helper_set_config,
	.destroy = drm_crtc_cleanup,
	.page_flip = drm_atomic_helper_page_flip,
	.reset = drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
};

static const struct drm_crtc_helper_funcs bochs_helper_funcs = {
	.mode_set_nofb = bochs_crtc_mode_set_nofb,
	.atomic_enable = bochs_crtc_atomic_enable,
	.atomic_flush = bochs_crtc_atomic_flush,
};

static const uint32_t bochs_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_BGRX8888,
};

static void bochs_plane_atomic_update(struct drm_plane *plane,
				      struct drm_plane_state *old_state)
{
	struct bochs_device *bochs = plane->dev->dev_private;
	struct bochs_bo *bo;

	if (!plane->state->fb)
		return;
	bo = gem_to_bochs_bo(plane->state->fb->obj[0]);
	bochs_hw_setbase(bochs,
			 plane->state->crtc_x,
			 plane->state->crtc_y,
			 bo->bo.offset);
	bochs_hw_setformat(bochs, plane->state->fb->format);
}

static int bochs_plane_prepare_fb(struct drm_plane *plane,
				struct drm_plane_state *new_state)
{
	struct bochs_bo *bo;

	if (!new_state->fb)
		return 0;
	bo = gem_to_bochs_bo(new_state->fb->obj[0]);
	return bochs_bo_pin(bo, TTM_PL_FLAG_VRAM);
}

static void bochs_plane_cleanup_fb(struct drm_plane *plane,
				   struct drm_plane_state *old_state)
{
	struct bochs_bo *bo;

	if (!old_state->fb)
		return;
	bo = gem_to_bochs_bo(old_state->fb->obj[0]);
	bochs_bo_unpin(bo);
}

static const struct drm_plane_helper_funcs bochs_plane_helper_funcs = {
	.atomic_update = bochs_plane_atomic_update,
	.prepare_fb = bochs_plane_prepare_fb,
	.cleanup_fb = bochs_plane_cleanup_fb,
};

static const struct drm_plane_funcs bochs_plane_funcs = {
       .update_plane   = drm_atomic_helper_update_plane,
       .disable_plane  = drm_atomic_helper_disable_plane,
       .destroy        = drm_primary_helper_destroy,
       .reset          = drm_atomic_helper_plane_reset,
       .atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
       .atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
};

static struct drm_plane *bochs_primary_plane(struct drm_device *dev)
{
	struct drm_plane *primary;
	int ret;

	primary = kzalloc(sizeof(*primary), GFP_KERNEL);
	if (primary == NULL) {
		DRM_DEBUG_KMS("Failed to allocate primary plane\n");
		return NULL;
	}

	ret = drm_universal_plane_init(dev, primary, 0,
				       &bochs_plane_funcs,
				       bochs_formats,
				       ARRAY_SIZE(bochs_formats),
				       NULL,
				       DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret) {
		kfree(primary);
		return NULL;
	}

	drm_plane_helper_add(primary, &bochs_plane_helper_funcs);
	return primary;
}

static void bochs_crtc_init(struct drm_device *dev)
{
	struct bochs_device *bochs = dev->dev_private;
	struct drm_crtc *crtc = &bochs->crtc;
	struct drm_plane *primary = bochs_primary_plane(dev);

	drm_crtc_init_with_planes(dev, crtc, primary, NULL,
				  &bochs_crtc_funcs, NULL);
	drm_crtc_helper_add(crtc, &bochs_helper_funcs);
}

static const struct drm_encoder_funcs bochs_encoder_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static void bochs_encoder_init(struct drm_device *dev)
{
	struct bochs_device *bochs = dev->dev_private;
	struct drm_encoder *encoder = &bochs->encoder;

	encoder->possible_crtcs = 0x1;
	drm_encoder_init(dev, encoder, &bochs_encoder_encoder_funcs,
			 DRM_MODE_ENCODER_DAC, NULL);
}


static int bochs_connector_get_modes(struct drm_connector *connector)
{
	struct bochs_device *bochs =
		container_of(connector, struct bochs_device, connector);
	int count = 0;

	if (bochs->edid)
		count = drm_add_edid_modes(connector, bochs->edid);

	if (!count) {
		count = drm_add_modes_noedid(connector, 8192, 8192);
		drm_set_preferred_mode(connector, defx, defy);
	}
	return count;
}

static enum drm_mode_status bochs_connector_mode_valid(struct drm_connector *connector,
				      struct drm_display_mode *mode)
{
	struct bochs_device *bochs =
		container_of(connector, struct bochs_device, connector);
	unsigned long size = mode->hdisplay * mode->vdisplay * 4;

	/*
	 * Make sure we can fit two framebuffers into video memory.
	 * This allows up to 1600x1200 with 16 MB (default size).
	 * If you want more try this:
	 *     'qemu -vga std -global VGA.vgamem_mb=32 $otherargs'
	 */
	if (size * 2 > bochs->fb_size)
		return MODE_BAD;

	return MODE_OK;
}

static const struct drm_connector_helper_funcs bochs_connector_connector_helper_funcs = {
	.get_modes = bochs_connector_get_modes,
	.mode_valid = bochs_connector_mode_valid,
};

static const struct drm_connector_funcs bochs_connector_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static void bochs_connector_init(struct drm_device *dev)
{
	struct bochs_device *bochs = dev->dev_private;
	struct drm_connector *connector = &bochs->connector;

	drm_connector_init(dev, connector, &bochs_connector_connector_funcs,
			   DRM_MODE_CONNECTOR_VIRTUAL);
	drm_connector_helper_add(connector,
				 &bochs_connector_connector_helper_funcs);
	drm_connector_register(connector);

	bochs_hw_load_edid(bochs);
	if (bochs->edid) {
		DRM_INFO("Found EDID data blob.\n");
		drm_connector_attach_edid_property(connector);
		drm_connector_update_edid_property(connector, bochs->edid);
	}
}

static struct drm_framebuffer *
bochs_gem_fb_create(struct drm_device *dev, struct drm_file *file,
		    const struct drm_mode_fb_cmd2 *mode_cmd)
{
	if (mode_cmd->pixel_format != DRM_FORMAT_XRGB8888 &&
	    mode_cmd->pixel_format != DRM_FORMAT_BGRX8888)
		return ERR_PTR(-EINVAL);

	return drm_gem_fb_create(dev, file, mode_cmd);
}

const struct drm_mode_config_funcs bochs_mode_funcs = {
	.fb_create = bochs_gem_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

int bochs_kms_init(struct bochs_device *bochs)
{
	drm_mode_config_init(bochs->dev);
	bochs->mode_config_initialized = true;

	bochs->dev->mode_config.max_width = 8192;
	bochs->dev->mode_config.max_height = 8192;

	bochs->dev->mode_config.fb_base = bochs->fb_base;
	bochs->dev->mode_config.preferred_depth = 24;
	bochs->dev->mode_config.prefer_shadow = 0;
	bochs->dev->mode_config.quirk_addfb_prefer_host_byte_order = true;

	bochs->dev->mode_config.funcs = &bochs_mode_funcs;

	bochs_crtc_init(bochs->dev);
	bochs_encoder_init(bochs->dev);
	bochs_connector_init(bochs->dev);
	drm_connector_attach_encoder(&bochs->connector,
					  &bochs->encoder);

	drm_mode_config_reset(bochs->dev);

	return 0;
}

void bochs_kms_fini(struct bochs_device *bochs)
{
	if (bochs->mode_config_initialized) {
		drm_atomic_helper_shutdown(bochs->dev);
		drm_mode_config_cleanup(bochs->dev);
		bochs->mode_config_initialized = false;
	}
}
