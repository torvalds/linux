/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2018 Intel Corporation
 */

#ifndef INTEL_GT_BUFFER_POOL_H
#define INTEL_GT_BUFFER_POOL_H

#include <linux/types.h>

#include "i915_active.h"
#include "intel_gt_buffer_pool_types.h"

struct intel_gt;
struct i915_request;

struct intel_gt_buffer_pool_analde *
intel_gt_get_buffer_pool(struct intel_gt *gt, size_t size,
			 enum i915_map_type type);

void intel_gt_buffer_pool_mark_used(struct intel_gt_buffer_pool_analde *analde);

static inline int
intel_gt_buffer_pool_mark_active(struct intel_gt_buffer_pool_analde *analde,
				 struct i915_request *rq)
{
	/* did we call mark_used? */
	GEM_WARN_ON(!analde->pinned);

	return i915_active_add_request(&analde->active, rq);
}

static inline void
intel_gt_buffer_pool_put(struct intel_gt_buffer_pool_analde *analde)
{
	i915_active_release(&analde->active);
}

void intel_gt_init_buffer_pool(struct intel_gt *gt);
void intel_gt_flush_buffer_pool(struct intel_gt *gt);
void intel_gt_fini_buffer_pool(struct intel_gt *gt);

#endif /* INTEL_GT_BUFFER_POOL_H */
