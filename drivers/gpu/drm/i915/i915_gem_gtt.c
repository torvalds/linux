/*
 * Copyright © 2010 Daniel Vetter
 * Copyright © 2011-2014 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include <linux/slab.h> /* fault-inject.h is not standalone! */

#include <linux/fault-inject.h>
#include <linux/log2.h>
#include <linux/random.h>
#include <linux/seq_file.h>
#include <linux/stop_machine.h>

#include <asm/set_memory.h>
#include <asm/smp.h>

#include <drm/i915_drm.h>

#include "display/intel_frontbuffer.h"
#include "gt/intel_gt.h"
#include "gt/intel_gt_requests.h"

#include "i915_drv.h"
#include "i915_scatterlist.h"
#include "i915_trace.h"
#include "i915_vgpu.h"

#define I915_GFP_ALLOW_FAIL (GFP_KERNEL | __GFP_RETRY_MAYFAIL | __GFP_NOWARN)

#if IS_ENABLED(CONFIG_DRM_I915_TRACE_GTT)
#define DBG(...) trace_printk(__VA_ARGS__)
#else
#define DBG(...)
#endif

/**
 * DOC: Global GTT views
 *
 * Background and previous state
 *
 * Historically objects could exists (be bound) in global GTT space only as
 * singular instances with a view representing all of the object's backing pages
 * in a linear fashion. This view will be called a normal view.
 *
 * To support multiple views of the same object, where the number of mapped
 * pages is not equal to the backing store, or where the layout of the pages
 * is not linear, concept of a GGTT view was added.
 *
 * One example of an alternative view is a stereo display driven by a single
 * image. In this case we would have a framebuffer looking like this
 * (2x2 pages):
 *
 *    12
 *    34
 *
 * Above would represent a normal GGTT view as normally mapped for GPU or CPU
 * rendering. In contrast, fed to the display engine would be an alternative
 * view which could look something like this:
 *
 *   1212
 *   3434
 *
 * In this example both the size and layout of pages in the alternative view is
 * different from the normal view.
 *
 * Implementation and usage
 *
 * GGTT views are implemented using VMAs and are distinguished via enum
 * i915_ggtt_view_type and struct i915_ggtt_view.
 *
 * A new flavour of core GEM functions which work with GGTT bound objects were
 * added with the _ggtt_ infix, and sometimes with _view postfix to avoid
 * renaming  in large amounts of code. They take the struct i915_ggtt_view
 * parameter encapsulating all metadata required to implement a view.
 *
 * As a helper for callers which are only interested in the normal view,
 * globally const i915_ggtt_view_normal singleton instance exists. All old core
 * GEM API functions, the ones not taking the view parameter, are operating on,
 * or with the normal GGTT view.
 *
 * Code wanting to add or use a new GGTT view needs to:
 *
 * 1. Add a new enum with a suitable name.
 * 2. Extend the metadata in the i915_ggtt_view structure if required.
 * 3. Add support to i915_get_vma_pages().
 *
 * New views are required to build a scatter-gather table from within the
 * i915_get_vma_pages function. This table is stored in the vma.ggtt_view and
 * exists for the lifetime of an VMA.
 *
 * Core API is designed to have copy semantics which means that passed in
 * struct i915_ggtt_view does not need to be persistent (left around after
 * calling the core API functions).
 *
 */

#define as_pd(x) container_of((x), typeof(struct i915_page_directory), pt)

static int
i915_get_ggtt_vma_pages(struct i915_vma *vma);

static void gen6_ggtt_invalidate(struct i915_ggtt *ggtt)
{
	struct intel_uncore *uncore = ggtt->vm.gt->uncore;

	/*
	 * Note that as an uncached mmio write, this will flush the
	 * WCB of the writes into the GGTT before it triggers the invalidate.
	 */
	intel_uncore_write_fw(uncore, GFX_FLSH_CNTL_GEN6, GFX_FLSH_CNTL_EN);
}

static void guc_ggtt_invalidate(struct i915_ggtt *ggtt)
{
	struct intel_uncore *uncore = ggtt->vm.gt->uncore;
	struct drm_i915_private *i915 = ggtt->vm.i915;

	gen6_ggtt_invalidate(ggtt);

	if (INTEL_GEN(i915) >= 12)
		intel_uncore_write_fw(uncore, GEN12_GUC_TLB_INV_CR,
				      GEN12_GUC_TLB_INV_CR_INVALIDATE);
	else
		intel_uncore_write_fw(uncore, GEN8_GTCR, GEN8_GTCR_INVALIDATE);
}

