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

void intel_context_init(struct intel_context *ce,
			struct i915_gem_context *ctx,
			struct intel_engine_cs *engine);

struct intel_context *
intel_context_create(struct i915_gem_context *ctx,
		     struct intel_engine_cs *engine);

void intel_context_free(struct intel_context *ce);

/**
 * intel_context_lock_pinned - Stablises the 'pinned' status of the HW context
 * @ce - the context
 *
 * Acquire a lock on the pinned status of the HW context, such that the context
 * can neither be bound to the GPU or unbound whilst the lock is held, i.e.
 * intel_context_is_pinned() remains stable.
 */
static inline int intel_context_lock_pinned(struct intel_context *ce)
	__acquires(ce->pin_mutex)
{
	return mutex_lock_interruptible(&ce->pin_mutex);
}

/**
 * intel_context_is_pinned - Reports the 'pinned' status
 * @ce - the context
 *
 * While in use by the GPU, the context, along with its ring and page
 * tables is pinned into memory and the GTT.
 *
 * Returns: true if the context is currently pinned for use by the GPU.
 */
static inline bool
intel_context_is_pinned(struct intel_context *ce)
{
	return atomic_read(&ce->pin_count);
}

/**
 * intel_context_unlock_pinned - Releases the earlier locking of 'pinned' status
 * @ce - the context
 *
 * Releases the lock earlier acquired by intel_context_unlock_pinned().
 */
static inline void intel_context_unlock_pinned(struct intel_context *ce)
	__releases(ce->pin_mutex)
{
	mutex_unlock(&ce->pin_mutex);
}

int __intel_context_do_pin(struct intel_context *ce);

static inline int intel_context_pin(struct intel_context *ce)
{
	if (likely(atomic_inc_not_zero(&ce->pin_count)))
		return 0;

	return __intel_context_do_pin(ce);
}

static inline void __intel_context_pin(struct intel_context *ce)
{
	GEM_BUG_ON(!intel_context_is_pinned(ce));
	atomic_inc(&ce->pin_count);
}

void intel_context_unpin(struct intel_context *ce);

void intel_context_enter_engine(struct intel_context *ce);
void intel_context_exit_engine(struct intel_context *ce);

static inline void intel_context_enter(struct intel_context *ce)
{
	if (!ce->active_count++)
		ce->ops->enter(ce);
}

static inline void intel_context_mark_active(struct intel_context *ce)
{
	++ce->active_count;
}

static inline void intel_context_exit(struct intel_context *ce)
{
	GEM_BUG_ON(!ce->active_count);
	if (!--ce->active_count)
		ce->ops->exit(ce);
}

static inline struct intel_context *intel_context_get(struct intel_context *ce)
{
	kref_get(&ce->ref);
	return ce;
}

static inline void intel_context_put(struct intel_context *ce)
{
	kref_put(&ce->ref, ce->ops->destroy);
}

static inline int __must_check
intel_context_timeline_lock(struct intel_context *ce)
	__acquires(&ce->ring->timeline->mutex)
{
	return mutex_lock_interruptible(&ce->ring->timeline->mutex);
}

static inline void intel_context_timeline_unlock(struct intel_context *ce)
	__releases(&ce->ring->timeline->mutex)
{
	mutex_unlock(&ce->ring->timeline->mutex);
}

struct i915_request *intel_context_create_request(struct intel_context *ce);

#endif /* __INTEL_CONTEXT_H__ */
