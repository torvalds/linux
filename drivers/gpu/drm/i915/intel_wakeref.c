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