static void gmch_ggtt_invalidate(struct i915_ggtt *ggtt)
{
	intel_gtt_chipset_flush();
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

static int ppgtt_set_pages(struct i915_vma *vma)
{
	GEM_BUG_ON(vma->pages);

	vma->pages = vma->obj->mm.pages;

	vma->page_sizes = vma->obj->mm.page_sizes;

	return 0;
}

static void clear_pages(struct i915_vma *vma)
{
	GEM_BUG_ON(!vma->pages);

	if (vma->pages != vma->obj->mm.pages) {
		sg_free_table(vma->pages);
		kfree(vma->pages);
	}
	vma->pages = NULL;

	memset(&vma->page_sizes, 0, sizeof(vma->page_sizes));
}

static u64 gen8_pte_encode(dma_addr_t addr,
			   enum i915_cache_level level,
			   u32 flags)
{
	gen8_pte_t pte = addr | _PAGE_PRESENT | _PAGE_RW;

	if (unlikely(flags & PTE_READ_ONLY))
		pte &= ~_PAGE_RW;

	switch (level) {
	case I915_CACHE_NONE:
		pte |= PPAT_UNCACHED;
		break;
	case I915_CACHE_WT:
		pte |= PPAT_DISPLAY_ELLC;
		break;
	default:
		pte |= PPAT_CACHED;
		break;
	}

	return pte;
}

static u64 gen8_pde_encode(const dma_addr_t addr,
			   const enum i915_cache_level level)
{
	u64 pde = _PAGE_PRESENT | _PAGE_RW;
	pde |= addr;
	if (level != I915_CACHE_NONE)
		pde |= PPAT_CACHED_PDE;
	else
		pde |= PPAT_UNCACHED;
	return pde;
}

static u64 snb_pte_encode(dma_addr_t addr,
			  enum i915_cache_level level,
			  u32 flags)
{
	gen6_pte_t pte = GEN6_PTE_VALID;
	pte |= GEN6_PTE_ADDR_ENCODE(addr);

	switch (level) {
	case I915_CACHE_L3_LLC:
	case I915_CACHE_LLC:
		pte |= GEN6_PTE_CACHE_LLC;
		break;
	case I915_CACHE_NONE:
		pte |= GEN6_PTE_UNCACHED;
		break;
	default:
		MISSING_CASE(level);
	}

	return pte;
}

static u64 ivb_pte_encode(dma_addr_t addr,
			  enum i915_cache_level level,
			  u32 flags)
{
	gen6_pte_t pte = GEN6_PTE_VALID;
	pte |= GEN6_PTE_ADDR_ENCODE(addr);

	switch (level) {
	case I915_CACHE_L3_LLC:
		pte |= GEN7_PTE_CACHE_L3_LLC;
		break;
	case I915_CACHE_LLC:
		pte |= GEN6_PTE_CACHE_LLC;
		break;
	case I915_CACHE_NONE:
		pte |= GEN6_PTE_UNCACHED;
		break;
	default:
		MISSING_CASE(level);
	}

	return pte;
}

static u64 byt_pte_encode(dma_addr_t addr,
			  enum i915_cache_level level,
			  u32 flags)
{
	gen6_pte_t pte = GEN6_PTE_VALID;
	pte |= GEN6_PTE_ADDR_ENCODE(addr);

	if (!(flags & PTE_READ_ONLY))
		pte |= BYT_PTE_WRITEABLE;

	if (level != I915_CACHE_NONE)
		pte |= BYT_PTE_SNOOPED_BY_CPU_CACHES;

	return pte;
}

static u64 hsw_pte_encode(dma_addr_t addr,
			  enum i915_cache_level level,
			  u32 flags)
{
	gen6_pte_t pte = GEN6_PTE_VALID;
	pte |= HSW_PTE_ADDR_ENCODE(addr);

	if (level != I915_CACHE_NONE)
		pte |= HSW_WB_LLC_AGE3;

	return pte;
}

static u64 iris_pte_encode(dma_addr_t addr,
			   enum i915_cache_level level,
			   u32 flags)
{
	gen6_pte_t pte = GEN6_PTE_VALID;
	pte |= HSW_PTE_ADDR_ENCODE(addr);

	switch (level) {
	case I915_CACHE_NONE:
		break;
	case I915_CACHE_WT:
		pte |= HSW_WT_ELLC_LLC_AGE3;
		break;
	default:
		pte |= HSW_WB_ELLC_LLC_AGE3;
		break;
	}

	return pte;
}

static void stash_init(struct pagestash *stash)
{
	pagevec_init(&stash->pvec);
	spin_lock_init(&stash->lock);
}

static struct page *stash_pop_page(struct pagestash *stash)
{
	struct page *page = NULL;

	spin_lock(&stash->lock);
	if (likely(stash->pvec.nr))
		page = stash->pvec.pages[--stash->pvec.nr];
	spin_unlock(&stash->lock);

	return page;
}

static void stash_push_pagevec(struct pagestash *stash, struct pagevec *pvec)
{
	unsigned int nr;

	spin_lock_nested(&stash->lock, SINGLE_DEPTH_NESTING);

	nr = min_t(typeof(nr), pvec->nr, pagevec_space(&stash->pvec));
	memcpy(stash->pvec.pages + stash->pvec.nr,
	       pvec->pages + pvec->nr - nr,
	       sizeof(pvec->pages[0]) * nr);
	stash->pvec.nr += nr;

	spin_unlock(&stash->lock);

	pvec->nr -= nr;
}

static struct page *vm_alloc_page(struct i915_address_space *vm, gfp_t gfp)
{
	struct pagevec stack;
	struct page *page;

	if (I915_SELFTEST_ONLY(should_fail(&vm->fault_attr, 1)))
		i915_gem_shrink_all(vm->i915);

	page = stash_pop_page(&vm->free_pages);
	if (page)
		return page;

	if (!vm->pt_kmap_wc)
		return alloc_page(gfp);

	/* Look in our global stash of WC pages... */
	page = stash_pop_page(&vm->i915->mm.wc_stash);
	if (page)
		return page;

	/*
	 * Otherwise batch allocate pages to amortize cost of set_pages_wc.
	 *
	 * We have to be careful as page allocation may trigger the shrinker
	 * (via direct reclaim) which will fill up the WC stash underneath us.
	 * So we add our WB pages into a temporary pvec on the stack and merge
	 * them into the WC stash after all the allocations are complete.
	 */
	pagevec_init(&stack);
	do {
		struct page *page;

		page = alloc_page(gfp);
		if (unlikely(!page))
			break;

		stack.pages[stack.nr++] = page;
	} while (pagevec_space(&stack));

	if (stack.nr && !set_pages_array_wc(stack.pages, stack.nr)) {
		page = stack.pages[--stack.nr];

		/* Merge spare WC pages to the global stash */
		if (stack.nr)
			stash_push_pagevec(&vm->i915->mm.wc_stash, &stack);

		/* Push any surplus WC pages onto the local VM stash */
		if (stack.nr)
			stash_push_pagevec(&vm->free_pages, &stack);
	}

	/* Return unwanted leftovers */
	if (unlikely(stack.nr)) {
		WARN_ON_ONCE(set_pages_array_wb(stack.pages, stack.nr));
		__pagevec_release(&stack);
	}

	return page;
}

static void vm_free_pages_release(struct i915_address_space *vm,
				  bool immediate)
{
	struct pagevec *pvec = &vm->free_pages.pvec;
	struct pagevec stack;

	lockdep_assert_held(&vm->free_pages.lock);
	GEM_BUG_ON(!pagevec_count(pvec));

	if (vm->pt_kmap_wc) {
		/*
		 * When we use WC, first fill up the global stash and then
		 * only if full immediately free the overflow.
		 */
		stash_push_pagevec(&vm->i915->mm.wc_stash, pvec);

		/*
		 * As we have made some room in the VM's free_pages,
		 * we can wait for it to fill again. Unless we are
		 * inside i915_address_space_fini() and must
		 * immediately release the pages!
		 */
		if (pvec->nr <= (immediate ? 0 : PAGEVEC_SIZE - 1))
			return;

		/*
		 * We have to drop the lock to allow ourselves to sleep,
		 * so take a copy of the pvec and clear the stash for
		 * others to use it as we sleep.
		 */
		stack = *pvec;
		pagevec_reinit(pvec);
		spin_unlock(&vm->free_pages.lock);

		pvec = &stack;
		set_pages_array_wb(pvec->pages, pvec->nr);

		spin_lock(&vm->free_pages.lock);
	}

	__pagevec_release(pvec);
}

static void vm_free_page(struct i915_address_space *vm, struct page *page)
{
	/*
	 * On !llc, we need to change the pages back to WB. We only do so
	 * in bulk, so we rarely need to change the page attributes here,
	 * but doing so requires a stop_machine() from deep inside arch/x86/mm.
	 * To make detection of the possible sleep more likely, use an
	 * unconditional might_sleep() for everybody.
	 */
	might_sleep();
	spin_lock(&vm->free_pages.lock);
	while (!pagevec_space(&vm->free_pages.pvec))
		vm_free_pages_release(vm, false);
	GEM_BUG_ON(pagevec_count(&vm->free_pages.pvec) >= PAGEVEC_SIZE);
	pagevec_add(&vm->free_pages.pvec, page);
	spin_unlock(&vm->free_pages.lock);
}

static void i915_address_space_fini(struct i915_address_space *vm)
{
	spin_lock(&vm->free_pages.lock);
	if (pagevec_count(&vm->free_pages.pvec))
		vm_free_pages_release(vm, true);
	GEM_BUG_ON(pagevec_count(&vm->free_pages.pvec));
	spin_unlock(&vm->free_pages.lock);

	drm_mm_takedown(&vm->mm);

	mutex_destroy(&vm->mutex);
}

void __i915_vm_close(struct i915_address_space *vm)
{
	struct i915_vma *vma, *vn;

	mutex_lock(&vm->mutex);
	list_for_each_entry_safe(vma, vn, &vm->bound_list, vm_link) {
		struct drm_i915_gem_object *obj = vma->obj;

		/* Keep the obj (and hence the vma) alive as _we_ destroy it */
		if (!kref_get_unless_zero(&obj->base.refcount))
			continue;

		atomic_and(~I915_VMA_PIN_MASK, &vma->flags);
		WARN_ON(__i915_vma_unbind(vma));
		i915_vma_destroy(vma);

		i915_gem_object_put(obj);
	}
	GEM_BUG_ON(!list_empty(&vm->bound_list));
	mutex_unlock(&vm->mutex);
}

static void __i915_vm_release(struct work_struct *work)
{
	struct i915_address_space *vm =
		container_of(work, struct i915_address_space, rcu.work);

	vm->cleanup(vm);
	i915_address_space_fini(vm);

	kfree(vm);
}

void i915_vm_release(struct kref *kref)
{
	struct i915_address_space *vm =
		container_of(kref, struct i915_address_space, ref);

	GEM_BUG_ON(i915_is_ggtt(vm));
	trace_i915_ppgtt_release(vm);

	queue_rcu_work(vm->i915->wq, &vm->rcu);
}

static void i915_address_space_init(struct i915_address_space *vm, int subclass)
{
	kref_init(&vm->ref);
	INIT_RCU_WORK(&vm->rcu, __i915_vm_release);
	atomic_set(&vm->open, 1);

	/*
	 * The vm->mutex must be reclaim safe (for use in the shrinker).
	 * Do a dummy acquire now under fs_reclaim so that any allocation
	 * attempt holding the lock is immediately reported by lockdep.
	 */
	mutex_init(&vm->mutex);
	lockdep_set_subclass(&vm->mutex, subclass);
	i915_gem_shrinker_taints_mutex(vm->i915, &vm->mutex);

	GEM_BUG_ON(!vm->total);
	drm_mm_init(&vm->mm, 0, vm->total);
	vm->mm.head_node.color = I915_COLOR_UNEVICTABLE;

	stash_init(&vm->free_pages);

	INIT_LIST_HEAD(&vm->bound_list);
}

static int __setup_page_dma(struct i915_address_space *vm,
			    struct i915_page_dma *p,
			    gfp_t gfp)
{
	p->page = vm_alloc_page(vm, gfp | I915_GFP_ALLOW_FAIL);
	if (unlikely(!p->page))
		return -ENOMEM;

	p->daddr = dma_map_page_attrs(vm->dma,
				      p->page, 0, PAGE_SIZE,
				      PCI_DMA_BIDIRECTIONAL,
				      DMA_ATTR_SKIP_CPU_SYNC |
				      DMA_ATTR_NO_WARN);
	if (unlikely(dma_mapping_error(vm->dma, p->daddr))) {
		vm_free_page(vm, p->page);
		return -ENOMEM;
	}

	return 0;
}

static int setup_page_dma(struct i915_address_space *vm,
			  struct i915_page_dma *p)
{
	return __setup_page_dma(vm, p, __GFP_HIGHMEM);
}

static void cleanup_page_dma(struct i915_address_space *vm,
			     struct i915_page_dma *p)
{
	dma_unmap_page(vm->dma, p->daddr, PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
	vm_free_page(vm, p->page);
}

#define kmap_atomic_px(px) kmap_atomic(px_base(px)->page)

static void
fill_page_dma(const struct i915_page_dma *p, const u64 val, unsigned int count)
{
	kunmap_atomic(memset64(kmap_atomic(p->page), val, count));
}

#define fill_px(px, v) fill_page_dma(px_base(px), (v), PAGE_SIZE / sizeof(u64))
#define fill32_px(px, v) do {						\
	u64 v__ = lower_32_bits(v);					\
	fill_px((px), v__ << 32 | v__);					\
} while (0)

static int
setup_scratch_page(struct i915_address_space *vm, gfp_t gfp)
{
	unsigned long size;

	/*
	 * In order to utilize 64K pages for an object with a size < 2M, we will
	 * need to support a 64K scratch page, given that every 16th entry for a
	 * page-table operating in 64K mode must point to a properly aligned 64K
	 * region, including any PTEs which happen to point to scratch.
	 *
	 * This is only relevant for the 48b PPGTT where we support
	 * huge-gtt-pages, see also i915_vma_insert(). However, as we share the
	 * scratch (read-only) between all vm, we create one 64k scratch page
	 * for all.
	 */
	size = I915_GTT_PAGE_SIZE_4K;
	if (i915_vm_is_4lvl(vm) &&
	    HAS_PAGE_SIZES(vm->i915, I915_GTT_PAGE_SIZE_64K)) {
		size = I915_GTT_PAGE_SIZE_64K;
		gfp |= __GFP_NOWARN;
	}
	gfp |= __GFP_ZERO | __GFP_RETRY_MAYFAIL;

	do {
		unsigned int order = get_order(size);
		struct page *page;
		dma_addr_t addr;

		page = alloc_pages(gfp, order);
		if (unlikely(!page))
			goto skip;

		addr = dma_map_page_attrs(vm->dma,
					  page, 0, size,
					  PCI_DMA_BIDIRECTIONAL,
					  DMA_ATTR_SKIP_CPU_SYNC |
					  DMA_ATTR_NO_WARN);
		if (unlikely(dma_mapping_error(vm->dma, addr)))
			goto free_page;

		if (unlikely(!IS_ALIGNED(addr, size)))
			goto unmap_page;

		vm->scratch[0].base.page = page;
		vm->scratch[0].base.daddr = addr;
		vm->scratch_order = order;
		return 0;

unmap_page:
		dma_unmap_page(vm->dma, addr, size, PCI_DMA_BIDIRECTIONAL);
free_page:
		__free_pages(page, order);
skip:
		if (size == I915_GTT_PAGE_SIZE_4K)
			return -ENOMEM;

		size = I915_GTT_PAGE_SIZE_4K;
		gfp &= ~__GFP_NOWARN;
	} while (1);
}

static void cleanup_scratch_page(struct i915_address_space *vm)
{
	struct i915_page_dma *p = px_base(&vm->scratch[0]);
	unsigned int order = vm->scratch_order;

	dma_unmap_page(vm->dma, p->daddr, BIT(order) << PAGE_SHIFT,
		       PCI_DMA_BIDIRECTIONAL);
	__free_pages(p->page, order);
}

static void free_scratch(struct i915_address_space *vm)
{
	int i;

	if (!px_dma(&vm->scratch[0])) /* set to 0 on clones */
		return;

	for (i = 1; i <= vm->top; i++) {
		if (!px_dma(&vm->scratch[i]))
			break;
		cleanup_page_dma(vm, px_base(&vm->scratch[i]));
	}

	cleanup_scratch_page(vm);
}

static struct i915_page_table *alloc_pt(struct i915_address_space *vm)
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

static struct i915_page_directory *__alloc_pd(size_t sz)
{
	struct i915_page_directory *pd;

	pd = kzalloc(sz, I915_GFP_ALLOW_FAIL);
	if (unlikely(!pd))
		return NULL;

	spin_lock_init(&pd->lock);
	return pd;
}

static struct i915_page_directory *alloc_pd(struct i915_address_space *vm)
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

static void free_pd(struct i915_address_space *vm, struct i915_page_dma *pd)
{
	cleanup_page_dma(vm, pd);
	kfree(pd);
}

#define free_px(vm, px) free_pd(vm, px_base(px))

static inline void
write_dma_entry(struct i915_page_dma * const pdma,
		const unsigned short idx,
		const u64 encoded_entry)
{
	u64 * const vaddr = kmap_atomic(pdma->page);

	vaddr[idx] = encoded_entry;
	kunmap_atomic(vaddr);
}

static inline void
__set_pd_entry(struct i915_page_directory * const pd,
	       const unsigned short idx,
	       struct i915_page_dma * const to,
	       u64 (*encode)(const dma_addr_t, const enum i915_cache_level))
{
	/* Each thread pre-pins the pd, and we may have a thread per pde. */
	GEM_BUG_ON(atomic_read(px_used(pd)) > 2 * ARRAY_SIZE(pd->entry));

	atomic_inc(px_used(pd));
	pd->entry[idx] = to;
	write_dma_entry(px_base(pd), idx, encode(to->daddr, I915_CACHE_LLC));
}

#define set_pd_entry(pd, idx, to) \
	__set_pd_entry((pd), (idx), px_base(to), gen8_pde_encode)

static inline void
clear_pd_entry(struct i915_page_directory * const pd,
	       const unsigned short idx,
	       const struct i915_page_scratch * const scratch)
{
	GEM_BUG_ON(atomic_read(px_used(pd)) == 0);

	write_dma_entry(px_base(pd), idx, scratch->encode);
	pd->entry[idx] = NULL;
	atomic_dec(px_used(pd));
}

static bool
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

static void gen8_ppgtt_notify_vgt(struct i915_ppgtt *ppgtt, bool create)
{
	struct drm_i915_private *dev_priv = ppgtt->vm.i915;
	enum vgt_g2v_type msg;
	int i;

	if (create)
		atomic_inc(px_used(ppgtt->pd)); /* never remove */
	else
		atomic_dec(px_used(ppgtt->pd));

	mutex_lock(&dev_priv->vgpu.lock);

	if (i915_vm_is_4lvl(&ppgtt->vm)) {
		const u64 daddr = px_dma(ppgtt->pd);

		I915_WRITE(vgtif_reg(pdp[0].lo), lower_32_bits(daddr));
		I915_WRITE(vgtif_reg(pdp[0].hi), upper_32_bits(daddr));

		msg = (create ? VGT_G2V_PPGTT_L4_PAGE_TABLE_CREATE :
				VGT_G2V_PPGTT_L4_PAGE_TABLE_DESTROY);
	} else {
		for (i = 0; i < GEN8_3LVL_PDPES; i++) {
			const u64 daddr = i915_page_dir_dma_addr(ppgtt, i);

			I915_WRITE(vgtif_reg(pdp[i].lo), lower_32_bits(daddr));
			I915_WRITE(vgtif_reg(pdp[i].hi), upper_32_bits(daddr));
		}

		msg = (create ? VGT_G2V_PPGTT_L3_PAGE_TABLE_CREATE :
				VGT_G2V_PPGTT_L3_PAGE_TABLE_DESTROY);
	}

	/* g2v_notify atomically (via hv trap) consumes the message packet. */
	I915_WRITE(vgtif_reg(g2v_notify), msg);

	mutex_unlock(&dev_priv->vgpu.lock);
}

/* Index shifts into the pagetable are offset by GEN8_PTE_SHIFT [12] */
#define GEN8_PAGE_SIZE (SZ_4K) /* page and page-directory sizes are the same */
#define GEN8_PTE_SHIFT (ilog2(GEN8_PAGE_SIZE))
#define GEN8_PDES (GEN8_PAGE_SIZE / sizeof(u64))
#define gen8_pd_shift(lvl) ((lvl) * ilog2(GEN8_PDES))
#define gen8_pd_index(i, lvl) i915_pde_index((i), gen8_pd_shift(lvl))
#define __gen8_pte_shift(lvl) (GEN8_PTE_SHIFT + gen8_pd_shift(lvl))
#define __gen8_pte_index(a, lvl) i915_pde_index((a), __gen8_pte_shift(lvl))

static inline unsigned int
gen8_pd_range(u64 start, u64 end, int lvl, unsigned int *idx)
{
	const int shift = gen8_pd_shift(lvl);
	const u64 mask = ~0ull << gen8_pd_shift(lvl + 1);

	GEM_BUG_ON(start >= end);
	end += ~mask >> gen8_pd_shift(1);

	*idx = i915_pde_index(start, shift);
	if ((start ^ end) & mask)
		return GEN8_PDES - *idx;
	else
		return i915_pde_index(end, shift) - *idx;
}

static inline bool gen8_pd_contains(u64 start, u64 end, int lvl)
{
	const u64 mask = ~0ull << gen8_pd_shift(lvl + 1);

	GEM_BUG_ON(start >= end);
	return (start ^ end) & mask && (start & ~mask) == 0;
}

static inline unsigned int gen8_pt_count(u64 start, u64 end)
{
	GEM_BUG_ON(start >= end);
	if ((start ^ end) >> gen8_pd_shift(1))
		return GEN8_PDES - (start & (GEN8_PDES - 1));
	else
		return end - start;
}

static inline unsigned int gen8_pd_top_count(const struct i915_address_space *vm)
{
	unsigned int shift = __gen8_pte_shift(vm->top);
	return (vm->total + (1ull << shift) - 1) >> shift;
}

static inline struct i915_page_directory *
gen8_pdp_for_page_index(struct i915_address_space * const vm, const u64 idx)
{
	struct i915_ppgtt * const ppgtt = i915_vm_to_ppgtt(vm);

	if (vm->top == 2)
		return ppgtt->pd;
	else
		return i915_pd_entry(ppgtt->pd, gen8_pd_index(idx, vm->top));
}

static inline struct i915_page_directory *
gen8_pdp_for_page_address(struct i915_address_space * const vm, const u64 addr)
{
	return gen8_pdp_for_page_index(vm, addr >> GEN8_PTE_SHIFT);
}

static void __gen8_ppgtt_cleanup(struct i915_address_space *vm,
				 struct i915_page_directory *pd,
				 int count, int lvl)
{
	if (lvl) {
		void **pde = pd->entry;

		do {
			if (!*pde)
				continue;

			__gen8_ppgtt_cleanup(vm, *pde, GEN8_PDES, lvl - 1);
		} while (pde++, --count);
	}

	free_px(vm, pd);
}

static void gen8_ppgtt_cleanup(struct i915_address_space *vm)
{
	struct i915_ppgtt *ppgtt = i915_vm_to_ppgtt(vm);

	if (intel_vgpu_active(vm->i915))
		gen8_ppgtt_notify_vgt(ppgtt, false);

	__gen8_ppgtt_cleanup(vm, ppgtt->pd, gen8_pd_top_count(vm), vm->top);
	free_scratch(vm);
}

static u64 __gen8_ppgtt_clear(struct i915_address_space * const vm,
			      struct i915_page_directory * const pd,
			      u64 start, const u64 end, int lvl)
{
	const struct i915_page_scratch * const scratch = &vm->scratch[lvl];
	unsigned int idx, len;

	GEM_BUG_ON(end > vm->total >> GEN8_PTE_SHIFT);

	len = gen8_pd_range(start, end, lvl--, &idx);
	DBG("%s(%p):{ lvl:%d, start:%llx, end:%llx, idx:%d, len:%d, used:%d }\n",
	    __func__, vm, lvl + 1, start, end,
	    idx, len, atomic_read(px_used(pd)));
	GEM_BUG_ON(!len || len >= atomic_read(px_used(pd)));

	do {
		struct i915_page_table *pt = pd->entry[idx];

		if (atomic_fetch_inc(&pt->used) >> gen8_pd_shift(1) &&
		    gen8_pd_contains(start, end, lvl)) {
			DBG("%s(%p):{ lvl:%d, idx:%d, start:%llx, end:%llx } removing pd\n",
			    __func__, vm, lvl + 1, idx, start, end);
			clear_pd_entry(pd, idx, scratch);
			__gen8_ppgtt_cleanup(vm, as_pd(pt), I915_PDES, lvl);
			start += (u64)I915_PDES << gen8_pd_shift(lvl);
			continue;
		}

		if (lvl) {
			start = __gen8_ppgtt_clear(vm, as_pd(pt),
						   start, end, lvl);
		} else {
			unsigned int count;
			u64 *vaddr;

			count = gen8_pt_count(start, end);
			DBG("%s(%p):{ lvl:%d, start:%llx, end:%llx, idx:%d, len:%d, used:%d } removing pte\n",
			    __func__, vm, lvl, start, end,
			    gen8_pd_index(start, 0), count,
			    atomic_read(&pt->used));
			GEM_BUG_ON(!count || count >= atomic_read(&pt->used));

			vaddr = kmap_atomic_px(pt);
			memset64(vaddr + gen8_pd_index(start, 0),
				 vm->scratch[0].encode,
				 count);
			kunmap_atomic(vaddr);

			atomic_sub(count, &pt->used);
			start += count;
		}

		if (release_pd_entry(pd, idx, pt, scratch))
			free_px(vm, pt);
	} while (idx++, --len);

	return start;
}

static void gen8_ppgtt_clear(struct i915_address_space *vm,
			     u64 start, u64 length)
{
	GEM_BUG_ON(!IS_ALIGNED(start, BIT_ULL(GEN8_PTE_SHIFT)));
	GEM_BUG_ON(!IS_ALIGNED(length, BIT_ULL(GEN8_PTE_SHIFT)));
	GEM_BUG_ON(range_overflows(start, length, vm->total));

	start >>= GEN8_PTE_SHIFT;
	length >>= GEN8_PTE_SHIFT;
	GEM_BUG_ON(length == 0);

	__gen8_ppgtt_clear(vm, i915_vm_to_ppgtt(vm)->pd,
			   start, start + length, vm->top);
}

static int __gen8_ppgtt_alloc(struct i915_address_space * const vm,
			      struct i915_page_directory * const pd,
			      u64 * const start, const u64 end, int lvl)
{
	const struct i915_page_scratch * const scratch = &vm->scratch[lvl];
	struct i915_page_table *alloc = NULL;
	unsigned int idx, len;
	int ret = 0;

	GEM_BUG_ON(end > vm->total >> GEN8_PTE_SHIFT);

	len = gen8_pd_range(*start, end, lvl--, &idx);
	DBG("%s(%p):{ lvl:%d, start:%llx, end:%llx, idx:%d, len:%d, used:%d }\n",
	    __func__, vm, lvl + 1, *start, end,
	    idx, len, atomic_read(px_used(pd)));
	GEM_BUG_ON(!len || (idx + len - 1) >> gen8_pd_shift(1));

	spin_lock(&pd->lock);
	GEM_BUG_ON(!atomic_read(px_used(pd))); /* Must be pinned! */
	do {
		struct i915_page_table *pt = pd->entry[idx];

		if (!pt) {
			spin_unlock(&pd->lock);

			DBG("%s(%p):{ lvl:%d, idx:%d } allocating new tree\n",
			    __func__, vm, lvl + 1, idx);

			pt = fetch_and_zero(&alloc);
			if (lvl) {
				if (!pt) {
					pt = &alloc_pd(vm)->pt;
					if (IS_ERR(pt)) {
						ret = PTR_ERR(pt);
						goto out;
					}
				}

				fill_px(pt, vm->scratch[lvl].encode);
			} else {
				if (!pt) {
					pt = alloc_pt(vm);
					if (IS_ERR(pt)) {
						ret = PTR_ERR(pt);
						goto out;
					}
				}

				if (intel_vgpu_active(vm->i915) ||
				    gen8_pt_count(*start, end) < I915_PDES)
					fill_px(pt, vm->scratch[lvl].encode);
			}

			spin_lock(&pd->lock);
			if (likely(!pd->entry[idx]))
				set_pd_entry(pd, idx, pt);
			else
				alloc = pt, pt = pd->entry[idx];
		}

		if (lvl) {
			atomic_inc(&pt->used);
			spin_unlock(&pd->lock);

			ret = __gen8_ppgtt_alloc(vm, as_pd(pt),
						 start, end, lvl);
			if (unlikely(ret)) {
				if (release_pd_entry(pd, idx, pt, scratch))
					free_px(vm, pt);
				goto out;
			}

			spin_lock(&pd->lock);
			atomic_dec(&pt->used);
			GEM_BUG_ON(!atomic_read(&pt->used));
		} else {
			unsigned int count = gen8_pt_count(*start, end);

			DBG("%s(%p):{ lvl:%d, start:%llx, end:%llx, idx:%d, len:%d, used:%d } inserting pte\n",
			    __func__, vm, lvl, *start, end,
			    gen8_pd_index(*start, 0), count,
			    atomic_read(&pt->used));

			atomic_add(count, &pt->used);
			/* All other pdes may be simultaneously removed */
			GEM_BUG_ON(atomic_read(&pt->used) > 2 * I915_PDES);
			*start += count;
		}
	} while (idx++, --len);
	spin_unlock(&pd->lock);
out:
	if (alloc)
		free_px(vm, alloc);
	return ret;
}

static int gen8_ppgtt_alloc(struct i915_address_space *vm,
			    u64 start, u64 length)
{
	u64 from;
	int err;

	GEM_BUG_ON(!IS_ALIGNED(start, BIT_ULL(GEN8_PTE_SHIFT)));
	GEM_BUG_ON(!IS_ALIGNED(length, BIT_ULL(GEN8_PTE_SHIFT)));
	GEM_BUG_ON(range_overflows(start, length, vm->total));

	start >>= GEN8_PTE_SHIFT;
	length >>= GEN8_PTE_SHIFT;
	GEM_BUG_ON(length == 0);
	from = start;

	err = __gen8_ppgtt_alloc(vm, i915_vm_to_ppgtt(vm)->pd,
				 &start, start + length, vm->top);
	if (unlikely(err && from != start))
		__gen8_ppgtt_clear(vm, i915_vm_to_ppgtt(vm)->pd,
				   from, start, vm->top);

	return err;
}

static inline struct sgt_dma {
	struct scatterlist *sg;
	dma_addr_t dma, max;
} sgt_dma(struct i915_vma *vma) {
	struct scatterlist *sg = vma->pages->sgl;
	dma_addr_t addr = sg_dma_address(sg);
	return (struct sgt_dma) { sg, addr, addr + sg->length };
}

static __always_inline u64
gen8_ppgtt_insert_pte(struct i915_ppgtt *ppgtt,
		      struct i915_page_directory *pdp,
		      struct sgt_dma *iter,
		      u64 idx,
		      enum i915_cache_level cache_level,
		      u32 flags)
{
	struct i915_page_directory *pd;
	const gen8_pte_t pte_encode = gen8_pte_encode(0, cache_level, flags);
	gen8_pte_t *vaddr;

	pd = i915_pd_entry(pdp, gen8_pd_index(idx, 2));
	vaddr = kmap_atomic_px(i915_pt_entry(pd, gen8_pd_index(idx, 1)));
	do {
		GEM_BUG_ON(iter->sg->length < I915_GTT_PAGE_SIZE);
		vaddr[gen8_pd_index(idx, 0)] = pte_encode | iter->dma;

		iter->dma += I915_GTT_PAGE_SIZE;
		if (iter->dma >= iter->max) {
			iter->sg = __sg_next(iter->sg);
			if (!iter->sg) {
				idx = 0;
				break;
			}

			iter->dma = sg_dma_address(iter->sg);
			iter->max = iter->dma + iter->sg->length;
		}

		if (gen8_pd_index(++idx, 0) == 0) {
			if (gen8_pd_index(idx, 1) == 0) {
				/* Limited by sg length for 3lvl */
				if (gen8_pd_index(idx, 2) == 0)
					break;

				pd = pdp->entry[gen8_pd_index(idx, 2)];
			}

			kunmap_atomic(vaddr);
			vaddr = kmap_atomic_px(i915_pt_entry(pd, gen8_pd_index(idx, 1)));
		}
	} while (1);
	kunmap_atomic(vaddr);

	return idx;
}

static void gen8_ppgtt_insert_huge(struct i915_vma *vma,
				   struct sgt_dma *iter,
				   enum i915_cache_level cache_level,
				   u32 flags)
{
	const gen8_pte_t pte_encode = gen8_pte_encode(0, cache_level, flags);
	u64 start = vma->node.start;
	dma_addr_t rem = iter->sg->length;

	GEM_BUG_ON(!i915_vm_is_4lvl(vma->vm));

	do {
		struct i915_page_directory * const pdp =
			gen8_pdp_for_page_address(vma->vm, start);
		struct i915_page_directory * const pd =
			i915_pd_entry(pdp, __gen8_pte_index(start, 2));
		gen8_pte_t encode = pte_encode;
		unsigned int maybe_64K = -1;
		unsigned int page_size;
		gen8_pte_t *vaddr;
		u16 index;

		if (vma->page_sizes.sg & I915_GTT_PAGE_SIZE_2M &&
		    IS_ALIGNED(iter->dma, I915_GTT_PAGE_SIZE_2M) &&
		    rem >= I915_GTT_PAGE_SIZE_2M &&
		    !__gen8_pte_index(start, 0)) {
			index = __gen8_pte_index(start, 1);
			encode |= GEN8_PDE_PS_2M;
			page_size = I915_GTT_PAGE_SIZE_2M;

			vaddr = kmap_atomic_px(pd);
		} else {
			struct i915_page_table *pt =
				i915_pt_entry(pd, __gen8_pte_index(start, 1));

			index = __gen8_pte_index(start, 0);
			page_size = I915_GTT_PAGE_SIZE;

			if (!index &&
			    vma->page_sizes.sg & I915_GTT_PAGE_SIZE_64K &&
			    IS_ALIGNED(iter->dma, I915_GTT_PAGE_SIZE_64K) &&
			    (IS_ALIGNED(rem, I915_GTT_PAGE_SIZE_64K) ||
			     rem >= (I915_PDES - index) * I915_GTT_PAGE_SIZE))
				maybe_64K = __gen8_pte_index(start, 1);

			vaddr = kmap_atomic_px(pt);
		}

		do {
			GEM_BUG_ON(iter->sg->length < page_size);
			vaddr[index++] = encode | iter->dma;

			start += page_size;
			iter->dma += page_size;
			rem -= page_size;
			if (iter->dma >= iter->max) {
				iter->sg = __sg_next(iter->sg);
				if (!iter->sg)
					break;

				rem = iter->sg->length;
				iter->dma = sg_dma_address(iter->sg);
				iter->max = iter->dma + rem;

				if (maybe_64K != -1 && index < I915_PDES &&
				    !(IS_ALIGNED(iter->dma, I915_GTT_PAGE_SIZE_64K) &&
				      (IS_ALIGNED(rem, I915_GTT_PAGE_SIZE_64K) ||
				       rem >= (I915_PDES - index) * I915_GTT_PAGE_SIZE)))
					maybe_64K = -1;

				if (unlikely(!IS_ALIGNED(iter->dma, page_size)))
					break;
			}
		} while (rem >= page_size && index < I915_PDES);

		kunmap_atomic(vaddr);

		/*
		 * Is it safe to mark the 2M block as 64K? -- Either we have
		 * filled whole page-table with 64K entries, or filled part of
		 * it and have reached the end of the sg table and we have
		 * enough padding.
		 */
		if (maybe_64K != -1 &&
		    (index == I915_PDES ||
		     (i915_vm_has_scratch_64K(vma->vm) &&
		      !iter->sg && IS_ALIGNED(vma->node.start +
					      vma->node.size,
					      I915_GTT_PAGE_SIZE_2M)))) {
			vaddr = kmap_atomic_px(pd);
			vaddr[maybe_64K] |= GEN8_PDE_IPS_64K;
			kunmap_atomic(vaddr);
			page_size = I915_GTT_PAGE_SIZE_64K;

			/*
			 * We write all 4K page entries, even when using 64K
			 * pages. In order to verify that the HW isn't cheating
			 * by using the 4K PTE instead of the 64K PTE, we want
			 * to remove all the surplus entries. If the HW skipped
			 * the 64K PTE, it will read/write into the scratch page
			 * instead - which we detect as missing results during
			 * selftests.
			 */
			if (I915_SELFTEST_ONLY(vma->vm->scrub_64K)) {
				u16 i;

				encode = vma->vm->scratch[0].encode;
				vaddr = kmap_atomic_px(i915_pt_entry(pd, maybe_64K));

				for (i = 1; i < index; i += 16)
					memset64(vaddr + i, encode, 15);

				kunmap_atomic(vaddr);
			}
		}

		vma->page_sizes.gtt |= page_size;
	} while (iter->sg);
}

static void gen8_ppgtt_insert(struct i915_address_space *vm,
			      struct i915_vma *vma,
			      enum i915_cache_level cache_level,
			      u32 flags)
{
	struct i915_ppgtt * const ppgtt = i915_vm_to_ppgtt(vm);
	struct sgt_dma iter = sgt_dma(vma);

	if (vma->page_sizes.sg > I915_GTT_PAGE_SIZE) {
		gen8_ppgtt_insert_huge(vma, &iter, cache_level, flags);
	} else  {
		u64 idx = vma->node.start >> GEN8_PTE_SHIFT;

		do {
			struct i915_page_directory * const pdp =
				gen8_pdp_for_page_index(vm, idx);

			idx = gen8_ppgtt_insert_pte(ppgtt, pdp, &iter, idx,
						    cache_level, flags);
		} while (idx);

		vma->page_sizes.gtt = I915_GTT_PAGE_SIZE;
	}
}

static int gen8_init_scratch(struct i915_address_space *vm)
{
	int ret;
	int i;

	/*
	 * If everybody agrees to not to write into the scratch page,
	 * we can reuse it for all vm, keeping contexts and processes separate.
	 */
	if (vm->has_read_only &&
	    vm->i915->kernel_context &&
	    vm->i915->kernel_context->vm) {
		struct i915_address_space *clone =
			rcu_dereference_protected(vm->i915->kernel_context->vm,
						  true); /* static */

		GEM_BUG_ON(!clone->has_read_only);

		vm->scratch_order = clone->scratch_order;
		memcpy(vm->scratch, clone->scratch, sizeof(vm->scratch));
		px_dma(&vm->scratch[0]) = 0; /* no xfer of ownership */
		return 0;
	}

	ret = setup_scratch_page(vm, __GFP_HIGHMEM);
	if (ret)
		return ret;

	vm->scratch[0].encode =
		gen8_pte_encode(px_dma(&vm->scratch[0]),
				I915_CACHE_LLC, vm->has_read_only);

	for (i = 1; i <= vm->top; i++) {
		if (unlikely(setup_page_dma(vm, px_base(&vm->scratch[i]))))
			goto free_scratch;

		fill_px(&vm->scratch[i], vm->scratch[i - 1].encode);
		vm->scratch[i].encode =
			gen8_pde_encode(px_dma(&vm->scratch[i]),
					I915_CACHE_LLC);
	}

	return 0;

free_scratch:
	free_scratch(vm);
	return -ENOMEM;
}

static int gen8_preallocate_top_level_pdp(struct i915_ppgtt *ppgtt)
{
	struct i915_address_space *vm = &ppgtt->vm;
	struct i915_page_directory *pd = ppgtt->pd;
	unsigned int idx;

	GEM_BUG_ON(vm->top != 2);
	GEM_BUG_ON(gen8_pd_top_count(vm) != GEN8_3LVL_PDPES);

	for (idx = 0; idx < GEN8_3LVL_PDPES; idx++) {
		struct i915_page_directory *pde;

		pde = alloc_pd(vm);
		if (IS_ERR(pde))
			return PTR_ERR(pde);

		fill_px(pde, vm->scratch[1].encode);
		set_pd_entry(pd, idx, pde);
		atomic_inc(px_used(pde)); /* keep pinned */
	}
	wmb();

	return 0;
}

static void ppgtt_init(struct i915_ppgtt *ppgtt, struct intel_gt *gt)
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

static struct i915_page_directory *
gen8_alloc_top_pd(struct i915_address_space *vm)
{
	const unsigned int count = gen8_pd_top_count(vm);
	struct i915_page_directory *pd;

	GEM_BUG_ON(count > ARRAY_SIZE(pd->entry));

	pd = __alloc_pd(offsetof(typeof(*pd), entry[count]));
	if (unlikely(!pd))
		return ERR_PTR(-ENOMEM);

	if (unlikely(setup_page_dma(vm, px_base(pd)))) {
		kfree(pd);
		return ERR_PTR(-ENOMEM);
	}

	fill_page_dma(px_base(pd), vm->scratch[vm->top].encode, count);
	atomic_inc(px_used(pd)); /* mark as pinned */
	return pd;
}

/*
 * GEN8 legacy ppgtt programming is accomplished through a max 4 PDP registers
 * with a net effect resembling a 2-level page table in normal x86 terms. Each
 * PDP represents 1GB of memory 4 * 512 * 512 * 4096 = 4GB legacy 32b address
 * space.
 *
 */
static struct i915_ppgtt *gen8_ppgtt_create(struct drm_i915_private *i915)
{
	struct i915_ppgtt *ppgtt;
	int err;

	ppgtt = kzalloc(sizeof(*ppgtt), GFP_KERNEL);
	if (!ppgtt)
		return ERR_PTR(-ENOMEM);

	ppgtt_init(ppgtt, &i915->gt);
	ppgtt->vm.top = i915_vm_is_4lvl(&ppgtt->vm) ? 3 : 2;

	/*
	 * From bdw, there is hw support for read-only pages in the PPGTT.
	 *
	 * Gen11 has HSDES#:1807136187 unresolved. Disable ro support
	 * for now.
	 *
	 * Gen12 has inherited the same read-only fault issue from gen11.
	 */
	ppgtt->vm.has_read_only = !IS_GEN_RANGE(i915, 11, 12);

	/* There are only few exceptions for gen >=6. chv and bxt.
	 * And we are not sure about the latter so play safe for now.
	 */
	if (IS_CHERRYVIEW(i915) || IS_BROXTON(i915))
		ppgtt->vm.pt_kmap_wc = true;

	err = gen8_init_scratch(&ppgtt->vm);
	if (err)
		goto err_free;

	ppgtt->pd = gen8_alloc_top_pd(&ppgtt->vm);
	if (IS_ERR(ppgtt->pd)) {
		err = PTR_ERR(ppgtt->pd);
		goto err_free_scratch;
	}

	if (!i915_vm_is_4lvl(&ppgtt->vm)) {
		err = gen8_preallocate_top_level_pdp(ppgtt);
		if (err)
			goto err_free_pd;
	}

	ppgtt->vm.bind_async_flags = I915_VMA_LOCAL_BIND;
	ppgtt->vm.insert_entries = gen8_ppgtt_insert;
	ppgtt->vm.allocate_va_range = gen8_ppgtt_alloc;
	ppgtt->vm.clear_range = gen8_ppgtt_clear;

	if (intel_vgpu_active(i915))
		gen8_ppgtt_notify_vgt(ppgtt, true);

	ppgtt->vm.cleanup = gen8_ppgtt_cleanup;

	return ppgtt;

err_free_pd:
	__gen8_ppgtt_cleanup(&ppgtt->vm, ppgtt->pd,
			     gen8_pd_top_count(&ppgtt->vm), ppgtt->vm.top);
err_free_scratch:
	free_scratch(&ppgtt->vm);
err_free:
	kfree(ppgtt);
	return ERR_PTR(err);
}

/* Write pde (index) from the page directory @pd to the page table @pt */
static inline void gen6_write_pde(const struct gen6_ppgtt *ppgtt,
				  const unsigned int pde,
				  const struct i915_page_table *pt)
{
	/* Caller needs to make sure the write completes if necessary */
	iowrite32(GEN6_PDE_ADDR_ENCODE(px_dma(pt)) | GEN6_PDE_VALID,
		  ppgtt->pd_addr + pde);
}

static void gen7_ppgtt_enable(struct intel_gt *gt)
{
	struct drm_i915_private *i915 = gt->i915;
	struct intel_uncore *uncore = gt->uncore;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	u32 ecochk;

	intel_uncore_rmw(uncore, GAC_ECO_BITS, 0, ECOBITS_PPGTT_CACHE64B);

	ecochk = intel_uncore_read(uncore, GAM_ECOCHK);
	if (IS_HASWELL(i915)) {
		ecochk |= ECOCHK_PPGTT_WB_HSW;
	} else {
		ecochk |= ECOCHK_PPGTT_LLC_IVB;
		ecochk &= ~ECOCHK_PPGTT_GFDT_IVB;
	}
	intel_uncore_write(uncore, GAM_ECOCHK, ecochk);

	for_each_engine(engine, gt, id) {
		/* GFX_MODE is per-ring on gen7+ */
		ENGINE_WRITE(engine,
			     RING_MODE_GEN7,
			     _MASKED_BIT_ENABLE(GFX_PPGTT_ENABLE));
	}
}

static void gen6_ppgtt_enable(struct intel_gt *gt)
{
	struct intel_uncore *uncore = gt->uncore;

	intel_uncore_rmw(uncore,
			 GAC_ECO_BITS,
			 0,
			 ECOBITS_SNB_BIT | ECOBITS_PPGTT_CACHE64B);

	intel_uncore_rmw(uncore,
			 GAB_CTL,
			 0,
			 GAB_CTL_CONT_AFTER_PAGEFAULT);

	intel_uncore_rmw(uncore,
			 GAM_ECOCHK,
			 0,
			 ECOCHK_SNB_BIT | ECOCHK_PPGTT_CACHE64B);

	if (HAS_PPGTT(uncore->i915)) /* may be disabled for VT-d */
		intel_uncore_write(uncore,
				   GFX_MODE,
				   _MASKED_BIT_ENABLE(GFX_PPGTT_ENABLE));
}

/* PPGTT support for Sandybdrige/Gen6 and later */
static void gen6_ppgtt_clear_range(struct i915_address_space *vm,
				   u64 start, u64 length)
{
	struct gen6_ppgtt * const ppgtt = to_gen6_ppgtt(i915_vm_to_ppgtt(vm));
	const unsigned int first_entry = start / I915_GTT_PAGE_SIZE;
	const gen6_pte_t scratch_pte = vm->scratch[0].encode;
	unsigned int pde = first_entry / GEN6_PTES;
	unsigned int pte = first_entry % GEN6_PTES;
	unsigned int num_entries = length / I915_GTT_PAGE_SIZE;

	while (num_entries) {
		struct i915_page_table * const pt =
			i915_pt_entry(ppgtt->base.pd, pde++);
		const unsigned int count = min(num_entries, GEN6_PTES - pte);
		gen6_pte_t *vaddr;

		GEM_BUG_ON(px_base(pt) == px_base(&vm->scratch[1]));

		num_entries -= count;

		GEM_BUG_ON(count > atomic_read(&pt->used));
		if (!atomic_sub_return(count, &pt->used))
			ppgtt->scan_for_unused_pt = true;

		/*
		 * Note that the hw doesn't support removing PDE on the fly
		 * (they are cached inside the context with no means to
		 * invalidate the cache), so we can only reset the PTE
		 * entries back to scratch.
		 */

		vaddr = kmap_atomic_px(pt);
		memset32(vaddr + pte, scratch_pte, count);
		kunmap_atomic(vaddr);

		pte = 0;
	}
}

static void gen6_ppgtt_insert_entries(struct i915_address_space *vm,
				      struct i915_vma *vma,
				      enum i915_cache_level cache_level,
				      u32 flags)
{
	struct i915_ppgtt *ppgtt = i915_vm_to_ppgtt(vm);
	struct i915_page_directory * const pd = ppgtt->pd;
	unsigned first_entry = vma->node.start / I915_GTT_PAGE_SIZE;
	unsigned act_pt = first_entry / GEN6_PTES;
	unsigned act_pte = first_entry % GEN6_PTES;
	const u32 pte_encode = vm->pte_encode(0, cache_level, flags);
	struct sgt_dma iter = sgt_dma(vma);
	gen6_pte_t *vaddr;

	GEM_BUG_ON(pd->entry[act_pt] == &vm->scratch[1]);

	vaddr = kmap_atomic_px(i915_pt_entry(pd, act_pt));
	do {
		GEM_BUG_ON(iter.sg->length < I915_GTT_PAGE_SIZE);
		vaddr[act_pte] = pte_encode | GEN6_PTE_ADDR_ENCODE(iter.dma);

		iter.dma += I915_GTT_PAGE_SIZE;
		if (iter.dma == iter.max) {
			iter.sg = __sg_next(iter.sg);
			if (!iter.sg)
				break;

			iter.dma = sg_dma_address(iter.sg);
			iter.max = iter.dma + iter.sg->length;
		}

		if (++act_pte == GEN6_PTES) {
			kunmap_atomic(vaddr);
			vaddr = kmap_atomic_px(i915_pt_entry(pd, ++act_pt));
			act_pte = 0;
		}
	} while (1);
	kunmap_atomic(vaddr);

	vma->page_sizes.gtt = I915_GTT_PAGE_SIZE;
}

static int gen6_alloc_va_range(struct i915_address_space *vm,
			       u64 start, u64 length)
{
	struct gen6_ppgtt *ppgtt = to_gen6_ppgtt(i915_vm_to_ppgtt(vm));
	struct i915_page_directory * const pd = ppgtt->base.pd;
	struct i915_page_table *pt, *alloc = NULL;
	intel_wakeref_t wakeref;
	u64 from = start;
	unsigned int pde;
	bool flush = false;
	int ret = 0;

	wakeref = intel_runtime_pm_get(&vm->i915->runtime_pm);

	spin_lock(&pd->lock);
	gen6_for_each_pde(pt, pd, start, length, pde) {
		const unsigned int count = gen6_pte_count(start, length);

		if (px_base(pt) == px_base(&vm->scratch[1])) {
			spin_unlock(&pd->lock);

			pt = fetch_and_zero(&alloc);
			if (!pt)
				pt = alloc_pt(vm);
			if (IS_ERR(pt)) {
				ret = PTR_ERR(pt);
				goto unwind_out;
			}

			fill32_px(pt, vm->scratch[0].encode);

			spin_lock(&pd->lock);
			if (pd->entry[pde] == &vm->scratch[1]) {
				pd->entry[pde] = pt;
				if (i915_vma_is_bound(ppgtt->vma,
						      I915_VMA_GLOBAL_BIND)) {
					gen6_write_pde(ppgtt, pde, pt);
					flush = true;
				}
			} else {
				alloc = pt;
				pt = pd->entry[pde];
			}
		}

		atomic_add(count, &pt->used);
	}
	spin_unlock(&pd->lock);

	if (flush)
		gen6_ggtt_invalidate(vm->gt->ggtt);

	goto out;

unwind_out:
	gen6_ppgtt_clear_range(vm, from, start - from);
out:
	if (alloc)
		free_px(vm, alloc);
	intel_runtime_pm_put(&vm->i915->runtime_pm, wakeref);
	return ret;
}

static int gen6_ppgtt_init_scratch(struct gen6_ppgtt *ppgtt)
{
	struct i915_address_space * const vm = &ppgtt->base.vm;
	struct i915_page_directory * const pd = ppgtt->base.pd;
	int ret;

	ret = setup_scratch_page(vm, __GFP_HIGHMEM);
	if (ret)
		return ret;

	vm->scratch[0].encode =
		vm->pte_encode(px_dma(&vm->scratch[0]),
			       I915_CACHE_NONE, PTE_READ_ONLY);

	if (unlikely(setup_page_dma(vm, px_base(&vm->scratch[1])))) {
		cleanup_scratch_page(vm);
		return -ENOMEM;
	}

	fill32_px(&vm->scratch[1], vm->scratch[0].encode);
	memset_p(pd->entry, &vm->scratch[1], I915_PDES);

	return 0;
}

static void gen6_ppgtt_free_pd(struct gen6_ppgtt *ppgtt)
{
	struct i915_page_directory * const pd = ppgtt->base.pd;
	struct i915_page_dma * const scratch =
		px_base(&ppgtt->base.vm.scratch[1]);
	struct i915_page_table *pt;
	u32 pde;

	gen6_for_all_pdes(pt, pd, pde)
		if (px_base(pt) != scratch)
			free_px(&ppgtt->base.vm, pt);
}

static void gen6_ppgtt_cleanup(struct i915_address_space *vm)
{
	struct gen6_ppgtt *ppgtt = to_gen6_ppgtt(i915_vm_to_ppgtt(vm));

	i915_vma_destroy(ppgtt->vma);

	gen6_ppgtt_free_pd(ppgtt);
	free_scratch(vm);

	mutex_destroy(&ppgtt->pin_mutex);
	kfree(ppgtt->base.pd);
}

static int pd_vma_set_pages(struct i915_vma *vma)
{
	vma->pages = ERR_PTR(-ENODEV);
	return 0;
}

static void pd_vma_clear_pages(struct i915_vma *vma)
{
	GEM_BUG_ON(!vma->pages);

	vma->pages = NULL;
}

static int pd_vma_bind(struct i915_vma *vma,
		       enum i915_cache_level cache_level,
		       u32 unused)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vma->vm);
	struct gen6_ppgtt *ppgtt = vma->private;
	u32 ggtt_offset = i915_ggtt_offset(vma) / I915_GTT_PAGE_SIZE;
	struct i915_page_table *pt;
	unsigned int pde;

	px_base(ppgtt->base.pd)->ggtt_offset = ggtt_offset * sizeof(gen6_pte_t);
	ppgtt->pd_addr = (gen6_pte_t __iomem *)ggtt->gsm + ggtt_offset;

	gen6_for_all_pdes(pt, ppgtt->base.pd, pde)
		gen6_write_pde(ppgtt, pde, pt);

	gen6_ggtt_invalidate(ggtt);

	return 0;
}

