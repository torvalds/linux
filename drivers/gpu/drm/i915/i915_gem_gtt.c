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

#include <drm/drmP.h>
#include <drm/i915_drm.h>

#include "i915_drv.h"
#include "i915_vgpu.h"
#include "i915_trace.h"
#include "intel_drv.h"
#include "intel_frontbuffer.h"

#define I915_GFP_DMA (GFP_KERNEL | __GFP_HIGHMEM)

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

static int
i915_get_ggtt_vma_pages(struct i915_vma *vma);

static void gen6_ggtt_invalidate(struct drm_i915_private *dev_priv)
{
	/* Note that as an uncached mmio write, this should flush the
	 * WCB of the writes into the GGTT before it triggers the invalidate.
	 */
	I915_WRITE(GFX_FLSH_CNTL_GEN6, GFX_FLSH_CNTL_EN);
}

static void guc_ggtt_invalidate(struct drm_i915_private *dev_priv)
{
	gen6_ggtt_invalidate(dev_priv);
	I915_WRITE(GEN8_GTCR, GEN8_GTCR_INVALIDATE);
}

static void gmch_ggtt_invalidate(struct drm_i915_private *dev_priv)
{
	intel_gtt_chipset_flush();
}

static inline void i915_ggtt_invalidate(struct drm_i915_private *i915)
{
	i915->ggtt.invalidate(i915);
}

int intel_sanitize_enable_ppgtt(struct drm_i915_private *dev_priv,
			       	int enable_ppgtt)
{
	bool has_aliasing_ppgtt;
	bool has_full_ppgtt;
	bool has_full_48bit_ppgtt;

	has_aliasing_ppgtt = dev_priv->info.has_aliasing_ppgtt;
	has_full_ppgtt = dev_priv->info.has_full_ppgtt;
	has_full_48bit_ppgtt = dev_priv->info.has_full_48bit_ppgtt;

	if (intel_vgpu_active(dev_priv)) {
		/* emulation is too hard */
		has_full_ppgtt = false;
		has_full_48bit_ppgtt = false;
	}

	if (!has_aliasing_ppgtt)
		return 0;

	/*
	 * We don't allow disabling PPGTT for gen9+ as it's a requirement for
	 * execlists, the sole mechanism available to submit work.
	 */
	if (enable_ppgtt == 0 && INTEL_GEN(dev_priv) < 9)
		return 0;

	if (enable_ppgtt == 1)
		return 1;

	if (enable_ppgtt == 2 && has_full_ppgtt)
		return 2;

	if (enable_ppgtt == 3 && has_full_48bit_ppgtt)
		return 3;

#ifdef CONFIG_INTEL_IOMMU
	/* Disable ppgtt on SNB if VT-d is on. */
	if (IS_GEN6(dev_priv) && intel_iommu_gfx_mapped) {
		DRM_INFO("Disabling PPGTT because VT-d is on\n");
		return 0;
	}
#endif

	/* Early VLV doesn't have this */
	if (IS_VALLEYVIEW(dev_priv) && dev_priv->drm.pdev->revision < 0xb) {
		DRM_DEBUG_DRIVER("disabling PPGTT on pre-B3 step VLV\n");
		return 0;
	}

	if (INTEL_GEN(dev_priv) >= 8 && i915.enable_execlists && has_full_ppgtt)
		return has_full_48bit_ppgtt ? 3 : 2;
	else
		return has_aliasing_ppgtt ? 1 : 0;
}

static int ppgtt_bind_vma(struct i915_vma *vma,
			  enum i915_cache_level cache_level,
			  u32 unused)
{
	u32 pte_flags;
	int ret;

	ret = vma->vm->allocate_va_range(vma->vm, vma->node.start, vma->size);
	if (ret)
		return ret;

	vma->pages = vma->obj->mm.pages;

	/* Currently applicable only to VLV */
	pte_flags = 0;
	if (vma->obj->gt_ro)
		pte_flags |= PTE_READ_ONLY;

	vma->vm->insert_entries(vma->vm, vma->pages, vma->node.start,
				cache_level, pte_flags);

	return 0;
}

static void ppgtt_unbind_vma(struct i915_vma *vma)
{
	vma->vm->clear_range(vma->vm, vma->node.start, vma->size);
}

static gen8_pte_t gen8_pte_encode(dma_addr_t addr,
				  enum i915_cache_level level)
{
	gen8_pte_t pte = _PAGE_PRESENT | _PAGE_RW;
	pte |= addr;

	switch (level) {
	case I915_CACHE_NONE:
		pte |= PPAT_UNCACHED_INDEX;
		break;
	case I915_CACHE_WT:
		pte |= PPAT_DISPLAY_ELLC_INDEX;
		break;
	default:
		pte |= PPAT_CACHED_INDEX;
		break;
	}

	return pte;
}

static gen8_pde_t gen8_pde_encode(const dma_addr_t addr,
				  const enum i915_cache_level level)
{
	gen8_pde_t pde = _PAGE_PRESENT | _PAGE_RW;
	pde |= addr;
	if (level != I915_CACHE_NONE)
		pde |= PPAT_CACHED_PDE_INDEX;
	else
		pde |= PPAT_UNCACHED_INDEX;
	return pde;
}

#define gen8_pdpe_encode gen8_pde_encode
#define gen8_pml4e_encode gen8_pde_encode

static gen6_pte_t snb_pte_encode(dma_addr_t addr,
				 enum i915_cache_level level,
				 u32 unused)
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

static gen6_pte_t ivb_pte_encode(dma_addr_t addr,
				 enum i915_cache_level level,
				 u32 unused)
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

