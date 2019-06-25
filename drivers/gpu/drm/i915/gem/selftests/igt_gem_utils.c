/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

#include "igt_gem_utils.h"

#include "gem/i915_gem_context.h"
#include "gem/i915_gem_pm.h"
#include "gt/intel_context.h"

#include "i915_request.h"

struct i915_request *
igt_request_alloc(struct i915_gem_context *ctx, struct intel_engine_cs *engine)
{
	struct intel_context *ce;
	struct i915_request *rq;

	/*
	 * Pinning the contexts may generate requests in order to acquire
	 * GGTT space, so do this first before we reserve a seqno for
	 * ourselves.
	 */
	ce = i915_gem_context_get_engine(ctx, engine->id);
	if (IS_ERR(ce))
		return ERR_CAST(ce);

	rq = intel_context_create_request(ce);
	intel_context_put(ce);

	return rq;
}
