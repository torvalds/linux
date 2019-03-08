/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_CONTEXT_H__
#define __INTEL_CONTEXT_H__

#include "intel_context_types.h"
#include "intel_engine_types.h"

struct intel_context *intel_context_alloc(void);
void intel_context_free(struct intel_context *ce);

void intel_context_init(struct intel_context *ce,
			struct i915_gem_context *ctx,
			struct intel_engine_cs *engine);

/**
 * intel_context_lookup - Find the matching HW context for this (ctx, engine)
 * @ctx - the parent GEM context
 * @engine - the target HW engine
 *
 * May return NULL if the HW context hasn't been instantiated (i.e. unused).
 */
struct intel_context *
intel_context_lookup(struct i915_gem_context *ctx,
		     struct intel_engine_cs *engine);

/**
 * intel_context_instance - Lookup or allocate the HW context for (ctx, engine)
 * @ctx - the parent GEM context
 * @engine - the target HW engine
 *
 * Returns the existing HW context for this pair of (GEM context, engine), or
 * allocates and initialises a fresh context. Once allocated, the HW context
 * remains resident until the GEM context is destroyed.
 */
struct intel_context *
intel_context_instance(struct i915_gem_context *ctx,
		       struct intel_engine_cs *engine);

struct intel_context *
__intel_context_insert(struct i915_gem_context *ctx,
		       struct intel_engine_cs *engine,
		       struct intel_context *ce);
void
__intel_context_remove(struct intel_context *ce);

struct intel_context *
intel_context_pin(struct i915_gem_context *ctx, struct intel_engine_cs *engine);

static inline void __intel_context_pin(struct intel_context *ce)
{
	GEM_BUG_ON(!ce->pin_count);
	ce->pin_count++;
}

static inline void intel_context_unpin(struct intel_context *ce)
{
	GEM_BUG_ON(!ce->pin_count);
	if (--ce->pin_count)
		return;

	GEM_BUG_ON(!ce->ops);
	ce->ops->unpin(ce);
}

#endif /* __INTEL_CONTEXT_H__ */
