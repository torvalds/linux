/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2014-2018 Intel Corporation
 */

#ifndef I915_GEM_BATCH_POOL_H
#define I915_GEM_BATCH_POOL_H

#include <linux/types.h>

struct drm_i915_gem_object;
struct intel_engine_cs;

struct i915_gem_batch_pool {
	struct intel_engine_cs *engine;
	struct list_head cache_list[4];
};

void i915_gem_batch_pool_init(struct i915_gem_batch_pool *pool,
			      struct intel_engine_cs *engine);
void i915_gem_batch_pool_fini(struct i915_gem_batch_pool *pool);
struct drm_i915_gem_object *
i915_gem_batch_pool_get(struct i915_gem_batch_pool *pool, size_t size);

#endif /* I915_GEM_BATCH_POOL_H */
