// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <drm/drm_atomic_uapi.h>
#include <drm/drm_gem_framebuffer_helper.h>

#include "msm_drv.h"
#include "msm_gem.h"
#include "msm_kms.h"

static void msm_atomic_wait_for_commit_done(struct drm_device *dev,
		struct drm_atomic_state *old_state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *new_crtc_state;
	struct msm_drm_private *priv = old_state->dev->dev_private;
	struct msm_kms *kms = priv->kms;
	int i;

	for_each_new_crtc_in_state(old_state, crtc, new_crtc_state, i) {
		if (!new_crtc_state->active)
			continue;

		if (drm_crtc_vblank_get(crtc))
			continue;

		kms->funcs->wait_for_crtc_commit_done(kms, crtc);

		drm_crtc_vblank_put(crtc);
	}
}

int msm_atomic_prepare_fb(struct drm_plane *plane,
			  struct drm_plane_state *new_state)
{
	struct msm_drm_private *priv = plane->dev->dev_private;
	struct msm_kms *kms = priv->kms;

	if (!new_state->fb)
		return 0;

	drm_gem_fb_prepare_fb(plane, new_state);

	return msm_framebuffer_prepare(new_state->fb, kms->aspace);
}

void msm_atomic_commit_tail(struct drm_atomic_state *state)
{
	struct drm_device *dev = state->dev;
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_kms *kms = priv->kms;

	kms->funcs->prepare_commit(kms, state);

	drm_atomic_helper_commit_modeset_disables(dev, state);

	drm_atomic_helper_commit_planes(dev, state, 0);

	drm_atomic_helper_commit_modeset_enables(dev, state);

	if (kms->funcs->commit) {
		DRM_DEBUG_ATOMIC("triggering commit\n");
		kms->funcs->commit(kms, state);
	}

	if (!state->legacy_cursor_update)
		msm_atomic_wait_for_commit_done(dev, state);

	kms->funcs->complete_commit(kms, state);

	drm_atomic_helper_commit_hw_done(state);

	drm_atomic_helper_cleanup_planes(dev, state);
}
