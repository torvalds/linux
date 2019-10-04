/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#ifndef _I915_ACTIVE_TYPES_H_
#define _I915_ACTIVE_TYPES_H_

#include <linux/atomic.h>
#include <linux/dma-fence.h>
#include <linux/llist.h>
#include <linux/mutex.h>
#include <linux/rbtree.h>
#include <linux/rcupdate.h>
#include <linux/workqueue.h>

#include "i915_utils.h"

struct i915_active_fence {
	struct dma_fence __rcu *fence;
	struct dma_fence_cb cb;
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

#define I915_ACTIVE_MAY_SLEEP BIT(0)

#define __i915_active_call __aligned(4)
#define i915_active_may_sleep(fn) ptr_pack_bits(&(fn), I915_ACTIVE_MAY_SLEEP, 2)

struct i915_active {
	atomic_t count;
	struct mutex mutex;

	struct active_node *cache;
	struct rb_root tree;

	/* Preallocated "exclusive" node */
	struct i915_active_fence excl;

	unsigned long flags;
#define I915_ACTIVE_RETIRE_SLEEPS BIT(0)

	int (*active)(struct i915_active *ref);
	void (*retire)(struct i915_active *ref);

	struct work_struct work;

	struct llist_head preallocated_barriers;
};

#endif /* _I915_ACTIVE_TYPES_H_ */