static gen6_pte_t byt_pte_encode(dma_addr_t addr,
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

static gen6_pte_t hsw_pte_encode(dma_addr_t addr,
				 enum i915_cache_level level,
				 u32 unused)
{
	gen6_pte_t pte = GEN6_PTE_VALID;
	pte |= HSW_PTE_ADDR_ENCODE(addr);

	if (level != I915_CACHE_NONE)
		pte |= HSW_WB_LLC_AGE3;

	return pte;
}

static gen6_pte_t iris_pte_encode(dma_addr_t addr,
				  enum i915_cache_level level,
				  u32 unused)
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

static struct page *vm_alloc_page(struct i915_address_space *vm, gfp_t gfp)
{
	struct page *page;

	if (I915_SELFTEST_ONLY(should_fail(&vm->fault_attr, 1)))
		i915_gem_shrink_all(vm->i915);

	if (vm->free_pages.nr)
		return vm->free_pages.pages[--vm->free_pages.nr];

	page = alloc_page(gfp);
	if (!page)
		return NULL;

	if (vm->pt_kmap_wc)
		set_pages_array_wc(&page, 1);

	return page;
}

static void vm_free_pages_release(struct i915_address_space *vm)
{
	GEM_BUG_ON(!pagevec_count(&vm->free_pages));

	if (vm->pt_kmap_wc)
		set_pages_array_wb(vm->free_pages.pages,
				   pagevec_count(&vm->free_pages));

	__pagevec_release(&vm->free_pages);
}

static void vm_free_page(struct i915_address_space *vm, struct page *page)
{
	if (!pagevec_add(&vm->free_pages, page))
		vm_free_pages_release(vm);
}

static int __setup_page_dma(struct i915_address_space *vm,
			    struct i915_page_dma *p,
			    gfp_t gfp)
{
	p->page = vm_alloc_page(vm, gfp | __GFP_NOWARN | __GFP_NORETRY);
	if (unlikely(!p->page))
		return -ENOMEM;

	p->daddr = dma_map_page(vm->dma, p->page, 0, PAGE_SIZE,
				PCI_DMA_BIDIRECTIONAL);
	if (unlikely(dma_mapping_error(vm->dma, p->daddr))) {
		vm_free_page(vm, p->page);
		return -ENOMEM;
	}

	return 0;
}

static int setup_page_dma(struct i915_address_space *vm,
			  struct i915_page_dma *p)
{
	return __setup_page_dma(vm, p, I915_GFP_DMA);
}

static void cleanup_page_dma(struct i915_address_space *vm,
			     struct i915_page_dma *p)
{
	dma_unmap_page(vm->dma, p->daddr, PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
	vm_free_page(vm, p->page);
}

#define kmap_atomic_px(px) kmap_atomic(px_base(px)->page)

#define setup_px(vm, px) setup_page_dma((vm), px_base(px))
#define cleanup_px(vm, px) cleanup_page_dma((vm), px_base(px))
#define fill_px(ppgtt, px, v) fill_page_dma((vm), px_base(px), (v))
#define fill32_px(ppgtt, px, v) fill_page_dma_32((vm), px_base(px), (v))

static void fill_page_dma(struct i915_address_space *vm,
			  struct i915_page_dma *p,
			  const u64 val)
{
	u64 * const vaddr = kmap_atomic(p->page);
	int i;

	for (i = 0; i < 512; i++)
		vaddr[i] = val;

	kunmap_atomic(vaddr);
}

static void fill_page_dma_32(struct i915_address_space *vm,
			     struct i915_page_dma *p,
			     const u32 v)
{
	fill_page_dma(vm, p, (u64)v << 32 | v);
}

static int
setup_scratch_page(struct i915_address_space *vm, gfp_t gfp)
{
	return __setup_page_dma(vm, &vm->scratch_page, gfp | __GFP_ZERO);
}

static void cleanup_scratch_page(struct i915_address_space *vm)
{
	cleanup_page_dma(vm, &vm->scratch_page);
}

static struct i915_page_table *alloc_pt(struct i915_address_space *vm)
{
	struct i915_page_table *pt;

	pt = kmalloc(sizeof(*pt), GFP_KERNEL | __GFP_NOWARN);
	if (unlikely(!pt))
		return ERR_PTR(-ENOMEM);

	if (unlikely(setup_px(vm, pt))) {
		kfree(pt);
		return ERR_PTR(-ENOMEM);
	}

	pt->used_ptes = 0;
	return pt;
}

static void free_pt(struct i915_address_space *vm, struct i915_page_table *pt)
{
	cleanup_px(vm, pt);
	kfree(pt);
}

static void gen8_initialize_pt(struct i915_address_space *vm,
			       struct i915_page_table *pt)
{
	fill_px(vm, pt,
		gen8_pte_encode(vm->scratch_page.daddr, I915_CACHE_LLC));
}

static void gen6_initialize_pt(struct i915_address_space *vm,
			       struct i915_page_table *pt)
{
	fill32_px(vm, pt,
		  vm->pte_encode(vm->scratch_page.daddr, I915_CACHE_LLC, 0));
}

static struct i915_page_directory *alloc_pd(struct i915_address_space *vm)
{
	struct i915_page_directory *pd;

	pd = kzalloc(sizeof(*pd), GFP_KERNEL | __GFP_NOWARN);
	if (unlikely(!pd))
		return ERR_PTR(-ENOMEM);

	if (unlikely(setup_px(vm, pd))) {
		kfree(pd);
		return ERR_PTR(-ENOMEM);
	}

	pd->used_pdes = 0;
	return pd;
}

static void free_pd(struct i915_address_space *vm,
		    struct i915_page_directory *pd)
{
	cleanup_px(vm, pd);
	kfree(pd);
}

static void gen8_initialize_pd(struct i915_address_space *vm,
			       struct i915_page_directory *pd)
{
	unsigned int i;

	fill_px(vm, pd,
		gen8_pde_encode(px_dma(vm->scratch_pt), I915_CACHE_LLC));
	for (i = 0; i < I915_PDES; i++)
		pd->page_table[i] = vm->scratch_pt;
}

static int __pdp_init(struct i915_address_space *vm,
		      struct i915_page_directory_pointer *pdp)
{
	const unsigned int pdpes = I915_PDPES_PER_PDP(vm->i915);
	unsigned int i;

	pdp->page_directory = kmalloc_array(pdpes, sizeof(*pdp->page_directory),
					    GFP_KERNEL | __GFP_NOWARN);
	if (unlikely(!pdp->page_directory))
		return -ENOMEM;

	for (i = 0; i < pdpes; i++)
		pdp->page_directory[i] = vm->scratch_pd;

	return 0;
}

static void __pdp_fini(struct i915_page_directory_pointer *pdp)
{
	kfree(pdp->page_directory);
	pdp->page_directory = NULL;
}

static struct i915_page_directory_pointer *
alloc_pdp(struct i915_address_space *vm)
{
	struct i915_page_directory_pointer *pdp;
	int ret = -ENOMEM;

	WARN_ON(!USES_FULL_48BIT_PPGTT(vm->i915));

	pdp = kzalloc(sizeof(*pdp), GFP_KERNEL);
	if (!pdp)
		return ERR_PTR(-ENOMEM);

	ret = __pdp_init(vm, pdp);
	if (ret)
		goto fail_bitmap;

	ret = setup_px(vm, pdp);
	if (ret)
		goto fail_page_m;

	return pdp;

fail_page_m:
	__pdp_fini(pdp);
fail_bitmap:
	kfree(pdp);

	return ERR_PTR(ret);
}

static void free_pdp(struct i915_address_space *vm,
		     struct i915_page_directory_pointer *pdp)
{
	__pdp_fini(pdp);
	if (USES_FULL_48BIT_PPGTT(vm->i915)) {
		cleanup_px(vm, pdp);
		kfree(pdp);
	}
}

static void gen8_initialize_pdp(struct i915_address_space *vm,
				struct i915_page_directory_pointer *pdp)
{
	gen8_ppgtt_pdpe_t scratch_pdpe;

	scratch_pdpe = gen8_pdpe_encode(px_dma(vm->scratch_pd), I915_CACHE_LLC);

	fill_px(vm, pdp, scratch_pdpe);
}

static void gen8_initialize_pml4(struct i915_address_space *vm,
				 struct i915_pml4 *pml4)
{
	unsigned int i;

	fill_px(vm, pml4,
		gen8_pml4e_encode(px_dma(vm->scratch_pdp), I915_CACHE_LLC));
	for (i = 0; i < GEN8_PML4ES_PER_PML4; i++)
		pml4->pdps[i] = vm->scratch_pdp;
}

/* Broadwell Page Directory Pointer Descriptors */
static int gen8_write_pdp(struct drm_i915_gem_request *req,
			  unsigned entry,
			  dma_addr_t addr)
{
	struct intel_engine_cs *engine = req->engine;
	u32 *cs;

	BUG_ON(entry >= 4);

	cs = intel_ring_begin(req, 6);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	*cs++ = MI_LOAD_REGISTER_IMM(1);
	*cs++ = i915_mmio_reg_offset(GEN8_RING_PDP_UDW(engine, entry));
	*cs++ = upper_32_bits(addr);
	*cs++ = MI_LOAD_REGISTER_IMM(1);
	*cs++ = i915_mmio_reg_offset(GEN8_RING_PDP_LDW(engine, entry));
	*cs++ = lower_32_bits(addr);
	intel_ring_advance(req, cs);

	return 0;
}

static int gen8_legacy_mm_switch(struct i915_hw_ppgtt *ppgtt,
				 struct drm_i915_gem_request *req)
{
	int i, ret;

	for (i = GEN8_LEGACY_PDPES - 1; i >= 0; i--) {
		const dma_addr_t pd_daddr = i915_page_dir_dma_addr(ppgtt, i);

		ret = gen8_write_pdp(req, i, pd_daddr);
		if (ret)
			return ret;
	}

	return 0;
}

static int gen8_48b_mm_switch(struct i915_hw_ppgtt *ppgtt,
			      struct drm_i915_gem_request *req)
{
	return gen8_write_pdp(req, 0, px_dma(&ppgtt->pml4));
}

/* PDE TLBs are a pain to invalidate on GEN8+. When we modify
 * the page table structures, we mark them dirty so that
 * context switching/execlist queuing code takes extra steps
 * to ensure that tlbs are flushed.
 */
static void mark_tlbs_dirty(struct i915_hw_ppgtt *ppgtt)
{
	ppgtt->pd_dirty_rings = INTEL_INFO(ppgtt->base.i915)->ring_mask;
}

/* Removes entries from a single page table, releasing it if it's empty.
 * Caller can use the return value to update higher-level entries.
 */
static bool gen8_ppgtt_clear_pt(struct i915_address_space *vm,
				struct i915_page_table *pt,
				u64 start, u64 length)
{
	unsigned int num_entries = gen8_pte_count(start, length);
	unsigned int pte = gen8_pte_index(start);
	unsigned int pte_end = pte + num_entries;
	const gen8_pte_t scratch_pte =
		gen8_pte_encode(vm->scratch_page.daddr, I915_CACHE_LLC);
	gen8_pte_t *vaddr;

	GEM_BUG_ON(num_entries > pt->used_ptes);

	pt->used_ptes -= num_entries;
	if (!pt->used_ptes)
		return true;

	vaddr = kmap_atomic_px(pt);
	while (pte < pte_end)
		vaddr[pte++] = scratch_pte;
	kunmap_atomic(vaddr);

	return false;
}

static void gen8_ppgtt_set_pde(struct i915_address_space *vm,
			       struct i915_page_directory *pd,
			       struct i915_page_table *pt,
			       unsigned int pde)
{
	gen8_pde_t *vaddr;

	pd->page_table[pde] = pt;

	vaddr = kmap_atomic_px(pd);
	vaddr[pde] = gen8_pde_encode(px_dma(pt), I915_CACHE_LLC);
	kunmap_atomic(vaddr);
}

static bool gen8_ppgtt_clear_pd(struct i915_address_space *vm,
				struct i915_page_directory *pd,
				u64 start, u64 length)
{
	struct i915_page_table *pt;
	u32 pde;

	gen8_for_each_pde(pt, pd, start, length, pde) {
		GEM_BUG_ON(pt == vm->scratch_pt);

		if (!gen8_ppgtt_clear_pt(vm, pt, start, length))
			continue;

		gen8_ppgtt_set_pde(vm, pd, vm->scratch_pt, pde);
		GEM_BUG_ON(!pd->used_pdes);
		pd->used_pdes--;

		free_pt(vm, pt);
	}

	return !pd->used_pdes;
}

static void gen8_ppgtt_set_pdpe(struct i915_address_space *vm,
				struct i915_page_directory_pointer *pdp,
				struct i915_page_directory *pd,
				unsigned int pdpe)
{
	gen8_ppgtt_pdpe_t *vaddr;

	pdp->page_directory[pdpe] = pd;
	if (!USES_FULL_48BIT_PPGTT(vm->i915))
		return;

	vaddr = kmap_atomic_px(pdp);
	vaddr[pdpe] = gen8_pdpe_encode(px_dma(pd), I915_CACHE_LLC);
	kunmap_atomic(vaddr);
}

/* Removes entries from a single page dir pointer, releasing it if it's empty.
 * Caller can use the return value to update higher-level entries
 */
static bool gen8_ppgtt_clear_pdp(struct i915_address_space *vm,
				 struct i915_page_directory_pointer *pdp,
				 u64 start, u64 length)
{
	struct i915_page_directory *pd;
	unsigned int pdpe;

	gen8_for_each_pdpe(pd, pdp, start, length, pdpe) {
		GEM_BUG_ON(pd == vm->scratch_pd);

		if (!gen8_ppgtt_clear_pd(vm, pd, start, length))
			continue;

		gen8_ppgtt_set_pdpe(vm, pdp, vm->scratch_pd, pdpe);
		GEM_BUG_ON(!pdp->used_pdpes);
		pdp->used_pdpes--;

		free_pd(vm, pd);
	}

	return !pdp->used_pdpes;
}

static void gen8_ppgtt_clear_3lvl(struct i915_address_space *vm,
				  u64 start, u64 length)
{
	gen8_ppgtt_clear_pdp(vm, &i915_vm_to_ppgtt(vm)->pdp, start, length);
}

static void gen8_ppgtt_set_pml4e(struct i915_pml4 *pml4,
				 struct i915_page_directory_pointer *pdp,
				 unsigned int pml4e)
{
	gen8_ppgtt_pml4e_t *vaddr;

	pml4->pdps[pml4e] = pdp;

	vaddr = kmap_atomic_px(pml4);
	vaddr[pml4e] = gen8_pml4e_encode(px_dma(pdp), I915_CACHE_LLC);
	kunmap_atomic(vaddr);
}

/* Removes entries from a single pml4.
 * This is the top-level structure in 4-level page tables used on gen8+.
 * Empty entries are always scratch pml4e.
 */
static void gen8_ppgtt_clear_4lvl(struct i915_address_space *vm,
				  u64 start, u64 length)
{
	struct i915_hw_ppgtt *ppgtt = i915_vm_to_ppgtt(vm);
	struct i915_pml4 *pml4 = &ppgtt->pml4;
	struct i915_page_directory_pointer *pdp;
	unsigned int pml4e;

	GEM_BUG_ON(!USES_FULL_48BIT_PPGTT(vm->i915));

	gen8_for_each_pml4e(pdp, pml4, start, length, pml4e) {
		GEM_BUG_ON(pdp == vm->scratch_pdp);

		if (!gen8_ppgtt_clear_pdp(vm, pdp, start, length))
			continue;

		gen8_ppgtt_set_pml4e(pml4, vm->scratch_pdp, pml4e);

		free_pdp(vm, pdp);
	}
}

struct sgt_dma {
	struct scatterlist *sg;
	dma_addr_t dma, max;
};

struct gen8_insert_pte {
	u16 pml4e;
	u16 pdpe;
	u16 pde;
	u16 pte;
};

static __always_inline struct gen8_insert_pte gen8_insert_pte(u64 start)
{
	return (struct gen8_insert_pte) {
		 gen8_pml4e_index(start),
		 gen8_pdpe_index(start),
		 gen8_pde_index(start),
		 gen8_pte_index(start),
	};
}

static __always_inline bool
gen8_ppgtt_insert_pte_entries(struct i915_hw_ppgtt *ppgtt,
			      struct i915_page_directory_pointer *pdp,
			      struct sgt_dma *iter,
			      struct gen8_insert_pte *idx,
			      enum i915_cache_level cache_level)
{
	struct i915_page_directory *pd;
	const gen8_pte_t pte_encode = gen8_pte_encode(0, cache_level);
	gen8_pte_t *vaddr;
	bool ret;

	GEM_BUG_ON(idx->pdpe >= I915_PDPES_PER_PDP(vm));
	pd = pdp->page_directory[idx->pdpe];
	vaddr = kmap_atomic_px(pd->page_table[idx->pde]);
	do {
		vaddr[idx->pte] = pte_encode | iter->dma;

		iter->dma += PAGE_SIZE;
		if (iter->dma >= iter->max) {
			iter->sg = __sg_next(iter->sg);
			if (!iter->sg) {
				ret = false;
				break;
			}

			iter->dma = sg_dma_address(iter->sg);
			iter->max = iter->dma + iter->sg->length;
		}

		if (++idx->pte == GEN8_PTES) {
			idx->pte = 0;

			if (++idx->pde == I915_PDES) {
				idx->pde = 0;

				/* Limited by sg length for 3lvl */
				if (++idx->pdpe == GEN8_PML4ES_PER_PML4) {
					idx->pdpe = 0;
					ret = true;
					break;
				}

				GEM_BUG_ON(idx->pdpe >= I915_PDPES_PER_PDP(vm));
				pd = pdp->page_directory[idx->pdpe];
			}

			kunmap_atomic(vaddr);
			vaddr = kmap_atomic_px(pd->page_table[idx->pde]);
		}
	} while (1);
	kunmap_atomic(vaddr);

	return ret;
}

static void gen8_ppgtt_insert_3lvl(struct i915_address_space *vm,
				   struct sg_table *pages,
				   u64 start,
				   enum i915_cache_level cache_level,
				   u32 unused)
{
	struct i915_hw_ppgtt *ppgtt = i915_vm_to_ppgtt(vm);
	struct sgt_dma iter = {
		.sg = pages->sgl,
		.dma = sg_dma_address(iter.sg),
		.max = iter.dma + iter.sg->length,
	};
	struct gen8_insert_pte idx = gen8_insert_pte(start);

	gen8_ppgtt_insert_pte_entries(ppgtt, &ppgtt->pdp, &iter, &idx,
				      cache_level);
}

static void gen8_ppgtt_insert_4lvl(struct i915_address_space *vm,
				   struct sg_table *pages,
				   u64 start,
				   enum i915_cache_level cache_level,
				   u32 unused)
{
	struct i915_hw_ppgtt *ppgtt = i915_vm_to_ppgtt(vm);
	struct sgt_dma iter = {
		.sg = pages->sgl,
		.dma = sg_dma_address(iter.sg),
		.max = iter.dma + iter.sg->length,
	};
	struct i915_page_directory_pointer **pdps = ppgtt->pml4.pdps;
	struct gen8_insert_pte idx = gen8_insert_pte(start);

	while (gen8_ppgtt_insert_pte_entries(ppgtt, pdps[idx.pml4e++], &iter,
					     &idx, cache_level))
		GEM_BUG_ON(idx.pml4e >= GEN8_PML4ES_PER_PML4);
}

static void gen8_free_page_tables(struct i915_address_space *vm,
				  struct i915_page_directory *pd)
{
	int i;

	if (!px_page(pd))
		return;

	for (i = 0; i < I915_PDES; i++) {
		if (pd->page_table[i] != vm->scratch_pt)
			free_pt(vm, pd->page_table[i]);
	}
}

static int gen8_init_scratch(struct i915_address_space *vm)
{
	int ret;

	ret = setup_scratch_page(vm, I915_GFP_DMA);
	if (ret)
		return ret;

	vm->scratch_pt = alloc_pt(vm);
	if (IS_ERR(vm->scratch_pt)) {
		ret = PTR_ERR(vm->scratch_pt);
		goto free_scratch_page;
	}

	vm->scratch_pd = alloc_pd(vm);
	if (IS_ERR(vm->scratch_pd)) {
		ret = PTR_ERR(vm->scratch_pd);
		goto free_pt;
	}

	if (USES_FULL_48BIT_PPGTT(dev)) {
		vm->scratch_pdp = alloc_pdp(vm);
		if (IS_ERR(vm->scratch_pdp)) {
			ret = PTR_ERR(vm->scratch_pdp);
			goto free_pd;
		}
	}

	gen8_initialize_pt(vm, vm->scratch_pt);
	gen8_initialize_pd(vm, vm->scratch_pd);
	if (USES_FULL_48BIT_PPGTT(dev_priv))
		gen8_initialize_pdp(vm, vm->scratch_pdp);

	return 0;

free_pd:
	free_pd(vm, vm->scratch_pd);
free_pt:
	free_pt(vm, vm->scratch_pt);
free_scratch_page:
	cleanup_scratch_page(vm);

	return ret;
}

static int gen8_ppgtt_notify_vgt(struct i915_hw_ppgtt *ppgtt, bool create)
{
	enum vgt_g2v_type msg;
	struct drm_i915_private *dev_priv = ppgtt->base.i915;
	int i;

	if (USES_FULL_48BIT_PPGTT(dev_priv)) {
		u64 daddr = px_dma(&ppgtt->pml4);

		I915_WRITE(vgtif_reg(pdp[0].lo), lower_32_bits(daddr));
		I915_WRITE(vgtif_reg(pdp[0].hi), upper_32_bits(daddr));

		msg = (create ? VGT_G2V_PPGTT_L4_PAGE_TABLE_CREATE :
				VGT_G2V_PPGTT_L4_PAGE_TABLE_DESTROY);
	} else {
		for (i = 0; i < GEN8_LEGACY_PDPES; i++) {
			u64 daddr = i915_page_dir_dma_addr(ppgtt, i);

			I915_WRITE(vgtif_reg(pdp[i].lo), lower_32_bits(daddr));
			I915_WRITE(vgtif_reg(pdp[i].hi), upper_32_bits(daddr));
		}

		msg = (create ? VGT_G2V_PPGTT_L3_PAGE_TABLE_CREATE :
				VGT_G2V_PPGTT_L3_PAGE_TABLE_DESTROY);
	}

	I915_WRITE(vgtif_reg(g2v_notify), msg);

	return 0;
}

static void gen8_free_scratch(struct i915_address_space *vm)
{
	if (USES_FULL_48BIT_PPGTT(vm->i915))
		free_pdp(vm, vm->scratch_pdp);
	free_pd(vm, vm->scratch_pd);
	free_pt(vm, vm->scratch_pt);
	cleanup_scratch_page(vm);
}

static void gen8_ppgtt_cleanup_3lvl(struct i915_address_space *vm,
				    struct i915_page_directory_pointer *pdp)
{
	int i;

	for (i = 0; i < I915_PDPES_PER_PDP(vm->i915); i++) {
		if (pdp->page_directory[i] == vm->scratch_pd)
			continue;

		gen8_free_page_tables(vm, pdp->page_directory[i]);
		free_pd(vm, pdp->page_directory[i]);
	}

	free_pdp(vm, pdp);
}

static void gen8_ppgtt_cleanup_4lvl(struct i915_hw_ppgtt *ppgtt)
{
	int i;

	for (i = 0; i < GEN8_PML4ES_PER_PML4; i++) {
		if (ppgtt->pml4.pdps[i] == ppgtt->base.scratch_pdp)
			continue;

		gen8_ppgtt_cleanup_3lvl(&ppgtt->base, ppgtt->pml4.pdps[i]);
	}

	cleanup_px(&ppgtt->base, &ppgtt->pml4);
}

static void gen8_ppgtt_cleanup(struct i915_address_space *vm)
{
	struct drm_i915_private *dev_priv = vm->i915;
	struct i915_hw_ppgtt *ppgtt = i915_vm_to_ppgtt(vm);

	if (intel_vgpu_active(dev_priv))
		gen8_ppgtt_notify_vgt(ppgtt, false);

	if (!USES_FULL_48BIT_PPGTT(vm->i915))
		gen8_ppgtt_cleanup_3lvl(&ppgtt->base, &ppgtt->pdp);
	else
		gen8_ppgtt_cleanup_4lvl(ppgtt);

	gen8_free_scratch(vm);
}

static int gen8_ppgtt_alloc_pd(struct i915_address_space *vm,
			       struct i915_page_directory *pd,
			       u64 start, u64 length)
{
	struct i915_page_table *pt;
	u64 from = start;
	unsigned int pde;

	gen8_for_each_pde(pt, pd, start, length, pde) {
		if (pt == vm->scratch_pt) {
			pt = alloc_pt(vm);
			if (IS_ERR(pt))
				goto unwind;

			gen8_initialize_pt(vm, pt);

			gen8_ppgtt_set_pde(vm, pd, pt, pde);
			pd->used_pdes++;
			GEM_BUG_ON(pd->used_pdes > I915_PDES);
		}

		pt->used_ptes += gen8_pte_count(start, length);
	}
	return 0;

unwind:
	gen8_ppgtt_clear_pd(vm, pd, from, start - from);
	return -ENOMEM;
}

static int gen8_ppgtt_alloc_pdp(struct i915_address_space *vm,
				struct i915_page_directory_pointer *pdp,
				u64 start, u64 length)
{
	struct i915_page_directory *pd;
	u64 from = start;
	unsigned int pdpe;
	int ret;

	gen8_for_each_pdpe(pd, pdp, start, length, pdpe) {
		if (pd == vm->scratch_pd) {
			pd = alloc_pd(vm);
			if (IS_ERR(pd))
				goto unwind;

			gen8_initialize_pd(vm, pd);
			gen8_ppgtt_set_pdpe(vm, pdp, pd, pdpe);
			pdp->used_pdpes++;
			GEM_BUG_ON(pdp->used_pdpes > I915_PDPES_PER_PDP(vm));

			mark_tlbs_dirty(i915_vm_to_ppgtt(vm));
		}

		ret = gen8_ppgtt_alloc_pd(vm, pd, start, length);
		if (unlikely(ret))
			goto unwind_pd;
	}

	return 0;

unwind_pd:
	if (!pd->used_pdes) {
		gen8_ppgtt_set_pdpe(vm, pdp, vm->scratch_pd, pdpe);
		GEM_BUG_ON(!pdp->used_pdpes);
		pdp->used_pdpes--;
		free_pd(vm, pd);
	}
unwind:
	gen8_ppgtt_clear_pdp(vm, pdp, from, start - from);
	return -ENOMEM;
}

static int gen8_ppgtt_alloc_3lvl(struct i915_address_space *vm,
				 u64 start, u64 length)
{
	return gen8_ppgtt_alloc_pdp(vm,
				    &i915_vm_to_ppgtt(vm)->pdp, start, length);
}

static int gen8_ppgtt_alloc_4lvl(struct i915_address_space *vm,
				 u64 start, u64 length)
{
	struct i915_hw_ppgtt *ppgtt = i915_vm_to_ppgtt(vm);
	struct i915_pml4 *pml4 = &ppgtt->pml4;
	struct i915_page_directory_pointer *pdp;
	u64 from = start;
	u32 pml4e;
	int ret;

	gen8_for_each_pml4e(pdp, pml4, start, length, pml4e) {
		if (pml4->pdps[pml4e] == vm->scratch_pdp) {
			pdp = alloc_pdp(vm);
			if (IS_ERR(pdp))
				goto unwind;

			gen8_initialize_pdp(vm, pdp);
			gen8_ppgtt_set_pml4e(pml4, pdp, pml4e);
		}

		ret = gen8_ppgtt_alloc_pdp(vm, pdp, start, length);
		if (unlikely(ret))
			goto unwind_pdp;
	}

	return 0;

unwind_pdp:
	if (!pdp->used_pdpes) {
		gen8_ppgtt_set_pml4e(pml4, vm->scratch_pdp, pml4e);
		free_pdp(vm, pdp);
	}
unwind:
	gen8_ppgtt_clear_4lvl(vm, from, start - from);
	return -ENOMEM;
}

static void gen8_dump_pdp(struct i915_hw_ppgtt *ppgtt,
			  struct i915_page_directory_pointer *pdp,
			  u64 start, u64 length,
			  gen8_pte_t scratch_pte,
			  struct seq_file *m)
{
	struct i915_page_directory *pd;
	u32 pdpe;

	gen8_for_each_pdpe(pd, pdp, start, length, pdpe) {
		struct i915_page_table *pt;
		u64 pd_len = length;
		u64 pd_start = start;
		u32 pde;

		if (pdp->page_directory[pdpe] == ppgtt->base.scratch_pd)
			continue;

		seq_printf(m, "\tPDPE #%d\n", pdpe);
		gen8_for_each_pde(pt, pd, pd_start, pd_len, pde) {
			u32 pte;
			gen8_pte_t *pt_vaddr;

			if (pd->page_table[pde] == ppgtt->base.scratch_pt)
				continue;

			pt_vaddr = kmap_atomic_px(pt);
			for (pte = 0; pte < GEN8_PTES; pte += 4) {
				u64 va = (pdpe << GEN8_PDPE_SHIFT |
					  pde << GEN8_PDE_SHIFT |
					  pte << GEN8_PTE_SHIFT);
				int i;
				bool found = false;

				for (i = 0; i < 4; i++)
					if (pt_vaddr[pte + i] != scratch_pte)
						found = true;
				if (!found)
					continue;

				seq_printf(m, "\t\t0x%llx [%03d,%03d,%04d]: =", va, pdpe, pde, pte);
				for (i = 0; i < 4; i++) {
					if (pt_vaddr[pte + i] != scratch_pte)
						seq_printf(m, " %llx", pt_vaddr[pte + i]);
					else
						seq_puts(m, "  SCRATCH ");
				}
				seq_puts(m, "\n");
			}
			kunmap_atomic(pt_vaddr);
		}
	}
}

static void gen8_dump_ppgtt(struct i915_hw_ppgtt *ppgtt, struct seq_file *m)
{
	struct i915_address_space *vm = &ppgtt->base;
	const gen8_pte_t scratch_pte =
		gen8_pte_encode(vm->scratch_page.daddr, I915_CACHE_LLC);
	u64 start = 0, length = ppgtt->base.total;

	if (!USES_FULL_48BIT_PPGTT(vm->i915)) {
		gen8_dump_pdp(ppgtt, &ppgtt->pdp, start, length, scratch_pte, m);
	} else {
		u64 pml4e;
		struct i915_pml4 *pml4 = &ppgtt->pml4;
		struct i915_page_directory_pointer *pdp;

		gen8_for_each_pml4e(pdp, pml4, start, length, pml4e) {
			if (pml4->pdps[pml4e] == ppgtt->base.scratch_pdp)
				continue;

			seq_printf(m, "    PML4E #%llu\n", pml4e);
			gen8_dump_pdp(ppgtt, pdp, start, length, scratch_pte, m);
		}
	}
}

static int gen8_preallocate_top_level_pdp(struct i915_hw_ppgtt *ppgtt)
{
	struct i915_address_space *vm = &ppgtt->base;
	struct i915_page_directory_pointer *pdp = &ppgtt->pdp;
	struct i915_page_directory *pd;
	u64 start = 0, length = ppgtt->base.total;
	u64 from = start;
	unsigned int pdpe;

	gen8_for_each_pdpe(pd, pdp, start, length, pdpe) {
		pd = alloc_pd(vm);
		if (IS_ERR(pd))
			goto unwind;

		gen8_initialize_pd(vm, pd);
		gen8_ppgtt_set_pdpe(vm, pdp, pd, pdpe);
		pdp->used_pdpes++;
	}

	pdp->used_pdpes++; /* never remove */
	return 0;

unwind:
	start -= from;
	gen8_for_each_pdpe(pd, pdp, from, start, pdpe) {
		gen8_ppgtt_set_pdpe(vm, pdp, vm->scratch_pd, pdpe);
		free_pd(vm, pd);
	}
	pdp->used_pdpes = 0;
	return -ENOMEM;
}

/*
 * GEN8 legacy ppgtt programming is accomplished through a max 4 PDP registers
 * with a net effect resembling a 2-level page table in normal x86 terms. Each
 * PDP represents 1GB of memory 4 * 512 * 512 * 4096 = 4GB legacy 32b address
 * space.
 *
 */
static int gen8_ppgtt_init(struct i915_hw_ppgtt *ppgtt)
{
	struct drm_i915_private *dev_priv = ppgtt->base.i915;
	int ret;

	ret = gen8_init_scratch(&ppgtt->base);
	if (ret)
		return ret;

	ppgtt->base.cleanup = gen8_ppgtt_cleanup;
	ppgtt->base.unbind_vma = ppgtt_unbind_vma;
	ppgtt->base.bind_vma = ppgtt_bind_vma;
	ppgtt->debug_dump = gen8_dump_ppgtt;

	/* There are only few exceptions for gen >=6. chv and bxt.
	 * And we are not sure about the latter so play safe for now.
	 */
	if (IS_CHERRYVIEW(dev_priv) || IS_BROXTON(dev_priv))
		ppgtt->base.pt_kmap_wc = true;

	if (USES_FULL_48BIT_PPGTT(dev_priv)) {
		ret = setup_px(&ppgtt->base, &ppgtt->pml4);
		if (ret)
			goto free_scratch;

		gen8_initialize_pml4(&ppgtt->base, &ppgtt->pml4);

		ppgtt->base.total = 1ULL << 48;
		ppgtt->switch_mm = gen8_48b_mm_switch;

		ppgtt->base.allocate_va_range = gen8_ppgtt_alloc_4lvl;
		ppgtt->base.insert_entries = gen8_ppgtt_insert_4lvl;
		ppgtt->base.clear_range = gen8_ppgtt_clear_4lvl;
	} else {
		ret = __pdp_init(&ppgtt->base, &ppgtt->pdp);
		if (ret)
			goto free_scratch;

		ppgtt->base.total = 1ULL << 32;
		ppgtt->switch_mm = gen8_legacy_mm_switch;

		if (intel_vgpu_active(dev_priv)) {
			ret = gen8_preallocate_top_level_pdp(ppgtt);
			if (ret) {
				__pdp_fini(&ppgtt->pdp);
				goto free_scratch;
			}
		}

		ppgtt->base.allocate_va_range = gen8_ppgtt_alloc_3lvl;
		ppgtt->base.insert_entries = gen8_ppgtt_insert_3lvl;
		ppgtt->base.clear_range = gen8_ppgtt_clear_3lvl;
	}

	if (intel_vgpu_active(dev_priv))
		gen8_ppgtt_notify_vgt(ppgtt, true);

	return 0;

free_scratch:
	gen8_free_scratch(&ppgtt->base);
	return ret;
}

static void gen6_dump_ppgtt(struct i915_hw_ppgtt *ppgtt, struct seq_file *m)
{
	struct i915_address_space *vm = &ppgtt->base;
	struct i915_page_table *unused;
	gen6_pte_t scratch_pte;
	u32 pd_entry, pte, pde;
	u32 start = 0, length = ppgtt->base.total;

	scratch_pte = vm->pte_encode(vm->scratch_page.daddr,
				     I915_CACHE_LLC, 0);

	gen6_for_each_pde(unused, &ppgtt->pd, start, length, pde) {
		u32 expected;
		gen6_pte_t *pt_vaddr;
		const dma_addr_t pt_addr = px_dma(ppgtt->pd.page_table[pde]);
		pd_entry = readl(ppgtt->pd_addr + pde);
		expected = (GEN6_PDE_ADDR_ENCODE(pt_addr) | GEN6_PDE_VALID);

		if (pd_entry != expected)
			seq_printf(m, "\tPDE #%d mismatch: Actual PDE: %x Expected PDE: %x\n",
				   pde,
				   pd_entry,
				   expected);
		seq_printf(m, "\tPDE: %x\n", pd_entry);

		pt_vaddr = kmap_atomic_px(ppgtt->pd.page_table[pde]);

		for (pte = 0; pte < GEN6_PTES; pte+=4) {
			unsigned long va =
				(pde * PAGE_SIZE * GEN6_PTES) +
				(pte * PAGE_SIZE);
			int i;
			bool found = false;
			for (i = 0; i < 4; i++)
				if (pt_vaddr[pte + i] != scratch_pte)
					found = true;
			if (!found)
				continue;

			seq_printf(m, "\t\t0x%lx [%03d,%04d]: =", va, pde, pte);
			for (i = 0; i < 4; i++) {
				if (pt_vaddr[pte + i] != scratch_pte)
					seq_printf(m, " %08x", pt_vaddr[pte + i]);
				else
					seq_puts(m, "  SCRATCH ");
			}
			seq_puts(m, "\n");
		}
		kunmap_atomic(pt_vaddr);
	}
}

/* Write pde (index) from the page directory @pd to the page table @pt */
static inline void gen6_write_pde(const struct i915_hw_ppgtt *ppgtt,
				  const unsigned int pde,
				  const struct i915_page_table *pt)
{
	/* Caller needs to make sure the write completes if necessary */
	writel_relaxed(GEN6_PDE_ADDR_ENCODE(px_dma(pt)) | GEN6_PDE_VALID,
		       ppgtt->pd_addr + pde);
}

/* Write all the page tables found in the ppgtt structure to incrementing page
 * directories. */
static void gen6_write_page_range(struct i915_hw_ppgtt *ppgtt,
				  u32 start, u32 length)
{
	struct i915_page_table *pt;
	unsigned int pde;

	gen6_for_each_pde(pt, &ppgtt->pd, start, length, pde)
		gen6_write_pde(ppgtt, pde, pt);

	mark_tlbs_dirty(ppgtt);
	wmb();
}

static inline u32 get_pd_offset(struct i915_hw_ppgtt *ppgtt)
{
	GEM_BUG_ON(ppgtt->pd.base.ggtt_offset & 0x3f);
	return ppgtt->pd.base.ggtt_offset << 10;
}

static int hsw_mm_switch(struct i915_hw_ppgtt *ppgtt,
			 struct drm_i915_gem_request *req)
{
	struct intel_engine_cs *engine = req->engine;
	u32 *cs;
	int ret;

	/* NB: TLBs must be flushed and invalidated before a switch */
	ret = engine->emit_flush(req, EMIT_INVALIDATE | EMIT_FLUSH);
	if (ret)
		return ret;

	cs = intel_ring_begin(req, 6);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	*cs++ = MI_LOAD_REGISTER_IMM(2);
	*cs++ = i915_mmio_reg_offset(RING_PP_DIR_DCLV(engine));
	*cs++ = PP_DIR_DCLV_2G;
	*cs++ = i915_mmio_reg_offset(RING_PP_DIR_BASE(engine));
	*cs++ = get_pd_offset(ppgtt);
	*cs++ = MI_NOOP;
	intel_ring_advance(req, cs);

	return 0;
}

static int gen7_mm_switch(struct i915_hw_ppgtt *ppgtt,
			  struct drm_i915_gem_request *req)
{
	struct intel_engine_cs *engine = req->engine;
	u32 *cs;
	int ret;

	/* NB: TLBs must be flushed and invalidated before a switch */
	ret = engine->emit_flush(req, EMIT_INVALIDATE | EMIT_FLUSH);
	if (ret)
		return ret;

	cs = intel_ring_begin(req, 6);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	*cs++ = MI_LOAD_REGISTER_IMM(2);
	*cs++ = i915_mmio_reg_offset(RING_PP_DIR_DCLV(engine));
	*cs++ = PP_DIR_DCLV_2G;
	*cs++ = i915_mmio_reg_offset(RING_PP_DIR_BASE(engine));
	*cs++ = get_pd_offset(ppgtt);
	*cs++ = MI_NOOP;
	intel_ring_advance(req, cs);

	/* XXX: RCS is the only one to auto invalidate the TLBs? */
	if (engine->id != RCS) {
		ret = engine->emit_flush(req, EMIT_INVALIDATE | EMIT_FLUSH);
		if (ret)
			return ret;
	}

	return 0;
}

static int gen6_mm_switch(struct i915_hw_ppgtt *ppgtt,
			  struct drm_i915_gem_request *req)
{
	struct intel_engine_cs *engine = req->engine;
	struct drm_i915_private *dev_priv = req->i915;

	I915_WRITE(RING_PP_DIR_DCLV(engine), PP_DIR_DCLV_2G);
	I915_WRITE(RING_PP_DIR_BASE(engine), get_pd_offset(ppgtt));
	return 0;
}

static void gen8_ppgtt_enable(struct drm_i915_private *dev_priv)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	for_each_engine(engine, dev_priv, id) {
		u32 four_level = USES_FULL_48BIT_PPGTT(dev_priv) ?
				 GEN8_GFX_PPGTT_48B : 0;
		I915_WRITE(RING_MODE_GEN7(engine),
			   _MASKED_BIT_ENABLE(GFX_PPGTT_ENABLE | four_level));
	}
}

static void gen7_ppgtt_enable(struct drm_i915_private *dev_priv)
{
	struct intel_engine_cs *engine;
	u32 ecochk, ecobits;
	enum intel_engine_id id;

	ecobits = I915_READ(GAC_ECO_BITS);
	I915_WRITE(GAC_ECO_BITS, ecobits | ECOBITS_PPGTT_CACHE64B);

	ecochk = I915_READ(GAM_ECOCHK);
	if (IS_HASWELL(dev_priv)) {
		ecochk |= ECOCHK_PPGTT_WB_HSW;
	} else {
		ecochk |= ECOCHK_PPGTT_LLC_IVB;
		ecochk &= ~ECOCHK_PPGTT_GFDT_IVB;
	}
	I915_WRITE(GAM_ECOCHK, ecochk);

	for_each_engine(engine, dev_priv, id) {
		/* GFX_MODE is per-ring on gen7+ */
		I915_WRITE(RING_MODE_GEN7(engine),
			   _MASKED_BIT_ENABLE(GFX_PPGTT_ENABLE));
	}
}

static void gen6_ppgtt_enable(struct drm_i915_private *dev_priv)
{
	u32 ecochk, gab_ctl, ecobits;

	ecobits = I915_READ(GAC_ECO_BITS);
	I915_WRITE(GAC_ECO_BITS, ecobits | ECOBITS_SNB_BIT |
		   ECOBITS_PPGTT_CACHE64B);

	gab_ctl = I915_READ(GAB_CTL);
	I915_WRITE(GAB_CTL, gab_ctl | GAB_CTL_CONT_AFTER_PAGEFAULT);

	ecochk = I915_READ(GAM_ECOCHK);
	I915_WRITE(GAM_ECOCHK, ecochk | ECOCHK_SNB_BIT | ECOCHK_PPGTT_CACHE64B);

	I915_WRITE(GFX_MODE, _MASKED_BIT_ENABLE(GFX_PPGTT_ENABLE));
}

/* PPGTT support for Sandybdrige/Gen6 and later */
static void gen6_ppgtt_clear_range(struct i915_address_space *vm,
				   u64 start, u64 length)
{
	struct i915_hw_ppgtt *ppgtt = i915_vm_to_ppgtt(vm);
	unsigned int first_entry = start >> PAGE_SHIFT;
	unsigned int pde = first_entry / GEN6_PTES;
	unsigned int pte = first_entry % GEN6_PTES;
	unsigned int num_entries = length >> PAGE_SHIFT;
	gen6_pte_t scratch_pte =
		vm->pte_encode(vm->scratch_page.daddr, I915_CACHE_LLC, 0);

	while (num_entries) {
		struct i915_page_table *pt = ppgtt->pd.page_table[pde++];
		unsigned int end = min(pte + num_entries, GEN6_PTES);
		gen6_pte_t *vaddr;

		num_entries -= end - pte;

		/* Note that the hw doesn't support removing PDE on the fly
		 * (they are cached inside the context with no means to
		 * invalidate the cache), so we can only reset the PTE
		 * entries back to scratch.
		 */

		vaddr = kmap_atomic_px(pt);
		do {
			vaddr[pte++] = scratch_pte;
		} while (pte < end);
		kunmap_atomic(vaddr);

		pte = 0;
	}
}

static void gen6_ppgtt_insert_entries(struct i915_address_space *vm,
				      struct sg_table *pages,
				      u64 start,
				      enum i915_cache_level cache_level,
				      u32 flags)
{
	struct i915_hw_ppgtt *ppgtt = i915_vm_to_ppgtt(vm);
	unsigned first_entry = start >> PAGE_SHIFT;
	unsigned act_pt = first_entry / GEN6_PTES;
	unsigned act_pte = first_entry % GEN6_PTES;
	const u32 pte_encode = vm->pte_encode(0, cache_level, flags);
	struct sgt_dma iter;
	gen6_pte_t *vaddr;

	vaddr = kmap_atomic_px(ppgtt->pd.page_table[act_pt]);
	iter.sg = pages->sgl;
	iter.dma = sg_dma_address(iter.sg);
	iter.max = iter.dma + iter.sg->length;
	do {
		vaddr[act_pte] = pte_encode | GEN6_PTE_ADDR_ENCODE(iter.dma);

		iter.dma += PAGE_SIZE;
		if (iter.dma == iter.max) {
			iter.sg = __sg_next(iter.sg);
			if (!iter.sg)
				break;

			iter.dma = sg_dma_address(iter.sg);
			iter.max = iter.dma + iter.sg->length;
		}

		if (++act_pte == GEN6_PTES) {
			kunmap_atomic(vaddr);
			vaddr = kmap_atomic_px(ppgtt->pd.page_table[++act_pt]);
			act_pte = 0;
		}
	} while (1);
	kunmap_atomic(vaddr);
}

static int gen6_alloc_va_range(struct i915_address_space *vm,
			       u64 start, u64 length)
{
	struct i915_hw_ppgtt *ppgtt = i915_vm_to_ppgtt(vm);
	struct i915_page_table *pt;
	u64 from = start;
	unsigned int pde;
	bool flush = false;

	gen6_for_each_pde(pt, &ppgtt->pd, start, length, pde) {
		if (pt == vm->scratch_pt) {
			pt = alloc_pt(vm);
			if (IS_ERR(pt))
				goto unwind_out;

			gen6_initialize_pt(vm, pt);
			ppgtt->pd.page_table[pde] = pt;
			gen6_write_pde(ppgtt, pde, pt);
			flush = true;
		}
	}

	if (flush) {
		mark_tlbs_dirty(ppgtt);
		wmb();
	}

	return 0;

unwind_out:
	gen6_ppgtt_clear_range(vm, from, start);
	return -ENOMEM;
}

static int gen6_init_scratch(struct i915_address_space *vm)
{
	int ret;

	ret = setup_scratch_page(vm, I915_GFP_DMA);
	if (ret)
		return ret;

	vm->scratch_pt = alloc_pt(vm);
	if (IS_ERR(vm->scratch_pt)) {
		cleanup_scratch_page(vm);
		return PTR_ERR(vm->scratch_pt);
	}

	gen6_initialize_pt(vm, vm->scratch_pt);

	return 0;
}

static void gen6_free_scratch(struct i915_address_space *vm)
{
	free_pt(vm, vm->scratch_pt);
	cleanup_scratch_page(vm);
}

static void gen6_ppgtt_cleanup(struct i915_address_space *vm)
{
	struct i915_hw_ppgtt *ppgtt = i915_vm_to_ppgtt(vm);
	struct i915_page_directory *pd = &ppgtt->pd;
	struct i915_page_table *pt;
	u32 pde;

	drm_mm_remove_node(&ppgtt->node);

	gen6_for_all_pdes(pt, pd, pde)
		if (pt != vm->scratch_pt)
			free_pt(vm, pt);

	gen6_free_scratch(vm);
}

static int gen6_ppgtt_allocate_page_directories(struct i915_hw_ppgtt *ppgtt)
{
	struct i915_address_space *vm = &ppgtt->base;
	struct drm_i915_private *dev_priv = ppgtt->base.i915;
	struct i915_ggtt *ggtt = &dev_priv->ggtt;
	int ret;

	/* PPGTT PDEs reside in the GGTT and consists of 512 entries. The
	 * allocator works in address space sizes, so it's multiplied by page
	 * size. We allocate at the top of the GTT to avoid fragmentation.
	 */
	BUG_ON(!drm_mm_initialized(&ggtt->base.mm));

	ret = gen6_init_scratch(vm);
	if (ret)
		return ret;

	ret = i915_gem_gtt_insert(&ggtt->base, &ppgtt->node,
				  GEN6_PD_SIZE, GEN6_PD_ALIGN,
				  I915_COLOR_UNEVICTABLE,
				  0, ggtt->base.total,
				  PIN_HIGH);
	if (ret)
		goto err_out;

	if (ppgtt->node.start < ggtt->mappable_end)
		DRM_DEBUG("Forced to use aperture for PDEs\n");

	ppgtt->pd.base.ggtt_offset =
		ppgtt->node.start / PAGE_SIZE * sizeof(gen6_pte_t);

	ppgtt->pd_addr = (gen6_pte_t __iomem *)ggtt->gsm +
		ppgtt->pd.base.ggtt_offset / sizeof(gen6_pte_t);

	return 0;

err_out:
	gen6_free_scratch(vm);
	return ret;
}

static int gen6_ppgtt_alloc(struct i915_hw_ppgtt *ppgtt)
{
	return gen6_ppgtt_allocate_page_directories(ppgtt);
}

static void gen6_scratch_va_range(struct i915_hw_ppgtt *ppgtt,
				  u64 start, u64 length)
{
	struct i915_page_table *unused;
	u32 pde;

	gen6_for_each_pde(unused, &ppgtt->pd, start, length, pde)
		ppgtt->pd.page_table[pde] = ppgtt->base.scratch_pt;
}

static int gen6_ppgtt_init(struct i915_hw_ppgtt *ppgtt)
{
	struct drm_i915_private *dev_priv = ppgtt->base.i915;
	struct i915_ggtt *ggtt = &dev_priv->ggtt;
	int ret;

	ppgtt->base.pte_encode = ggtt->base.pte_encode;
	if (intel_vgpu_active(dev_priv) || IS_GEN6(dev_priv))
		ppgtt->switch_mm = gen6_mm_switch;
	else if (IS_HASWELL(dev_priv))
		ppgtt->switch_mm = hsw_mm_switch;
	else if (IS_GEN7(dev_priv))
		ppgtt->switch_mm = gen7_mm_switch;
	else
		BUG();

	ret = gen6_ppgtt_alloc(ppgtt);
	if (ret)
		return ret;

	ppgtt->base.clear_range = gen6_ppgtt_clear_range;
	ppgtt->base.insert_entries = gen6_ppgtt_insert_entries;
	ppgtt->base.unbind_vma = ppgtt_unbind_vma;
	ppgtt->base.bind_vma = ppgtt_bind_vma;
	ppgtt->base.cleanup = gen6_ppgtt_cleanup;
	ppgtt->base.total = I915_PDES * GEN6_PTES * PAGE_SIZE;
	ppgtt->debug_dump = gen6_dump_ppgtt;

	gen6_scratch_va_range(ppgtt, 0, ppgtt->base.total);
	gen6_write_page_range(ppgtt, 0, ppgtt->base.total);

	ret = gen6_alloc_va_range(&ppgtt->base, 0, ppgtt->base.total);
	if (ret) {
		gen6_ppgtt_cleanup(&ppgtt->base);
		return ret;
	}

	DRM_DEBUG_DRIVER("Allocated pde space (%lldM) at GTT entry: %llx\n",
			 ppgtt->node.size >> 20,
			 ppgtt->node.start / PAGE_SIZE);

	DRM_DEBUG_DRIVER("Adding PPGTT at offset %x\n",
			 ppgtt->pd.base.ggtt_offset << 10);

	return 0;
}

static int __hw_ppgtt_init(struct i915_hw_ppgtt *ppgtt,
			   struct drm_i915_private *dev_priv)
{
	ppgtt->base.i915 = dev_priv;
	ppgtt->base.dma = &dev_priv->drm.pdev->dev;

	if (INTEL_INFO(dev_priv)->gen < 8)
		return gen6_ppgtt_init(ppgtt);
	else
		return gen8_ppgtt_init(ppgtt);
}

static void i915_address_space_init(struct i915_address_space *vm,
				    struct drm_i915_private *dev_priv,
				    const char *name)
{
	i915_gem_timeline_init(dev_priv, &vm->timeline, name);

	drm_mm_init(&vm->mm, 0, vm->total);
	vm->mm.head_node.color = I915_COLOR_UNEVICTABLE;

	INIT_LIST_HEAD(&vm->active_list);
	INIT_LIST_HEAD(&vm->inactive_list);
	INIT_LIST_HEAD(&vm->unbound_list);

	list_add_tail(&vm->global_link, &dev_priv->vm_list);
	pagevec_init(&vm->free_pages, false);
}

static void i915_address_space_fini(struct i915_address_space *vm)
{
	if (pagevec_count(&vm->free_pages))
		vm_free_pages_release(vm);

	i915_gem_timeline_fini(&vm->timeline);
	drm_mm_takedown(&vm->mm);
	list_del(&vm->global_link);
}

static void gtt_write_workarounds(struct drm_i915_private *dev_priv)
{
	/* This function is for gtt related workarounds. This function is
	 * called on driver load and after a GPU reset, so you can place
	 * workarounds here even if they get overwritten by GPU reset.
	 */
	/* WaIncreaseDefaultTLBEntries:chv,bdw,skl,bxt,kbl,glk */
	if (IS_BROADWELL(dev_priv))
		I915_WRITE(GEN8_L3_LRA_1_GPGPU, GEN8_L3_LRA_1_GPGPU_DEFAULT_VALUE_BDW);
	else if (IS_CHERRYVIEW(dev_priv))
		I915_WRITE(GEN8_L3_LRA_1_GPGPU, GEN8_L3_LRA_1_GPGPU_DEFAULT_VALUE_CHV);
	else if (IS_GEN9_BC(dev_priv))
		I915_WRITE(GEN8_L3_LRA_1_GPGPU, GEN9_L3_LRA_1_GPGPU_DEFAULT_VALUE_SKL);
	else if (IS_GEN9_LP(dev_priv))
		I915_WRITE(GEN8_L3_LRA_1_GPGPU, GEN9_L3_LRA_1_GPGPU_DEFAULT_VALUE_BXT);
}

int i915_ppgtt_init_hw(struct drm_i915_private *dev_priv)
{
	gtt_write_workarounds(dev_priv);

	/* In the case of execlists, PPGTT is enabled by the context descriptor
	 * and the PDPs are contained within the context itself.  We don't
	 * need to do anything here. */
	if (i915.enable_execlists)
		return 0;

	if (!USES_PPGTT(dev_priv))
		return 0;

	if (IS_GEN6(dev_priv))
		gen6_ppgtt_enable(dev_priv);
	else if (IS_GEN7(dev_priv))
		gen7_ppgtt_enable(dev_priv);
	else if (INTEL_GEN(dev_priv) >= 8)
		gen8_ppgtt_enable(dev_priv);
	else
		MISSING_CASE(INTEL_GEN(dev_priv));

	return 0;
}

struct i915_hw_ppgtt *
i915_ppgtt_create(struct drm_i915_private *dev_priv,
		  struct drm_i915_file_private *fpriv,
		  const char *name)
{
	struct i915_hw_ppgtt *ppgtt;
	int ret;

	ppgtt = kzalloc(sizeof(*ppgtt), GFP_KERNEL);
	if (!ppgtt)
		return ERR_PTR(-ENOMEM);

	ret = __hw_ppgtt_init(ppgtt, dev_priv);
	if (ret) {
		kfree(ppgtt);
		return ERR_PTR(ret);
	}

	kref_init(&ppgtt->ref);
	i915_address_space_init(&ppgtt->base, dev_priv, name);
	ppgtt->base.file = fpriv;

	trace_i915_ppgtt_create(&ppgtt->base);

	return ppgtt;
}

void i915_ppgtt_close(struct i915_address_space *vm)
{
	struct list_head *phases[] = {
		&vm->active_list,
		&vm->inactive_list,
		&vm->unbound_list,
		NULL,
	}, **phase;

	GEM_BUG_ON(vm->closed);
	vm->closed = true;

	for (phase = phases; *phase; phase++) {
		struct i915_vma *vma, *vn;

		list_for_each_entry_safe(vma, vn, *phase, vm_link)
			if (!i915_vma_is_closed(vma))
				i915_vma_close(vma);
	}
}

void i915_ppgtt_release(struct kref *kref)
{
	struct i915_hw_ppgtt *ppgtt =
		container_of(kref, struct i915_hw_ppgtt, ref);

	trace_i915_ppgtt_release(&ppgtt->base);

	/* vmas should already be unbound and destroyed */
	WARN_ON(!list_empty(&ppgtt->base.active_list));
	WARN_ON(!list_empty(&ppgtt->base.inactive_list));
	WARN_ON(!list_empty(&ppgtt->base.unbound_list));

	ppgtt->base.cleanup(&ppgtt->base);
	i915_address_space_fini(&ppgtt->base);
	kfree(ppgtt);
}

/* Certain Gen5 chipsets require require idling the GPU before
 * unmapping anything from the GTT when VT-d is enabled.
 */
static bool needs_idle_maps(struct drm_i915_private *dev_priv)
{
#ifdef CONFIG_INTEL_IOMMU
	/* Query intel_iommu to see if we need the workaround. Presumably that
	 * was loaded first.
	 */
	if (IS_GEN5(dev_priv) && IS_MOBILE(dev_priv) && intel_iommu_gfx_mapped)
		return true;
#endif
	return false;
}

void i915_check_and_clear_faults(struct drm_i915_private *dev_priv)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	if (INTEL_INFO(dev_priv)->gen < 6)
		return;

	for_each_engine(engine, dev_priv, id) {
		u32 fault_reg;
		fault_reg = I915_READ(RING_FAULT_REG(engine));
		if (fault_reg & RING_FAULT_VALID) {
			DRM_DEBUG_DRIVER("Unexpected fault\n"
					 "\tAddr: 0x%08lx\n"
					 "\tAddress space: %s\n"
					 "\tSource ID: %d\n"
					 "\tType: %d\n",
					 fault_reg & PAGE_MASK,
					 fault_reg & RING_FAULT_GTTSEL_MASK ? "GGTT" : "PPGTT",
					 RING_FAULT_SRCID(fault_reg),
					 RING_FAULT_FAULT_TYPE(fault_reg));
			I915_WRITE(RING_FAULT_REG(engine),
				   fault_reg & ~RING_FAULT_VALID);
		}
	}

	/* Engine specific init may not have been done till this point. */
	if (dev_priv->engine[RCS])
		POSTING_READ(RING_FAULT_REG(dev_priv->engine[RCS]));
}