static void pd_vma_unbind(struct i915_vma *vma)
{
	struct gen6_ppgtt *ppgtt = vma->private;
	struct i915_page_directory * const pd = ppgtt->base.pd;
	struct i915_page_dma * const scratch =
		px_base(&ppgtt->base.vm.scratch[1]);
	struct i915_page_table *pt;
	unsigned int pde;

	if (!ppgtt->scan_for_unused_pt)
		return;

	/* Free all no longer used page tables */
	gen6_for_all_pdes(pt, ppgtt->base.pd, pde) {
		if (px_base(pt) == scratch || atomic_read(&pt->used))
			continue;

		free_px(&ppgtt->base.vm, pt);
		pd->entry[pde] = scratch;
	}

	ppgtt->scan_for_unused_pt = false;
}

static const struct i915_vma_ops pd_vma_ops = {
	.set_pages = pd_vma_set_pages,
	.clear_pages = pd_vma_clear_pages,
	.bind_vma = pd_vma_bind,
	.unbind_vma = pd_vma_unbind,
};

static struct i915_vma *pd_vma_create(struct gen6_ppgtt *ppgtt, int size)
{
	struct i915_ggtt *ggtt = ppgtt->base.vm.gt->ggtt;
	struct i915_vma *vma;

	GEM_BUG_ON(!IS_ALIGNED(size, I915_GTT_PAGE_SIZE));
	GEM_BUG_ON(size > ggtt->vm.total);

