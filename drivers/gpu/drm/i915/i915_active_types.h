/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#ifndef _I915_ACTIVE_TYPES_H_
#define _I915_ACTIVE_TYPES_H_

#include <linux/rbtree.h>

#include "i915_request.h"

struct drm_i915_private;

struct i915_active {
	struct drm_i915_private *i915;

	struct rb_root tree;
	struct i915_gem_active last;
	unsigned int count;

	void (*retire)(struct i915_active *ref);
};

#endif /* _I915_ACTIVE_TYPES_H_ */