void i915_gem_suspend_gtt_mappings(struct drm_i915_private *dev_priv)
{
	struct i915_ggtt *ggtt = &dev_priv->ggtt;

	/* Don't bother messing with faults pre GEN6 as we have little
	 * documentation supporting that it's a good idea.
	 */
	if (INTEL_GEN(dev_priv) < 6)
		return;

	i915_check_and_clear_faults(dev_priv);

	ggtt->base.clear_range(&ggtt->base, 0, ggtt->base.total);

	i915_ggtt_invalidate(dev_priv);
}

int i915_gem_gtt_prepare_pages(struct drm_i915_gem_object *obj,
			       struct sg_table *pages)
{
	do {
		if (dma_map_sg(&obj->base.dev->pdev->dev,
			       pages->sgl, pages->nents,
			       PCI_DMA_BIDIRECTIONAL))
			return 0;

		/* If the DMA remap fails, one cause can be that we have
		 * too many objects pinned in a small remapping table,
		 * such as swiotlb. Incrementally purge all other objects and
		 * try again - if there are no more pages to remove from
		 * the DMA remapper, i915_gem_shrink will return 0.
		 */
		GEM_BUG_ON(obj->mm.pages == pages);
	} while (i915_gem_shrink(to_i915(obj->base.dev),
				 obj->base.size >> PAGE_SHIFT,
				 I915_SHRINK_BOUND |
				 I915_SHRINK_UNBOUND |
				 I915_SHRINK_ACTIVE));

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
		(gen8_pte_t __iomem *)ggtt->gsm + (offset >> PAGE_SHIFT);