	vma = i915_vma_alloc();
	if (!vma)
		return ERR_PTR(-ENOMEM);

	i915_active_init(&vma->active, NULL, NULL);

	mutex_init(&vma->pages_mutex);
	vma->vm = i915_vm_get(&ggtt->vm);
	vma->ops = &pd_vma_ops;
	vma->private = ppgtt;

	vma->size = size;
	vma->fence_size = size;
	atomic_set(&vma->flags, I915_VMA_GGTT);
	vma->ggtt_view.type = I915_GGTT_VIEW_ROTATED; /* prevent fencing */

	INIT_LIST_HEAD(&vma->obj_link);
	INIT_LIST_HEAD(&vma->closed_link);

	return vma;
}

int gen6_ppgtt_pin(struct i915_ppgtt *base)
{
	struct gen6_ppgtt *ppgtt = to_gen6_ppgtt(base);
	int err = 0;

	GEM_BUG_ON(!atomic_read(&ppgtt->base.vm.open));

	/*
	 * Workaround the limited maximum vma->pin_count and the aliasing_ppgtt
	 * which will be pinned into every active context.
	 * (When vma->pin_count becomes atomic, I expect we will naturally
	 * need a larger, unpacked, type and kill this redundancy.)
	 */
	if (atomic_add_unless(&ppgtt->pin_count, 1, 0))
		return 0;

	if (mutex_lock_interruptible(&ppgtt->pin_mutex))
		return -EINTR;

	/*
	 * PPGTT PDEs reside in the GGTT and consists of 512 entries. The
	 * allocator works in address space sizes, so it's multiplied by page
	 * size. We allocate at the top of the GTT to avoid fragmentation.
	 */
	if (!atomic_read(&ppgtt->pin_count)) {
		err = i915_vma_pin(ppgtt->vma,
				   0, GEN6_PD_ALIGN,
				   PIN_GLOBAL | PIN_HIGH);
	}
	if (!err)
		atomic_inc(&ppgtt->pin_count);
	mutex_unlock(&ppgtt->pin_mutex);

	return err;
}

void gen6_ppgtt_unpin(struct i915_ppgtt *base)
{
	struct gen6_ppgtt *ppgtt = to_gen6_ppgtt(base);

	GEM_BUG_ON(!atomic_read(&ppgtt->pin_count));
	if (atomic_dec_and_test(&ppgtt->pin_count))
		i915_vma_unpin(ppgtt->vma);
}

void gen6_ppgtt_unpin_all(struct i915_ppgtt *base)
{
	struct gen6_ppgtt *ppgtt = to_gen6_ppgtt(base);

	if (!atomic_read(&ppgtt->pin_count))
		return;

	i915_vma_unpin(ppgtt->vma);
	atomic_set(&ppgtt->pin_count, 0);
}

static struct i915_ppgtt *gen6_ppgtt_create(struct drm_i915_private *i915)
{
	struct i915_ggtt * const ggtt = &i915->ggtt;
	struct gen6_ppgtt *ppgtt;
	int err;

	ppgtt = kzalloc(sizeof(*ppgtt), GFP_KERNEL);
	if (!ppgtt)
		return ERR_PTR(-ENOMEM);

	mutex_init(&ppgtt->pin_mutex);

	ppgtt_init(&ppgtt->base, &i915->gt);
	ppgtt->base.vm.top = 1;

	ppgtt->base.vm.bind_async_flags = I915_VMA_LOCAL_BIND;
	ppgtt->base.vm.allocate_va_range = gen6_alloc_va_range;
	ppgtt->base.vm.clear_range = gen6_ppgtt_clear_range;
	ppgtt->base.vm.insert_entries = gen6_ppgtt_insert_entries;
	ppgtt->base.vm.cleanup = gen6_ppgtt_cleanup;

	ppgtt->base.vm.pte_encode = ggtt->vm.pte_encode;

	ppgtt->base.pd = __alloc_pd(sizeof(*ppgtt->base.pd));
	if (!ppgtt->base.pd) {
		err = -ENOMEM;
		goto err_free;
	}

	err = gen6_ppgtt_init_scratch(ppgtt);
	if (err)
		goto err_pd;

	ppgtt->vma = pd_vma_create(ppgtt, GEN6_PD_SIZE);
	if (IS_ERR(ppgtt->vma)) {
		err = PTR_ERR(ppgtt->vma);
		goto err_scratch;
	}

	return &ppgtt->base;

err_scratch:
	free_scratch(&ppgtt->base.vm);
err_pd:
	kfree(ppgtt->base.pd);
err_free:
	kfree(ppgtt);
	return ERR_PTR(err);
}

