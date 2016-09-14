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
#include "msm_fence.h"

struct msm_commit {
	struct drm_device *dev;
	struct drm_atomic_state *state;
	struct work_struct work;
	uint32_t crtc_mask;
};

static void commit_worker(struct work_struct *work);

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

static struct msm_commit *commit_init(struct drm_atomic_state *state)
{
	struct msm_commit *c = kzalloc(sizeof(*c), GFP_KERNEL);

	if (!c)
		return NULL;

	c->dev = state->dev;
	c->state = state;

	INIT_WORK(&c->work, commit_worker);

	return c;
}

static void commit_destroy(struct msm_commit *c)
{
	end_atomic(c->dev->dev_private, c->crtc_mask);
	kfree(c);
}

static void msm_atomic_wait_for_commit_done(struct drm_device *dev,
		struct drm_atomic_state *old_state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	struct msm_drm_private *priv = old_state->dev->dev_private;
	struct msm_kms *kms = priv->kms;
	int i;

	for_each_crtc_in_state(old_state, crtc, crtc_state, i) {
		if (!crtc->state->enable)
			continue;

		/* Legacy cursor ioctls are completely unsynced, and userspace
		 * relies on that (by doing tons of cursor updates). */
		if (old_state->legacy_cursor_update)
			continue;

		kms->funcs->wait_for_crtc_commit_done(kms, crtc);
	}
}

/* The (potentially) asynchronous part of the commit.  At this point
 * nothing can fail short of armageddon.
 */
static void complete_commit(struct msm_commit *c, bool async)
{
	struct drm_atomic_state *state = c->state;
	struct drm_device *dev = state->dev;
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_kms *kms = priv->kms;

	drm_atomic_helper_wait_for_fences(dev, state);

	kms->funcs->prepare_commit(kms, state);

	drm_atomic_helper_commit_modeset_disables(dev, state);

	drm_atomic_helper_commit_planes(dev, state, false);

	drm_atomic_helper_commit_modeset_enables(dev, state);

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

	msm_atomic_wait_for_commit_done(dev, state);

	drm_atomic_helper_cleanup_planes(dev, state);

	kms->funcs->complete_commit(kms, state);

	drm_atomic_state_free(state);

	commit_destroy(c);
}

static void commit_worker(struct work_struct *work)
{
	complete_commit(container_of(work, struct msm_commit, work), true);
}

int msm_atomic_check(struct drm_device *dev,
		     struct drm_atomic_state *state)
{
	int ret;

	/*
	 * msm ->atomic_check can update ->mode_changed for pixel format
	 * changes, hence must be run before we check the modeset changes.
	 */
	ret = drm_atomic_helper_check_planes(dev, state);
	if (ret)
		return ret;

	ret = drm_atomic_helper_check_modeset(dev, state);
	if (ret)
		return ret;

	return ret;
}

/**
 * drm_atomic_helper_commit - commit validated state object
 * @dev: DRM device
 * @state: the driver state object
 * @nonblock: nonblocking commit
 *
 * This function commits a with drm_atomic_helper_check() pre-validated state
 * object. This can still fail when e.g. the framebuffer reservation fails.
 *
 * RETURNS
 * Zero for success or -errno.
 */
int msm_atomic_commit(struct drm_device *dev,
		struct drm_atomic_state *state, bool nonblock)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_commit *c;
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	struct drm_plane *plane;
	struct drm_plane_state *plane_state;
	int i, ret;

	ret = drm_atomic_helper_prepare_planes(dev, state);
	if (ret)
		return ret;

	c = commit_init(state);
	if (!c) {
		ret = -ENOMEM;
		goto error;
	}

	/*
	 * Figure out what crtcs we have:
	 */
	for_each_crtc_in_state(state, crtc, crtc_state, i)
		c->crtc_mask |= drm_crtc_mask(crtc);

	/*
	 * Figure out what fence to wait for:
	 */
	for_each_plane_in_state(state, plane, plane_state, i) {
		if ((plane->state->fb != plane_state->fb) && plane_state->fb) {
			struct drm_gem_object *obj = msm_framebuffer_bo(plane_state->fb, 0);
			struct msm_gem_object *msm_obj = to_msm_bo(obj);

			plane_state->fence = reservation_object_get_excl_rcu(msm_obj->resv);
		}
	}

	/*
	 * Wait for pending updates on any of the same crtc's and then
	 * mark our set of crtc's as busy:
	 */
	ret = start_atomic(dev->dev_private, c->crtc_mask);
	if (ret) {
		kfree(c);
		goto error;
	}

	/*
	 * This is the point of no return - everything below never fails except
	 * when the hw goes bonghits. Which means we can commit the new state on
	 * the software side now.
	 */

	drm_atomic_helper_swap_state(state, true);

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

	if (nonblock) {
		queue_work(priv->atomic_wq, &c->work);
		return 0;
	}

	complete_commit(c, false);

	return 0;

error:
	drm_atomic_helper_cleanup_planes(dev, state);
	return ret;
}