	gen8_set_pte(pte, gen8_pte_encode(addr, level));

	ggtt->invalidate(vm->i915);
}

static void gen8_ggtt_insert_entries(struct i915_address_space *vm,
				     struct sg_table *st,
				     u64 start,
				     enum i915_cache_level level,
				     u32 unused)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
	struct sgt_iter sgt_iter;
	gen8_pte_t __iomem *gtt_entries;
	const gen8_pte_t pte_encode = gen8_pte_encode(0, level);
	dma_addr_t addr;

	gtt_entries = (gen8_pte_t __iomem *)ggtt->gsm;
	gtt_entries += start >> PAGE_SHIFT;
	for_each_sgt_dma(addr, sgt_iter, st)
		gen8_set_pte(gtt_entries++, pte_encode | addr);

	wmb();

	/* This next bit makes the above posting read even more important. We
	 * want to flush the TLBs only after we're certain all the PTE updates
	 * have finished.
	 */
	ggtt->invalidate(vm->i915);
}

static void gen6_ggtt_insert_page(struct i915_address_space *vm,
				  dma_addr_t addr,
				  u64 offset,
				  enum i915_cache_level level,
				  u32 flags)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
	gen6_pte_t __iomem *pte =
		(gen6_pte_t __iomem *)ggtt->gsm + (offset >> PAGE_SHIFT);

	iowrite32(vm->pte_encode(addr, level, flags), pte);

	ggtt->invalidate(vm->i915);
}

