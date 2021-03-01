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

struct intel_gt_buffer_pool_node *
intel_gt_get_buffer_pool(struct intel_gt *gt, size_t size,
			 enum i915_map_type type);

static inline int
intel_gt_buffer_pool_mark_active(struct intel_gt_buffer_pool_node *node,
				 struct i915_request *rq)
{
	return i915_active_add_request(&node->active, rq);
}

static inline void
intel_gt_buffer_pool_put(struct intel_gt_buffer_pool_node *node)
{
	i915_active_release(&node->active);
}

void intel_gt_init_buffer_pool(struct intel_gt *gt);
void intel_gt_flush_buffer_pool(struct intel_gt *gt);
void intel_gt_fini_buffer_pool(struct intel_gt *gt);

#endif /* INTEL_GT_BUFFER_POOL_H */
