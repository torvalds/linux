/*
 * Copyright (C) 2016 Noralf Trønnes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <linux/slab.h>

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

static int drm_simple_kms_crtc_check(struct drm_crtc *crtc,
				     struct drm_crtc_state *state)
{
	bool has_primary = state->plane_mask &
			   BIT(drm_plane_index(crtc->primary));

	/* We always want to have an active plane with an active CRTC */
	if (has_primary != state->enable)
		return -EINVAL;

	return drm_atomic_add_affected_planes(state->state, crtc);
}

static void drm_simple_kms_crtc_enable(struct drm_crtc *crtc,
				       struct drm_crtc_state *old_state)
{
	struct drm_simple_display_pipe *pipe;

	pipe = container_of(crtc, struct drm_simple_display_pipe, crtc);
	if (!pipe->funcs || !pipe->funcs->enable)
		return;

	pipe->funcs->enable(pipe, crtc->state);
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
	.atomic_check = drm_simple_kms_crtc_check,
	.atomic_enable = drm_simple_kms_crtc_enable,
	.atomic_disable = drm_simple_kms_crtc_disable,
};

static const struct drm_crtc_funcs drm_simple_kms_crtc_funcs = {
	.reset = drm_atomic_helper_crtc_reset,
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
};

static int drm_simple_kms_plane_atomic_check(struct drm_plane *plane,
					struct drm_plane_state *plane_state)
{
	struct drm_rect clip = { 0 };
	struct drm_simple_display_pipe *pipe;
	struct drm_crtc_state *crtc_state;
	int ret;

	pipe = container_of(plane, struct drm_simple_display_pipe, plane);
	crtc_state = drm_atomic_get_new_crtc_state(plane_state->state,
						   &pipe->crtc);
	if (!crtc_state->enable)
		return 0; /* nothing to check when disabling or disabled */

	clip.x2 = crtc_state->adjusted_mode.hdisplay;
	clip.y2 = crtc_state->adjusted_mode.vdisplay;

	ret = drm_plane_helper_check_state(plane_state, crtc_state, &clip,
					   DRM_PLANE_HELPER_NO_SCALING,
					   DRM_PLANE_HELPER_NO_SCALING,
					   false, true);
	if (ret)
		return ret;

	if (!plane_state->visible)
		return -EINVAL;

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

	encoder->possible_crtcs = 1 << drm_crtc_index(crtc);
	ret = drm_encoder_init(dev, encoder, &drm_simple_kms_encoder_funcs,
			       DRM_MODE_ENCODER_NONE, NULL);
	if (ret || !connector)
		return ret;

	return drm_mode_connector_attach_encoder(connector, encoder);
}
EXPORT_SYMBOL(drm_simple_display_pipe_init);

MODULE_LICENSE("GPL");
