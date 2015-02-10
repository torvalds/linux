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

#include "msm_drv.h"
#include "msm_kms.h"
#include "msm_gem.h"

struct msm_commit {
	struct drm_atomic_state *state;
	uint32_t fence;
	struct msm_fence_cb fence_cb;
	uint32_t crtc_mask;
};

static void fence_cb(struct msm_fence_cb *cb);

/* block until specified crtcs are no longer pending update, and
 * atomically mark them as pending update
 */
static int start_atomic(struct msm_drm_private *priv, uint32_t crtc_mask)
{
	int ret;

	spin_lock(&priv->pending_crtcs_event.lock);
	ret = wait_event_interruptible_locked(priv->pending_crtcs_event,
			!(priv->pending_crtcs & crtc_mask));
	if (ret == 0) {
		DBG("start: %08x", crtc_mask);
		priv->pending_crtcs |= crtc_mask;
	}
	spin_unlock(&priv->pending_crtcs_event.lock);

	return ret;
}

/* clear specified crtcs (no longer pending update)
 */
static void end_atomic(struct msm_drm_private *priv, uint32_t crtc_mask)
{
	spin_lock(&priv->pending_crtcs_event.lock);
	DBG("end: %08x", crtc_mask);
	priv->pending_crtcs &= ~crtc_mask;
	wake_up_all_locked(&priv->pending_crtcs_event);
	spin_unlock(&priv->pending_crtcs_event.lock);
}

static struct msm_commit *new_commit(struct drm_atomic_state *state)
{
	struct msm_commit *c = kzalloc(sizeof(*c), GFP_KERNEL);

	if (!c)
		return NULL;

	c->state = state;
	/* TODO we might need a way to indicate to run the cb on a
	 * different wq so wait_for_vblanks() doesn't block retiring
	 * bo's..
	 */
	INIT_FENCE_CB(&c->fence_cb, fence_cb);

	return c;
}

/* The (potentially) asynchronous part of the commit.  At this point
 * nothing can fail short of armageddon.
 */
static void complete_commit(struct msm_commit *c)
{
	struct drm_atomic_state *state = c->state;
	struct drm_device *dev = state->dev;

	drm_atomic_helper_commit_pre_planes(dev, state);

	drm_atomic_helper_commit_planes(dev, state);

	drm_atomic_helper_commit_post_planes(dev, state);

	/* NOTE: _wait_for_vblanks() only waits for vblank on
	 * enabled CRTCs.  So we end up faulting when disabling
	 * due to (potentially) unref'ing the outgoing fb's
	 * before the vblank when the disable has latched.
	 *
	 * But if it did wait on disabled (or newly disabled)
	 * CRTCs, that would be racy (ie. we could have missed
	 * the irq.  We need some way to poll for pipe shut
	 * down.  Or just live with occasionally hitting the
	 * timeout in the CRTC disable path (which really should
	 * not be critical path)
	 */

	drm_atomic_helper_wait_for_vblanks(dev, state);

	drm_atomic_helper_cleanup_planes(dev, state);

	drm_atomic_state_free(state);

	end_atomic(dev->dev_private, c->crtc_mask);

	kfree(c);
}

static void fence_cb(struct msm_fence_cb *cb)
{
	struct msm_commit *c =
			container_of(cb, struct msm_commit, fence_cb);
	complete_commit(c);
}

static void add_fb(struct msm_commit *c, struct drm_framebuffer *fb)
{
	struct drm_gem_object *obj = msm_framebuffer_bo(fb, 0);
	c->fence = max(c->fence, msm_gem_fence(to_msm_bo(obj), MSM_PREP_READ));
}


/**
 * drm_atomic_helper_commit - commit validated state object
 * @dev: DRM device
 * @state: the driver state object
 * @async: asynchronous commit
 *
 * This function commits a with drm_atomic_helper_check() pre-validated state
 * object. This can still fail when e.g. the framebuffer reservation fails. For
 * now this doesn't implement asynchronous commits.
 *
 * RETURNS
 * Zero for success or -errno.
 */
int msm_atomic_commit(struct drm_device *dev,
		struct drm_atomic_state *state, bool async)
{
	int nplanes = dev->mode_config.num_total_plane;
	int ncrtcs = dev->mode_config.num_crtc;
	struct msm_commit *c;
	int i, ret;

	ret = drm_atomic_helper_prepare_planes(dev, state);
	if (ret)
		return ret;

	c = new_commit(state);
	if (!c)
		return -ENOMEM;

	/*
	 * Figure out what crtcs we have:
	 */
	for (i = 0; i < ncrtcs; i++) {
		struct drm_crtc *crtc = state->crtcs[i];
		if (!crtc)
			continue;
		c->crtc_mask |= (1 << drm_crtc_index(crtc));
	}

	/*
	 * Figure out what fence to wait for:
	 */
	for (i = 0; i < nplanes; i++) {
		struct drm_plane *plane = state->planes[i];
		struct drm_plane_state *new_state = state->plane_states[i];

		if (!plane)
			continue;

		if ((plane->state->fb != new_state->fb) && new_state->fb)
			add_fb(c, new_state->fb);
	}

	/*
	 * Wait for pending updates on any of the same crtc's and then
	 * mark our set of crtc's as busy:
	 */
	ret = start_atomic(dev->dev_private, c->crtc_mask);
	if (ret)
		return ret;

	/*
	 * This is the point of no return - everything below never fails except
	 * when the hw goes bonghits. Which means we can commit the new state on
	 * the software side now.
	 */

	drm_atomic_helper_swap_state(dev, state);

	/*
	 * Everything below can be run asynchronously without the need to grab
	 * any modeset locks at all under one conditions: It must be guaranteed
	 * that the asynchronous work has either been cancelled (if the driver
	 * supports it, which at least requires that the framebuffers get
	 * cleaned up with drm_atomic_helper_cleanup_planes()) or completed
	 * before the new state gets committed on the software side with
	 * drm_atomic_helper_swap_state().
	 *
	 * This scheme allows new atomic state updates to be prepared and
	 * checked in parallel to the asynchronous completion of the previous
	 * update. Which is important since compositors need to figure out the
	 * composition of the next frame right after having submitted the
	 * current layout.
	 */

	if (async) {
		msm_queue_fence_cb(dev, &c->fence_cb, c->fence);
		return 0;
	}

	ret = msm_wait_fence_interruptable(dev, c->fence, NULL);
	if (ret) {
		WARN_ON(ret);  // TODO unswap state back?  or??
		kfree(c);
		return ret;
	}

	complete_commit(c);

	return 0;
}
