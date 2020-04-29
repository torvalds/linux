/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_CONTEXT_H__
#define __INTEL_CONTEXT_H__

#include <linux/bitops.h>
#include <linux/lockdep.h>
#include <linux/types.h>

#include "i915_active.h"
#include "i915_drv.h"
#include "intel_context_types.h"
#include "intel_engine_types.h"
#include "intel_ring_types.h"
#include "intel_timeline_types.h"

#define CE_TRACE(ce, fmt, ...) do {					\
	const struct intel_context *ce__ = (ce);			\
	ENGINE_TRACE(ce__->engine, "context:%llx " fmt,			\
		     ce__->timeline->fence_context,			\
		     ##__VA_ARGS__);					\
} while (0)

void intel_context_init(struct intel_context *ce,
			struct intel_engine_cs *engine);
void intel_context_fini(struct intel_context *ce);

struct intel_context *
intel_context_create(struct intel_engine_cs *engine);

int intel_context_alloc_state(struct intel_context *ce);

void intel_context_free(struct intel_context *ce);

int intel_context_reconfigure_sseu(struct intel_context *ce,
				   const struct intel_sseu sseu);

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

static inline bool intel_context_pin_if_active(struct intel_context *ce)
{
	return atomic_inc_not_zero(&ce->pin_count);
}

static inline int intel_context_pin(struct intel_context *ce)
{
	if (likely(intel_context_pin_if_active(ce)))
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
	lockdep_assert_held(&ce->timeline->mutex);
	if (!ce->active_count++)
		ce->ops->enter(ce);
}

static inline void intel_context_mark_active(struct intel_context *ce)
{
	lockdep_assert_held(&ce->timeline->mutex);
	++ce->active_count;
}

static inline void intel_context_exit(struct intel_context *ce)
{
	lockdep_assert_held(&ce->timeline->mutex);
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

static inline struct intel_timeline *__must_check
intel_context_timeline_lock(struct intel_context *ce)
	__acquires(&ce->timeline->mutex)
{
	struct intel_timeline *tl = ce->timeline;
	int err;

	err = mutex_lock_interruptible(&tl->mutex);
	if (err)
		return ERR_PTR(err);

	return tl;
}

static inline void intel_context_timeline_unlock(struct intel_timeline *tl)
	__releases(&tl->mutex)
{
	mutex_unlock(&tl->mutex);
}

int intel_context_prepare_remote_request(struct intel_context *ce,
					 struct i915_request *rq);

struct i915_request *intel_context_create_request(struct intel_context *ce);

static inline struct intel_ring *__intel_context_ring_size(u64 sz)
{
	return u64_to_ptr(struct intel_ring, sz);
}

static inline bool intel_context_is_barrier(const struct intel_context *ce)
{
	return test_bit(CONTEXT_BARRIER_BIT, &ce->flags);
}

static inline bool intel_context_is_closed(const struct intel_context *ce)
{
	return test_bit(CONTEXT_CLOSED_BIT, &ce->flags);
}

static inline bool intel_context_use_semaphores(const struct intel_context *ce)
{
	return test_bit(CONTEXT_USE_SEMAPHORES, &ce->flags);
}

static inline void intel_context_set_use_semaphores(struct intel_context *ce)
{
	set_bit(CONTEXT_USE_SEMAPHORES, &ce->flags);
}

static inline void intel_context_clear_use_semaphores(struct intel_context *ce)
{
	clear_bit(CONTEXT_USE_SEMAPHORES, &ce->flags);
}

static inline bool intel_context_is_banned(const struct intel_context *ce)
{
	return test_bit(CONTEXT_BANNED, &ce->flags);
}

static inline bool intel_context_set_banned(struct intel_context *ce)
{
	return test_and_set_bit(CONTEXT_BANNED, &ce->flags);
}

static inline bool
intel_context_force_single_submission(const struct intel_context *ce)
{
	return test_bit(CONTEXT_FORCE_SINGLE_SUBMISSION, &ce->flags);
}

static inline void
intel_context_set_single_submission(struct intel_context *ce)
{
	__set_bit(CONTEXT_FORCE_SINGLE_SUBMISSION, &ce->flags);
}

static inline bool
intel_context_nopreempt(const struct intel_context *ce)
{
	return test_bit(CONTEXT_NOPREEMPT, &ce->flags);
}

static inline void
intel_context_set_nopreempt(struct intel_context *ce)
{
	set_bit(CONTEXT_NOPREEMPT, &ce->flags);
}

static inline void
intel_context_clear_nopreempt(struct intel_context *ce)
{
	clear_bit(CONTEXT_NOPREEMPT, &ce->flags);
}

static inline u64 intel_context_get_total_runtime_ns(struct intel_context *ce)
{
	const u32 period =
		RUNTIME_INFO(ce->engine->i915)->cs_timestamp_period_ns;

	return READ_ONCE(ce->runtime.total) * period;
}

static inline u64 intel_context_get_avg_runtime_ns(struct intel_context *ce)
{
	const u32 period =
		RUNTIME_INFO(ce->engine->i915)->cs_timestamp_period_ns;

	return mul_u32_u32(ewma_runtime_read(&ce->runtime.avg), period);
}

#endif /* __INTEL_CONTEXT_H__ */
