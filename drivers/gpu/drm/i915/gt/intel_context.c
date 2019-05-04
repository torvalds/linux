/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#include "i915_drv.h"
#include "i915_gem_context.h"
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
		with_intel_runtime_pm(ce->engine->i915, wakeref)
			err = ce->ops->pin(ce);
		if (err)
			goto err;

		i915_gem_context_get(ce->gem_context); /* for ctx->ppgtt */

		intel_context_get(ce);
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
		ce->ops->unpin(ce);

		i915_gem_context_put(ce->gem_context);
		intel_context_put(ce);
	}

	mutex_unlock(&ce->pin_mutex);
	intel_context_put(ce);
}

static void intel_context_retire(struct i915_active_request *active,
				 struct i915_request *rq)
{
	struct intel_context *ce =
		container_of(active, typeof(*ce), active_tracker);

	intel_context_unpin(ce);
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
	ce->saturated = 0;

	INIT_LIST_HEAD(&ce->signal_link);
	INIT_LIST_HEAD(&ce->signals);

	mutex_init(&ce->pin_mutex);

	i915_active_request_init(&ce->active_tracker,
				 NULL, intel_context_retire);
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
	ce->saturated = 0;
	intel_engine_pm_put(ce->engine);
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
