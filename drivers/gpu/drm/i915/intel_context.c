/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#include "i915_drv.h"
#include "i915_gem_context.h"
#include "i915_globals.h"
#include "intel_context.h"
#include "intel_ringbuffer.h"

static struct i915_global_context {
	struct i915_global base;
	struct kmem_cache *slab_ce;
} global;

struct intel_context *intel_context_alloc(void)
{
	return kmem_cache_zalloc(global.slab_ce, GFP_KERNEL);
}

void intel_context_free(struct intel_context *ce)
{
	kmem_cache_free(global.slab_ce, ce);
}

struct intel_context *
intel_context_lookup(struct i915_gem_context *ctx,
		     struct intel_engine_cs *engine)
{
	struct intel_context *ce = NULL;
	struct rb_node *p;

	spin_lock(&ctx->hw_contexts_lock);
	p = ctx->hw_contexts.rb_node;
	while (p) {
		struct intel_context *this =
			rb_entry(p, struct intel_context, node);

		if (this->engine == engine) {
			GEM_BUG_ON(this->gem_context != ctx);
			ce = this;
			break;
		}

		if (this->engine < engine)
			p = p->rb_right;
		else
			p = p->rb_left;
	}
	spin_unlock(&ctx->hw_contexts_lock);

	return ce;
}

struct intel_context *
__intel_context_insert(struct i915_gem_context *ctx,
		       struct intel_engine_cs *engine,
		       struct intel_context *ce)
{
	struct rb_node **p, *parent;
	int err = 0;

	spin_lock(&ctx->hw_contexts_lock);

	parent = NULL;
	p = &ctx->hw_contexts.rb_node;
	while (*p) {
		struct intel_context *this;

		parent = *p;
		this = rb_entry(parent, struct intel_context, node);

		if (this->engine == engine) {
			err = -EEXIST;
			ce = this;
			break;
		}

		if (this->engine < engine)
			p = &parent->rb_right;
		else
			p = &parent->rb_left;
	}
	if (!err) {
		rb_link_node(&ce->node, parent, p);
		rb_insert_color(&ce->node, &ctx->hw_contexts);
	}

	spin_unlock(&ctx->hw_contexts_lock);

	return ce;
}

void __intel_context_remove(struct intel_context *ce)
{
	struct i915_gem_context *ctx = ce->gem_context;

	spin_lock(&ctx->hw_contexts_lock);
	rb_erase(&ce->node, &ctx->hw_contexts);
	spin_unlock(&ctx->hw_contexts_lock);
}

struct intel_context *
intel_context_instance(struct i915_gem_context *ctx,
		       struct intel_engine_cs *engine)
{
	struct intel_context *ce, *pos;

	ce = intel_context_lookup(ctx, engine);
	if (likely(ce))
		return ce;

	ce = intel_context_alloc();
	if (!ce)
		return ERR_PTR(-ENOMEM);

	intel_context_init(ce, ctx, engine);

	pos = __intel_context_insert(ctx, engine, ce);
	if (unlikely(pos != ce)) /* Beaten! Use their HW context instead */
		intel_context_free(ce);

	GEM_BUG_ON(intel_context_lookup(ctx, engine) != pos);
	return pos;
}

struct intel_context *
intel_context_pin(struct i915_gem_context *ctx,
		  struct intel_engine_cs *engine)
{
	struct intel_context *ce;
	int err;

	lockdep_assert_held(&ctx->i915->drm.struct_mutex);

	ce = intel_context_instance(ctx, engine);
	if (IS_ERR(ce))
		return ce;

	if (unlikely(!ce->pin_count++)) {
		err = ce->ops->pin(ce);
		if (err)
			goto err_unpin;

		mutex_lock(&ctx->mutex);
		list_add(&ce->active_link, &ctx->active_engines);
		mutex_unlock(&ctx->mutex);

		i915_gem_context_get(ctx);
		GEM_BUG_ON(ce->gem_context != ctx);
	}
	GEM_BUG_ON(!ce->pin_count); /* no overflow! */

	return ce;

err_unpin:
	ce->pin_count = 0;
	return ERR_PTR(err);
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
	ce->gem_context = ctx;
	ce->engine = engine;
	ce->ops = engine->cops;

	INIT_LIST_HEAD(&ce->signal_link);
	INIT_LIST_HEAD(&ce->signals);

	/* Use the whole device by default */
	ce->sseu = intel_device_default_sseu(ctx->i915);

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
