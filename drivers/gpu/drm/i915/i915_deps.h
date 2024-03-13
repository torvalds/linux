/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef _I915_DEPS_H_
#define _I915_DEPS_H_

#include <linux/types.h>

struct ttm_operation_ctx;
struct dma_fence;
struct dma_resv;

/**
 * struct i915_deps - Collect dependencies into a single dma-fence
 * @single: Storage for pointer if the collection is a single fence.
 * @fences: Allocated array of fence pointers if more than a single fence;
 * otherwise points to the address of @single.
 * @num_deps: Current number of dependency fences.
 * @fences_size: Size of the @fences array in number of pointers.
 * @gfp: Allocation mode.
 */
struct i915_deps {
	struct dma_fence *single;
	struct dma_fence **fences;
	unsigned int num_deps;
	unsigned int fences_size;
	gfp_t gfp;
};

void i915_deps_init(struct i915_deps *deps, gfp_t gfp);

void i915_deps_fini(struct i915_deps *deps);

int i915_deps_add_dependency(struct i915_deps *deps,
			     struct dma_fence *fence,
			     const struct ttm_operation_ctx *ctx);

int i915_deps_add_resv(struct i915_deps *deps, struct dma_resv *resv,
		       const struct ttm_operation_ctx *ctx);

int i915_deps_sync(const struct i915_deps *deps,
		   const struct ttm_operation_ctx *ctx);
#endif
