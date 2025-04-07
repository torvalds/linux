/* SPDX-License-Identifier: MIT */
/* Copyright © 2025 Intel Corporation */

#ifndef __I915_GEM_OBJECT_H__
#define __I915_GEM_OBJECT_H__

struct dma_fence;
struct i915_sched_attr;

static inline void i915_gem_fence_wait_priority(struct dma_fence *fence,
						const struct i915_sched_attr *attr)
{
}

#endif