/*
 * Binds an object into the global gtt with the specified cache level. The object
 * will be accessible to the GPU via commands whose operands reference offsets
 * within the global GTT as well as accessible by the GPU through the GMADR
 * mapped BAR (dev_priv->mm.gtt->gtt).
 */
static void gen6_ggtt_insert_entries(struct i915_address_space *vm,
				     struct sg_table *st,
				     u64 start,
				     enum i915_cache_level level,
				     u32 flags)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
	gen6_pte_t __iomem *entries = (gen6_pte_t __iomem *)ggtt->gsm;
	unsigned int i = start >> PAGE_SHIFT;
	struct sgt_iter iter;
	dma_addr_t addr;
	for_each_sgt_dma(addr, iter, st)
		iowrite32(vm->pte_encode(addr, level, flags), &entries[i++]);
	wmb();

	/* This next bit makes the above posting read even more important. We
	 * want to flush the TLBs only after we're certain all the PTE updates
	 * have finished.
	 */
	ggtt->invalidate(vm->i915);
}

static void nop_clear_range(struct i915_address_space *vm,
			    u64 start, u64 length)
{
}

static void gen8_ggtt_clear_range(struct i915_address_space *vm,
				  u64 start, u64 length)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
	unsigned first_entry = start >> PAGE_SHIFT;
	unsigned num_entries = length >> PAGE_SHIFT;
	const gen8_pte_t scratch_pte =
		gen8_pte_encode(vm->scratch_page.daddr, I915_CACHE_LLC);
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

