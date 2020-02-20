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
#include "intel_ring.h"

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
intel_context_create(struct intel_engine_cs *engine)
{
	struct intel_context *ce;

	ce = intel_context_alloc();
	if (!ce)
		return ERR_PTR(-ENOMEM);

	intel_context_init(ce, engine);
	return ce;
}

int intel_context_alloc_state(struct intel_context *ce)
{
	int err = 0;

	if (mutex_lock_interruptible(&ce->pin_mutex))
		return -EINTR;

	if (!test_bit(CONTEXT_ALLOC_BIT, &ce->flags)) {
		err = ce->ops->alloc(ce);
		if (unlikely(err))
			goto unlock;

		set_bit(CONTEXT_ALLOC_BIT, &ce->flags);
	}

unlock:
	mutex_unlock(&ce->pin_mutex);
	return err;
}

static int intel_context_active_acquire(struct intel_context *ce)
{
	int err;

	__i915_active_acquire(&ce->active);

	if (intel_context_is_barrier(ce))
		return 0;

	/* Preallocate tracking nodes */
	err = i915_active_acquire_preallocate_barrier(&ce->active,
						      ce->engine);
	if (err)
		i915_active_release(&ce->active);

	return err;
}

static void intel_context_active_release(struct intel_context *ce)
{
	/* Nodes preallocated in intel_context_active() */
	i915_active_acquire_barrier(&ce->active);
	i915_active_release(&ce->active);
}

int __intel_context_do_pin(struct intel_context *ce)
{
	int err;

	if (unlikely(!test_bit(CONTEXT_ALLOC_BIT, &ce->flags))) {
		err = intel_context_alloc_state(ce);
		if (err)
			return err;
	}

	err = i915_active_acquire(&ce->active);
	if (err)
		return err;

	if (mutex_lock_interruptible(&ce->pin_mutex)) {
		err = -EINTR;
		goto out_release;
	}

	if (likely(!atomic_add_unless(&ce->pin_count, 1, 0))) {
		err = intel_context_active_acquire(ce);
		if (unlikely(err))
			goto out_unlock;

		err = ce->ops->pin(ce);
		if (unlikely(err))
			goto err_active;

		CE_TRACE(ce, "pin ring:{start:%08x, head:%04x, tail:%04x}\n",
			 i915_ggtt_offset(ce->ring->vma),
			 ce->ring->head, ce->ring->tail);

		smp_mb__before_atomic(); /* flush pin before it is visible */
		atomic_inc(&ce->pin_count);
	}

	GEM_BUG_ON(!intel_context_is_pinned(ce)); /* no overflow! */
	GEM_BUG_ON(i915_active_is_idle(&ce->active));
	goto out_unlock;

err_active:
	intel_context_active_release(ce);
out_unlock:
	mutex_unlock(&ce->pin_mutex);
out_release:
	i915_active_release(&ce->active);
	return err;
}

void intel_context_unpin(struct intel_context *ce)
{
	if (!atomic_dec_and_test(&ce->pin_count))
		return;

	CE_TRACE(ce, "unpin\n");
	ce->ops->unpin(ce);

	/*
	 * Once released, we may asynchronously drop the active reference.
	 * As that may be the only reference keeping the context alive,
	 * take an extra now so that it is not freed before we finish
	 * dereferencing it.
	 */
	intel_context_get(ce);
	intel_context_active_release(ce);
	intel_context_put(ce);
}

static int __context_pin_state(struct i915_vma *vma)
{
	unsigned int bias = i915_ggtt_pin_bias(vma) | PIN_OFFSET_BIAS;
	int err;

	err = i915_ggtt_pin(vma, 0, bias | PIN_HIGH);
	if (err)
		return err;

	err = i915_active_acquire(&vma->active);
	if (err)
		goto err_unpin;

	/*
	 * And mark it as a globally pinned object to let the shrinker know
	 * it cannot reclaim the object until we release it.
	 */
	i915_vma_make_unshrinkable(vma);
	vma->obj->mm.dirty = true;

	return 0;

err_unpin:
	i915_vma_unpin(vma);
	return err;
}