static void gtt_write_workarounds(struct intel_gt *gt)
{
	struct drm_i915_private *i915 = gt->i915;
	struct intel_uncore *uncore = gt->uncore;

	/* This function is for gtt related workarounds. This function is
	 * called on driver load and after a GPU reset, so you can place
	 * workarounds here even if they get overwritten by GPU reset.
	 */
	/* WaIncreaseDefaultTLBEntries:chv,bdw,skl,bxt,kbl,glk,cfl,cnl,icl */
	if (IS_BROADWELL(i915))
		intel_uncore_write(uncore,
				   GEN8_L3_LRA_1_GPGPU,
				   GEN8_L3_LRA_1_GPGPU_DEFAULT_VALUE_BDW);
	else if (IS_CHERRYVIEW(i915))
		intel_uncore_write(uncore,
				   GEN8_L3_LRA_1_GPGPU,
				   GEN8_L3_LRA_1_GPGPU_DEFAULT_VALUE_CHV);
	else if (IS_GEN9_LP(i915))
		intel_uncore_write(uncore,
				   GEN8_L3_LRA_1_GPGPU,
				   GEN9_L3_LRA_1_GPGPU_DEFAULT_VALUE_BXT);
	else if (INTEL_GEN(i915) >= 9 && INTEL_GEN(i915) <= 11)
		intel_uncore_write(uncore,
				   GEN8_L3_LRA_1_GPGPU,
				   GEN9_L3_LRA_1_GPGPU_DEFAULT_VALUE_SKL);

	/*
	 * To support 64K PTEs we need to first enable the use of the
	 * Intermediate-Page-Size(IPS) bit of the PDE field via some magical
	 * mmio, otherwise the page-walker will simply ignore the IPS bit. This
	 * shouldn't be needed after GEN10.
	 *
	 * 64K pages were first introduced from BDW+, although technically they
	 * only *work* from gen9+. For pre-BDW we instead have the option for
	 * 32K pages, but we don't currently have any support for it in our
	 * driver.
	 */
	if (HAS_PAGE_SIZES(i915, I915_GTT_PAGE_SIZE_64K) &&
	    INTEL_GEN(i915) <= 10)
		intel_uncore_rmw(uncore,
				 GEN8_GAMW_ECO_DEV_RW_IA,
				 0,
				 GAMW_ECO_ENABLE_64K_IPS_FIELD);

	if (IS_GEN_RANGE(i915, 8, 11)) {
		bool can_use_gtt_cache = true;

		/*
		 * According to the BSpec if we use 2M/1G pages then we also
		 * need to disable the GTT cache. At least on BDW we can see
		 * visual corruption when using 2M pages, and not disabling the
		 * GTT cache.
		 */
		if (HAS_PAGE_SIZES(i915, I915_GTT_PAGE_SIZE_2M))
			can_use_gtt_cache = false;

		/* WaGttCachingOffByDefault */
		intel_uncore_write(uncore,
				   HSW_GTT_CACHE_EN,
				   can_use_gtt_cache ? GTT_CACHE_EN_ALL : 0);
		WARN_ON_ONCE(can_use_gtt_cache &&
			     intel_uncore_read(uncore,
					       HSW_GTT_CACHE_EN) == 0);
	}
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
__ppgtt_create(struct drm_i915_private *i915)
{
	if (INTEL_GEN(i915) < 8)
		return gen6_ppgtt_create(i915);
	else
		return gen8_ppgtt_create(i915);
}

struct i915_ppgtt *
i915_ppgtt_create(struct drm_i915_private *i915)
{
	struct i915_ppgtt *ppgtt;

	ppgtt = __ppgtt_create(i915);
	if (IS_ERR(ppgtt))
		return ppgtt;

	trace_i915_ppgtt_create(&ppgtt->vm);

	return ppgtt;
}

/* Certain Gen5 chipsets require require idling the GPU before
 * unmapping anything from the GTT when VT-d is enabled.
 */
static bool needs_idle_maps(struct drm_i915_private *dev_priv)
{
	/* Query intel_iommu to see if we need the workaround. Presumably that
	 * was loaded first.
	 */
	return IS_GEN(dev_priv, 5) && IS_MOBILE(dev_priv) && intel_vtd_active();
}

static void ggtt_suspend_mappings(struct i915_ggtt *ggtt)
{
	struct drm_i915_private *i915 = ggtt->vm.i915;

	/* Don't bother messing with faults pre GEN6 as we have little
	 * documentation supporting that it's a good idea.
	 */
	if (INTEL_GEN(i915) < 6)
		return;

	intel_gt_check_and_clear_faults(ggtt->vm.gt);

	ggtt->vm.clear_range(&ggtt->vm, 0, ggtt->vm.total);

	ggtt->invalidate(ggtt);
}

void i915_gem_suspend_gtt_mappings(struct drm_i915_private *i915)
{
	ggtt_suspend_mappings(&i915->ggtt);
}

int i915_gem_gtt_prepare_pages(struct drm_i915_gem_object *obj,
			       struct sg_table *pages)
{
	do {
		if (dma_map_sg_attrs(&obj->base.dev->pdev->dev,
				     pages->sgl, pages->nents,
				     PCI_DMA_BIDIRECTIONAL,
				     DMA_ATTR_NO_WARN))
			return 0;

		/*
		 * If the DMA remap fails, one cause can be that we have
		 * too many objects pinned in a small remapping table,
		 * such as swiotlb. Incrementally purge all other objects and
		 * try again - if there are no more pages to remove from
		 * the DMA remapper, i915_gem_shrink will return 0.
		 */
		GEM_BUG_ON(obj->mm.pages == pages);
	} while (i915_gem_shrink(to_i915(obj->base.dev),
				 obj->base.size >> PAGE_SHIFT, NULL,
				 I915_SHRINK_BOUND |
				 I915_SHRINK_UNBOUND));

	return -ENOSPC;
}

static void gen8_set_pte(void __iomem *addr, gen8_pte_t pte)
{
	writeq(pte, addr);
}

static void gen8_ggtt_insert_page(struct i915_address_space *vm,
				  dma_addr_t addr,
				  u64 offset,
				  enum i915_cache_level level,
				  u32 unused)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
	gen8_pte_t __iomem *pte =
		(gen8_pte_t __iomem *)ggtt->gsm + offset / I915_GTT_PAGE_SIZE;

	gen8_set_pte(pte, gen8_pte_encode(addr, level, 0));

	ggtt->invalidate(ggtt);
}

static void gen8_ggtt_insert_entries(struct i915_address_space *vm,
				     struct i915_vma *vma,
				     enum i915_cache_level level,
				     u32 flags)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
	struct sgt_iter sgt_iter;
	gen8_pte_t __iomem *gtt_entries;
	const gen8_pte_t pte_encode = gen8_pte_encode(0, level, 0);
	dma_addr_t addr;

	/*
	 * Note that we ignore PTE_READ_ONLY here. The caller must be careful
	 * not to allow the user to override access to a read only page.
	 */

	gtt_entries = (gen8_pte_t __iomem *)ggtt->gsm;
	gtt_entries += vma->node.start / I915_GTT_PAGE_SIZE;
	for_each_sgt_daddr(addr, sgt_iter, vma->pages)
		gen8_set_pte(gtt_entries++, pte_encode | addr);

	/*
	 * We want to flush the TLBs only after we're certain all the PTE
	 * updates have finished.
	 */
	ggtt->invalidate(ggtt);
}

static void gen6_ggtt_insert_page(struct i915_address_space *vm,
				  dma_addr_t addr,
				  u64 offset,
				  enum i915_cache_level level,
				  u32 flags)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
	gen6_pte_t __iomem *pte =
		(gen6_pte_t __iomem *)ggtt->gsm + offset / I915_GTT_PAGE_SIZE;

	iowrite32(vm->pte_encode(addr, level, flags), pte);

	ggtt->invalidate(ggtt);
}

/*
 * Binds an object into the global gtt with the specified cache level. The object
 * will be accessible to the GPU via commands whose operands reference offsets
 * within the global GTT as well as accessible by the GPU through the GMADR
 * mapped BAR (dev_priv->mm.gtt->gtt).
 */
static void gen6_ggtt_insert_entries(struct i915_address_space *vm,
				     struct i915_vma *vma,
				     enum i915_cache_level level,
				     u32 flags)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
	gen6_pte_t __iomem *entries = (gen6_pte_t __iomem *)ggtt->gsm;
	unsigned int i = vma->node.start / I915_GTT_PAGE_SIZE;
	struct sgt_iter iter;
	dma_addr_t addr;
	for_each_sgt_daddr(addr, iter, vma->pages)
		iowrite32(vm->pte_encode(addr, level, flags), &entries[i++]);

	/*
	 * We want to flush the TLBs only after we're certain all the PTE
	 * updates have finished.
	 */
	ggtt->invalidate(ggtt);
}

static void nop_clear_range(struct i915_address_space *vm,
			    u64 start, u64 length)
{
}

static void gen8_ggtt_clear_range(struct i915_address_space *vm,
				  u64 start, u64 length)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
	unsigned first_entry = start / I915_GTT_PAGE_SIZE;
	unsigned num_entries = length / I915_GTT_PAGE_SIZE;
	const gen8_pte_t scratch_pte = vm->scratch[0].encode;
	gen8_pte_t __iomem *gtt_base =
		(gen8_pte_t __iomem *)ggtt->gsm + first_entry;
	const int max_entries = ggtt_total_entries(ggtt) - first_entry;
	int i;

	if (WARN(num_entries > max_entries,
		 "First entry = %d; Num entries = %d (max=%d)\n",
		 first_entry, num_entries, max_entries))
		num_entries = max_entries;

	for (i = 0; i < num_entries; i++)
		gen8_set_pte(&gtt_base[i], scratch_pte);
}

static void bxt_vtd_ggtt_wa(struct i915_address_space *vm)
{
	struct drm_i915_private *dev_priv = vm->i915;

	/*
	 * Make sure the internal GAM fifo has been cleared of all GTT
	 * writes before exiting stop_machine(). This guarantees that
	 * any aperture accesses waiting to start in another process
	 * cannot back up behind the GTT writes causing a hang.
	 * The register can be any arbitrary GAM register.
	 */
	POSTING_READ(GFX_FLSH_CNTL_GEN6);
}

struct insert_page {
	struct i915_address_space *vm;
	dma_addr_t addr;
	u64 offset;
	enum i915_cache_level level;
};

static int bxt_vtd_ggtt_insert_page__cb(void *_arg)
{
	struct insert_page *arg = _arg;

	gen8_ggtt_insert_page(arg->vm, arg->addr, arg->offset, arg->level, 0);
	bxt_vtd_ggtt_wa(arg->vm);

	return 0;
}

static void bxt_vtd_ggtt_insert_page__BKL(struct i915_address_space *vm,
					  dma_addr_t addr,
					  u64 offset,
					  enum i915_cache_level level,
					  u32 unused)
{
	struct insert_page arg = { vm, addr, offset, level };

	stop_machine(bxt_vtd_ggtt_insert_page__cb, &arg, NULL);
}

struct insert_entries {
	struct i915_address_space *vm;
	struct i915_vma *vma;
	enum i915_cache_level level;
	u32 flags;
};

static int bxt_vtd_ggtt_insert_entries__cb(void *_arg)
{
	struct insert_entries *arg = _arg;

	gen8_ggtt_insert_entries(arg->vm, arg->vma, arg->level, arg->flags);
	bxt_vtd_ggtt_wa(arg->vm);

	return 0;
}

static void bxt_vtd_ggtt_insert_entries__BKL(struct i915_address_space *vm,
					     struct i915_vma *vma,
					     enum i915_cache_level level,
					     u32 flags)
{
	struct insert_entries arg = { vm, vma, level, flags };

	stop_machine(bxt_vtd_ggtt_insert_entries__cb, &arg, NULL);
}

struct clear_range {
	struct i915_address_space *vm;
	u64 start;
	u64 length;
};

static int bxt_vtd_ggtt_clear_range__cb(void *_arg)
{
	struct clear_range *arg = _arg;

	gen8_ggtt_clear_range(arg->vm, arg->start, arg->length);
	bxt_vtd_ggtt_wa(arg->vm);

	return 0;
}

static void bxt_vtd_ggtt_clear_range__BKL(struct i915_address_space *vm,
					  u64 start,
					  u64 length)
{
	struct clear_range arg = { vm, start, length };

	stop_machine(bxt_vtd_ggtt_clear_range__cb, &arg, NULL);
}

static void gen6_ggtt_clear_range(struct i915_address_space *vm,
				  u64 start, u64 length)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
	unsigned first_entry = start / I915_GTT_PAGE_SIZE;
	unsigned num_entries = length / I915_GTT_PAGE_SIZE;
	gen6_pte_t scratch_pte, __iomem *gtt_base =
		(gen6_pte_t __iomem *)ggtt->gsm + first_entry;
	const int max_entries = ggtt_total_entries(ggtt) - first_entry;
	int i;

	if (WARN(num_entries > max_entries,
		 "First entry = %d; Num entries = %d (max=%d)\n",
		 first_entry, num_entries, max_entries))
		num_entries = max_entries;

	scratch_pte = vm->scratch[0].encode;
	for (i = 0; i < num_entries; i++)
		iowrite32(scratch_pte, &gtt_base[i]);
}

static void i915_ggtt_insert_page(struct i915_address_space *vm,
				  dma_addr_t addr,
				  u64 offset,
				  enum i915_cache_level cache_level,
				  u32 unused)
{
	unsigned int flags = (cache_level == I915_CACHE_NONE) ?
		AGP_USER_MEMORY : AGP_USER_CACHED_MEMORY;

	intel_gtt_insert_page(addr, offset >> PAGE_SHIFT, flags);
}

static void i915_ggtt_insert_entries(struct i915_address_space *vm,
				     struct i915_vma *vma,
				     enum i915_cache_level cache_level,
				     u32 unused)
{
	unsigned int flags = (cache_level == I915_CACHE_NONE) ?
		AGP_USER_MEMORY : AGP_USER_CACHED_MEMORY;

	intel_gtt_insert_sg_entries(vma->pages, vma->node.start >> PAGE_SHIFT,
				    flags);
}

static void i915_ggtt_clear_range(struct i915_address_space *vm,
				  u64 start, u64 length)
{
	intel_gtt_clear_range(start >> PAGE_SHIFT, length >> PAGE_SHIFT);
}

static int ggtt_bind_vma(struct i915_vma *vma,
			 enum i915_cache_level cache_level,
			 u32 flags)
{
	struct drm_i915_private *i915 = vma->vm->i915;
	struct drm_i915_gem_object *obj = vma->obj;
	intel_wakeref_t wakeref;
	u32 pte_flags;

	/* Applicable to VLV (gen8+ do not support RO in the GGTT) */
	pte_flags = 0;
	if (i915_gem_object_is_readonly(obj))
		pte_flags |= PTE_READ_ONLY;

	with_intel_runtime_pm(&i915->runtime_pm, wakeref)
		vma->vm->insert_entries(vma->vm, vma, cache_level, pte_flags);

	vma->page_sizes.gtt = I915_GTT_PAGE_SIZE;

	/*
	 * Without aliasing PPGTT there's no difference between
	 * GLOBAL/LOCAL_BIND, it's all the same ptes. Hence unconditionally
	 * upgrade to both bound if we bind either to avoid double-binding.
	 */
	atomic_or(I915_VMA_GLOBAL_BIND | I915_VMA_LOCAL_BIND, &vma->flags);

	return 0;
}

static void ggtt_unbind_vma(struct i915_vma *vma)
{
	struct drm_i915_private *i915 = vma->vm->i915;
	intel_wakeref_t wakeref;

	with_intel_runtime_pm(&i915->runtime_pm, wakeref)
		vma->vm->clear_range(vma->vm, vma->node.start, vma->size);
}

static int aliasing_gtt_bind_vma(struct i915_vma *vma,
				 enum i915_cache_level cache_level,
				 u32 flags)
{
	struct drm_i915_private *i915 = vma->vm->i915;
	u32 pte_flags;
	int ret;

	/* Currently applicable only to VLV */
	pte_flags = 0;
	if (i915_gem_object_is_readonly(vma->obj))
		pte_flags |= PTE_READ_ONLY;

	if (flags & I915_VMA_LOCAL_BIND) {
		struct i915_ppgtt *alias = i915_vm_to_ggtt(vma->vm)->alias;

		if (flags & I915_VMA_ALLOC) {
			ret = alias->vm.allocate_va_range(&alias->vm,
							  vma->node.start,
							  vma->size);
			if (ret)
				return ret;

			set_bit(I915_VMA_ALLOC_BIT, __i915_vma_flags(vma));
		}

		GEM_BUG_ON(!test_bit(I915_VMA_ALLOC_BIT,
				     __i915_vma_flags(vma)));
		alias->vm.insert_entries(&alias->vm, vma,
					 cache_level, pte_flags);
	}

	if (flags & I915_VMA_GLOBAL_BIND) {
		intel_wakeref_t wakeref;

		with_intel_runtime_pm(&i915->runtime_pm, wakeref) {
			vma->vm->insert_entries(vma->vm, vma,
						cache_level, pte_flags);
		}
	}

	return 0;
}