static void gen6_ggtt_clear_range(struct i915_address_space *vm,
				  u64 start, u64 length)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
	unsigned first_entry = start >> PAGE_SHIFT;
	unsigned num_entries = length >> PAGE_SHIFT;
	gen6_pte_t scratch_pte, __iomem *gtt_base =
		(gen6_pte_t __iomem *)ggtt->gsm + first_entry;
	const int max_entries = ggtt_total_entries(ggtt) - first_entry;
	int i;

	if (WARN(num_entries > max_entries,
		 "First entry = %d; Num entries = %d (max=%d)\n",
		 first_entry, num_entries, max_entries))
		num_entries = max_entries;

	scratch_pte = vm->pte_encode(vm->scratch_page.daddr,
				     I915_CACHE_LLC, 0);

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
				     struct sg_table *pages,
				     u64 start,
				     enum i915_cache_level cache_level,
				     u32 unused)
{
	unsigned int flags = (cache_level == I915_CACHE_NONE) ?
		AGP_USER_MEMORY : AGP_USER_CACHED_MEMORY;

	intel_gtt_insert_sg_entries(pages, start >> PAGE_SHIFT, flags);
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
	u32 pte_flags;

	if (unlikely(!vma->pages)) {
		int ret = i915_get_ggtt_vma_pages(vma);
		if (ret)
			return ret;
	}

	/* Currently applicable only to VLV */
	pte_flags = 0;
	if (obj->gt_ro)
		pte_flags |= PTE_READ_ONLY;

	intel_runtime_pm_get(i915);
	vma->vm->insert_entries(vma->vm, vma->pages, vma->node.start,
				cache_level, pte_flags);
	intel_runtime_pm_put(i915);

	/*
	 * Without aliasing PPGTT there's no difference between
	 * GLOBAL/LOCAL_BIND, it's all the same ptes. Hence unconditionally
	 * upgrade to both bound if we bind either to avoid double-binding.
	 */
	vma->flags |= I915_VMA_GLOBAL_BIND | I915_VMA_LOCAL_BIND;

	return 0;
}

static void ggtt_unbind_vma(struct i915_vma *vma)
{
	struct drm_i915_private *i915 = vma->vm->i915;

	intel_runtime_pm_get(i915);
	vma->vm->clear_range(vma->vm, vma->node.start, vma->size);
	intel_runtime_pm_put(i915);
}

static int aliasing_gtt_bind_vma(struct i915_vma *vma,
				 enum i915_cache_level cache_level,
				 u32 flags)
{
	struct drm_i915_private *i915 = vma->vm->i915;
	u32 pte_flags;
	int ret;

	if (unlikely(!vma->pages)) {
		ret = i915_get_ggtt_vma_pages(vma);
		if (ret)
			return ret;
	}

	/* Currently applicable only to VLV */
	pte_flags = 0;
	if (vma->obj->gt_ro)
		pte_flags |= PTE_READ_ONLY;

	if (flags & I915_VMA_LOCAL_BIND) {
		struct i915_hw_ppgtt *appgtt = i915->mm.aliasing_ppgtt;

		if (appgtt->base.allocate_va_range) {
			ret = appgtt->base.allocate_va_range(&appgtt->base,
							     vma->node.start,
							     vma->node.size);
			if (ret)
				return ret;
		}

		appgtt->base.insert_entries(&appgtt->base,
					    vma->pages, vma->node.start,
					    cache_level, pte_flags);
	}

	if (flags & I915_VMA_GLOBAL_BIND) {
		intel_runtime_pm_get(i915);
		vma->vm->insert_entries(vma->vm,
					vma->pages, vma->node.start,
					cache_level, pte_flags);
		intel_runtime_pm_put(i915);
	}

	return 0;
}

static void aliasing_gtt_unbind_vma(struct i915_vma *vma)
{
	struct drm_i915_private *i915 = vma->vm->i915;

	if (vma->flags & I915_VMA_GLOBAL_BIND) {
		intel_runtime_pm_get(i915);
		vma->vm->clear_range(vma->vm, vma->node.start, vma->size);
		intel_runtime_pm_put(i915);
	}

	if (vma->flags & I915_VMA_LOCAL_BIND) {
		struct i915_address_space *vm = &i915->mm.aliasing_ppgtt->base;

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
		if (i915_gem_wait_for_idle(dev_priv, I915_WAIT_LOCKED)) {
			DRM_ERROR("Failed to wait for idle; VT'd may hang.\n");
			/* Wait a bit, in hopes it avoids the hang */
			udelay(10);
		}
	}

	dma_unmap_sg(kdev, pages->sgl, pages->nents, PCI_DMA_BIDIRECTIONAL);
}

static void i915_gtt_color_adjust(const struct drm_mm_node *node,
				  unsigned long color,
				  u64 *start,
				  u64 *end)
{
	if (node->allocated && node->color != color)
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

int i915_gem_init_aliasing_ppgtt(struct drm_i915_private *i915)
{
	struct i915_ggtt *ggtt = &i915->ggtt;
	struct i915_hw_ppgtt *ppgtt;
	int err;

	ppgtt = i915_ppgtt_create(i915, ERR_PTR(-EPERM), "[alias]");
	if (IS_ERR(ppgtt))
		return PTR_ERR(ppgtt);

	if (WARN_ON(ppgtt->base.total < ggtt->base.total)) {
		err = -ENODEV;
		goto err_ppgtt;
	}

	if (ppgtt->base.allocate_va_range) {
		/* Note we only pre-allocate as far as the end of the global
		 * GTT. On 48b / 4-level page-tables, the difference is very,
		 * very significant! We have to preallocate as GVT/vgpu does
		 * not like the page directory disappearing.
		 */
		err = ppgtt->base.allocate_va_range(&ppgtt->base,
						    0, ggtt->base.total);
		if (err)
			goto err_ppgtt;
	}

	i915->mm.aliasing_ppgtt = ppgtt;

	WARN_ON(ggtt->base.bind_vma != ggtt_bind_vma);
	ggtt->base.bind_vma = aliasing_gtt_bind_vma;

	WARN_ON(ggtt->base.unbind_vma != ggtt_unbind_vma);
	ggtt->base.unbind_vma = aliasing_gtt_unbind_vma;

	return 0;

err_ppgtt:
	i915_ppgtt_put(ppgtt);
	return err;
}

void i915_gem_fini_aliasing_ppgtt(struct drm_i915_private *i915)
{
	struct i915_ggtt *ggtt = &i915->ggtt;
	struct i915_hw_ppgtt *ppgtt;

	ppgtt = fetch_and_zero(&i915->mm.aliasing_ppgtt);
	if (!ppgtt)
		return;

	i915_ppgtt_put(ppgtt);

	ggtt->base.bind_vma = ggtt_bind_vma;
	ggtt->base.unbind_vma = ggtt_unbind_vma;
}

int i915_gem_init_ggtt(struct drm_i915_private *dev_priv)
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
	struct i915_ggtt *ggtt = &dev_priv->ggtt;
	unsigned long hole_start, hole_end;
	struct drm_mm_node *entry;
	int ret;

	ret = intel_vgt_balloon(dev_priv);
	if (ret)
		return ret;

	/* Reserve a mappable slot for our lockless error capture */
	ret = drm_mm_insert_node_in_range(&ggtt->base.mm, &ggtt->error_capture,
					  PAGE_SIZE, 0, I915_COLOR_UNEVICTABLE,
					  0, ggtt->mappable_end,
					  DRM_MM_INSERT_LOW);
	if (ret)
		return ret;

	/* Clear any non-preallocated blocks */
	drm_mm_for_each_hole(entry, &ggtt->base.mm, hole_start, hole_end) {
		DRM_DEBUG_KMS("clearing unused GTT space: [%lx, %lx]\n",
			      hole_start, hole_end);
		ggtt->base.clear_range(&ggtt->base, hole_start,
				       hole_end - hole_start);
	}

	/* And finally clear the reserved guard page */
	ggtt->base.clear_range(&ggtt->base,
			       ggtt->base.total - PAGE_SIZE, PAGE_SIZE);

	if (USES_PPGTT(dev_priv) && !USES_FULL_PPGTT(dev_priv)) {
		ret = i915_gem_init_aliasing_ppgtt(dev_priv);
		if (ret)
			goto err;
	}

	return 0;

err:
	drm_mm_remove_node(&ggtt->error_capture);
	return ret;
}

