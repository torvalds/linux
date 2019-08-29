// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <drm/drm_atomic_uapi.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_vblank.h>

#include "msm_drv.h"
#include "msm_gem.h"
#include "msm_kms.h"

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

/* Get bitmask of crtcs that will need to be flushed.  The bitmask
 * can be used with for_each_crtc_mask() iterator, to iterate
 * effected crtcs without needing to preserve the atomic state.
 */
static unsigned get_crtc_mask(struct drm_atomic_state *state)
{
	struct drm_crtc_state *crtc_state;
	struct drm_crtc *crtc;
	unsigned i, mask = 0;

	for_each_new_crtc_in_state(state, crtc, crtc_state, i)
		mask |= drm_crtc_mask(crtc);

	return mask;
}

void msm_atomic_commit_tail(struct drm_atomic_state *state)
{
	struct drm_device *dev = state->dev;
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_kms *kms = priv->kms;
	unsigned crtc_mask = get_crtc_mask(state);

	kms->funcs->prepare_commit(kms, state);

	drm_atomic_helper_commit_modeset_disables(dev, state);

	drm_atomic_helper_commit_planes(dev, state, 0);

	drm_atomic_helper_commit_modeset_enables(dev, state);

	if (kms->funcs->commit) {
		DRM_DEBUG_ATOMIC("triggering commit\n");
		kms->funcs->commit(kms, state);
	}

	kms->funcs->wait_flush(kms, crtc_mask);
	kms->funcs->complete_commit(kms, crtc_mask);

	drm_atomic_helper_commit_hw_done(state);

	drm_atomic_helper_cleanup_planes(dev, state);
}
