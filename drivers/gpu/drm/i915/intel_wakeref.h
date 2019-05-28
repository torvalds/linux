/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#ifndef INTEL_WAKEREF_H
#define INTEL_WAKEREF_H

#include <linux/atomic.h>
#include <linux/mutex.h>
#include <linux/stackdepot.h>

struct drm_i915_private;

typedef depot_stack_handle_t intel_wakeref_t;

struct intel_wakeref {
	atomic_t count;
	struct mutex mutex;
	intel_wakeref_t wakeref;
};

void __intel_wakeref_init(struct intel_wakeref *wf,
			  struct lock_class_key *key);
#define intel_wakeref_init(wf) do {					\
	static struct lock_class_key __key;				\
									\
	__intel_wakeref_init((wf), &__key);				\
} while (0)

int __intel_wakeref_get_first(struct drm_i915_private *i915,
			      struct intel_wakeref *wf,
			      int (*fn)(struct intel_wakeref *wf));
int __intel_wakeref_put_last(struct drm_i915_private *i915,
			     struct intel_wakeref *wf,
			     int (*fn)(struct intel_wakeref *wf));

/**
 * intel_wakeref_get: Acquire the wakeref
 * @i915: the drm_i915_private device
 * @wf: the wakeref
 * @fn: callback for acquired the wakeref, called only on first acquire.
 *
 * Acquire a hold on the wakeref. The first user to do so, will acquire
 * the runtime pm wakeref and then call the @fn underneath the wakeref
 * mutex.
 *
 * Note that @fn is allowed to fail, in which case the runtime-pm wakeref
 * will be released and the acquisition unwound, and an error reported.
 *
 * Returns: 0 if the wakeref was acquired successfully, or a negative error
 * code otherwise.
 */
static inline int
intel_wakeref_get(struct drm_i915_private *i915,
		  struct intel_wakeref *wf,
		  int (*fn)(struct intel_wakeref *wf))
{
	if (unlikely(!atomic_inc_not_zero(&wf->count)))
		return __intel_wakeref_get_first(i915, wf, fn);

	return 0;
}

/**
 * intel_wakeref_put: Release the wakeref
 * @i915: the drm_i915_private device
 * @wf: the wakeref
 * @fn: callback for releasing the wakeref, called only on final release.
 *
 * Release our hold on the wakeref. When there are no more users,
 * the runtime pm wakeref will be released after the @fn callback is called
 * underneath the wakeref mutex.
 *
 * Note that @fn is allowed to fail, in which case the runtime-pm wakeref
 * is retained and an error reported.
 *
 * Returns: 0 if the wakeref was released successfully, or a negative error
 * code otherwise.
 */
static inline int
intel_wakeref_put(struct drm_i915_private *i915,
		  struct intel_wakeref *wf,
		  int (*fn)(struct intel_wakeref *wf))
{
	if (atomic_dec_and_mutex_lock(&wf->count, &wf->mutex))
		return __intel_wakeref_put_last(i915, wf, fn);

	return 0;
}

/**
 * intel_wakeref_lock: Lock the wakeref (mutex)
 * @wf: the wakeref
 *
 * Locks the wakeref to prevent it being acquired or released. New users
 * can still adjust the counter, but the wakeref itself (and callback)
 * cannot be acquired or released.
 */
static inline void
intel_wakeref_lock(struct intel_wakeref *wf)
	__acquires(wf->mutex)
{
	mutex_lock(&wf->mutex);
}

/**
 * intel_wakeref_unlock: Unlock the wakeref
 * @wf: the wakeref
 *
 * Releases a previously acquired intel_wakeref_lock().
 */
static inline void
intel_wakeref_unlock(struct intel_wakeref *wf)
	__releases(wf->mutex)
{
	mutex_unlock(&wf->mutex);
}

/**
 * intel_wakeref_active: Query whether the wakeref is currently held
 * @wf: the wakeref
 *
 * Returns: true if the wakeref is currently held.
 */
static inline bool
intel_wakeref_active(struct intel_wakeref *wf)
{
	return READ_ONCE(wf->wakeref);
}

#endif /* INTEL_WAKEREF_H */
