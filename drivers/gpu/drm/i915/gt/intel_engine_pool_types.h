/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2014-2018 Intel Corporation
 */

#ifndef INTEL_ENGINE_POOL_TYPES_H
#define INTEL_ENGINE_POOL_TYPES_H

#include <linux/list.h>
#include <linux/spinlock.h>

#include "i915_active_types.h"

struct drm_i915_gem_object;

struct intel_engine_pool {
	spinlock_t lock;
	struct list_head cache_list[4];
};

struct intel_engine_pool_node {
	struct i915_active active;
	struct drm_i915_gem_object *obj;
	struct list_head link;
	struct intel_engine_pool *pool;
};

#endif /* INTEL_ENGINE_POOL_TYPES_H */