static void aliasing_gtt_unbind_vma(struct i915_vma *vma)
{
	struct drm_i915_private *i915 = vma->vm->i915;

	if (i915_vma_is_bound(vma, I915_VMA_GLOBAL_BIND)) {
		struct i915_address_space *vm = vma->vm;
		intel_wakeref_t wakeref;

		with_intel_runtime_pm(&i915->runtime_pm, wakeref)
			vm->clear_range(vm, vma->node.start, vma->size);
	}

	if (test_and_clear_bit(I915_VMA_ALLOC_BIT, __i915_vma_flags(vma))) {
		struct i915_address_space *vm =
			&i915_vm_to_ggtt(vma->vm)->alias->vm;

		vm->clear_range(vm, vma->node.start, vma->size);
	}
}

void i915_gem_gtt_finish_pages(struct drm_i915_gem_object *obj,
			       struct sg_table *pages)
{
	struct drm_i915_private *dev_priv = to_i915(obj->base.dev);
	struct device *kdev = &dev_priv->drm.pdev->dev;
	struct i915_ggtt *ggtt = &dev_priv->ggtt;

	if (unlikely(ggtt->do_idle_maps)) {
		/* XXX This does not prevent more requests being submitted! */
		if (intel_gt_retire_requests_timeout(ggtt->vm.gt,
						     -MAX_SCHEDULE_TIMEOUT)) {
			DRM_ERROR("Failed to wait for idle; VT'd may hang.\n");
			/* Wait a bit, in hopes it avoids the hang */
			udelay(10);
		}
	}

	dma_unmap_sg(kdev, pages->sgl, pages->nents, PCI_DMA_BIDIRECTIONAL);
}

static int ggtt_set_pages(struct i915_vma *vma)
{
	int ret;

	GEM_BUG_ON(vma->pages);

	ret = i915_get_ggtt_vma_pages(vma);
	if (ret)
		return ret;

	vma->page_sizes = vma->obj->mm.page_sizes;

	return 0;
}

static void i915_ggtt_color_adjust(const struct drm_mm_node *node,
				   unsigned long color,
				   u64 *start,
				   u64 *end)
{
	if (i915_node_color_differs(node, color))
		*start += I915_GTT_PAGE_SIZE;

	/* Also leave a space between the unallocated reserved node after the
	 * GTT and any objects within the GTT, i.e. we use the color adjustment
	 * to insert a guard page to prevent prefetches crossing over the
	 * GTT boundary.
	 */
	node = list_next_entry(node, node_list);
	if (node->color != color)
		*end -= I915_GTT_PAGE_SIZE;
}

static int init_aliasing_ppgtt(struct i915_ggtt *ggtt)
{
	struct i915_ppgtt *ppgtt;
	int err;

	ppgtt = i915_ppgtt_create(ggtt->vm.i915);
	if (IS_ERR(ppgtt))
		return PTR_ERR(ppgtt);

	if (GEM_WARN_ON(ppgtt->vm.total < ggtt->vm.total)) {
		err = -ENODEV;
		goto err_ppgtt;
	}

	/*
	 * Note we only pre-allocate as far as the end of the global
	 * GTT. On 48b / 4-level page-tables, the difference is very,
	 * very significant! We have to preallocate as GVT/vgpu does
	 * not like the page directory disappearing.
	 */
	err = ppgtt->vm.allocate_va_range(&ppgtt->vm, 0, ggtt->vm.total);
	if (err)
		goto err_ppgtt;

	ggtt->alias = ppgtt;
	ggtt->vm.bind_async_flags |= ppgtt->vm.bind_async_flags;

	GEM_BUG_ON(ggtt->vm.vma_ops.bind_vma != ggtt_bind_vma);
	ggtt->vm.vma_ops.bind_vma = aliasing_gtt_bind_vma;

	GEM_BUG_ON(ggtt->vm.vma_ops.unbind_vma != ggtt_unbind_vma);
	ggtt->vm.vma_ops.unbind_vma = aliasing_gtt_unbind_vma;

	return 0;

err_ppgtt:
	i915_vm_put(&ppgtt->vm);
	return err;
}

static void fini_aliasing_ppgtt(struct i915_ggtt *ggtt)
{
	struct i915_ppgtt *ppgtt;

	ppgtt = fetch_and_zero(&ggtt->alias);
	if (!ppgtt)
		return;

	i915_vm_put(&ppgtt->vm);

	ggtt->vm.vma_ops.bind_vma   = ggtt_bind_vma;
	ggtt->vm.vma_ops.unbind_vma = ggtt_unbind_vma;
}

static int ggtt_reserve_guc_top(struct i915_ggtt *ggtt)
{
	u64 size;
	int ret;

	if (!USES_GUC(ggtt->vm.i915))
		return 0;

	GEM_BUG_ON(ggtt->vm.total <= GUC_GGTT_TOP);
	size = ggtt->vm.total - GUC_GGTT_TOP;

	ret = i915_gem_gtt_reserve(&ggtt->vm, &ggtt->uc_fw, size,
				   GUC_GGTT_TOP, I915_COLOR_UNEVICTABLE,
				   PIN_NOEVICT);
	if (ret)
		DRM_DEBUG_DRIVER("Failed to reserve top of GGTT for GuC\n");

	return ret;
}

static void ggtt_release_guc_top(struct i915_ggtt *ggtt)
{
	if (drm_mm_node_allocated(&ggtt->uc_fw))
		drm_mm_remove_node(&ggtt->uc_fw);
}

static void cleanup_init_ggtt(struct i915_ggtt *ggtt)
{
	ggtt_release_guc_top(ggtt);
	if (drm_mm_node_allocated(&ggtt->error_capture))
		drm_mm_remove_node(&ggtt->error_capture);
}

static int init_ggtt(struct i915_ggtt *ggtt)
{
	/* Let GEM Manage all of the aperture.
	 *
	 * However, leave one page at the end still bound to the scratch page.
	 * There are a number of places where the hardware apparently prefetches
	 * past the end of the object, and we've seen multiple hangs with the
	 * GPU head pointer stuck in a batchbuffer bound at the last page of the
	 * aperture.  One page should be enough to keep any prefetching inside
	 * of the aperture.
	 */
	unsigned long hole_start, hole_end;
	struct drm_mm_node *entry;
	int ret;

	/*
	 * GuC requires all resources that we're sharing with it to be placed in
	 * non-WOPCM memory. If GuC is not present or not in use we still need a
	 * small bias as ring wraparound at offset 0 sometimes hangs. No idea
	 * why.
	 */
	ggtt->pin_bias = max_t(u32, I915_GTT_PAGE_SIZE,
			       intel_wopcm_guc_size(&ggtt->vm.i915->wopcm));

	ret = intel_vgt_balloon(ggtt);
	if (ret)
		return ret;

	if (ggtt->mappable_end) {
		/* Reserve a mappable slot for our lockless error capture */
		ret = drm_mm_insert_node_in_range(&ggtt->vm.mm, &ggtt->error_capture,
						  PAGE_SIZE, 0, I915_COLOR_UNEVICTABLE,
						  0, ggtt->mappable_end,
						  DRM_MM_INSERT_LOW);
		if (ret)
			return ret;
	}

	/*
	 * The upper portion of the GuC address space has a sizeable hole
	 * (several MB) that is inaccessible by GuC. Reserve this range within
	 * GGTT as it can comfortably hold GuC/HuC firmware images.
	 */
	ret = ggtt_reserve_guc_top(ggtt);
	if (ret)
		goto err;

	/* Clear any non-preallocated blocks */
	drm_mm_for_each_hole(entry, &ggtt->vm.mm, hole_start, hole_end) {
		DRM_DEBUG_KMS("clearing unused GTT space: [%lx, %lx]\n",
			      hole_start, hole_end);
		ggtt->vm.clear_range(&ggtt->vm, hole_start,
				     hole_end - hole_start);
	}

	/* And finally clear the reserved guard page */
	ggtt->vm.clear_range(&ggtt->vm, ggtt->vm.total - PAGE_SIZE, PAGE_SIZE);

	return 0;

err:
	cleanup_init_ggtt(ggtt);
	return ret;
}

int i915_init_ggtt(struct drm_i915_private *i915)
{
	int ret;

	ret = init_ggtt(&i915->ggtt);
	if (ret)
		return ret;

	if (INTEL_PPGTT(i915) == INTEL_PPGTT_ALIASING) {
		ret = init_aliasing_ppgtt(&i915->ggtt);
		if (ret)
			cleanup_init_ggtt(&i915->ggtt);
	}

	return 0;
}

static void ggtt_cleanup_hw(struct i915_ggtt *ggtt)
{
	struct i915_vma *vma, *vn;

	atomic_set(&ggtt->vm.open, 0);

	rcu_barrier(); /* flush the RCU'ed__i915_vm_release */
	flush_workqueue(ggtt->vm.i915->wq);

	mutex_lock(&ggtt->vm.mutex);

	list_for_each_entry_safe(vma, vn, &ggtt->vm.bound_list, vm_link)
		WARN_ON(__i915_vma_unbind(vma));

	if (drm_mm_node_allocated(&ggtt->error_capture))
		drm_mm_remove_node(&ggtt->error_capture);

	ggtt_release_guc_top(ggtt);
	intel_vgt_deballoon(ggtt);

	ggtt->vm.cleanup(&ggtt->vm);

	mutex_unlock(&ggtt->vm.mutex);
	i915_address_space_fini(&ggtt->vm);

	arch_phys_wc_del(ggtt->mtrr);

	if (ggtt->iomap.size)
		io_mapping_fini(&ggtt->iomap);
}

/**
 * i915_ggtt_driver_release - Clean up GGTT hardware initialization
 * @i915: i915 device
 */
void i915_ggtt_driver_release(struct drm_i915_private *i915)
{
	struct pagevec *pvec;

	fini_aliasing_ppgtt(&i915->ggtt);

	ggtt_cleanup_hw(&i915->ggtt);

	pvec = &i915->mm.wc_stash.pvec;
	if (pvec->nr) {
		set_pages_array_wb(pvec->pages, pvec->nr);
		__pagevec_release(pvec);
	}
}

static unsigned int gen6_get_total_gtt_size(u16 snb_gmch_ctl)
{
	snb_gmch_ctl >>= SNB_GMCH_GGMS_SHIFT;
	snb_gmch_ctl &= SNB_GMCH_GGMS_MASK;
	return snb_gmch_ctl << 20;
}

static unsigned int gen8_get_total_gtt_size(u16 bdw_gmch_ctl)
{
	bdw_gmch_ctl >>= BDW_GMCH_GGMS_SHIFT;
	bdw_gmch_ctl &= BDW_GMCH_GGMS_MASK;
	if (bdw_gmch_ctl)
		bdw_gmch_ctl = 1 << bdw_gmch_ctl;

#ifdef CONFIG_X86_32
	/* Limit 32b platforms to a 2GB GGTT: 4 << 20 / pte size * I915_GTT_PAGE_SIZE */
	if (bdw_gmch_ctl > 4)
		bdw_gmch_ctl = 4;
#endif

	return bdw_gmch_ctl << 20;
}

static unsigned int chv_get_total_gtt_size(u16 gmch_ctrl)
{
	gmch_ctrl >>= SNB_GMCH_GGMS_SHIFT;
	gmch_ctrl &= SNB_GMCH_GGMS_MASK;

	if (gmch_ctrl)
		return 1 << (20 + gmch_ctrl);

	return 0;
}

static int ggtt_probe_common(struct i915_ggtt *ggtt, u64 size)
{
	struct drm_i915_private *dev_priv = ggtt->vm.i915;
	struct pci_dev *pdev = dev_priv->drm.pdev;
	phys_addr_t phys_addr;
	int ret;

	/* For Modern GENs the PTEs and register space are split in the BAR */
	phys_addr = pci_resource_start(pdev, 0) + pci_resource_len(pdev, 0) / 2;

	/*
	 * On BXT+/CNL+ writes larger than 64 bit to the GTT pagetable range
	 * will be dropped. For WC mappings in general we have 64 byte burst
	 * writes when the WC buffer is flushed, so we can't use it, but have to
	 * resort to an uncached mapping. The WC issue is easily caught by the
	 * readback check when writing GTT PTE entries.
	 */
	if (IS_GEN9_LP(dev_priv) || INTEL_GEN(dev_priv) >= 10)
		ggtt->gsm = ioremap(phys_addr, size);
	else
		ggtt->gsm = ioremap_wc(phys_addr, size);
	if (!ggtt->gsm) {
		DRM_ERROR("Failed to map the ggtt page table\n");
		return -ENOMEM;
	}

	ret = setup_scratch_page(&ggtt->vm, GFP_DMA32);
	if (ret) {
		DRM_ERROR("Scratch setup failed\n");
		/* iounmap will also get called at remove, but meh */
		iounmap(ggtt->gsm);
		return ret;
	}

	ggtt->vm.scratch[0].encode =
		ggtt->vm.pte_encode(px_dma(&ggtt->vm.scratch[0]),
				    I915_CACHE_NONE, 0);

	return 0;
}

static void tgl_setup_private_ppat(struct intel_uncore *uncore)
{
	/* TGL doesn't support LLC or AGE settings */
	intel_uncore_write(uncore, GEN12_PAT_INDEX(0), GEN8_PPAT_WB);
	intel_uncore_write(uncore, GEN12_PAT_INDEX(1), GEN8_PPAT_WC);
	intel_uncore_write(uncore, GEN12_PAT_INDEX(2), GEN8_PPAT_WT);
	intel_uncore_write(uncore, GEN12_PAT_INDEX(3), GEN8_PPAT_UC);
	intel_uncore_write(uncore, GEN12_PAT_INDEX(4), GEN8_PPAT_WB);
	intel_uncore_write(uncore, GEN12_PAT_INDEX(5), GEN8_PPAT_WB);
	intel_uncore_write(uncore, GEN12_PAT_INDEX(6), GEN8_PPAT_WB);
	intel_uncore_write(uncore, GEN12_PAT_INDEX(7), GEN8_PPAT_WB);
}

static void cnl_setup_private_ppat(struct intel_uncore *uncore)
{
	intel_uncore_write(uncore,
			   GEN10_PAT_INDEX(0),
			   GEN8_PPAT_WB | GEN8_PPAT_LLC);
	intel_uncore_write(uncore,
			   GEN10_PAT_INDEX(1),
			   GEN8_PPAT_WC | GEN8_PPAT_LLCELLC);
	intel_uncore_write(uncore,
			   GEN10_PAT_INDEX(2),
			   GEN8_PPAT_WT | GEN8_PPAT_LLCELLC);
	intel_uncore_write(uncore,
			   GEN10_PAT_INDEX(3),
			   GEN8_PPAT_UC);
	intel_uncore_write(uncore,
			   GEN10_PAT_INDEX(4),
			   GEN8_PPAT_WB | GEN8_PPAT_LLCELLC | GEN8_PPAT_AGE(0));
	intel_uncore_write(uncore,
			   GEN10_PAT_INDEX(5),
			   GEN8_PPAT_WB | GEN8_PPAT_LLCELLC | GEN8_PPAT_AGE(1));
	intel_uncore_write(uncore,
			   GEN10_PAT_INDEX(6),
			   GEN8_PPAT_WB | GEN8_PPAT_LLCELLC | GEN8_PPAT_AGE(2));
	intel_uncore_write(uncore,
			   GEN10_PAT_INDEX(7),
			   GEN8_PPAT_WB | GEN8_PPAT_LLCELLC | GEN8_PPAT_AGE(3));
}

/* The GGTT and PPGTT need a private PPAT setup in order to handle cacheability
 * bits. When using advanced contexts each context stores its own PAT, but
 * writing this data shouldn't be harmful even in those cases. */
static void bdw_setup_private_ppat(struct intel_uncore *uncore)
{
	u64 pat;

	pat = GEN8_PPAT(0, GEN8_PPAT_WB | GEN8_PPAT_LLC) |	/* for normal objects, no eLLC */
	      GEN8_PPAT(1, GEN8_PPAT_WC | GEN8_PPAT_LLCELLC) |	/* for something pointing to ptes? */
	      GEN8_PPAT(2, GEN8_PPAT_WT | GEN8_PPAT_LLCELLC) |	/* for scanout with eLLC */
	      GEN8_PPAT(3, GEN8_PPAT_UC) |			/* Uncached objects, mostly for scanout */
	      GEN8_PPAT(4, GEN8_PPAT_WB | GEN8_PPAT_LLCELLC | GEN8_PPAT_AGE(0)) |
	      GEN8_PPAT(5, GEN8_PPAT_WB | GEN8_PPAT_LLCELLC | GEN8_PPAT_AGE(1)) |
	      GEN8_PPAT(6, GEN8_PPAT_WB | GEN8_PPAT_LLCELLC | GEN8_PPAT_AGE(2)) |
	      GEN8_PPAT(7, GEN8_PPAT_WB | GEN8_PPAT_LLCELLC | GEN8_PPAT_AGE(3));

	intel_uncore_write(uncore, GEN8_PRIVATE_PAT_LO, lower_32_bits(pat));
	intel_uncore_write(uncore, GEN8_PRIVATE_PAT_HI, upper_32_bits(pat));
}

