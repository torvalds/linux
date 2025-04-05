// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <drm/drm_atomic_uapi.h>
#include <drm/drm_vblank.h>

#include "msm_atomic_trace.h"
#include "msm_drv.h"
#include "msm_gem.h"
#include "msm_kms.h"

/*
 * Helpers to control vblanks while we flush.. basically just to ensure
 * that vblank accounting is switched on, so we get valid seqn/timestamp
 * on pageflip events (if requested)
 */

static void vblank_get(struct msm_kms *kms, unsigned crtc_mask)
{
	struct drm_crtc *crtc;

	for_each_crtc_mask(kms->dev, crtc, crtc_mask) {
		if (!crtc->state->active)
			continue;
		drm_crtc_vblank_get(crtc);
	}
}

static void vblank_put(struct msm_kms *kms, unsigned crtc_mask)
{
	struct drm_crtc *crtc;

	for_each_crtc_mask(kms->dev, crtc, crtc_mask) {
		if (!crtc->state->active)
			continue;
		drm_crtc_vblank_put(crtc);
	}
}

static void lock_crtcs(struct msm_kms *kms, unsigned int crtc_mask)
{
	int crtc_index;
	struct drm_crtc *crtc;

	for_each_crtc_mask(kms->dev, crtc, crtc_mask) {
		crtc_index = drm_crtc_index(crtc);
		mutex_lock_nested(&kms->commit_lock[crtc_index], crtc_index);
	}
}

static void unlock_crtcs(struct msm_kms *kms, unsigned int crtc_mask)
{
	struct drm_crtc *crtc;

	for_each_crtc_mask_reverse(kms->dev, crtc, crtc_mask)
		mutex_unlock(&kms->commit_lock[drm_crtc_index(crtc)]);
}

static void msm_atomic_async_commit(struct msm_kms *kms, int crtc_idx)
{
	unsigned crtc_mask = BIT(crtc_idx);

	trace_msm_atomic_async_commit_start(crtc_mask);

	lock_crtcs(kms, crtc_mask);

	if (!(kms->pending_crtc_mask & crtc_mask)) {
		unlock_crtcs(kms, crtc_mask);
		goto out;
	}

	kms->pending_crtc_mask &= ~crtc_mask;

	kms->funcs->enable_commit(kms);

	vblank_get(kms, crtc_mask);

	/*
	 * Flush hardware updates:
	 */
	trace_msm_atomic_flush_commit(crtc_mask);
	kms->funcs->flush_commit(kms, crtc_mask);

	/*
	 * Wait for flush to complete:
	 */
	trace_msm_atomic_wait_flush_start(crtc_mask);
	kms->funcs->wait_flush(kms, crtc_mask);
	trace_msm_atomic_wait_flush_finish(crtc_mask);

	vblank_put(kms, crtc_mask);

	kms->funcs->complete_commit(kms, crtc_mask);
	unlock_crtcs(kms, crtc_mask);
	kms->funcs->disable_commit(kms);

out:
	trace_msm_atomic_async_commit_finish(crtc_mask);
}

static void msm_atomic_pending_work(struct kthread_work *work)
{
	struct msm_pending_timer *timer = container_of(work,
			struct msm_pending_timer, work.work);

	msm_atomic_async_commit(timer->kms, timer->crtc_idx);
}

int msm_atomic_init_pending_timer(struct msm_pending_timer *timer,
		struct msm_kms *kms, int crtc_idx)
{
	timer->kms = kms;
	timer->crtc_idx = crtc_idx;

	timer->worker = kthread_run_worker(0, "atomic-worker-%d", crtc_idx);
	if (IS_ERR(timer->worker)) {
		int ret = PTR_ERR(timer->worker);
		timer->worker = NULL;
		return ret;
	}
	sched_set_fifo(timer->worker->task);

	msm_hrtimer_work_init(&timer->work, timer->worker,
			      msm_atomic_pending_work,
			      CLOCK_MONOTONIC, HRTIMER_MODE_ABS);

	return 0;
}

void msm_atomic_destroy_pending_timer(struct msm_pending_timer *timer)
{
	if (timer->worker)
		kthread_destroy_worker(timer->worker);
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
		if (!crtc_state->active)
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

int msm_atomic_check(struct drm_device *dev, struct drm_atomic_state *state)
{
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct drm_crtc *crtc;
	int i;

	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state,
				      new_crtc_state, i) {
		if ((old_crtc_state->ctm && !new_crtc_state->ctm) ||
		    (!old_crtc_state->ctm && new_crtc_state->ctm)) {
			new_crtc_state->mode_changed = true;
			state->allow_modeset = true;
		}
	}

	return drm_atomic_helper_check(dev, state);
}

void msm_atomic_commit_tail(struct drm_atomic_state *state)
{
	struct drm_device *dev = state->dev;
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_kms *kms = priv->kms;
	struct drm_crtc *async_crtc = NULL;
	unsigned crtc_mask = get_crtc_mask(state);
	bool async = can_do_async(state, &async_crtc);

	trace_msm_atomic_commit_tail_start(async, crtc_mask);

	kms->funcs->enable_commit(kms);

	/*
	 * Ensure any previous (potentially async) commit has
	 * completed:
	 */
	lock_crtcs(kms, crtc_mask);
	trace_msm_atomic_wait_flush_start(crtc_mask);
	kms->funcs->wait_flush(kms, crtc_mask);
	trace_msm_atomic_wait_flush_finish(crtc_mask);

	/*
	 * Now that there is no in-progress flush, prepare the
	 * current update:
	 */
	if (kms->funcs->prepare_commit)
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

			if (drm_crtc_next_vblank_start(async_crtc, &vsync_time))
				goto fallback;

			wakeup_time = ktime_sub(vsync_time, ms_to_ktime(1));

			msm_hrtimer_queue_work(&timer->work, wakeup_time,
					HRTIMER_MODE_ABS);
		}

		kms->funcs->disable_commit(kms);
		unlock_crtcs(kms, crtc_mask);
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

fallback:
	/*
	 * If there is any async flush pending on updated crtcs, fold
	 * them into the current flush.
	 */
	kms->pending_crtc_mask &= ~crtc_mask;

	vblank_get(kms, crtc_mask);

	/*
	 * Flush hardware updates:
	 */
	trace_msm_atomic_flush_commit(crtc_mask);
	kms->funcs->flush_commit(kms, crtc_mask);
	unlock_crtcs(kms, crtc_mask);
	/*
	 * Wait for flush to complete:
	 */
	trace_msm_atomic_wait_flush_start(crtc_mask);
	kms->funcs->wait_flush(kms, crtc_mask);
	trace_msm_atomic_wait_flush_finish(crtc_mask);

	vblank_put(kms, crtc_mask);

	lock_crtcs(kms, crtc_mask);
	kms->funcs->complete_commit(kms, crtc_mask);
	unlock_crtcs(kms, crtc_mask);
	kms->funcs->disable_commit(kms);

	drm_atomic_helper_commit_hw_done(state);
	drm_atomic_helper_cleanup_planes(dev, state);

	trace_msm_atomic_commit_tail_finish(async, crtc_mask);
}
