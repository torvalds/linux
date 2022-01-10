// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */
#include <linux/slab.h>

#include "i915_vma_resource.h"

/* Callbacks for the unbind dma-fence. */
static const char *get_driver_name(struct dma_fence *fence)
{
	return "vma unbind fence";
}

static const char *get_timeline_name(struct dma_fence *fence)
{
	return "unbound";
}

static struct dma_fence_ops unbind_fence_ops = {
	.get_driver_name = get_driver_name,
	.get_timeline_name = get_timeline_name,
};

/**
 * i915_vma_resource_init - Initialize a vma resource.
 * @vma_res: The vma resource to initialize
 *
 * Initializes a vma resource allocated using i915_vma_resource_alloc().
 * The reason for having separate allocate and initialize function is that
 * initialization may need to be performed from under a lock where
 * allocation is not allowed.
 */
void i915_vma_resource_init(struct i915_vma_resource *vma_res)
{
	spin_lock_init(&vma_res->lock);
	dma_fence_init(&vma_res->unbind_fence, &unbind_fence_ops,
		       &vma_res->lock, 0, 0);
	refcount_set(&vma_res->hold_count, 1);
}

/**
 * i915_vma_resource_alloc - Allocate a vma resource
 *
 * Return: A pointer to a cleared struct i915_vma_resource or
 * a -ENOMEM error pointer if allocation fails.
 */
struct i915_vma_resource *i915_vma_resource_alloc(void)
{
	struct i915_vma_resource *vma_res =
		kzalloc(sizeof(*vma_res), GFP_KERNEL);

	return vma_res ? vma_res : ERR_PTR(-ENOMEM);
}

static void __i915_vma_resource_unhold(struct i915_vma_resource *vma_res)
{
	if (refcount_dec_and_test(&vma_res->hold_count))
		dma_fence_signal(&vma_res->unbind_fence);
}

/**
 * i915_vma_resource_unhold - Unhold the signaling of the vma resource unbind
 * fence.
 * @vma_res: The vma resource.
 * @lockdep_cookie: The lockdep cookie returned from i915_vma_resource_hold.
 *
 * The function may leave a dma_fence critical section.
 */
void i915_vma_resource_unhold(struct i915_vma_resource *vma_res,
			      bool lockdep_cookie)
{
	dma_fence_end_signalling(lockdep_cookie);

	if (IS_ENABLED(CONFIG_PROVE_LOCKING)) {
		unsigned long irq_flags;

		/* Inefficient open-coded might_lock_irqsave() */
		spin_lock_irqsave(&vma_res->lock, irq_flags);
		spin_unlock_irqrestore(&vma_res->lock, irq_flags);
	}

	__i915_vma_resource_unhold(vma_res);
}

/**
 * i915_vma_resource_hold - Hold the signaling of the vma resource unbind fence.
 * @vma_res: The vma resource.
 * @lockdep_cookie: Pointer to a bool serving as a lockdep cooke that should
 * be given as an argument to the pairing i915_vma_resource_unhold.
 *
 * If returning true, the function enters a dma_fence signalling critical
 * section if not in one already.
 *
 * Return: true if holding successful, false if not.
 */
bool i915_vma_resource_hold(struct i915_vma_resource *vma_res,
			    bool *lockdep_cookie)
{
	bool held = refcount_inc_not_zero(&vma_res->hold_count);

	if (held)
		*lockdep_cookie = dma_fence_begin_signalling();

	return held;
}

/**
 * i915_vma_resource_unbind - Unbind a vma resource
 * @vma_res: The vma resource to unbind.
 *
 * At this point this function does little more than publish a fence that
 * signals immediately unless signaling is held back.
 *
 * Return: A refcounted pointer to a dma-fence that signals when unbinding is
 * complete.
 */
struct dma_fence *
i915_vma_resource_unbind(struct i915_vma_resource *vma_res)
{
	__i915_vma_resource_unhold(vma_res);
	dma_fence_get(&vma_res->unbind_fence);
	return &vma_res->unbind_fence;
}
