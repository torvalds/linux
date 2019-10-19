/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2014-2018 Intel Corporation
 */

#ifndef INTEL_ENGINE_POOL_H
#define INTEL_ENGINE_POOL_H

#include "intel_engine_pool_types.h"
#include "i915_active.h"
#include "i915_request.h"

struct intel_engine_pool_node *
intel_engine_pool_get(struct intel_engine_pool *pool, size_t size);

static inline int
intel_engine_pool_mark_active(struct intel_engine_pool_node *node,
			      struct i915_request *rq)
{
	return i915_active_ref(&node->active, rq->timeline, rq);
}

static inline void
intel_engine_pool_put(struct intel_engine_pool_node *node)
{
	i915_active_release(&node->active);
}

void intel_engine_pool_init(struct intel_engine_pool *pool);
void intel_engine_pool_park(struct intel_engine_pool *pool);
void intel_engine_pool_fini(struct intel_engine_pool *pool);

#endif /* INTEL_ENGINE_POOL_H */
