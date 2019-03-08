/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_CONTEXT_H__
#define __INTEL_CONTEXT_H__

#include "i915_gem_context_types.h"
#include "intel_context_types.h"
#include "intel_engine_types.h"

void intel_context_init(struct intel_context *ce,
			struct i915_gem_context *ctx,
			struct intel_engine_cs *engine);

static inline struct intel_context *
to_intel_context(struct i915_gem_context *ctx,
		 const struct intel_engine_cs *engine)
{
	return &ctx->__engine[engine->id];
}

static inline struct intel_context *
intel_context_pin(struct i915_gem_context *ctx, struct intel_engine_cs *engine)
{
	return engine->context_pin(engine, ctx);
}

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
