/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#ifndef INTEL_WAKEREF_H
#define INTEL_WAKEREF_H

#include <drm/drm_print.h>

#include <linux/atomic.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/lockdep.h>
#include <linux/mutex.h>
#include <linux/refcount.h>
#include <linux/ref_tracker.h>
#include <linux/slab.h>
#include <linux/stackdepot.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

typedef struct ref_tracker *intel_wakeref_t;

#define INTEL_REFTRACK_DEAD_COUNT 16
#define INTEL_REFTRACK_PRINT_LIMIT 16

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG)
#define INTEL_WAKEREF_BUG_ON(expr) BUG_ON(expr)
#else
#define INTEL_WAKEREF_BUG_ON(expr) BUILD_BUG_ON_INVALID(expr)
#endif

struct intel_runtime_pm;
struct intel_wakeref;

struct intel_wakeref_ops {
	int (*get)(struct intel_wakeref *wf);
	int (*put)(struct intel_wakeref *wf);
};

struct intel_wakeref {
	atomic_t count;
	struct mutex mutex;

	intel_wakeref_t wakeref;

	struct drm_i915_private *i915;
	const struct intel_wakeref_ops *ops;

	struct delayed_work work;

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_WAKEREF)
	struct ref_tracker_dir debug;
#endif
};

struct intel_wakeref_lockclass {
	struct lock_class_key mutex;
	struct lock_class_key work;
};

void __intel_wakeref_init(struct intel_wakeref *wf,
			  struct drm_i915_private *i915,
			  const struct intel_wakeref_ops *ops,
			  struct intel_wakeref_lockclass *key,
			  const char *name);
#define intel_wakeref_init(wf, i915, ops, name) do {			\
	static struct intel_wakeref_lockclass __key;			\
									\
	__intel_wakeref_init((wf), (i915), (ops), &__key, name);	\
} while (0)

int __intel_wakeref_get_first(struct intel_wakeref *wf);
void __intel_wakeref_put_last(struct intel_wakeref *wf, unsigned long flags);

/**
 * intel_wakeref_get: Acquire the wakeref
 * @wf: the wakeref
 *
 * Acquire a hold on the wakeref. The first user to do so, will acquire
 * the runtime pm wakeref and then call the intel_wakeref_ops->get()
 * underneath the wakeref mutex.
 *
 * Note that intel_wakeref_ops->get() is allowed to fail, in which case
 * the runtime-pm wakeref will be released and the acquisition unwound,
 * and an error reported.
 *
 * Returns: 0 if the wakeref was acquired successfully, or a negative error
 * code otherwise.
 */
static inline int
intel_wakeref_get(struct intel_wakeref *wf)
{
	might_sleep();
	if (unlikely(!atomic_inc_not_zero(&wf->count)))
		return __intel_wakeref_get_first(wf);

	return 0;
}

/**
 * __intel_wakeref_get: Acquire the wakeref, again
 * @wf: the wakeref
 *
 * Increment the wakeref counter, only valid if it is already held by
 * the caller.
 *
 * See intel_wakeref_get().
 */
static inline void
__intel_wakeref_get(struct intel_wakeref *wf)
{
	INTEL_WAKEREF_BUG_ON(atomic_read(&wf->count) <= 0);
	atomic_inc(&wf->count);
}

/**
 * intel_wakeref_get_if_active: Acquire the wakeref
 * @wf: the wakeref
 *
 * Acquire a hold on the wakeref, but only if the wakeref is already
 * active.
 *
 * Returns: true if the wakeref was acquired, false otherwise.
 */
static inline bool
intel_wakeref_get_if_active(struct intel_wakeref *wf)
{
	return atomic_inc_not_zero(&wf->count);
}

enum {
	INTEL_WAKEREF_PUT_ASYNC_BIT = 0,
	__INTEL_WAKEREF_PUT_LAST_BIT__
};

static inline void
intel_wakeref_might_get(struct intel_wakeref *wf)
{
	might_lock(&wf->mutex);
}

/**
 * __intel_wakeref_put: Release the wakeref
 * @wf: the wakeref
 * @flags: control flags
 *
 * Release our hold on the wakeref. When there are no more users,
 * the runtime pm wakeref will be released after the intel_wakeref_ops->put()
 * callback is called underneath the wakeref mutex.
 *
 * Note that intel_wakeref_ops->put() is allowed to fail, in which case the
 * runtime-pm wakeref is retained.
 *
 */
static inline void
__intel_wakeref_put(struct intel_wakeref *wf, unsigned long flags)
#define INTEL_WAKEREF_PUT_ASYNC BIT(INTEL_WAKEREF_PUT_ASYNC_BIT)
#define INTEL_WAKEREF_PUT_DELAY \
	GENMASK(BITS_PER_LONG - 1, __INTEL_WAKEREF_PUT_LAST_BIT__)
{
	INTEL_WAKEREF_BUG_ON(atomic_read(&wf->count) <= 0);
	if (unlikely(!atomic_add_unless(&wf->count, -1, 1)))
		__intel_wakeref_put_last(wf, flags);
}

