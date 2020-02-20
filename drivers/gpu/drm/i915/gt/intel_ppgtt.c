// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <linux/slab.h>

#include "i915_trace.h"
#include "intel_gtt.h"
#include "gen6_ppgtt.h"
#include "gen8_ppgtt.h"

struct i915_page_table *alloc_pt(struct i915_address_space *vm)
{
	struct i915_page_table *pt;

	pt = kmalloc(sizeof(*pt), I915_GFP_ALLOW_FAIL);
	if (unlikely(!pt))
		return ERR_PTR(-ENOMEM);

	if (unlikely(setup_page_dma(vm, &pt->base))) {
		kfree(pt);
		return ERR_PTR(-ENOMEM);
	}

	atomic_set(&pt->used, 0);
	return pt;
}

struct i915_page_directory *__alloc_pd(size_t sz)
{
	struct i915_page_directory *pd;

	pd = kzalloc(sz, I915_GFP_ALLOW_FAIL);
	if (unlikely(!pd))
		return NULL;

	spin_lock_init(&pd->lock);
	return pd;
}

struct i915_page_directory *alloc_pd(struct i915_address_space *vm)
{
	struct i915_page_directory *pd;

	pd = __alloc_pd(sizeof(*pd));
	if (unlikely(!pd))
		return ERR_PTR(-ENOMEM);

	if (unlikely(setup_page_dma(vm, px_base(pd)))) {
		kfree(pd);
		return ERR_PTR(-ENOMEM);
	}

	return pd;
}

void free_pd(struct i915_address_space *vm, struct i915_page_dma *pd)
{
	cleanup_page_dma(vm, pd);
	kfree(pd);
}

static inline void
write_dma_entry(struct i915_page_dma * const pdma,
		const unsigned short idx,
		const u64 encoded_entry)
{
	u64 * const vaddr = kmap_atomic(pdma->page);

	vaddr[idx] = encoded_entry;
	kunmap_atomic(vaddr);
}

void
__set_pd_entry(struct i915_page_directory * const pd,
	       const unsigned short idx,
	       struct i915_page_dma * const to,
	       u64 (*encode)(const dma_addr_t, const enum i915_cache_level))
{
	/* Each thread pre-pins the pd, and we may have a thread per pde. */
	GEM_BUG_ON(atomic_read(px_used(pd)) > NALLOC * ARRAY_SIZE(pd->entry));

	atomic_inc(px_used(pd));
	pd->entry[idx] = to;
	write_dma_entry(px_base(pd), idx, encode(to->daddr, I915_CACHE_LLC));
}

void
clear_pd_entry(struct i915_page_directory * const pd,
	       const unsigned short idx,
	       const struct i915_page_scratch * const scratch)
{
	GEM_BUG_ON(atomic_read(px_used(pd)) == 0);

	write_dma_entry(px_base(pd), idx, scratch->encode);
	pd->entry[idx] = NULL;
	atomic_dec(px_used(pd));
}

bool
release_pd_entry(struct i915_page_directory * const pd,
		 const unsigned short idx,
		 struct i915_page_table * const pt,
		 const struct i915_page_scratch * const scratch)
{
	bool free = false;

	if (atomic_add_unless(&pt->used, -1, 1))
		return false;

	spin_lock(&pd->lock);
	if (atomic_dec_and_test(&pt->used)) {
		clear_pd_entry(pd, idx, scratch);
		free = true;
	}
	spin_unlock(&pd->lock);

	return free;
}

int i915_ppgtt_init_hw(struct intel_gt *gt)
{
	struct drm_i915_private *i915 = gt->i915;

	gtt_write_workarounds(gt);

	if (IS_GEN(i915, 6))
		gen6_ppgtt_enable(gt);
	else if (IS_GEN(i915, 7))
		gen7_ppgtt_enable(gt);

	return 0;
}

static struct i915_ppgtt *
__ppgtt_create(struct intel_gt *gt)
{
	if (INTEL_GEN(gt->i915) < 8)
		return gen6_ppgtt_create(gt);
	else
		return gen8_ppgtt_create(gt);
}

struct i915_ppgtt *i915_ppgtt_create(struct intel_gt *gt)
{
	struct i915_ppgtt *ppgtt;

	ppgtt = __ppgtt_create(gt);
	if (IS_ERR(ppgtt))
		return ppgtt;

	trace_i915_ppgtt_create(&ppgtt->vm);

	return ppgtt;
}

static int ppgtt_bind_vma(struct i915_vma *vma,
			  enum i915_cache_level cache_level,
			  u32 flags)
{
	u32 pte_flags;
	int err;

	if (flags & I915_VMA_ALLOC) {
		err = vma->vm->allocate_va_range(vma->vm,
						 vma->node.start, vma->size);
		if (err)
			return err;

		set_bit(I915_VMA_ALLOC_BIT, __i915_vma_flags(vma));
	}

	/* Applicable to VLV, and gen8+ */
	pte_flags = 0;
	if (i915_gem_object_is_readonly(vma->obj))
		pte_flags |= PTE_READ_ONLY;

	GEM_BUG_ON(!test_bit(I915_VMA_ALLOC_BIT, __i915_vma_flags(vma)));
	vma->vm->insert_entries(vma->vm, vma, cache_level, pte_flags);
	wmb();

	return 0;
}

static void ppgtt_unbind_vma(struct i915_vma *vma)
{
	if (test_and_clear_bit(I915_VMA_ALLOC_BIT, __i915_vma_flags(vma)))
		vma->vm->clear_range(vma->vm, vma->node.start, vma->size);
}

int ppgtt_set_pages(struct i915_vma *vma)
{
	GEM_BUG_ON(vma->pages);

	vma->pages = vma->obj->mm.pages;

	vma->page_sizes = vma->obj->mm.page_sizes;

	return 0;
}

void ppgtt_init(struct i915_ppgtt *ppgtt, struct intel_gt *gt)
{
	struct drm_i915_private *i915 = gt->i915;

	ppgtt->vm.gt = gt;
	ppgtt->vm.i915 = i915;
	ppgtt->vm.dma = &i915->drm.pdev->dev;
	ppgtt->vm.total = BIT_ULL(INTEL_INFO(i915)->ppgtt_size);

	i915_address_space_init(&ppgtt->vm, VM_CLASS_PPGTT);

	ppgtt->vm.vma_ops.bind_vma    = ppgtt_bind_vma;
	ppgtt->vm.vma_ops.unbind_vma  = ppgtt_unbind_vma;
	ppgtt->vm.vma_ops.set_pages   = ppgtt_set_pages;
	ppgtt->vm.vma_ops.clear_pages = clear_pages;
}
