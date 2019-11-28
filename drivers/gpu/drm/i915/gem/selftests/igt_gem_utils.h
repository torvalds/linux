/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

#ifndef __IGT_GEM_UTILS_H__
#define __IGT_GEM_UTILS_H__

#include <linux/types.h>

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

#endif /* __IGT_GEM_UTILS_H__ */
