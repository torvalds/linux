/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef __I915_VMA_RESOURCE_H__
#define __I915_VMA_RESOURCE_H__

#include <linux/dma-fence.h>
#include <linux/refcount.h>

/**
 * struct i915_vma_resource - Snapshotted unbind information.
 * @unbind_fence: Fence to mark unbinding complete. Note that this fence
 * is not considered published until unbind is scheduled, and as such it
 * is illegal to access this fence before scheduled unbind other than
 * for refcounting.
 * @lock: The @unbind_fence lock.
 * @hold_count: Number of holders blocking the fence from finishing.
 * The vma itself is keeping a hold, which is released when unbind
 * is scheduled.
 *
 * The lifetime of a struct i915_vma_resource is from a binding request to
 * the actual possible asynchronous unbind has completed.
 */
struct i915_vma_resource {
	struct dma_fence unbind_fence;
	/* See above for description of the lock. */
	spinlock_t lock;
	refcount_t hold_count;
};

bool i915_vma_resource_hold(struct i915_vma_resource *vma_res,
			    bool *lockdep_cookie);

void i915_vma_resource_unhold(struct i915_vma_resource *vma_res,
			      bool lockdep_cookie);

struct i915_vma_resource *i915_vma_resource_alloc(void);

struct dma_fence *i915_vma_resource_unbind(struct i915_vma_resource *vma_res);

/**
 * i915_vma_resource_get - Take a reference on a vma resource
 * @vma_res: The vma resource on which to take a reference.
 *
 * Return: The @vma_res pointer
 */
static inline struct i915_vma_resource
*i915_vma_resource_get(struct i915_vma_resource *vma_res)
{
	dma_fence_get(&vma_res->unbind_fence);
	return vma_res;
}

/**
 * i915_vma_resource_put - Release a reference to a struct i915_vma_resource
 * @vma_res: The resource
 */
static inline void i915_vma_resource_put(struct i915_vma_resource *vma_res)
{
	dma_fence_put(&vma_res->unbind_fence);
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
void i915_vma_resource_init(struct i915_vma_resource *vma_res);
#endif

#endif
