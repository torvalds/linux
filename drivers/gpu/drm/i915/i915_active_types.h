/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#ifndef _I915_ACTIVE_TYPES_H_
#define _I915_ACTIVE_TYPES_H_

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
};

struct i915_active {
	struct drm_i915_private *i915;

	struct rb_root tree;
	struct i915_active_request last;
	unsigned int count;

	void (*retire)(struct i915_active *ref);
};

#endif /* _I915_ACTIVE_TYPES_H_ */
