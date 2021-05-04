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

	pt->base = vm->alloc_pt_dma(vm, I915_GTT_PAGE_SIZE_4K);
	if (IS_ERR(pt->base)) {
		kfree(pt);
		return ERR_PTR(-ENOMEM);
	}

	atomic_set(&pt->used, 0);
	return pt;
}

struct i915_page_directory *__alloc_pd(int count)
{
	struct i915_page_directory *pd;

	pd = kzalloc(sizeof(*pd), I915_GFP_ALLOW_FAIL);
	if (unlikely(!pd))
		return NULL;

	pd->entry = kcalloc(count, sizeof(*pd->entry), I915_GFP_ALLOW_FAIL);
	if (unlikely(!pd->entry)) {
		kfree(pd);
		return NULL;
	}

	spin_lock_init(&pd->lock);
	return pd;
}

struct i915_page_directory *alloc_pd(struct i915_address_space *vm)
{
	struct i915_page_directory *pd;

	pd = __alloc_pd(I915_PDES);
	if (unlikely(!pd))
		return ERR_PTR(-ENOMEM);

	pd->pt.base = vm->alloc_pt_dma(vm, I915_GTT_PAGE_SIZE_4K);
	if (IS_ERR(pd->pt.base)) {
		kfree(pd->entry);
		kfree(pd);
		return ERR_PTR(-ENOMEM);
	}

	return pd;
}

void free_px(struct i915_address_space *vm, struct i915_page_table *pt, int lvl)
{
	BUILD_BUG_ON(offsetof(struct i915_page_directory, pt));

	if (lvl) {
		struct i915_page_directory *pd =
			container_of(pt, typeof(*pd), pt);
		kfree(pd->entry);
	}

	if (pt->base)
		i915_gem_object_put(pt->base);

	kfree(pt);
}

static void
write_dma_entry(struct drm_i915_gem_object * const pdma,
		const unsigned short idx,
		const u64 encoded_entry)
{
	u64 * const vaddr = kmap_atomic(__px_page(pdma));

	vaddr[idx] = encoded_entry;
	clflush_cache_range(&vaddr[idx], sizeof(u64));
	kunmap_atomic(vaddr);
}

void
__set_pd_entry(struct i915_page_directory * const pd,
	       const unsigned short idx,
	       struct i915_page_table * const to,
	       u64 (*encode)(const dma_addr_t, const enum i915_cache_level))
{
	/* Each thread pre-pins the pd, and we may have a thread per pde. */
	GEM_BUG_ON(atomic_read(px_used(pd)) > NALLOC * I915_PDES);

	atomic_inc(px_used(pd));
	pd->entry[idx] = to;
	write_dma_entry(px_base(pd), idx, encode(px_dma(to), I915_CACHE_LLC));
}

void
clear_pd_entry(struct i915_page_directory * const pd,
	       const unsigned short idx,
	       const struct drm_i915_gem_object * const scratch)
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
		 const struct drm_i915_gem_object * const scratch)
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

void ppgtt_bind_vma(struct i915_address_space *vm,
		    struct i915_vm_pt_stash *stash,
		    struct i915_vma *vma,
		    enum i915_cache_level cache_level,
		    u32 flags)
{
	u32 pte_flags;

	if (!test_bit(I915_VMA_ALLOC_BIT, __i915_vma_flags(vma))) {
		vm->allocate_va_range(vm, stash, vma->node.start, vma->size);
		set_bit(I915_VMA_ALLOC_BIT, __i915_vma_flags(vma));
	}

	/* Applicable to VLV, and gen8+ */
	pte_flags = 0;
	if (i915_gem_object_is_readonly(vma->obj))
		pte_flags |= PTE_READ_ONLY;

	vm->insert_entries(vm, vma, cache_level, pte_flags);
	wmb();
}

void ppgtt_unbind_vma(struct i915_address_space *vm, struct i915_vma *vma)
{
	if (test_and_clear_bit(I915_VMA_ALLOC_BIT, __i915_vma_flags(vma)))
		vm->clear_range(vm, vma->node.start, vma->size);
}

static unsigned long pd_count(u64 size, int shift)
{
	/* Beware later misalignment */
	return (size + 2 * (BIT_ULL(shift) - 1)) >> shift;
}

int i915_vm_alloc_pt_stash(struct i915_address_space *vm,
			   struct i915_vm_pt_stash *stash,
			   u64 size)
{
	unsigned long count;
	int shift, n;

	shift = vm->pd_shift;
	if (!shift)
		return 0;

	count = pd_count(size, shift);
	while (count--) {
		struct i915_page_table *pt;

		pt = alloc_pt(vm);
		if (IS_ERR(pt)) {
			i915_vm_free_pt_stash(vm, stash);
			return PTR_ERR(pt);
		}

		pt->stash = stash->pt[0];
		stash->pt[0] = pt;
	}

	for (n = 1; n < vm->top; n++) {
		shift += ilog2(I915_PDES); /* Each PD holds 512 entries */
		count = pd_count(size, shift);
		while (count--) {
			struct i915_page_directory *pd;

			pd = alloc_pd(vm);
			if (IS_ERR(pd)) {
				i915_vm_free_pt_stash(vm, stash);
				return PTR_ERR(pd);
			}

			pd->pt.stash = stash->pt[1];
			stash->pt[1] = &pd->pt;
		}
	}

	return 0;
}

int i915_vm_pin_pt_stash(struct i915_address_space *vm,
			 struct i915_vm_pt_stash *stash)
{
	struct i915_page_table *pt;
	int n, err;

	for (n = 0; n < ARRAY_SIZE(stash->pt); n++) {
		for (pt = stash->pt[n]; pt; pt = pt->stash) {
			err = pin_pt_dma(vm, pt->base);
			if (err)
				return err;
		}
	}

	return 0;
}

void i915_vm_free_pt_stash(struct i915_address_space *vm,
			   struct i915_vm_pt_stash *stash)
{
	struct i915_page_table *pt;
	int n;

	for (n = 0; n < ARRAY_SIZE(stash->pt); n++) {
		while ((pt = stash->pt[n])) {
			stash->pt[n] = pt->stash;
			free_px(vm, pt, n);
		}
	}
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