/**
 * i915_ggtt_cleanup_hw - Clean up GGTT hardware initialization
 * @dev_priv: i915 device
 */
void i915_ggtt_cleanup_hw(struct drm_i915_private *dev_priv)
{
	struct i915_ggtt *ggtt = &dev_priv->ggtt;
	struct i915_vma *vma, *vn;

	ggtt->base.closed = true;

	mutex_lock(&dev_priv->drm.struct_mutex);
	WARN_ON(!list_empty(&ggtt->base.active_list));
	list_for_each_entry_safe(vma, vn, &ggtt->base.inactive_list, vm_link)
		WARN_ON(i915_vma_unbind(vma));
	mutex_unlock(&dev_priv->drm.struct_mutex);

	i915_gem_cleanup_stolen(&dev_priv->drm);

	mutex_lock(&dev_priv->drm.struct_mutex);
	i915_gem_fini_aliasing_ppgtt(dev_priv);

	if (drm_mm_node_allocated(&ggtt->error_capture))
		drm_mm_remove_node(&ggtt->error_capture);

	if (drm_mm_initialized(&ggtt->base.mm)) {
		intel_vgt_deballoon(dev_priv);
		i915_address_space_fini(&ggtt->base);
	}

	ggtt->base.cleanup(&ggtt->base);
	mutex_unlock(&dev_priv->drm.struct_mutex);

	arch_phys_wc_del(ggtt->mtrr);
	io_mapping_fini(&ggtt->mappable);
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
	/* Limit 32b platforms to a 2GB GGTT: 4 << 20 / pte size * PAGE_SIZE */
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

static size_t gen6_get_stolen_size(u16 snb_gmch_ctl)
{
	snb_gmch_ctl >>= SNB_GMCH_GMS_SHIFT;
	snb_gmch_ctl &= SNB_GMCH_GMS_MASK;
	return snb_gmch_ctl << 25; /* 32 MB units */
}

static size_t gen8_get_stolen_size(u16 bdw_gmch_ctl)
{
	bdw_gmch_ctl >>= BDW_GMCH_GMS_SHIFT;
	bdw_gmch_ctl &= BDW_GMCH_GMS_MASK;
	return bdw_gmch_ctl << 25; /* 32 MB units */
}

static size_t chv_get_stolen_size(u16 gmch_ctrl)
{
	gmch_ctrl >>= SNB_GMCH_GMS_SHIFT;
	gmch_ctrl &= SNB_GMCH_GMS_MASK;

	/*
	 * 0x0  to 0x10: 32MB increments starting at 0MB
	 * 0x11 to 0x16: 4MB increments starting at 8MB
	 * 0x17 to 0x1d: 4MB increments start at 36MB
	 */
	if (gmch_ctrl < 0x11)
		return gmch_ctrl << 25;
	else if (gmch_ctrl < 0x17)
		return (gmch_ctrl - 0x11 + 2) << 22;
	else
		return (gmch_ctrl - 0x17 + 9) << 22;
}

static size_t gen9_get_stolen_size(u16 gen9_gmch_ctl)
{
	gen9_gmch_ctl >>= BDW_GMCH_GMS_SHIFT;
	gen9_gmch_ctl &= BDW_GMCH_GMS_MASK;

	if (gen9_gmch_ctl < 0xf0)
		return gen9_gmch_ctl << 25; /* 32 MB units */
	else
		/* 4MB increments starting at 0xf0 for 4MB */
		return (gen9_gmch_ctl - 0xf0 + 1) << 22;
}

static int ggtt_probe_common(struct i915_ggtt *ggtt, u64 size)
{
	struct drm_i915_private *dev_priv = ggtt->base.i915;
	struct pci_dev *pdev = dev_priv->drm.pdev;
	phys_addr_t phys_addr;
	int ret;

	/* For Modern GENs the PTEs and register space are split in the BAR */
	phys_addr = pci_resource_start(pdev, 0) + pci_resource_len(pdev, 0) / 2;

	/*
	 * On BXT writes larger than 64 bit to the GTT pagetable range will be
	 * dropped. For WC mappings in general we have 64 byte burst writes
	 * when the WC buffer is flushed, so we can't use it, but have to
	 * resort to an uncached mapping. The WC issue is easily caught by the
	 * readback check when writing GTT PTE entries.
	 */
	if (IS_GEN9_LP(dev_priv))
		ggtt->gsm = ioremap_nocache(phys_addr, size);
	else
		ggtt->gsm = ioremap_wc(phys_addr, size);
	if (!ggtt->gsm) {
		DRM_ERROR("Failed to map the ggtt page table\n");
		return -ENOMEM;
	}

	ret = setup_scratch_page(&ggtt->base, GFP_DMA32);
	if (ret) {
		DRM_ERROR("Scratch setup failed\n");
		/* iounmap will also get called at remove, but meh */
		iounmap(ggtt->gsm);
		return ret;
	}

	return 0;
}

/* The GGTT and PPGTT need a private PPAT setup in order to handle cacheability
 * bits. When using advanced contexts each context stores its own PAT, but
 * writing this data shouldn't be harmful even in those cases. */
static void bdw_setup_private_ppat(struct drm_i915_private *dev_priv)
{
	u64 pat;

	pat = GEN8_PPAT(0, GEN8_PPAT_WB | GEN8_PPAT_LLC)     | /* for normal objects, no eLLC */
	      GEN8_PPAT(1, GEN8_PPAT_WC | GEN8_PPAT_LLCELLC) | /* for something pointing to ptes? */
	      GEN8_PPAT(2, GEN8_PPAT_WT | GEN8_PPAT_LLCELLC) | /* for scanout with eLLC */
	      GEN8_PPAT(3, GEN8_PPAT_UC)                     | /* Uncached objects, mostly for scanout */
	      GEN8_PPAT(4, GEN8_PPAT_WB | GEN8_PPAT_LLCELLC | GEN8_PPAT_AGE(0)) |
	      GEN8_PPAT(5, GEN8_PPAT_WB | GEN8_PPAT_LLCELLC | GEN8_PPAT_AGE(1)) |
	      GEN8_PPAT(6, GEN8_PPAT_WB | GEN8_PPAT_LLCELLC | GEN8_PPAT_AGE(2)) |
	      GEN8_PPAT(7, GEN8_PPAT_WB | GEN8_PPAT_LLCELLC | GEN8_PPAT_AGE(3));

	if (!USES_PPGTT(dev_priv))
		/* Spec: "For GGTT, there is NO pat_sel[2:0] from the entry,
		 * so RTL will always use the value corresponding to
		 * pat_sel = 000".
		 * So let's disable cache for GGTT to avoid screen corruptions.
		 * MOCS still can be used though.
		 * - System agent ggtt writes (i.e. cpu gtt mmaps) already work
		 * before this patch, i.e. the same uncached + snooping access
		 * like on gen6/7 seems to be in effect.
		 * - So this just fixes blitter/render access. Again it looks
		 * like it's not just uncached access, but uncached + snooping.
		 * So we can still hold onto all our assumptions wrt cpu
		 * clflushing on LLC machines.
		 */
		pat = GEN8_PPAT(0, GEN8_PPAT_UC);

	/* XXX: spec defines this as 2 distinct registers. It's unclear if a 64b
	 * write would work. */
	I915_WRITE(GEN8_PRIVATE_PAT_LO, pat);
	I915_WRITE(GEN8_PRIVATE_PAT_HI, pat >> 32);
}

static void chv_setup_private_ppat(struct drm_i915_private *dev_priv)
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

	I915_WRITE(GEN8_PRIVATE_PAT_LO, pat);
	I915_WRITE(GEN8_PRIVATE_PAT_HI, pat >> 32);
}

static void gen6_gmch_remove(struct i915_address_space *vm)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);

	iounmap(ggtt->gsm);
	cleanup_scratch_page(vm);
}

static int gen8_gmch_probe(struct i915_ggtt *ggtt)
{
	struct drm_i915_private *dev_priv = ggtt->base.i915;
	struct pci_dev *pdev = dev_priv->drm.pdev;
	unsigned int size;
	u16 snb_gmch_ctl;

	/* TODO: We're not aware of mappable constraints on gen8 yet */
	ggtt->mappable_base = pci_resource_start(pdev, 2);
	ggtt->mappable_end = pci_resource_len(pdev, 2);

	if (!pci_set_dma_mask(pdev, DMA_BIT_MASK(39)))
		pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(39));

	pci_read_config_word(pdev, SNB_GMCH_CTRL, &snb_gmch_ctl);

	if (INTEL_GEN(dev_priv) >= 9) {
		ggtt->stolen_size = gen9_get_stolen_size(snb_gmch_ctl);
		size = gen8_get_total_gtt_size(snb_gmch_ctl);
	} else if (IS_CHERRYVIEW(dev_priv)) {
		ggtt->stolen_size = chv_get_stolen_size(snb_gmch_ctl);
		size = chv_get_total_gtt_size(snb_gmch_ctl);
	} else {
		ggtt->stolen_size = gen8_get_stolen_size(snb_gmch_ctl);
		size = gen8_get_total_gtt_size(snb_gmch_ctl);
	}

	ggtt->base.total = (size / sizeof(gen8_pte_t)) << PAGE_SHIFT;

	if (IS_CHERRYVIEW(dev_priv) || IS_GEN9_LP(dev_priv))
		chv_setup_private_ppat(dev_priv);
	else
		bdw_setup_private_ppat(dev_priv);

	ggtt->base.cleanup = gen6_gmch_remove;
	ggtt->base.bind_vma = ggtt_bind_vma;
	ggtt->base.unbind_vma = ggtt_unbind_vma;
	ggtt->base.insert_page = gen8_ggtt_insert_page;
	ggtt->base.clear_range = nop_clear_range;
	if (!USES_FULL_PPGTT(dev_priv) || intel_scanout_needs_vtd_wa(dev_priv))
		ggtt->base.clear_range = gen8_ggtt_clear_range;

	ggtt->base.insert_entries = gen8_ggtt_insert_entries;

	ggtt->invalidate = gen6_ggtt_invalidate;

	return ggtt_probe_common(ggtt, size);
}

static int gen6_gmch_probe(struct i915_ggtt *ggtt)
{
	struct drm_i915_private *dev_priv = ggtt->base.i915;
	struct pci_dev *pdev = dev_priv->drm.pdev;
	unsigned int size;
	u16 snb_gmch_ctl;

	ggtt->mappable_base = pci_resource_start(pdev, 2);
	ggtt->mappable_end = pci_resource_len(pdev, 2);

	/* 64/512MB is the current min/max we actually know of, but this is just
	 * a coarse sanity check.
	 */
	if (ggtt->mappable_end < (64<<20) || ggtt->mappable_end > (512<<20)) {
		DRM_ERROR("Unknown GMADR size (%llx)\n", ggtt->mappable_end);
		return -ENXIO;
	}

	if (!pci_set_dma_mask(pdev, DMA_BIT_MASK(40)))
		pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(40));
	pci_read_config_word(pdev, SNB_GMCH_CTRL, &snb_gmch_ctl);

	ggtt->stolen_size = gen6_get_stolen_size(snb_gmch_ctl);

	size = gen6_get_total_gtt_size(snb_gmch_ctl);
	ggtt->base.total = (size / sizeof(gen6_pte_t)) << PAGE_SHIFT;

	ggtt->base.clear_range = gen6_ggtt_clear_range;
	ggtt->base.insert_page = gen6_ggtt_insert_page;
	ggtt->base.insert_entries = gen6_ggtt_insert_entries;
	ggtt->base.bind_vma = ggtt_bind_vma;
	ggtt->base.unbind_vma = ggtt_unbind_vma;
	ggtt->base.cleanup = gen6_gmch_remove;

	ggtt->invalidate = gen6_ggtt_invalidate;

	if (HAS_EDRAM(dev_priv))
		ggtt->base.pte_encode = iris_pte_encode;
	else if (IS_HASWELL(dev_priv))
		ggtt->base.pte_encode = hsw_pte_encode;
	else if (IS_VALLEYVIEW(dev_priv))
		ggtt->base.pte_encode = byt_pte_encode;
	else if (INTEL_GEN(dev_priv) >= 7)
		ggtt->base.pte_encode = ivb_pte_encode;
	else
		ggtt->base.pte_encode = snb_pte_encode;

	return ggtt_probe_common(ggtt, size);
}

static void i915_gmch_remove(struct i915_address_space *vm)
{
	intel_gmch_remove();
}

