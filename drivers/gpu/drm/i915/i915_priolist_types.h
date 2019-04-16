/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

#ifndef _I915_PRIOLIST_TYPES_H_
#define _I915_PRIOLIST_TYPES_H_

#include <linux/list.h>
#include <linux/rbtree.h>

#include <uapi/drm/i915_drm.h>

enum {
	I915_PRIORITY_MIN = I915_CONTEXT_MIN_USER_PRIORITY - 1,
	I915_PRIORITY_NORMAL = I915_CONTEXT_DEFAULT_PRIORITY,
	I915_PRIORITY_MAX = I915_CONTEXT_MAX_USER_PRIORITY + 1,

	I915_PRIORITY_INVALID = INT_MIN
};

#define I915_USER_PRIORITY_SHIFT 3
#define I915_USER_PRIORITY(x) ((x) << I915_USER_PRIORITY_SHIFT)

#define I915_PRIORITY_COUNT BIT(I915_USER_PRIORITY_SHIFT)
#define I915_PRIORITY_MASK (I915_PRIORITY_COUNT - 1)

#define I915_PRIORITY_WAIT		((u8)BIT(0))
#define I915_PRIORITY_NEWCLIENT		((u8)BIT(1))
#define I915_PRIORITY_NOSEMAPHORE	((u8)BIT(2))

#define __NO_PREEMPTION (I915_PRIORITY_WAIT)

struct i915_priolist {
	struct list_head requests[I915_PRIORITY_COUNT];
	struct rb_node node;
	unsigned long used;
	int priority;
};

#endif /* _I915_PRIOLIST_TYPES_H_ */
