// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <drm/drm_atomic_uapi.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_vblank.h>

#include "msm_atomic_trace.h"
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

static void msm_atomic_async_commit(struct msm_kms *kms, int crtc_idx)
{
	unsigned crtc_mask = BIT(crtc_idx);

	trace_msm_atomic_async_commit_start(crtc_mask);

	mutex_lock(&kms->commit_lock);

	if (!(kms->pending_crtc_mask & crtc_mask)) {
		mutex_unlock(&kms->commit_lock);
		goto out;
	}

	kms->pending_crtc_mask &= ~crtc_mask;

	kms->funcs->enable_commit(kms);

	/*
	 * Flush hardware updates:
	 */
	trace_msm_atomic_flush_commit(crtc_mask);
	kms->funcs->flush_commit(kms, crtc_mask);
	mutex_unlock(&kms->commit_lock);

	/*
	 * Wait for flush to complete:
	 */
	trace_msm_atomic_wait_flush_start(crtc_mask);
	kms->funcs->wait_flush(kms, crtc_mask);
	trace_msm_atomic_wait_flush_finish(crtc_mask);

	mutex_lock(&kms->commit_lock);
	kms->funcs->complete_commit(kms, crtc_mask);
	mutex_unlock(&kms->commit_lock);
	kms->funcs->disable_commit(kms);

out:
	trace_msm_atomic_async_commit_finish(crtc_mask);
}

static enum hrtimer_restart msm_atomic_pending_timer(struct hrtimer *t)
{
	struct msm_pending_timer *timer = container_of(t,
			struct msm_pending_timer, timer);
	struct msm_drm_private *priv = timer->kms->dev->dev_private;

	queue_work(priv->wq, &timer->work);

	return HRTIMER_NORESTART;
}

static void msm_atomic_pending_work(struct work_struct *work)
{
	struct msm_pending_timer *timer = container_of(work,
			struct msm_pending_timer, work);

	msm_atomic_async_commit(timer->kms, timer->crtc_idx);
}

void msm_atomic_init_pending_timer(struct msm_pending_timer *timer,
		struct msm_kms *kms, int crtc_idx)
{
	timer->kms = kms;
	timer->crtc_idx = crtc_idx;
	hrtimer_init(&timer->timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	timer->timer.function = msm_atomic_pending_timer;
	INIT_WORK(&timer->work, msm_atomic_pending_work);
}

static bool can_do_async(struct drm_atomic_state *state,
		struct drm_crtc **async_crtc)
{
	struct drm_connector_state *connector_state;
	struct drm_connector *connector;
	struct drm_crtc_state *crtc_state;
	struct drm_crtc *crtc;
	int i, num_crtcs = 0;

	if (!(state->legacy_cursor_update || state->async_update))
		return false;

	/* any connector change, means slow path: */
	for_each_new_connector_in_state(state, connector, connector_state, i)
		return false;

	for_each_new_crtc_in_state(state, crtc, crtc_state, i) {
		if (drm_atomic_crtc_needs_modeset(crtc_state))
			return false;
		if (++num_crtcs > 1)
			return false;
		*async_crtc = crtc;
	}

	return true;
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
	struct drm_crtc *async_crtc = NULL;
	unsigned crtc_mask = get_crtc_mask(state);
	bool async = kms->funcs->vsync_time &&
			can_do_async(state, &async_crtc);

	trace_msm_atomic_commit_tail_start(async, crtc_mask);

	kms->funcs->enable_commit(kms);

	/*
	 * Ensure any previous (potentially async) commit has
	 * completed:
	 */
	trace_msm_atomic_wait_flush_start(crtc_mask);
	kms->funcs->wait_flush(kms, crtc_mask);
	trace_msm_atomic_wait_flush_finish(crtc_mask);

	mutex_lock(&kms->commit_lock);

	/*
	 * Now that there is no in-progress flush, prepare the
	 * current update:
	 */
	kms->funcs->prepare_commit(kms, state);

	/*
	 * Push atomic updates down to hardware:
	 */
	drm_atomic_helper_commit_modeset_disables(dev, state);
	drm_atomic_helper_commit_planes(dev, state, 0);
	drm_atomic_helper_commit_modeset_enables(dev, state);

	if (async) {
		struct msm_pending_timer *timer =
			&kms->pending_timers[drm_crtc_index(async_crtc)];

		/* async updates are limited to single-crtc updates: */
		WARN_ON(crtc_mask != drm_crtc_mask(async_crtc));

		/*
		 * Start timer if we don't already have an update pending
		 * on this crtc:
		 */
		if (!(kms->pending_crtc_mask & crtc_mask)) {
			ktime_t vsync_time, wakeup_time;

			kms->pending_crtc_mask |= crtc_mask;

			vsync_time = kms->funcs->vsync_time(kms, async_crtc);
			wakeup_time = ktime_sub(vsync_time, ms_to_ktime(1));

			hrtimer_start(&timer->timer, wakeup_time,
					HRTIMER_MODE_ABS);
		}

		kms->funcs->disable_commit(kms);
		mutex_unlock(&kms->commit_lock);

		/*
		 * At this point, from drm core's perspective, we
		 * are done with the atomic update, so we can just
		 * go ahead and signal that it is done:
		 */
		drm_atomic_helper_commit_hw_done(state);
		drm_atomic_helper_cleanup_planes(dev, state);

		trace_msm_atomic_commit_tail_finish(async, crtc_mask);

		return;
	}

	/*
	 * If there is any async flush pending on updated crtcs, fold
	 * them into the current flush.
	 */
	kms->pending_crtc_mask &= ~crtc_mask;

	/*
	 * Flush hardware updates:
	 */
	trace_msm_atomic_flush_commit(crtc_mask);
	kms->funcs->flush_commit(kms, crtc_mask);
	mutex_unlock(&kms->commit_lock);

	/*
	 * Wait for flush to complete:
	 */
	trace_msm_atomic_wait_flush_start(crtc_mask);
	kms->funcs->wait_flush(kms, crtc_mask);
	trace_msm_atomic_wait_flush_finish(crtc_mask);

	mutex_lock(&kms->commit_lock);
	kms->funcs->complete_commit(kms, crtc_mask);
	mutex_unlock(&kms->commit_lock);
	kms->funcs->disable_commit(kms);

	drm_atomic_helper_commit_hw_done(state);
	drm_atomic_helper_cleanup_planes(dev, state);

	trace_msm_atomic_commit_tail_finish(async, crtc_mask);
}
