// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 Noralf Trønnes
 */

#include <linux/module.h>
#include <linux/slab.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

/**
 * DOC: overview
 *
 * This helper library provides helpers for drivers for simple display
 * hardware.
 *
 * drm_simple_display_pipe_init() initializes a simple display pipeline
 * which has only one full-screen scanout buffer feeding one output. The
 * pipeline is represented by &struct drm_simple_display_pipe and binds
 * together &drm_plane, &drm_crtc and &drm_encoder structures into one fixed
 * entity. Some flexibility for code reuse is provided through a separately
 * allocated &drm_connector object and supporting optional &drm_bridge
 * encoder drivers.
 */

static const struct drm_encoder_funcs drm_simple_kms_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static enum drm_mode_status
drm_simple_kms_crtc_mode_valid(struct drm_crtc *crtc,
			       const struct drm_display_mode *mode)
{
	struct drm_simple_display_pipe *pipe;

	pipe = container_of(crtc, struct drm_simple_display_pipe, crtc);
	if (!pipe->funcs || !pipe->funcs->mode_valid)
		/* Anything goes */
		return MODE_OK;

	return pipe->funcs->mode_valid(crtc, mode);
}

static int drm_simple_kms_crtc_check(struct drm_crtc *crtc,
				     struct drm_crtc_state *state)
{
	bool has_primary = state->plane_mask &
			   drm_plane_mask(crtc->primary);

	/* We always want to have an active plane with an active CRTC */
	if (has_primary != state->enable)
		return -EINVAL;

	return drm_atomic_add_affected_planes(state->state, crtc);
}

static void drm_simple_kms_crtc_enable(struct drm_crtc *crtc,
				       struct drm_crtc_state *old_state)
{
	struct drm_plane *plane;
	struct drm_simple_display_pipe *pipe;

	pipe = container_of(crtc, struct drm_simple_display_pipe, crtc);
	if (!pipe->funcs || !pipe->funcs->enable)
		return;

	plane = &pipe->plane;
	pipe->funcs->enable(pipe, crtc->state, plane->state);
}

static void drm_simple_kms_crtc_disable(struct drm_crtc *crtc,
					struct drm_crtc_state *old_state)
{
	struct drm_simple_display_pipe *pipe;

	pipe = container_of(crtc, struct drm_simple_display_pipe, crtc);
	if (!pipe->funcs || !pipe->funcs->disable)
		return;

	pipe->funcs->disable(pipe);
}

static const struct drm_crtc_helper_funcs drm_simple_kms_crtc_helper_funcs = {
	.mode_valid = drm_simple_kms_crtc_mode_valid,
	.atomic_check = drm_simple_kms_crtc_check,
	.atomic_enable = drm_simple_kms_crtc_enable,
	.atomic_disable = drm_simple_kms_crtc_disable,
};

static int drm_simple_kms_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct drm_simple_display_pipe *pipe;

	pipe = container_of(crtc, struct drm_simple_display_pipe, crtc);
	if (!pipe->funcs || !pipe->funcs->enable_vblank)
		return 0;

	return pipe->funcs->enable_vblank(pipe);
}

static void drm_simple_kms_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct drm_simple_display_pipe *pipe;

	pipe = container_of(crtc, struct drm_simple_display_pipe, crtc);
	if (!pipe->funcs || !pipe->funcs->disable_vblank)
		return;

	pipe->funcs->disable_vblank(pipe);
}

static const struct drm_crtc_funcs drm_simple_kms_crtc_funcs = {
	.reset = drm_atomic_helper_crtc_reset,
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.enable_vblank = drm_simple_kms_crtc_enable_vblank,
	.disable_vblank = drm_simple_kms_crtc_disable_vblank,
};

static int drm_simple_kms_plane_atomic_check(struct drm_plane *plane,
					struct drm_plane_state *plane_state)
{
	struct drm_simple_display_pipe *pipe;
	struct drm_crtc_state *crtc_state;
	int ret;

	pipe = container_of(plane, struct drm_simple_display_pipe, plane);
	crtc_state = drm_atomic_get_new_crtc_state(plane_state->state,
						   &pipe->crtc);

	ret = drm_atomic_helper_check_plane_state(plane_state, crtc_state,
						  DRM_PLANE_HELPER_NO_SCALING,
						  DRM_PLANE_HELPER_NO_SCALING,
						  false, true);
	if (ret)
		return ret;

	if (!plane_state->visible)
		return 0;

	if (!pipe->funcs || !pipe->funcs->check)
		return 0;

	return pipe->funcs->check(pipe, plane_state, crtc_state);
}

static void drm_simple_kms_plane_atomic_update(struct drm_plane *plane,
					struct drm_plane_state *old_pstate)
{
	struct drm_simple_display_pipe *pipe;

	pipe = container_of(plane, struct drm_simple_display_pipe, plane);
	if (!pipe->funcs || !pipe->funcs->update)
		return;

	pipe->funcs->update(pipe, old_pstate);
}

