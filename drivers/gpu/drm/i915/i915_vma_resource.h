/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef __I915_VMA_RESOURCE_H__
#define __I915_VMA_RESOURCE_H__

#include <linux/dma-fence.h>
#include <linux/refcount.h>

#include "i915_gem.h"

struct i915_page_sizes {
	/**
	 * The sg mask of the pages sg_table. i.e the mask of
	 * the lengths for each sg entry.
	 */
	unsigned int phys;

	/**
	 * The gtt page sizes we are allowed to use given the
	 * sg mask and the supported page sizes. This will
	 * express the smallest unit we can use for the whole
	 * object, as well as the larger sizes we may be able
	 * to use opportunistically.
	 */
	unsigned int sg;
};

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
 * @private: Bind backend private info.
 * @start: Offset into the address space of bind range start.
 * @node_size: Size of the allocated range manager node.
 * @vma_size: Bind size.
 * @page_sizes_gtt: Resulting page sizes from the bind operation.
 * @bound_flags: Flags indicating binding status.
 * @allocated: Backend private data. TODO: Should move into @private.
 *
 * The lifetime of a struct i915_vma_resource is from a binding request to
 * the actual possible asynchronous unbind has completed.
 */
struct i915_vma_resource {
	struct dma_fence unbind_fence;
	/* See above for description of the lock. */
	spinlock_t lock;
	refcount_t hold_count;

	/**
	 * struct i915_vma_bindinfo - Information needed for async bind
	 * only but that can be dropped after the bind has taken place.
	 * Consider making this a separate argument to the bind_vma
	 * op, coalescing with other arguments like vm, stash, cache_level
	 * and flags
	 * @pages: The pages sg-table.
	 * @page_sizes: Page sizes of the pages.
	 * @readonly: Whether the vma should be bound read-only.
	 * @lmem: Whether the vma points to lmem.
	 */
	struct i915_vma_bindinfo {
		struct sg_table *pages;
		struct i915_page_sizes page_sizes;
		bool readonly:1;
		bool lmem:1;
	} bi;

	void *private;
	u64 start;
	u64 node_size;
	u64 vma_size;
	u32 page_sizes_gtt;
	u32 bound_flags;
	bool allocated:1;
};

bool i915_vma_resource_hold(struct i915_vma_resource *vma_res,
			    bool *lockdep_cookie);

void i915_vma_resource_unhold(struct i915_vma_resource *vma_res,
			      bool lockdep_cookie);

struct i915_vma_resource *i915_vma_resource_alloc(void);

struct dma_fence *i915_vma_resource_unbind(struct i915_vma_resource *vma_res);

void __i915_vma_resource_init(struct i915_vma_resource *vma_res);

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

/**
 * i915_vma_resource_init - Initialize a vma resource.
 * @vma_res: The vma resource to initialize
 * @pages: The pages sg-table.
 * @page_sizes: Page sizes of the pages.
 * @readonly: Whether the vma should be bound read-only.
 * @lmem: Whether the vma points to lmem.
 * @private: Bind backend private info.
 * @start: Offset into the address space of bind range start.
 * @node_size: Size of the allocated range manager node.
 * @size: Bind size.
 *
 * Initializes a vma resource allocated using i915_vma_resource_alloc().
 * The reason for having separate allocate and initialize function is that
 * initialization may need to be performed from under a lock where
 * allocation is not allowed.
 */
static inline void i915_vma_resource_init(struct i915_vma_resource *vma_res,
					  struct sg_table *pages,
					  const struct i915_page_sizes *page_sizes,
					  bool readonly,
					  bool lmem,
					  void *private,
					  u64 start,
					  u64 node_size,
					  u64 size)
{
	__i915_vma_resource_init(vma_res);
	vma_res->bi.pages = pages;
	vma_res->bi.page_sizes = *page_sizes;
	vma_res->bi.readonly = readonly;
	vma_res->bi.lmem = lmem;
	vma_res->private = private;
	vma_res->start = start;
	vma_res->node_size = node_size;
	vma_res->vma_size = size;
}

static inline void i915_vma_resource_fini(struct i915_vma_resource *vma_res)
{
	GEM_BUG_ON(refcount_read(&vma_res->hold_count) != 1);
}

#endif
