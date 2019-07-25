/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#include "gem/i915_gem_context.h"
#include "gem/i915_gem_pm.h"

#include "i915_drv.h"
#include "i915_globals.h"

#include "intel_context.h"
#include "intel_engine.h"
#include "intel_engine_pm.h"

static struct i915_global_context {
	struct i915_global base;
	struct kmem_cache *slab_ce;
} global;

static struct intel_context *intel_context_alloc(void)
{
	return kmem_cache_zalloc(global.slab_ce, GFP_KERNEL);
}

void intel_context_free(struct intel_context *ce)
{
	kmem_cache_free(global.slab_ce, ce);
}

struct intel_context *
intel_context_create(struct i915_gem_context *ctx,
		     struct intel_engine_cs *engine)
{
	struct intel_context *ce;

	ce = intel_context_alloc();
	if (!ce)
		return ERR_PTR(-ENOMEM);

	intel_context_init(ce, ctx, engine);
	return ce;
}

int __intel_context_do_pin(struct intel_context *ce)
{
	int err;

	if (mutex_lock_interruptible(&ce->pin_mutex))
		return -EINTR;

	if (likely(!atomic_read(&ce->pin_count))) {
		intel_wakeref_t wakeref;

		err = 0;
		with_intel_runtime_pm(&ce->engine->i915->runtime_pm, wakeref)
			err = ce->ops->pin(ce);
		if (err)
			goto err;

		GEM_TRACE("%s context:%llx pin ring:{head:%04x, tail:%04x}\n",
			  ce->engine->name, ce->ring->timeline->fence_context,
			  ce->ring->head, ce->ring->tail);

		i915_gem_context_get(ce->gem_context); /* for ctx->ppgtt */

		smp_mb__before_atomic(); /* flush pin before it is visible */
	}

	atomic_inc(&ce->pin_count);
	GEM_BUG_ON(!intel_context_is_pinned(ce)); /* no overflow! */

	mutex_unlock(&ce->pin_mutex);
	return 0;

err:
	mutex_unlock(&ce->pin_mutex);
	return err;
}

void intel_context_unpin(struct intel_context *ce)
{
	if (likely(atomic_add_unless(&ce->pin_count, -1, 1)))
		return;

	/* We may be called from inside intel_context_pin() to evict another */
	intel_context_get(ce);
	mutex_lock_nested(&ce->pin_mutex, SINGLE_DEPTH_NESTING);

	if (likely(atomic_dec_and_test(&ce->pin_count))) {
		GEM_TRACE("%s context:%llx retire\n",
			  ce->engine->name, ce->ring->timeline->fence_context);

		ce->ops->unpin(ce);

		i915_gem_context_put(ce->gem_context);
		intel_context_active_release(ce);
	}

	mutex_unlock(&ce->pin_mutex);
	intel_context_put(ce);
}

static int __context_pin_state(struct i915_vma *vma)
{
	u64 flags;
	int err;

	flags = i915_ggtt_pin_bias(vma) | PIN_OFFSET_BIAS;
	flags |= PIN_HIGH | PIN_GLOBAL;

	err = i915_vma_pin(vma, 0, 0, flags);
	if (err)
		return err;

	/*
	 * And mark it as a globally pinned object to let the shrinker know
	 * it cannot reclaim the object until we release it.
	 */
	vma->obj->pin_global++;
	vma->obj->mm.dirty = true;

	return 0;
}

static void __context_unpin_state(struct i915_vma *vma)
{
	vma->obj->pin_global--;
	__i915_vma_unpin(vma);
}

static void __intel_context_retire(struct i915_active *active)
{
	struct intel_context *ce = container_of(active, typeof(*ce), active);

	GEM_TRACE("%s context:%llx retire\n",
		  ce->engine->name, ce->ring->timeline->fence_context);

	if (ce->state)
		__context_unpin_state(ce->state);

	intel_ring_unpin(ce->ring);
	intel_context_put(ce);
}