static void chv_setup_private_ppat(struct intel_uncore *uncore)
{
	u64 pat;

	/*
	 * Map WB on BDW to snooped on CHV.
	 *
	 * Only the snoop bit has meaning for CHV, the rest is
	 * ignored.
	 *
	 * The hardware will never snoop for certain types of accesses:
	 * - CPU GTT (GMADR->GGTT->no snoop->memory)
	 * - PPGTT page tables
	 * - some other special cycles
	 *
	 * As with BDW, we also need to consider the following for GT accesses:
	 * "For GGTT, there is NO pat_sel[2:0] from the entry,
	 * so RTL will always use the value corresponding to
	 * pat_sel = 000".
	 * Which means we must set the snoop bit in PAT entry 0
	 * in order to keep the global status page working.
	 */

	pat = GEN8_PPAT(0, CHV_PPAT_SNOOP) |
	      GEN8_PPAT(1, 0) |
	      GEN8_PPAT(2, 0) |
	      GEN8_PPAT(3, 0) |
	      GEN8_PPAT(4, CHV_PPAT_SNOOP) |
	      GEN8_PPAT(5, CHV_PPAT_SNOOP) |
	      GEN8_PPAT(6, CHV_PPAT_SNOOP) |
	      GEN8_PPAT(7, CHV_PPAT_SNOOP);

	intel_uncore_write(uncore, GEN8_PRIVATE_PAT_LO, lower_32_bits(pat));
	intel_uncore_write(uncore, GEN8_PRIVATE_PAT_HI, upper_32_bits(pat));
}

static void gen6_gmch_remove(struct i915_address_space *vm)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);

	iounmap(ggtt->gsm);
	cleanup_scratch_page(vm);
}

static void setup_private_pat(struct intel_uncore *uncore)
{
	struct drm_i915_private *i915 = uncore->i915;

	GEM_BUG_ON(INTEL_GEN(i915) < 8);

	if (INTEL_GEN(i915) >= 12)
		tgl_setup_private_ppat(uncore);
	else if (INTEL_GEN(i915) >= 10)
		cnl_setup_private_ppat(uncore);
	else if (IS_CHERRYVIEW(i915) || IS_GEN9_LP(i915))
		chv_setup_private_ppat(uncore);
	else
		bdw_setup_private_ppat(uncore);
}

static struct resource pci_resource(struct pci_dev *pdev, int bar)
{
	return (struct resource)DEFINE_RES_MEM(pci_resource_start(pdev, bar),
					       pci_resource_len(pdev, bar));
}

static int gen8_gmch_probe(struct i915_ggtt *ggtt)
{
	struct drm_i915_private *dev_priv = ggtt->vm.i915;
	struct pci_dev *pdev = dev_priv->drm.pdev;
	unsigned int size;
	u16 snb_gmch_ctl;
	int err;

	/* TODO: We're not aware of mappable constraints on gen8 yet */
	if (!IS_DGFX(dev_priv)) {
		ggtt->gmadr = pci_resource(pdev, 2);
		ggtt->mappable_end = resource_size(&ggtt->gmadr);
	}

	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(39));
	if (!err)
		err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(39));
	if (err)
		DRM_ERROR("Can't set DMA mask/consistent mask (%d)\n", err);

	pci_read_config_word(pdev, SNB_GMCH_CTRL, &snb_gmch_ctl);
	if (IS_CHERRYVIEW(dev_priv))
		size = chv_get_total_gtt_size(snb_gmch_ctl);
	else
		size = gen8_get_total_gtt_size(snb_gmch_ctl);

	ggtt->vm.total = (size / sizeof(gen8_pte_t)) * I915_GTT_PAGE_SIZE;
	ggtt->vm.cleanup = gen6_gmch_remove;
	ggtt->vm.insert_page = gen8_ggtt_insert_page;
	ggtt->vm.clear_range = nop_clear_range;
	if (intel_scanout_needs_vtd_wa(dev_priv))
		ggtt->vm.clear_range = gen8_ggtt_clear_range;

	ggtt->vm.insert_entries = gen8_ggtt_insert_entries;

	/* Serialize GTT updates with aperture access on BXT if VT-d is on. */
	if (intel_ggtt_update_needs_vtd_wa(dev_priv) ||
	    IS_CHERRYVIEW(dev_priv) /* fails with concurrent use/update */) {
		ggtt->vm.insert_entries = bxt_vtd_ggtt_insert_entries__BKL;
		ggtt->vm.insert_page    = bxt_vtd_ggtt_insert_page__BKL;
		if (ggtt->vm.clear_range != nop_clear_range)
			ggtt->vm.clear_range = bxt_vtd_ggtt_clear_range__BKL;
	}

	ggtt->invalidate = gen6_ggtt_invalidate;

	ggtt->vm.vma_ops.bind_vma    = ggtt_bind_vma;
	ggtt->vm.vma_ops.unbind_vma  = ggtt_unbind_vma;
	ggtt->vm.vma_ops.set_pages   = ggtt_set_pages;
	ggtt->vm.vma_ops.clear_pages = clear_pages;

	ggtt->vm.pte_encode = gen8_pte_encode;

	setup_private_pat(ggtt->vm.gt->uncore);

	return ggtt_probe_common(ggtt, size);
}

static int gen6_gmch_probe(struct i915_ggtt *ggtt)
{
	struct drm_i915_private *dev_priv = ggtt->vm.i915;
	struct pci_dev *pdev = dev_priv->drm.pdev;
	unsigned int size;
	u16 snb_gmch_ctl;
	int err;

	ggtt->gmadr =
		(struct resource) DEFINE_RES_MEM(pci_resource_start(pdev, 2),
						 pci_resource_len(pdev, 2));
	ggtt->mappable_end = resource_size(&ggtt->gmadr);

	/* 64/512MB is the current min/max we actually know of, but this is just
	 * a coarse sanity check.
	 */
	if (ggtt->mappable_end < (64<<20) || ggtt->mappable_end > (512<<20)) {
		DRM_ERROR("Unknown GMADR size (%pa)\n", &ggtt->mappable_end);
		return -ENXIO;
	}

	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(40));
	if (!err)
		err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(40));
	if (err)
		DRM_ERROR("Can't set DMA mask/consistent mask (%d)\n", err);
	pci_read_config_word(pdev, SNB_GMCH_CTRL, &snb_gmch_ctl);

	size = gen6_get_total_gtt_size(snb_gmch_ctl);
	ggtt->vm.total = (size / sizeof(gen6_pte_t)) * I915_GTT_PAGE_SIZE;

	ggtt->vm.clear_range = nop_clear_range;
	if (!HAS_FULL_PPGTT(dev_priv) || intel_scanout_needs_vtd_wa(dev_priv))
		ggtt->vm.clear_range = gen6_ggtt_clear_range;
	ggtt->vm.insert_page = gen6_ggtt_insert_page;
	ggtt->vm.insert_entries = gen6_ggtt_insert_entries;
	ggtt->vm.cleanup = gen6_gmch_remove;

	ggtt->invalidate = gen6_ggtt_invalidate;

	if (HAS_EDRAM(dev_priv))
		ggtt->vm.pte_encode = iris_pte_encode;
	else if (IS_HASWELL(dev_priv))
		ggtt->vm.pte_encode = hsw_pte_encode;
	else if (IS_VALLEYVIEW(dev_priv))
		ggtt->vm.pte_encode = byt_pte_encode;
	else if (INTEL_GEN(dev_priv) >= 7)
		ggtt->vm.pte_encode = ivb_pte_encode;
	else
		ggtt->vm.pte_encode = snb_pte_encode;

	ggtt->vm.vma_ops.bind_vma    = ggtt_bind_vma;
	ggtt->vm.vma_ops.unbind_vma  = ggtt_unbind_vma;
	ggtt->vm.vma_ops.set_pages   = ggtt_set_pages;
	ggtt->vm.vma_ops.clear_pages = clear_pages;

	return ggtt_probe_common(ggtt, size);
}

static void i915_gmch_remove(struct i915_address_space *vm)
{
	intel_gmch_remove();
}

static int i915_gmch_probe(struct i915_ggtt *ggtt)
{
	struct drm_i915_private *dev_priv = ggtt->vm.i915;
	phys_addr_t gmadr_base;
	int ret;

	ret = intel_gmch_probe(dev_priv->bridge_dev, dev_priv->drm.pdev, NULL);
	if (!ret) {
		DRM_ERROR("failed to set up gmch\n");
		return -EIO;
	}

	intel_gtt_get(&ggtt->vm.total, &gmadr_base, &ggtt->mappable_end);

	ggtt->gmadr =
		(struct resource) DEFINE_RES_MEM(gmadr_base,
						 ggtt->mappable_end);

	ggtt->do_idle_maps = needs_idle_maps(dev_priv);
	ggtt->vm.insert_page = i915_ggtt_insert_page;
	ggtt->vm.insert_entries = i915_ggtt_insert_entries;
	ggtt->vm.clear_range = i915_ggtt_clear_range;
	ggtt->vm.cleanup = i915_gmch_remove;

	ggtt->invalidate = gmch_ggtt_invalidate;

	ggtt->vm.vma_ops.bind_vma    = ggtt_bind_vma;
	ggtt->vm.vma_ops.unbind_vma  = ggtt_unbind_vma;
	ggtt->vm.vma_ops.set_pages   = ggtt_set_pages;
	ggtt->vm.vma_ops.clear_pages = clear_pages;

	if (unlikely(ggtt->do_idle_maps))
		dev_notice(dev_priv->drm.dev,
			   "Applying Ironlake quirks for intel_iommu\n");

	return 0;
}

static int ggtt_probe_hw(struct i915_ggtt *ggtt, struct intel_gt *gt)
{
	struct drm_i915_private *i915 = gt->i915;
	int ret;

	ggtt->vm.gt = gt;
	ggtt->vm.i915 = i915;
	ggtt->vm.dma = &i915->drm.pdev->dev;

	if (INTEL_GEN(i915) <= 5)
		ret = i915_gmch_probe(ggtt);
	else if (INTEL_GEN(i915) < 8)
		ret = gen6_gmch_probe(ggtt);
	else
		ret = gen8_gmch_probe(ggtt);
	if (ret)
		return ret;

	if ((ggtt->vm.total - 1) >> 32) {
		DRM_ERROR("We never expected a Global GTT with more than 32bits"
			  " of address space! Found %lldM!\n",
			  ggtt->vm.total >> 20);
		ggtt->vm.total = 1ULL << 32;
		ggtt->mappable_end =
			min_t(u64, ggtt->mappable_end, ggtt->vm.total);
	}

	if (ggtt->mappable_end > ggtt->vm.total) {
		DRM_ERROR("mappable aperture extends past end of GGTT,"
			  " aperture=%pa, total=%llx\n",
			  &ggtt->mappable_end, ggtt->vm.total);
		ggtt->mappable_end = ggtt->vm.total;
	}

	/* GMADR is the PCI mmio aperture into the global GTT. */
	DRM_DEBUG_DRIVER("GGTT size = %lluM\n", ggtt->vm.total >> 20);
	DRM_DEBUG_DRIVER("GMADR size = %lluM\n", (u64)ggtt->mappable_end >> 20);
	DRM_DEBUG_DRIVER("DSM size = %lluM\n",
			 (u64)resource_size(&intel_graphics_stolen_res) >> 20);

	return 0;
}

/**
 * i915_ggtt_probe_hw - Probe GGTT hardware location
 * @i915: i915 device
 */
int i915_ggtt_probe_hw(struct drm_i915_private *i915)
{
	int ret;

	ret = ggtt_probe_hw(&i915->ggtt, &i915->gt);
	if (ret)
		return ret;

	if (intel_vtd_active())
		dev_info(i915->drm.dev, "VT-d active for gfx access\n");

	return 0;
}

static int ggtt_init_hw(struct i915_ggtt *ggtt)
{
	struct drm_i915_private *i915 = ggtt->vm.i915;

	i915_address_space_init(&ggtt->vm, VM_CLASS_GGTT);

	ggtt->vm.is_ggtt = true;

	/* Only VLV supports read-only GGTT mappings */
	ggtt->vm.has_read_only = IS_VALLEYVIEW(i915);

	if (!HAS_LLC(i915) && !HAS_PPGTT(i915))
		ggtt->vm.mm.color_adjust = i915_ggtt_color_adjust;

	if (ggtt->mappable_end) {
		if (!io_mapping_init_wc(&ggtt->iomap,
					ggtt->gmadr.start,
					ggtt->mappable_end)) {
			ggtt->vm.cleanup(&ggtt->vm);
			return -EIO;
		}

		ggtt->mtrr = arch_phys_wc_add(ggtt->gmadr.start,
					      ggtt->mappable_end);
	}

	i915_ggtt_init_fences(ggtt);

	return 0;
}

/**
 * i915_ggtt_init_hw - Initialize GGTT hardware
 * @dev_priv: i915 device
 */
int i915_ggtt_init_hw(struct drm_i915_private *dev_priv)
{
	int ret;

	stash_init(&dev_priv->mm.wc_stash);

	/* Note that we use page colouring to enforce a guard page at the
	 * end of the address space. This is required as the CS may prefetch
	 * beyond the end of the batch buffer, across the page boundary,
	 * and beyond the end of the GTT if we do not provide a guard.
	 */
	ret = ggtt_init_hw(&dev_priv->ggtt);
	if (ret)
		return ret;

	return 0;
}

int i915_ggtt_enable_hw(struct drm_i915_private *dev_priv)
{
	if (INTEL_GEN(dev_priv) < 6 && !intel_enable_gtt())
		return -EIO;

	return 0;
}

void i915_ggtt_enable_guc(struct i915_ggtt *ggtt)
{
	GEM_BUG_ON(ggtt->invalidate != gen6_ggtt_invalidate);

	ggtt->invalidate = guc_ggtt_invalidate;

	ggtt->invalidate(ggtt);
}

void i915_ggtt_disable_guc(struct i915_ggtt *ggtt)
{
	/* XXX Temporary pardon for error unload */
	if (ggtt->invalidate == gen6_ggtt_invalidate)
		return;

	/* We should only be called after i915_ggtt_enable_guc() */
	GEM_BUG_ON(ggtt->invalidate != guc_ggtt_invalidate);

	ggtt->invalidate = gen6_ggtt_invalidate;

	ggtt->invalidate(ggtt);
}

static void ggtt_restore_mappings(struct i915_ggtt *ggtt)
{
	struct i915_vma *vma;
	bool flush = false;
	int open;

	intel_gt_check_and_clear_faults(ggtt->vm.gt);

	mutex_lock(&ggtt->vm.mutex);

	/* First fill our portion of the GTT with scratch pages */
	ggtt->vm.clear_range(&ggtt->vm, 0, ggtt->vm.total);

	/* Skip rewriting PTE on VMA unbind. */
	open = atomic_xchg(&ggtt->vm.open, 0);

	/* clflush objects bound into the GGTT and rebind them. */
	list_for_each_entry(vma, &ggtt->vm.bound_list, vm_link) {
		struct drm_i915_gem_object *obj = vma->obj;

		if (!i915_vma_is_bound(vma, I915_VMA_GLOBAL_BIND))
			continue;

		clear_bit(I915_VMA_GLOBAL_BIND_BIT, __i915_vma_flags(vma));
		WARN_ON(i915_vma_bind(vma,
				      obj ? obj->cache_level : 0,
				      PIN_GLOBAL, NULL));
		if (obj) { /* only used during resume => exclusive access */
			flush |= fetch_and_zero(&obj->write_domain);
			obj->read_domains |= I915_GEM_DOMAIN_GTT;
		}
	}

	atomic_set(&ggtt->vm.open, open);
	ggtt->invalidate(ggtt);

	mutex_unlock(&ggtt->vm.mutex);

	if (flush)
		wbinvd_on_all_cpus();
}

void i915_gem_restore_gtt_mappings(struct drm_i915_private *i915)
{
	struct i915_ggtt *ggtt = &i915->ggtt;

	ggtt_restore_mappings(ggtt);

	if (INTEL_GEN(i915) >= 8)
		setup_private_pat(ggtt->vm.gt->uncore);
}

