/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __INTEL_MIGRATE__
#define __INTEL_MIGRATE__

#include <linux/types.h>

#include "intel_migrate_types.h"

struct dma_fence;
struct i915_request;
struct i915_gem_ww_ctx;
struct intel_gt;
struct scatterlist;
enum i915_cache_level;

int intel_migrate_init(struct intel_migrate *m, struct intel_gt *gt);

struct intel_context *intel_migrate_create_context(struct intel_migrate *m);

int intel_migrate_copy(struct intel_migrate *m,
		       struct i915_gem_ww_ctx *ww,
		       struct dma_fence *await,
		       struct scatterlist *src,
		       enum i915_cache_level src_cache_level,
		       bool src_is_lmem,
		       struct scatterlist *dst,
		       enum i915_cache_level dst_cache_level,
		       bool dst_is_lmem,
		       struct i915_request **out);

int intel_context_migrate_copy(struct intel_context *ce,
			       struct dma_fence *await,
			       struct scatterlist *src,
			       enum i915_cache_level src_cache_level,
			       bool src_is_lmem,
			       struct scatterlist *dst,
			       enum i915_cache_level dst_cache_level,
			       bool dst_is_lmem,
			       struct i915_request **out);

int
intel_migrate_clear(struct intel_migrate *m,
		    struct i915_gem_ww_ctx *ww,
		    struct dma_fence *await,
		    struct scatterlist *sg,
		    enum i915_cache_level cache_level,
		    bool is_lmem,
		    u32 value,
		    struct i915_request **out);
int
intel_context_migrate_clear(struct intel_context *ce,
			    struct dma_fence *await,
			    struct scatterlist *sg,
			    enum i915_cache_level cache_level,
			    bool is_lmem,
			    u32 value,
			    struct i915_request **out);

void intel_migrate_fini(struct intel_migrate *m);

#endif /* __INTEL_MIGRATE__ */