static void __context_unpin_state(struct i915_vma *vma)
{
	i915_vma_make_shrinkable(vma);
	i915_active_release(&vma->active);
	__i915_vma_unpin(vma);
}

static int __ring_active(struct intel_ring *ring)
{
	int err;

	err = i915_active_acquire(&ring->vma->active);
	if (err)
		return err;

	err = intel_ring_pin(ring);
	if (err)
		goto err_active;

	return 0;

err_active:
	i915_active_release(&ring->vma->active);
	return err;
}

static void __ring_retire(struct intel_ring *ring)
{
	intel_ring_unpin(ring);
	i915_active_release(&ring->vma->active);
}

__i915_active_call
static void __intel_context_retire(struct i915_active *active)
{
	struct intel_context *ce = container_of(active, typeof(*ce), active);

	CE_TRACE(ce, "retire runtime: { total:%lluns, avg:%lluns }\n",
		 intel_context_get_total_runtime_ns(ce),
		 intel_context_get_avg_runtime_ns(ce));

	set_bit(CONTEXT_VALID_BIT, &ce->flags);
	if (ce->state)
		__context_unpin_state(ce->state);

	intel_timeline_unpin(ce->timeline);
	__ring_retire(ce->ring);

	intel_context_put(ce);
}

static int __intel_context_active(struct i915_active *active)
{
	struct intel_context *ce = container_of(active, typeof(*ce), active);
	int err;

	CE_TRACE(ce, "active\n");

	intel_context_get(ce);

	err = __ring_active(ce->ring);
	if (err)
		goto err_put;

	err = intel_timeline_pin(ce->timeline);
	if (err)
		goto err_ring;

	if (!ce->state)
		return 0;

	err = __context_pin_state(ce->state);
	if (err)
		goto err_timeline;

	return 0;

err_timeline:
	intel_timeline_unpin(ce->timeline);
err_ring:
	__ring_retire(ce->ring);
err_put:
	intel_context_put(ce);
	return err;
}

void
intel_context_init(struct intel_context *ce,
		   struct intel_engine_cs *engine)
{
	GEM_BUG_ON(!engine->cops);
	GEM_BUG_ON(!engine->gt->vm);

	kref_init(&ce->ref);

	ce->engine = engine;
	ce->ops = engine->cops;
	ce->sseu = engine->sseu;
	ce->ring = __intel_context_ring_size(SZ_4K);

	ewma_runtime_init(&ce->runtime.avg);

	ce->vm = i915_vm_get(engine->gt->vm);

	INIT_LIST_HEAD(&ce->signal_link);
	INIT_LIST_HEAD(&ce->signals);

	mutex_init(&ce->pin_mutex);

	i915_active_init(&ce->active,
			 __intel_context_active, __intel_context_retire);
}

void intel_context_fini(struct intel_context *ce)
{
	if (ce->timeline)
		intel_timeline_put(ce->timeline);
	i915_vm_put(ce->vm);

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
	intel_timeline_enter(ce->timeline);
}

void intel_context_exit_engine(struct intel_context *ce)
{
	intel_timeline_exit(ce->timeline);
	intel_engine_pm_put(ce->engine);
}

int intel_context_prepare_remote_request(struct intel_context *ce,
					 struct i915_request *rq)
{
	struct intel_timeline *tl = ce->timeline;
	int err;

	/* Only suitable for use in remotely modifying this context */
	GEM_BUG_ON(rq->context == ce);

	if (rcu_access_pointer(rq->timeline) != tl) { /* timeline sharing! */
		/* Queue this switch after current activity by this context. */
		err = i915_active_fence_set(&tl->last_request, rq);
		if (err)
			return err;
	}

	/*
	 * Guarantee context image and the timeline remains pinned until the
	 * modifying request is retired by setting the ce activity tracker.
	 *
	 * But we only need to take one pin on the account of it. Or in other
	 * words transfer the pinned ce object to tracked active request.
	 */
	GEM_BUG_ON(i915_active_is_idle(&ce->active));
	return i915_active_add_request(&ce->active, rq);
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

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftest_context.c"
#endif