static int i915_gmch_probe(struct i915_ggtt *ggtt)
{
	struct drm_i915_private *dev_priv = ggtt->base.i915;
	int ret;

	ret = intel_gmch_probe(dev_priv->bridge_dev, dev_priv->drm.pdev, NULL);
	if (!ret) {
		DRM_ERROR("failed to set up gmch\n");
		return -EIO;
	}

	intel_gtt_get(&ggtt->base.total,
		      &ggtt->stolen_size,
		      &ggtt->mappable_base,
		      &ggtt->mappable_end);

	ggtt->do_idle_maps = needs_idle_maps(dev_priv);
	ggtt->base.insert_page = i915_ggtt_insert_page;
	ggtt->base.insert_entries = i915_ggtt_insert_entries;
	ggtt->base.clear_range = i915_ggtt_clear_range;
	ggtt->base.bind_vma = ggtt_bind_vma;
	ggtt->base.unbind_vma = ggtt_unbind_vma;
	ggtt->base.cleanup = i915_gmch_remove;

	ggtt->invalidate = gmch_ggtt_invalidate;

	if (unlikely(ggtt->do_idle_maps))
		DRM_INFO("applying Ironlake quirks for intel_iommu\n");

	return 0;
}

/**
 * i915_ggtt_probe_hw - Probe GGTT hardware location
 * @dev_priv: i915 device
 */
int i915_ggtt_probe_hw(struct drm_i915_private *dev_priv)
{
	struct i915_ggtt *ggtt = &dev_priv->ggtt;
	int ret;

	ggtt->base.i915 = dev_priv;
	ggtt->base.dma = &dev_priv->drm.pdev->dev;

	if (INTEL_GEN(dev_priv) <= 5)
		ret = i915_gmch_probe(ggtt);
	else if (INTEL_GEN(dev_priv) < 8)
		ret = gen6_gmch_probe(ggtt);
	else
		ret = gen8_gmch_probe(ggtt);
	if (ret)
		return ret;

	/* Trim the GGTT to fit the GuC mappable upper range (when enabled).
	 * This is easier than doing range restriction on the fly, as we
	 * currently don't have any bits spare to pass in this upper
	 * restriction!
	 */
	if (HAS_GUC(dev_priv) && i915.enable_guc_loading) {
		ggtt->base.total = min_t(u64, ggtt->base.total, GUC_GGTT_TOP);
		ggtt->mappable_end = min(ggtt->mappable_end, ggtt->base.total);
	}

	if ((ggtt->base.total - 1) >> 32) {
		DRM_ERROR("We never expected a Global GTT with more than 32bits"
			  " of address space! Found %lldM!\n",
			  ggtt->base.total >> 20);
		ggtt->base.total = 1ULL << 32;
		ggtt->mappable_end = min(ggtt->mappable_end, ggtt->base.total);
	}

	if (ggtt->mappable_end > ggtt->base.total) {
		DRM_ERROR("mappable aperture extends past end of GGTT,"
			  " aperture=%llx, total=%llx\n",
			  ggtt->mappable_end, ggtt->base.total);
		ggtt->mappable_end = ggtt->base.total;
	}

	/* GMADR is the PCI mmio aperture into the global GTT. */
	DRM_INFO("Memory usable by graphics device = %lluM\n",
		 ggtt->base.total >> 20);
	DRM_DEBUG_DRIVER("GMADR size = %lldM\n", ggtt->mappable_end >> 20);
	DRM_DEBUG_DRIVER("GTT stolen size = %uM\n", ggtt->stolen_size >> 20);
#ifdef CONFIG_INTEL_IOMMU
	if (intel_iommu_gfx_mapped)
		DRM_INFO("VT-d active for gfx access\n");
#endif

	return 0;
}

/**
 * i915_ggtt_init_hw - Initialize GGTT hardware
 * @dev_priv: i915 device
 */
int i915_ggtt_init_hw(struct drm_i915_private *dev_priv)
{
	struct i915_ggtt *ggtt = &dev_priv->ggtt;
	int ret;

	INIT_LIST_HEAD(&dev_priv->vm_list);

	/* Note that we use page colouring to enforce a guard page at the
	 * end of the address space. This is required as the CS may prefetch
	 * beyond the end of the batch buffer, across the page boundary,
	 * and beyond the end of the GTT if we do not provide a guard.
	 */
	mutex_lock(&dev_priv->drm.struct_mutex);
	i915_address_space_init(&ggtt->base, dev_priv, "[global]");
	if (!HAS_LLC(dev_priv) && !USES_PPGTT(dev_priv))
		ggtt->base.mm.color_adjust = i915_gtt_color_adjust;
	mutex_unlock(&dev_priv->drm.struct_mutex);

	if (!io_mapping_init_wc(&dev_priv->ggtt.mappable,
				dev_priv->ggtt.mappable_base,
				dev_priv->ggtt.mappable_end)) {
		ret = -EIO;
		goto out_gtt_cleanup;
	}

	ggtt->mtrr = arch_phys_wc_add(ggtt->mappable_base, ggtt->mappable_end);

	/*
	 * Initialise stolen early so that we may reserve preallocated
	 * objects for the BIOS to KMS transition.
	 */
	ret = i915_gem_init_stolen(dev_priv);
	if (ret)
		goto out_gtt_cleanup;

	return 0;

out_gtt_cleanup:
	ggtt->base.cleanup(&ggtt->base);
	return ret;
}

int i915_ggtt_enable_hw(struct drm_i915_private *dev_priv)
{
	if (INTEL_GEN(dev_priv) < 6 && !intel_enable_gtt())
		return -EIO;

	return 0;
}

void i915_ggtt_enable_guc(struct drm_i915_private *i915)
{
	i915->ggtt.invalidate = guc_ggtt_invalidate;
}

void i915_ggtt_disable_guc(struct drm_i915_private *i915)
{
	i915->ggtt.invalidate = gen6_ggtt_invalidate;
}

void i915_gem_restore_gtt_mappings(struct drm_i915_private *dev_priv)
{
	struct i915_ggtt *ggtt = &dev_priv->ggtt;
	struct drm_i915_gem_object *obj, *on;

	i915_check_and_clear_faults(dev_priv);

	/* First fill our portion of the GTT with scratch pages */
	ggtt->base.clear_range(&ggtt->base, 0, ggtt->base.total);

	ggtt->base.closed = true; /* skip rewriting PTE on VMA unbind */

	/* clflush objects bound into the GGTT and rebind them. */
	list_for_each_entry_safe(obj, on,
				 &dev_priv->mm.bound_list, global_link) {
		bool ggtt_bound = false;
		struct i915_vma *vma;

		list_for_each_entry(vma, &obj->vma_list, obj_link) {
			if (vma->vm != &ggtt->base)
				continue;

			if (!i915_vma_unbind(vma))
				continue;

			WARN_ON(i915_vma_bind(vma, obj->cache_level,
					      PIN_UPDATE));
			ggtt_bound = true;
		}

		if (ggtt_bound)
			WARN_ON(i915_gem_object_set_to_gtt_domain(obj, false));
	}

	ggtt->base.closed = false;

	if (INTEL_GEN(dev_priv) >= 8) {
		if (IS_CHERRYVIEW(dev_priv) || IS_GEN9_LP(dev_priv))
			chv_setup_private_ppat(dev_priv);
		else
			bdw_setup_private_ppat(dev_priv);

		return;
	}

	if (USES_PPGTT(dev_priv)) {
		struct i915_address_space *vm;

		list_for_each_entry(vm, &dev_priv->vm_list, global_link) {
			struct i915_hw_ppgtt *ppgtt;

			if (i915_is_ggtt(vm))
				ppgtt = dev_priv->mm.aliasing_ppgtt;
			else
				ppgtt = i915_vm_to_ppgtt(vm);

			gen6_write_page_range(ppgtt, 0, ppgtt->base.total);
		}
	}

	i915_ggtt_invalidate(dev_priv);
}

static struct scatterlist *
rotate_pages(const dma_addr_t *in, unsigned int offset,
	     unsigned int width, unsigned int height,
	     unsigned int stride,
	     struct sg_table *st, struct scatterlist *sg)
{
	unsigned int column, row;
	unsigned int src_idx;

	for (column = 0; column < width; column++) {
		src_idx = stride * (height - 1) + column;
		for (row = 0; row < height; row++) {
			st->nents++;
			/* We don't need the pages, but need to initialize
			 * the entries so the sg list can be happily traversed.
			 * The only thing we need are DMA addresses.
			 */
			sg_set_page(sg, NULL, PAGE_SIZE, 0);
			sg_dma_address(sg) = in[offset + src_idx];
			sg_dma_len(sg) = PAGE_SIZE;
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
	const unsigned long n_pages = obj->base.size / PAGE_SIZE;
	unsigned int size = intel_rotation_info_size(rot_info);
	struct sgt_iter sgt_iter;
	dma_addr_t dma_addr;
	unsigned long i;
	dma_addr_t *page_addr_list;
	struct sg_table *st;
	struct scatterlist *sg;
	int ret = -ENOMEM;

	/* Allocate a temporary list of source pages for random access. */
	page_addr_list = drm_malloc_gfp(n_pages,
					sizeof(dma_addr_t),
					GFP_TEMPORARY);
	if (!page_addr_list)
		return ERR_PTR(ret);

	/* Allocate target SG list. */
	st = kmalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		goto err_st_alloc;

	ret = sg_alloc_table(st, size, GFP_KERNEL);
	if (ret)
		goto err_sg_alloc;

	/* Populate source page list from the object. */
	i = 0;
	for_each_sgt_dma(dma_addr, sgt_iter, obj->mm.pages)
		page_addr_list[i++] = dma_addr;

	GEM_BUG_ON(i != n_pages);
	st->nents = 0;
	sg = st->sgl;

	for (i = 0 ; i < ARRAY_SIZE(rot_info->plane); i++) {
		sg = rotate_pages(page_addr_list, rot_info->plane[i].offset,
				  rot_info->plane[i].width, rot_info->plane[i].height,
				  rot_info->plane[i].stride, st, sg);
	}

	DRM_DEBUG_KMS("Created rotated page mapping for object size %zu (%ux%u tiles, %u pages)\n",
		      obj->base.size, rot_info->plane[0].width, rot_info->plane[0].height, size);

	drm_free_large(page_addr_list);

	return st;

err_sg_alloc:
	kfree(st);
err_st_alloc:
	drm_free_large(page_addr_list);

	DRM_DEBUG_KMS("Failed to create rotated mapping for object size %zu! (%ux%u tiles, %u pages)\n",
		      obj->base.size, rot_info->plane[0].width, rot_info->plane[0].height, size);

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
	case I915_GGTT_VIEW_NORMAL:
		vma->pages = vma->obj->mm.pages;
		return 0;

	case I915_GGTT_VIEW_ROTATED:
		vma->pages =
			intel_rotate_pages(&vma->ggtt_view.rotated, vma->obj);
		break;

	case I915_GGTT_VIEW_PARTIAL:
		vma->pages = intel_partial_pages(&vma->ggtt_view, vma->obj);
		break;

	default:
		WARN_ONCE(1, "GGTT view %u not implemented!\n",
			  vma->ggtt_view.type);
		return -EINVAL;
	}

	ret = 0;
	if (unlikely(IS_ERR(vma->pages))) {
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
	GEM_BUG_ON(vm == &vm->i915->mm.aliasing_ppgtt->base);
	GEM_BUG_ON(drm_mm_node_allocated(node));

	node->size = size;
	node->start = offset;
	node->color = color;

	err = drm_mm_reserve_node(&vm->mm, node);
	if (err != -ENOSPC)
		return err;

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

	lockdep_assert_held(&vm->i915->drm.struct_mutex);
	GEM_BUG_ON(!size);
	GEM_BUG_ON(!IS_ALIGNED(size, I915_GTT_PAGE_SIZE));
	GEM_BUG_ON(alignment && !is_power_of_2(alignment));
	GEM_BUG_ON(alignment && !IS_ALIGNED(alignment, I915_GTT_MIN_ALIGNMENT));
	GEM_BUG_ON(start >= end);
	GEM_BUG_ON(start > 0  && !IS_ALIGNED(start, I915_GTT_PAGE_SIZE));
	GEM_BUG_ON(end < U64_MAX && !IS_ALIGNED(end, I915_GTT_PAGE_SIZE));
	GEM_BUG_ON(vm == &vm->i915->mm.aliasing_ppgtt->base);
	GEM_BUG_ON(drm_mm_node_allocated(node));

	if (unlikely(range_overflows(start, size, end)))
		return -ENOSPC;

	if (unlikely(round_up(start, alignment) > round_down(end - size, alignment)))
		return -ENOSPC;

	mode = DRM_MM_INSERT_BEST;
	if (flags & PIN_HIGH)
		mode = DRM_MM_INSERT_HIGH;
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

	/* No free space, pick a slot at random.
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