static int drm_simple_kms_plane_prepare_fb(struct drm_plane *plane,
					   struct drm_plane_state *state)
{
	struct drm_simple_display_pipe *pipe;

	pipe = container_of(plane, struct drm_simple_display_pipe, plane);
	if (!pipe->funcs || !pipe->funcs->prepare_fb)
		return 0;

	return pipe->funcs->prepare_fb(pipe, state);
}

static void drm_simple_kms_plane_cleanup_fb(struct drm_plane *plane,
					    struct drm_plane_state *state)
{
	struct drm_simple_display_pipe *pipe;

	pipe = container_of(plane, struct drm_simple_display_pipe, plane);
	if (!pipe->funcs || !pipe->funcs->cleanup_fb)
		return;

	pipe->funcs->cleanup_fb(pipe, state);
}

static bool drm_simple_kms_format_mod_supported(struct drm_plane *plane,
						uint32_t format,
						uint64_t modifier)
{
	return modifier == DRM_FORMAT_MOD_LINEAR;
}

static const struct drm_plane_helper_funcs drm_simple_kms_plane_helper_funcs = {
	.prepare_fb = drm_simple_kms_plane_prepare_fb,
	.cleanup_fb = drm_simple_kms_plane_cleanup_fb,
	.atomic_check = drm_simple_kms_plane_atomic_check,
	.atomic_update = drm_simple_kms_plane_atomic_update,
};

static const struct drm_plane_funcs drm_simple_kms_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= drm_plane_cleanup,
	.reset			= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state	= drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
	.format_mod_supported   = drm_simple_kms_format_mod_supported,
};

/**
 * drm_simple_display_pipe_attach_bridge - Attach a bridge to the display pipe
 * @pipe: simple display pipe object
 * @bridge: bridge to attach
 *
 * Makes it possible to still use the drm_simple_display_pipe helpers when
 * a DRM bridge has to be used.
 *
 * Note that you probably want to initialize the pipe by passing a NULL
 * connector to drm_simple_display_pipe_init().
 *
 * Returns:
 * Zero on success, negative error code on failure.
 */
int drm_simple_display_pipe_attach_bridge(struct drm_simple_display_pipe *pipe,
					  struct drm_bridge *bridge)
{
	return drm_bridge_attach(&pipe->encoder, bridge, NULL);
}
EXPORT_SYMBOL(drm_simple_display_pipe_attach_bridge);

/**
 * drm_simple_display_pipe_init - Initialize a simple display pipeline
 * @dev: DRM device
 * @pipe: simple display pipe object to initialize
 * @funcs: callbacks for the display pipe (optional)
 * @formats: array of supported formats (DRM_FORMAT\_\*)
 * @format_count: number of elements in @formats
 * @format_modifiers: array of formats modifiers
 * @connector: connector to attach and register (optional)
 *
 * Sets up a display pipeline which consist of a really simple
 * plane-crtc-encoder pipe.
 *
 * If a connector is supplied, the pipe will be coupled with the provided
 * connector. You may supply a NULL connector when using drm bridges, that
 * handle connectors themselves (see drm_simple_display_pipe_attach_bridge()).
 *
 * Teardown of a simple display pipe is all handled automatically by the drm
 * core through calling drm_mode_config_cleanup(). Drivers afterwards need to
 * release the memory for the structure themselves.
 *
 * Returns:
 * Zero on success, negative error code on failure.
 */
int drm_simple_display_pipe_init(struct drm_device *dev,
			struct drm_simple_display_pipe *pipe,
			const struct drm_simple_display_pipe_funcs *funcs,
			const uint32_t *formats, unsigned int format_count,
			const uint64_t *format_modifiers,
			struct drm_connector *connector)
{
	struct drm_encoder *encoder = &pipe->encoder;
	struct drm_plane *plane = &pipe->plane;
	struct drm_crtc *crtc = &pipe->crtc;
	int ret;

	pipe->connector = connector;
	pipe->funcs = funcs;

	drm_plane_helper_add(plane, &drm_simple_kms_plane_helper_funcs);
	ret = drm_universal_plane_init(dev, plane, 0,
				       &drm_simple_kms_plane_funcs,
				       formats, format_count,
				       format_modifiers,
				       DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret)
		return ret;

	drm_crtc_helper_add(crtc, &drm_simple_kms_crtc_helper_funcs);
	ret = drm_crtc_init_with_planes(dev, crtc, plane, NULL,
					&drm_simple_kms_crtc_funcs, NULL);
	if (ret)
		return ret;

	encoder->possible_crtcs = drm_crtc_mask(crtc);
	ret = drm_encoder_init(dev, encoder, &drm_simple_kms_encoder_funcs,
			       DRM_MODE_ENCODER_NONE, NULL);
	if (ret || !connector)
		return ret;

	return drm_connector_attach_encoder(connector, encoder);
}
EXPORT_SYMBOL(drm_simple_display_pipe_init);

MODULE_LICENSE("GPL");
