/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_CONTEXT_H__
#define __INTEL_CONTEXT_H__

#include <linux/lockdep.h>

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
 * intel_context_pin_lock - Stablises the 'pinned' status of the HW context
 * @ctx - the parent GEM context
 * @engine - the target HW engine
 *
 * Acquire a lock on the pinned status of the HW context, such that the context
 * can neither be bound to the GPU or unbound whilst the lock is held, i.e.
 * intel_context_is_pinned() remains stable.
 */
struct intel_context *
intel_context_pin_lock(struct i915_gem_context *ctx,
		       struct intel_engine_cs *engine);

static inline bool
intel_context_is_pinned(struct intel_context *ce)
{
	return atomic_read(&ce->pin_count);
}

static inline void intel_context_pin_unlock(struct intel_context *ce)
__releases(ce->pin_mutex)
{
	mutex_unlock(&ce->pin_mutex);
}

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
	GEM_BUG_ON(!intel_context_is_pinned(ce));
	atomic_inc(&ce->pin_count);
}

void intel_context_unpin(struct intel_context *ce);

static inline struct intel_context *intel_context_get(struct intel_context *ce)
{
	kref_get(&ce->ref);
	return ce;
}

static inline void intel_context_put(struct intel_context *ce)
{
	kref_put(&ce->ref, ce->ops->destroy);
}

#endif /* __INTEL_CONTEXT_H__ */
