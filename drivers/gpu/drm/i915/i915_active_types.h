/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#ifndef _I915_ACTIVE_TYPES_H_
#define _I915_ACTIVE_TYPES_H_

#include <linux/atomic.h>
#include <linux/llist.h>
#include <linux/mutex.h>
#include <linux/rbtree.h>
#include <linux/rcupdate.h>

struct drm_i915_private;
struct i915_active_request;
struct i915_request;

typedef void (*i915_active_retire_fn)(struct i915_active_request *,
				      struct i915_request *);

struct i915_active_request {
	struct i915_request __rcu *request;
	struct list_head link;
	i915_active_retire_fn retire;
#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM)
	/*
	 * Incorporeal!
	 *
	 * Updates to the i915_active_request must be serialised under a lock
	 * to ensure that the timeline is ordered. Normally, this is the
	 * timeline->mutex, but another mutex may be used so long as it is
	 * done so consistently.
	 *
	 * For lockdep tracking of the above, we store the lock we intend
	 * to always use for updates of this i915_active_request during
	 * construction and assert that is held on every update.
	 */
	struct mutex *lock;
#endif
};

struct active_node;

struct i915_active {
	struct drm_i915_private *i915;

	struct active_node *cache;
	struct rb_root tree;
	struct mutex mutex;
	atomic_t count;

	unsigned long flags;
#define I915_ACTIVE_GRAB_BIT 0

	int (*active)(struct i915_active *ref);
	void (*retire)(struct i915_active *ref);

	struct llist_head preallocated_barriers;
};

#endif /* _I915_ACTIVE_TYPES_H_ */
