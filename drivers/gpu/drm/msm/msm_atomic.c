/*
 * Copyright (C) 2014 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <drm/drm_atomic_uapi.h>

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
	struct drm_gem_object *obj;
	struct msm_gem_object *msm_obj;
	struct dma_fence *fence;

	if (!new_state->fb)
		return 0;

	obj = msm_framebuffer_bo(new_state->fb, 0);
	msm_obj = to_msm_bo(obj);
	fence = reservation_object_get_excl_rcu(msm_obj->resv);

	drm_atomic_set_fence_for_plane(new_state, fence);

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

	msm_atomic_wait_for_commit_done(dev, state);

	kms->funcs->complete_commit(kms, state);

	drm_atomic_helper_commit_hw_done(state);

	drm_atomic_helper_cleanup_planes(dev, state);
}
