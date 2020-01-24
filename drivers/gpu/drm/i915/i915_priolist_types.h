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

	/* A preemptive pulse used to monitor the health of each engine */
	I915_PRIORITY_HEARTBEAT,

	/* Interactive workload, scheduled for immediate pageflipping */
	I915_PRIORITY_DISPLAY,
};

#define I915_USER_PRIORITY_SHIFT 2
#define I915_USER_PRIORITY(x) ((x) << I915_USER_PRIORITY_SHIFT)

#define I915_PRIORITY_COUNT BIT(I915_USER_PRIORITY_SHIFT)
#define I915_PRIORITY_MASK (I915_PRIORITY_COUNT - 1)

#define I915_PRIORITY_WAIT		((u8)BIT(0))
#define I915_PRIORITY_NOSEMAPHORE	((u8)BIT(1))

/* Smallest priority value that cannot be bumped. */
#define I915_PRIORITY_INVALID (INT_MIN | (u8)I915_PRIORITY_MASK)

/*
 * Requests containing performance queries must not be preempted by
 * another context. They get scheduled with their default priority and
 * once they reach the execlist ports we ensure that they stick on the
 * HW until finished by pretending that they have maximum priority,
 * i.e. nothing can have higher priority and force us to usurp the
 * active request.
 */
#define I915_PRIORITY_UNPREEMPTABLE INT_MAX
#define I915_PRIORITY_BARRIER INT_MAX

#define __NO_PREEMPTION (I915_PRIORITY_WAIT)

struct i915_priolist {
	struct list_head requests[I915_PRIORITY_COUNT];
	struct rb_node node;
	unsigned long used;
	int priority;
};

#endif /* _I915_PRIOLIST_TYPES_H_ */
