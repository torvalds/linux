// SPDX-License-Identifier: GPL-2.0-or-later
/*
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

static const uint32_t bochs_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_BGRX8888,
};

static void bochs_plane_update(struct bochs_device *bochs,
			       struct drm_plane_state *state)
{
	struct drm_gem_vram_object *gbo;

	if (!state->fb || !bochs->stride)
		return;

	gbo = drm_gem_vram_of_gem(state->fb->obj[0]);
	bochs_hw_setbase(bochs,
			 state->crtc_x,
			 state->crtc_y,
			 state->fb->pitches[0],
			 state->fb->offsets[0] + gbo->bo.offset);
	bochs_hw_setformat(bochs, state->fb->format);
}

static void bochs_pipe_enable(struct drm_simple_display_pipe *pipe,
			      struct drm_crtc_state *crtc_state,
			      struct drm_plane_state *plane_state)
{
	struct bochs_device *bochs = pipe->crtc.dev->dev_private;

	bochs_hw_setmode(bochs, &crtc_state->mode);
	bochs_plane_update(bochs, plane_state);
}

static void bochs_pipe_update(struct drm_simple_display_pipe *pipe,
			      struct drm_plane_state *old_state)
{
	struct bochs_device *bochs = pipe->crtc.dev->dev_private;
	struct drm_crtc *crtc = &pipe->crtc;

	bochs_plane_update(bochs, pipe->plane.state);

	if (crtc->state->event) {
		spin_lock_irq(&crtc->dev->event_lock);
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
		spin_unlock_irq(&crtc->dev->event_lock);
	}
}

static int bochs_pipe_prepare_fb(struct drm_simple_display_pipe *pipe,
				 struct drm_plane_state *new_state)
{
	struct drm_gem_vram_object *gbo;

	if (!new_state->fb)
		return 0;
	gbo = drm_gem_vram_of_gem(new_state->fb->obj[0]);
	return drm_gem_vram_pin(gbo, DRM_GEM_VRAM_PL_FLAG_VRAM);
}

static void bochs_pipe_cleanup_fb(struct drm_simple_display_pipe *pipe,
				  struct drm_plane_state *old_state)
{
	struct drm_gem_vram_object *gbo;

	if (!old_state->fb)
		return;
	gbo = drm_gem_vram_of_gem(old_state->fb->obj[0]);
	drm_gem_vram_unpin(gbo);
}

static const struct drm_simple_display_pipe_funcs bochs_pipe_funcs = {
	.enable	    = bochs_pipe_enable,
	.update	    = bochs_pipe_update,
	.prepare_fb = bochs_pipe_prepare_fb,
	.cleanup_fb = bochs_pipe_cleanup_fb,
};

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

	bochs->dev->mode_config.max_width = 8192;
	bochs->dev->mode_config.max_height = 8192;

	bochs->dev->mode_config.fb_base = bochs->fb_base;
	bochs->dev->mode_config.preferred_depth = 24;
	bochs->dev->mode_config.prefer_shadow = 0;
	bochs->dev->mode_config.quirk_addfb_prefer_host_byte_order = true;

	bochs->dev->mode_config.funcs = &bochs_mode_funcs;

	bochs_connector_init(bochs->dev);
	drm_simple_display_pipe_init(bochs->dev,
				     &bochs->pipe,
				     &bochs_pipe_funcs,
				     bochs_formats,
				     ARRAY_SIZE(bochs_formats),
				     NULL,
				     &bochs->connector);

	drm_mode_config_reset(bochs->dev);

	return 0;
}

void bochs_kms_fini(struct bochs_device *bochs)
{
	drm_atomic_helper_shutdown(bochs->dev);
	drm_mode_config_cleanup(bochs->dev);
}
