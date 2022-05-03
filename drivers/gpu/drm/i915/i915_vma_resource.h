/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef __I915_VMA_RESOURCE_H__
#define __I915_VMA_RESOURCE_H__

#include <linux/dma-fence.h>
#include <linux/refcount.h>

#include "i915_gem.h"
#include "i915_scatterlist.h"
#include "i915_sw_fence.h"
#include "intel_runtime_pm.h"

struct intel_memory_region;

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
 * @work: Work struct for deferred unbind work.
 * @chain: Pointer to struct i915_sw_fence used to await dependencies.
 * @rb: Rb node for the vm's pending unbind interval tree.
 * @__subtree_last: Interval tree private member.
 * @vm: non-refcounted pointer to the vm. This is for internal use only and
 * this member is cleared after vm_resource unbind.
 * @mr: The memory region of the object pointed to by the vma.
 * @ops: Pointer to the backend i915_vma_ops.
 * @private: Bind backend private info.
 * @start: Offset into the address space of bind range start.
 * @node_size: Size of the allocated range manager node.
 * @vma_size: Bind size.
 * @page_sizes_gtt: Resulting page sizes from the bind operation.
 * @bound_flags: Flags indicating binding status.
 * @allocated: Backend private data. TODO: Should move into @private.
 * @immediate_unbind: Unbind can be done immediately and doesn't need to be
 * deferred to a work item awaiting unsignaled fences. This is a hack.
 * (dma_fence_work uses a fence flag for this, but this seems slightly
 * cleaner).
 * @needs_wakeref: Whether a wakeref is needed during unbind. Since we can't
 * take a wakeref in the dma-fence signalling critical path, it needs to be
 * taken when the unbind is scheduled.
 * @skip_pte_rewrite: During ggtt suspend and vm takedown pte rewriting
 * needs to be skipped for unbind.
 *
 * The lifetime of a struct i915_vma_resource is from a binding request to
 * the actual possible asynchronous unbind has completed.
 */
struct i915_vma_resource {
	struct dma_fence unbind_fence;
	/* See above for description of the lock. */
	spinlock_t lock;
	refcount_t hold_count;
	struct work_struct work;
	struct i915_sw_fence chain;
	struct rb_node rb;
	u64 __subtree_last;
	struct i915_address_space *vm;
	intel_wakeref_t wakeref;

	/**
	 * struct i915_vma_bindinfo - Information needed for async bind
	 * only but that can be dropped after the bind has taken place.
	 * Consider making this a separate argument to the bind_vma
	 * op, coalescing with other arguments like vm, stash, cache_level
	 * and flags
	 * @pages: The pages sg-table.
	 * @page_sizes: Page sizes of the pages.
	 * @pages_rsgt: Refcounted sg-table when delayed object destruction
	 * is supported. May be NULL.
	 * @readonly: Whether the vma should be bound read-only.
	 * @lmem: Whether the vma points to lmem.
	 */
	struct i915_vma_bindinfo {
		struct sg_table *pages;
		struct i915_page_sizes page_sizes;
		struct i915_refct_sgt *pages_rsgt;
		bool readonly:1;
		bool lmem:1;
	} bi;

#if IS_ENABLED(CONFIG_DRM_I915_CAPTURE_ERROR)
	struct intel_memory_region *mr;
#endif
	const struct i915_vma_ops *ops;
	void *private;
	u64 start;
	u64 node_size;
	u64 vma_size;
	u32 page_sizes_gtt;

	u32 bound_flags;
	bool allocated:1;
	bool immediate_unbind:1;
	bool needs_wakeref:1;
	bool skip_pte_rewrite:1;
};

bool i915_vma_resource_hold(struct i915_vma_resource *vma_res,
			    bool *lockdep_cookie);

void i915_vma_resource_unhold(struct i915_vma_resource *vma_res,
			      bool lockdep_cookie);

struct i915_vma_resource *i915_vma_resource_alloc(void);

void i915_vma_resource_free(struct i915_vma_resource *vma_res);

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
 * @vm: Pointer to the vm.
 * @pages: The pages sg-table.
 * @page_sizes: Page sizes of the pages.
 * @pages_rsgt: Pointer to a struct i915_refct_sgt of an object with
 * delayed destruction.
 * @readonly: Whether the vma should be bound read-only.
 * @lmem: Whether the vma points to lmem.
 * @mr: The memory region of the object the vma points to.
 * @ops: The backend ops.
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
					  struct i915_address_space *vm,
					  struct sg_table *pages,
					  const struct i915_page_sizes *page_sizes,
					  struct i915_refct_sgt *pages_rsgt,
					  bool readonly,
					  bool lmem,
					  struct intel_memory_region *mr,
					  const struct i915_vma_ops *ops,
					  void *private,
					  u64 start,
					  u64 node_size,
					  u64 size)
{
	__i915_vma_resource_init(vma_res);
	vma_res->vm = vm;
	vma_res->bi.pages = pages;
	vma_res->bi.page_sizes = *page_sizes;
	if (pages_rsgt)
		vma_res->bi.pages_rsgt = i915_refct_sgt_get(pages_rsgt);
	vma_res->bi.readonly = readonly;
	vma_res->bi.lmem = lmem;
#if IS_ENABLED(CONFIG_DRM_I915_CAPTURE_ERROR)
	vma_res->mr = mr;
#endif
	vma_res->ops = ops;
	vma_res->private = private;
	vma_res->start = start;
	vma_res->node_size = node_size;
	vma_res->vma_size = size;
}

static inline void i915_vma_resource_fini(struct i915_vma_resource *vma_res)
{
	GEM_BUG_ON(refcount_read(&vma_res->hold_count) != 1);
	if (vma_res->bi.pages_rsgt)
		i915_refct_sgt_put(vma_res->bi.pages_rsgt);
	i915_sw_fence_fini(&vma_res->chain);
}

int i915_vma_resource_bind_dep_sync(struct i915_address_space *vm,
				    u64 first,
				    u64 last,
				    bool intr);

int i915_vma_resource_bind_dep_await(struct i915_address_space *vm,
				     struct i915_sw_fence *sw_fence,
				     u64 first,
				     u64 last,
				     bool intr,
				     gfp_t gfp);

void i915_vma_resource_bind_dep_sync_all(struct i915_address_space *vm);

void i915_vma_resource_module_exit(void);

int i915_vma_resource_module_init(void);

#endif