static struct scatterlist *
rotate_pages(struct drm_i915_gem_object *obj, unsigned int offset,
	     unsigned int width, unsigned int height,
	     unsigned int stride,
	     struct sg_table *st, struct scatterlist *sg)
{
	unsigned int column, row;
	unsigned int src_idx;

	for (column = 0; column < width; column++) {
		src_idx = stride * (height - 1) + column + offset;
		for (row = 0; row < height; row++) {
			st->nents++;
			/* We don't need the pages, but need to initialize
			 * the entries so the sg list can be happily traversed.
			 * The only thing we need are DMA addresses.
			 */
			sg_set_page(sg, NULL, I915_GTT_PAGE_SIZE, 0);
			sg_dma_address(sg) =
				i915_gem_object_get_dma_address(obj, src_idx);
			sg_dma_len(sg) = I915_GTT_PAGE_SIZE;
			sg = sg_next(sg);
			src_idx -= stride;
		}
	}

	return sg;
}

static noinline struct sg_table *
intel_rotate_pages(struct intel_rotation_info *rot_info,
		   struct drm_i915_gem_object *obj)
{
	unsigned int size = intel_rotation_info_size(rot_info);
	struct sg_table *st;
	struct scatterlist *sg;
	int ret = -ENOMEM;
	int i;

	/* Allocate target SG list. */
	st = kmalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		goto err_st_alloc;

	ret = sg_alloc_table(st, size, GFP_KERNEL);
	if (ret)
		goto err_sg_alloc;

	st->nents = 0;
	sg = st->sgl;

	for (i = 0 ; i < ARRAY_SIZE(rot_info->plane); i++) {
		sg = rotate_pages(obj, rot_info->plane[i].offset,
				  rot_info->plane[i].width, rot_info->plane[i].height,
				  rot_info->plane[i].stride, st, sg);
	}

	return st;

err_sg_alloc:
	kfree(st);
err_st_alloc:

	DRM_DEBUG_DRIVER("Failed to create rotated mapping for object size %zu! (%ux%u tiles, %u pages)\n",
			 obj->base.size, rot_info->plane[0].width, rot_info->plane[0].height, size);

	return ERR_PTR(ret);
}

static struct scatterlist *
remap_pages(struct drm_i915_gem_object *obj, unsigned int offset,
	    unsigned int width, unsigned int height,
	    unsigned int stride,
	    struct sg_table *st, struct scatterlist *sg)
{
	unsigned int row;

	for (row = 0; row < height; row++) {
		unsigned int left = width * I915_GTT_PAGE_SIZE;

		while (left) {
			dma_addr_t addr;
			unsigned int length;

			/* We don't need the pages, but need to initialize
			 * the entries so the sg list can be happily traversed.
			 * The only thing we need are DMA addresses.
			 */

			addr = i915_gem_object_get_dma_address_len(obj, offset, &length);

			length = min(left, length);

			st->nents++;

			sg_set_page(sg, NULL, length, 0);
			sg_dma_address(sg) = addr;
			sg_dma_len(sg) = length;
			sg = sg_next(sg);

			offset += length / I915_GTT_PAGE_SIZE;
			left -= length;
		}

		offset += stride - width;
	}

	return sg;
}

static noinline struct sg_table *
intel_remap_pages(struct intel_remapped_info *rem_info,
		  struct drm_i915_gem_object *obj)
{
	unsigned int size = intel_remapped_info_size(rem_info);
	struct sg_table *st;
	struct scatterlist *sg;
	int ret = -ENOMEM;
	int i;

	/* Allocate target SG list. */
	st = kmalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		goto err_st_alloc;

	ret = sg_alloc_table(st, size, GFP_KERNEL);
	if (ret)
		goto err_sg_alloc;

	st->nents = 0;
	sg = st->sgl;

	for (i = 0 ; i < ARRAY_SIZE(rem_info->plane); i++) {
		sg = remap_pages(obj, rem_info->plane[i].offset,
				 rem_info->plane[i].width, rem_info->plane[i].height,
				 rem_info->plane[i].stride, st, sg);
	}

	i915_sg_trim(st);

	return st;

err_sg_alloc:
	kfree(st);
err_st_alloc:

	DRM_DEBUG_DRIVER("Failed to create remapped mapping for object size %zu! (%ux%u tiles, %u pages)\n",
			 obj->base.size, rem_info->plane[0].width, rem_info->plane[0].height, size);

	return ERR_PTR(ret);
}

static noinline struct sg_table *
intel_partial_pages(const struct i915_ggtt_view *view,
		    struct drm_i915_gem_object *obj)
{
	struct sg_table *st;
	struct scatterlist *sg, *iter;
	unsigned int count = view->partial.size;
	unsigned int offset;
	int ret = -ENOMEM;

	st = kmalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		goto err_st_alloc;

	ret = sg_alloc_table(st, count, GFP_KERNEL);
	if (ret)
		goto err_sg_alloc;

	iter = i915_gem_object_get_sg(obj, view->partial.offset, &offset);
	GEM_BUG_ON(!iter);

	sg = st->sgl;
	st->nents = 0;
	do {
		unsigned int len;

		len = min(iter->length - (offset << PAGE_SHIFT),
			  count << PAGE_SHIFT);
		sg_set_page(sg, NULL, len, 0);
		sg_dma_address(sg) =
			sg_dma_address(iter) + (offset << PAGE_SHIFT);
		sg_dma_len(sg) = len;

		st->nents++;
		count -= len >> PAGE_SHIFT;
		if (count == 0) {
			sg_mark_end(sg);
			i915_sg_trim(st); /* Drop any unused tail entries. */

			return st;
		}

		sg = __sg_next(sg);
		iter = __sg_next(iter);
		offset = 0;
	} while (1);

err_sg_alloc:
	kfree(st);
err_st_alloc:
	return ERR_PTR(ret);
}

static int
i915_get_ggtt_vma_pages(struct i915_vma *vma)
{
	int ret;

	/* The vma->pages are only valid within the lifespan of the borrowed
	 * obj->mm.pages. When the obj->mm.pages sg_table is regenerated, so
	 * must be the vma->pages. A simple rule is that vma->pages must only
	 * be accessed when the obj->mm.pages are pinned.
	 */
	GEM_BUG_ON(!i915_gem_object_has_pinned_pages(vma->obj));

	switch (vma->ggtt_view.type) {
	default:
		GEM_BUG_ON(vma->ggtt_view.type);
		/* fall through */
	case I915_GGTT_VIEW_NORMAL:
		vma->pages = vma->obj->mm.pages;
		return 0;

	case I915_GGTT_VIEW_ROTATED:
		vma->pages =
			intel_rotate_pages(&vma->ggtt_view.rotated, vma->obj);
		break;

	case I915_GGTT_VIEW_REMAPPED:
		vma->pages =
			intel_remap_pages(&vma->ggtt_view.remapped, vma->obj);
		break;

	case I915_GGTT_VIEW_PARTIAL:
		vma->pages = intel_partial_pages(&vma->ggtt_view, vma->obj);
		break;
	}

	ret = 0;
	if (IS_ERR(vma->pages)) {
		ret = PTR_ERR(vma->pages);
		vma->pages = NULL;
		DRM_ERROR("Failed to get pages for VMA view type %u (%d)!\n",
			  vma->ggtt_view.type, ret);
	}
	return ret;
}

/**
 * i915_gem_gtt_reserve - reserve a node in an address_space (GTT)
 * @vm: the &struct i915_address_space
 * @node: the &struct drm_mm_node (typically i915_vma.mode)
 * @size: how much space to allocate inside the GTT,
 *        must be #I915_GTT_PAGE_SIZE aligned
 * @offset: where to insert inside the GTT,
 *          must be #I915_GTT_MIN_ALIGNMENT aligned, and the node
 *          (@offset + @size) must fit within the address space
 * @color: color to apply to node, if this node is not from a VMA,
 *         color must be #I915_COLOR_UNEVICTABLE
 * @flags: control search and eviction behaviour
 *
 * i915_gem_gtt_reserve() tries to insert the @node at the exact @offset inside
 * the address space (using @size and @color). If the @node does not fit, it
 * tries to evict any overlapping nodes from the GTT, including any
 * neighbouring nodes if the colors do not match (to ensure guard pages between
 * differing domains). See i915_gem_evict_for_node() for the gory details
 * on the eviction algorithm. #PIN_NONBLOCK may used to prevent waiting on
 * evicting active overlapping objects, and any overlapping node that is pinned
 * or marked as unevictable will also result in failure.
 *
 * Returns: 0 on success, -ENOSPC if no suitable hole is found, -EINTR if
 * asked to wait for eviction and interrupted.
 */
int i915_gem_gtt_reserve(struct i915_address_space *vm,
			 struct drm_mm_node *node,
			 u64 size, u64 offset, unsigned long color,
			 unsigned int flags)
{
	int err;

	GEM_BUG_ON(!size);
	GEM_BUG_ON(!IS_ALIGNED(size, I915_GTT_PAGE_SIZE));
	GEM_BUG_ON(!IS_ALIGNED(offset, I915_GTT_MIN_ALIGNMENT));
	GEM_BUG_ON(range_overflows(offset, size, vm->total));
	GEM_BUG_ON(vm == &vm->i915->ggtt.alias->vm);
	GEM_BUG_ON(drm_mm_node_allocated(node));

	node->size = size;
	node->start = offset;
	node->color = color;

	err = drm_mm_reserve_node(&vm->mm, node);
	if (err != -ENOSPC)
		return err;

	if (flags & PIN_NOEVICT)
		return -ENOSPC;

	err = i915_gem_evict_for_node(vm, node, flags);
	if (err == 0)
		err = drm_mm_reserve_node(&vm->mm, node);

	return err;
}

static u64 random_offset(u64 start, u64 end, u64 len, u64 align)
{
	u64 range, addr;

	GEM_BUG_ON(range_overflows(start, len, end));
	GEM_BUG_ON(round_up(start, align) > round_down(end - len, align));

	range = round_down(end - len, align) - round_up(start, align);
	if (range) {
		if (sizeof(unsigned long) == sizeof(u64)) {
			addr = get_random_long();
		} else {
			addr = get_random_int();
			if (range > U32_MAX) {
				addr <<= 32;
				addr |= get_random_int();
			}
		}
		div64_u64_rem(addr, range, &addr);
		start += addr;
	}

	return round_up(start, align);
}

/**
 * i915_gem_gtt_insert - insert a node into an address_space (GTT)
 * @vm: the &struct i915_address_space
 * @node: the &struct drm_mm_node (typically i915_vma.node)
 * @size: how much space to allocate inside the GTT,
 *        must be #I915_GTT_PAGE_SIZE aligned
 * @alignment: required alignment of starting offset, may be 0 but
 *             if specified, this must be a power-of-two and at least
 *             #I915_GTT_MIN_ALIGNMENT
 * @color: color to apply to node
 * @start: start of any range restriction inside GTT (0 for all),
 *         must be #I915_GTT_PAGE_SIZE aligned
 * @end: end of any range restriction inside GTT (U64_MAX for all),
 *       must be #I915_GTT_PAGE_SIZE aligned if not U64_MAX
 * @flags: control search and eviction behaviour
 *
 * i915_gem_gtt_insert() first searches for an available hole into which
 * is can insert the node. The hole address is aligned to @alignment and
 * its @size must then fit entirely within the [@start, @end] bounds. The
 * nodes on either side of the hole must match @color, or else a guard page
 * will be inserted between the two nodes (or the node evicted). If no
 * suitable hole is found, first a victim is randomly selected and tested
 * for eviction, otherwise then the LRU list of objects within the GTT
 * is scanned to find the first set of replacement nodes to create the hole.
 * Those old overlapping nodes are evicted from the GTT (and so must be
 * rebound before any future use). Any node that is currently pinned cannot
 * be evicted (see i915_vma_pin()). Similar if the node's VMA is currently
 * active and #PIN_NONBLOCK is specified, that node is also skipped when
 * searching for an eviction candidate. See i915_gem_evict_something() for
 * the gory details on the eviction algorithm.
 *
 * Returns: 0 on success, -ENOSPC if no suitable hole is found, -EINTR if
 * asked to wait for eviction and interrupted.
 */
int i915_gem_gtt_insert(struct i915_address_space *vm,
			struct drm_mm_node *node,
			u64 size, u64 alignment, unsigned long color,
			u64 start, u64 end, unsigned int flags)
{
	enum drm_mm_insert_mode mode;
	u64 offset;
	int err;

	lockdep_assert_held(&vm->mutex);

	GEM_BUG_ON(!size);
	GEM_BUG_ON(!IS_ALIGNED(size, I915_GTT_PAGE_SIZE));
	GEM_BUG_ON(alignment && !is_power_of_2(alignment));
	GEM_BUG_ON(alignment && !IS_ALIGNED(alignment, I915_GTT_MIN_ALIGNMENT));
	GEM_BUG_ON(start >= end);
	GEM_BUG_ON(start > 0  && !IS_ALIGNED(start, I915_GTT_PAGE_SIZE));
	GEM_BUG_ON(end < U64_MAX && !IS_ALIGNED(end, I915_GTT_PAGE_SIZE));
	GEM_BUG_ON(vm == &vm->i915->ggtt.alias->vm);
	GEM_BUG_ON(drm_mm_node_allocated(node));

	if (unlikely(range_overflows(start, size, end)))
		return -ENOSPC;

	if (unlikely(round_up(start, alignment) > round_down(end - size, alignment)))
		return -ENOSPC;

	mode = DRM_MM_INSERT_BEST;
	if (flags & PIN_HIGH)
		mode = DRM_MM_INSERT_HIGHEST;
	if (flags & PIN_MAPPABLE)
		mode = DRM_MM_INSERT_LOW;

	/* We only allocate in PAGE_SIZE/GTT_PAGE_SIZE (4096) chunks,
	 * so we know that we always have a minimum alignment of 4096.
	 * The drm_mm range manager is optimised to return results
	 * with zero alignment, so where possible use the optimal
	 * path.
	 */
	BUILD_BUG_ON(I915_GTT_MIN_ALIGNMENT > I915_GTT_PAGE_SIZE);
	if (alignment <= I915_GTT_MIN_ALIGNMENT)
		alignment = 0;

	err = drm_mm_insert_node_in_range(&vm->mm, node,
					  size, alignment, color,
					  start, end, mode);
	if (err != -ENOSPC)
		return err;

	if (mode & DRM_MM_INSERT_ONCE) {
		err = drm_mm_insert_node_in_range(&vm->mm, node,
						  size, alignment, color,
						  start, end,
						  DRM_MM_INSERT_BEST);
		if (err != -ENOSPC)
			return err;
	}

	if (flags & PIN_NOEVICT)
		return -ENOSPC;

	/*
	 * No free space, pick a slot at random.
	 *
	 * There is a pathological case here using a GTT shared between
	 * mmap and GPU (i.e. ggtt/aliasing_ppgtt but not full-ppgtt):
	 *
	 *    |<-- 256 MiB aperture -->||<-- 1792 MiB unmappable -->|
	 *         (64k objects)             (448k objects)
	 *
	 * Now imagine that the eviction LRU is ordered top-down (just because
	 * pathology meets real life), and that we need to evict an object to
	 * make room inside the aperture. The eviction scan then has to walk
	 * the 448k list before it finds one within range. And now imagine that
	 * it has to search for a new hole between every byte inside the memcpy,
	 * for several simultaneous clients.
	 *
	 * On a full-ppgtt system, if we have run out of available space, there
	 * will be lots and lots of objects in the eviction list! Again,
	 * searching that LRU list may be slow if we are also applying any
	 * range restrictions (e.g. restriction to low 4GiB) and so, for
	 * simplicity and similarilty between different GTT, try the single
	 * random replacement first.
	 */
	offset = random_offset(start, end,
			       size, alignment ?: I915_GTT_MIN_ALIGNMENT);
	err = i915_gem_gtt_reserve(vm, node, size, offset, color, flags);
	if (err != -ENOSPC)
		return err;

	if (flags & PIN_NOSEARCH)
		return -ENOSPC;

	/* Randomly selected placement is pinned, do a search */
	err = i915_gem_evict_something(vm, size, alignment, color,
				       start, end, flags);
	if (err)
		return err;

	return drm_mm_insert_node_in_range(&vm->mm, node,
					   size, alignment, color,
					   start, end, DRM_MM_INSERT_EVICT);
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/mock_gtt.c"
#include "selftests/i915_gem_gtt.c"
#endif