static inline void
intel_wakeref_put(struct intel_wakeref *wf)
{
	might_sleep();
	__intel_wakeref_put(wf, 0);
}

static inline void
intel_wakeref_put_async(struct intel_wakeref *wf)
{
	__intel_wakeref_put(wf, INTEL_WAKEREF_PUT_ASYNC);
}

static inline void
intel_wakeref_put_delay(struct intel_wakeref *wf, unsigned long delay)
{
	__intel_wakeref_put(wf,
			    INTEL_WAKEREF_PUT_ASYNC |
			    FIELD_PREP(INTEL_WAKEREF_PUT_DELAY, delay));
}

static inline void
intel_wakeref_might_put(struct intel_wakeref *wf)
{
	might_lock(&wf->mutex);
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
 * intel_wakeref_unlock_wait: Wait until the active callback is complete
 * @wf: the wakeref
 *
 * Waits for the active callback (under the @wf->mutex or another CPU) is
 * complete.
 */
static inline void
intel_wakeref_unlock_wait(struct intel_wakeref *wf)
{
	mutex_lock(&wf->mutex);
	mutex_unlock(&wf->mutex);
	flush_delayed_work(&wf->work);
}

/**
 * intel_wakeref_is_active: Query whether the wakeref is currently held
 * @wf: the wakeref
 *
 * Returns: true if the wakeref is currently held.
 */
static inline bool
intel_wakeref_is_active(const struct intel_wakeref *wf)
{
	return READ_ONCE(wf->wakeref);
}

/**
 * __intel_wakeref_defer_park: Defer the current park callback
 * @wf: the wakeref
 */
static inline void
__intel_wakeref_defer_park(struct intel_wakeref *wf)
{
	lockdep_assert_held(&wf->mutex);
	INTEL_WAKEREF_BUG_ON(atomic_read(&wf->count));
	atomic_set_release(&wf->count, 1);
}

/**
 * intel_wakeref_wait_for_idle: Wait until the wakeref is idle
 * @wf: the wakeref
 *
 * Wait for the earlier asynchronous release of the wakeref. Note
 * this will wait for any third party as well, so make sure you only wait
 * when you have control over the wakeref and trust no one else is acquiring
 * it.
 *
 * Return: 0 on success, error code if killed.
 */
int intel_wakeref_wait_for_idle(struct intel_wakeref *wf);

#define INTEL_WAKEREF_DEF ERR_PTR(-ENOENT)

static inline intel_wakeref_t intel_ref_tracker_alloc(struct ref_tracker_dir *dir)
{
	struct ref_tracker *user = NULL;

	ref_tracker_alloc(dir, &user, GFP_NOWAIT);

	return user ?: INTEL_WAKEREF_DEF;
}

static inline void intel_ref_tracker_free(struct ref_tracker_dir *dir,
					  intel_wakeref_t wakeref)
{
	if (wakeref == INTEL_WAKEREF_DEF)
		wakeref = NULL;

	if (WARN_ON(IS_ERR(wakeref)))
		return;

	ref_tracker_free(dir, &wakeref);
}

void intel_ref_tracker_show(struct ref_tracker_dir *dir,
			    struct drm_printer *p);

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_WAKEREF)

static inline intel_wakeref_t intel_wakeref_track(struct intel_wakeref *wf)
{
	return intel_ref_tracker_alloc(&wf->debug);
}

static inline void intel_wakeref_untrack(struct intel_wakeref *wf,
					 intel_wakeref_t handle)
{
	intel_ref_tracker_free(&wf->debug, handle);
}

#else

static inline intel_wakeref_t intel_wakeref_track(struct intel_wakeref *wf)
{
	return INTEL_WAKEREF_DEF;
}

static inline void intel_wakeref_untrack(struct intel_wakeref *wf,
					 intel_wakeref_t handle)
{
}

#endif

struct intel_wakeref_auto {
	struct drm_i915_private *i915;
	struct timer_list timer;
	intel_wakeref_t wakeref;
	spinlock_t lock;
	refcount_t count;
};

/**
 * intel_wakeref_auto: Delay the runtime-pm autosuspend
 * @wf: the wakeref
 * @timeout: relative timeout in jiffies
 *
 * The runtime-pm core uses a suspend delay after the last wakeref
 * is released before triggering runtime suspend of the device. That
 * delay is configurable via sysfs with little regard to the device
 * characteristics. Instead, we want to tune the autosuspend based on our
 * HW knowledge. intel_wakeref_auto() delays the sleep by the supplied
 * timeout.
 *
 * Pass @timeout = 0 to cancel a previous autosuspend by executing the
 * suspend immediately.
 */
void intel_wakeref_auto(struct intel_wakeref_auto *wf, unsigned long timeout);

void intel_wakeref_auto_init(struct intel_wakeref_auto *wf,
			     struct drm_i915_private *i915);
void intel_wakeref_auto_fini(struct intel_wakeref_auto *wf);

#endif /* INTEL_WAKEREF_H */
