/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

#ifndef __IGT_GEM_UTILS_H__
#define __IGT_GEM_UTILS_H__

#include <linux/types.h>

#include "i915_vma.h"

struct i915_request;
struct i915_gem_context;
struct i915_vma;

struct intel_context;
struct intel_engine_cs;

struct i915_request *
igt_request_alloc(struct i915_gem_context *ctx, struct intel_engine_cs *engine);

struct i915_vma *
igt_emit_store_dw(struct i915_vma *vma,
		  u64 offset,
		  unsigned long count,
		  u32 val);

int igt_gpu_fill_dw(struct intel_context *ce,
		    struct i915_vma *vma, u64 offset,
		    unsigned long count, u32 val);

static inline int __must_check
igt_vma_move_to_active_unlocked(struct i915_vma *vma, struct i915_request *rq,
				unsigned int flags)
{
	int err;

	i915_vma_lock(vma);
	err = i915_vma_move_to_active(vma, rq, flags);
	i915_vma_unlock(vma);
	return err;
}

#endif /* __IGT_GEM_UTILS_H__ */