static int __intel_context_active(struct i915_active *active)
{
	struct intel_context *ce = container_of(active, typeof(*ce), active);
	int err;

	intel_context_get(ce);

	err = intel_ring_pin(ce->ring);
	if (err)
		goto err_put;

	if (!ce->state)
		return 0;

	err = __context_pin_state(ce->state);
	if (err)
		goto err_ring;

	/* Preallocate tracking nodes */
	if (!i915_gem_context_is_kernel(ce->gem_context)) {
		err = i915_active_acquire_preallocate_barrier(&ce->active,
							      ce->engine);
		if (err)
			goto err_state;
	}

	return 0;

err_state:
	__context_unpin_state(ce->state);
err_ring:
	intel_ring_unpin(ce->ring);
err_put:
	intel_context_put(ce);
	return err;
}

void
intel_context_init(struct intel_context *ce,
		   struct i915_gem_context *ctx,
		   struct intel_engine_cs *engine)
{
	GEM_BUG_ON(!engine->cops);

	kref_init(&ce->ref);

	ce->gem_context = ctx;
	ce->engine = engine;
	ce->ops = engine->cops;
	ce->sseu = engine->sseu;

	INIT_LIST_HEAD(&ce->signal_link);
	INIT_LIST_HEAD(&ce->signals);

	mutex_init(&ce->pin_mutex);

	i915_active_init(ctx->i915, &ce->active,
			 __intel_context_active, __intel_context_retire);
}

void intel_context_fini(struct intel_context *ce)
{
	mutex_destroy(&ce->pin_mutex);
	i915_active_fini(&ce->active);
}

static void i915_global_context_shrink(void)
{
	kmem_cache_shrink(global.slab_ce);
}

static void i915_global_context_exit(void)
{
	kmem_cache_destroy(global.slab_ce);
}

static struct i915_global_context global = { {
	.shrink = i915_global_context_shrink,
	.exit = i915_global_context_exit,
} };

int __init i915_global_context_init(void)
{
	global.slab_ce = KMEM_CACHE(intel_context, SLAB_HWCACHE_ALIGN);
	if (!global.slab_ce)
		return -ENOMEM;

	i915_global_register(&global.base);
	return 0;
}

void intel_context_enter_engine(struct intel_context *ce)
{
	intel_engine_pm_get(ce->engine);
}

void intel_context_exit_engine(struct intel_context *ce)
{
	intel_engine_pm_put(ce->engine);
}

int intel_context_prepare_remote_request(struct intel_context *ce,
					 struct i915_request *rq)
{
	struct intel_timeline *tl = ce->ring->timeline;
	int err;

	/* Only suitable for use in remotely modifying this context */
	GEM_BUG_ON(rq->hw_context == ce);

	if (rq->timeline != tl) { /* beware timeline sharing */
		err = mutex_lock_interruptible_nested(&tl->mutex,
						      SINGLE_DEPTH_NESTING);
		if (err)
			return err;

		/* Queue this switch after current activity by this context. */
		err = i915_active_request_set(&tl->last_request, rq);
		if (err)
			goto unlock;
	}
	lockdep_assert_held(&tl->mutex);

	/*
	 * Guarantee context image and the timeline remains pinned until the
	 * modifying request is retired by setting the ce activity tracker.
	 *
	 * But we only need to take one pin on the account of it. Or in other
	 * words transfer the pinned ce object to tracked active request.
	 */
	GEM_BUG_ON(i915_active_is_idle(&ce->active));
	err = i915_active_ref(&ce->active, rq->fence.context, rq);

unlock:
	if (rq->timeline != tl)
		mutex_unlock(&tl->mutex);
	return err;
}

struct i915_request *intel_context_create_request(struct intel_context *ce)
{
	struct i915_request *rq;
	int err;

	err = intel_context_pin(ce);
	if (unlikely(err))
		return ERR_PTR(err);

	rq = i915_request_create(ce);
	intel_context_unpin(ce);

	return rq;
}
