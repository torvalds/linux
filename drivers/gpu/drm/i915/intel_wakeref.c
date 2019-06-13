/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#include "intel_drv.h"
#include "intel_wakeref.h"

static void rpm_get(struct drm_i915_private *i915, struct intel_wakeref *wf)
{
	wf->wakeref = intel_runtime_pm_get(i915);
}

static void rpm_put(struct drm_i915_private *i915, struct intel_wakeref *wf)
{
	intel_wakeref_t wakeref = fetch_and_zero(&wf->wakeref);

	intel_runtime_pm_put(i915, wakeref);
	GEM_BUG_ON(!wakeref);
}

int __intel_wakeref_get_first(struct drm_i915_private *i915,
			      struct intel_wakeref *wf,
			      int (*fn)(struct intel_wakeref *wf))
{
	/*
	 * Treat get/put as different subclasses, as we may need to run
	 * the put callback from under the shrinker and do not want to
	 * cross-contanimate that callback with any extra work performed
	 * upon acquiring the wakeref.
	 */
	mutex_lock_nested(&wf->mutex, SINGLE_DEPTH_NESTING);
	if (!atomic_read(&wf->count)) {
		int err;

		rpm_get(i915, wf);

		err = fn(wf);
		if (unlikely(err)) {
			rpm_put(i915, wf);
			mutex_unlock(&wf->mutex);
			return err;
		}

		smp_mb__before_atomic(); /* release wf->count */
	}
	atomic_inc(&wf->count);
	mutex_unlock(&wf->mutex);

	return 0;
}

int __intel_wakeref_put_last(struct drm_i915_private *i915,
			     struct intel_wakeref *wf,
			     int (*fn)(struct intel_wakeref *wf))
{
	int err;

	err = fn(wf);
	if (likely(!err))
		rpm_put(i915, wf);
	else
		atomic_inc(&wf->count);
	mutex_unlock(&wf->mutex);

	return err;
}

void __intel_wakeref_init(struct intel_wakeref *wf, struct lock_class_key *key)
{
	__mutex_init(&wf->mutex, "wakeref", key);
	atomic_set(&wf->count, 0);
	wf->wakeref = 0;
}

static void wakeref_auto_timeout(struct timer_list *t)
{
	struct intel_wakeref_auto *wf = from_timer(wf, t, timer);
	intel_wakeref_t wakeref;
	unsigned long flags;

	if (!refcount_dec_and_lock_irqsave(&wf->count, &wf->lock, &flags))
		return;

	wakeref = fetch_and_zero(&wf->wakeref);
	spin_unlock_irqrestore(&wf->lock, flags);

	intel_runtime_pm_put(wf->i915, wakeref);
}

void intel_wakeref_auto_init(struct intel_wakeref_auto *wf,
			     struct drm_i915_private *i915)
{
	spin_lock_init(&wf->lock);
	timer_setup(&wf->timer, wakeref_auto_timeout, 0);
	refcount_set(&wf->count, 0);
	wf->wakeref = 0;
	wf->i915 = i915;
}

void intel_wakeref_auto(struct intel_wakeref_auto *wf, unsigned long timeout)
{
	unsigned long flags;

	if (!timeout) {
		if (del_timer_sync(&wf->timer))
			wakeref_auto_timeout(&wf->timer);
		return;
	}

	/* Our mission is that we only extend an already active wakeref */
	assert_rpm_wakelock_held(&wf->i915->runtime_pm);

	if (!refcount_inc_not_zero(&wf->count)) {
		spin_lock_irqsave(&wf->lock, flags);
		if (!refcount_inc_not_zero(&wf->count)) {
			GEM_BUG_ON(wf->wakeref);
			wf->wakeref = intel_runtime_pm_get_if_in_use(wf->i915);
			refcount_set(&wf->count, 1);
		}
		spin_unlock_irqrestore(&wf->lock, flags);
	}

	/*
	 * If we extend a pending timer, we will only get a single timer
	 * callback and so need to cancel the local inc by running the
	 * elided callback to keep the wf->count balanced.
	 */
	if (mod_timer(&wf->timer, jiffies + timeout))
		wakeref_auto_timeout(&wf->timer);
}

void intel_wakeref_auto_fini(struct intel_wakeref_auto *wf)
{
	intel_wakeref_auto(wf, 0);
	GEM_BUG_ON(wf->wakeref);
}
