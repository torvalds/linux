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

static inline struct i915_ggtt *
i915_vm_to_ggtt(struct i915_address_space *vm)
{
	GEM_BUG_ON(!i915_is_ggtt(vm));
	return container_of(vm, struct i915_ggtt, base);
}

static int
i915_get_ggtt_vma_pages(struct i915_vma *vma);

const struct i915_ggtt_view i915_ggtt_view_normal = {
	.type = I915_GGTT_VIEW_NORMAL,
};
const struct i915_ggtt_view i915_ggtt_view_rotated = {
	.type = I915_GGTT_VIEW_ROTATED,
};

int intel_sanitize_enable_ppgtt(struct drm_i915_private *dev_priv,
			       	int enable_ppgtt)
{
	bool has_aliasing_ppgtt;
	bool has_full_ppgtt;
	bool has_full_48bit_ppgtt;

	has_aliasing_ppgtt = INTEL_GEN(dev_priv) >= 6;
	has_full_ppgtt = INTEL_GEN(dev_priv) >= 7;
	has_full_48bit_ppgtt =
	       	IS_BROADWELL(dev_priv) || INTEL_GEN(dev_priv) >= 9;

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
	u32 pte_flags = 0;

	vma->pages = vma->obj->mm.pages;

	/* Currently applicable only to VLV */
	if (vma->obj->gt_ro)
		pte_flags |= PTE_READ_ONLY;

	vma->vm->insert_entries(vma->vm, vma->pages, vma->node.start,
				cache_level, pte_flags);

	return 0;
}

static void ppgtt_unbind_vma(struct i915_vma *vma)
{
	vma->vm->clear_range(vma->vm,
			     vma->node.start,
			     vma->size);
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

static int __setup_page_dma(struct drm_device *dev,
			    struct i915_page_dma *p, gfp_t flags)
{
	struct device *kdev = &dev->pdev->dev;

	p->page = alloc_page(flags);
	if (!p->page)
		return -ENOMEM;

	p->daddr = dma_map_page(kdev,
				p->page, 0, 4096, PCI_DMA_BIDIRECTIONAL);

	if (dma_mapping_error(kdev, p->daddr)) {
		__free_page(p->page);
		return -EINVAL;
	}

	return 0;
}

static int setup_page_dma(struct drm_device *dev, struct i915_page_dma *p)
{
	return __setup_page_dma(dev, p, I915_GFP_DMA);
}

static void cleanup_page_dma(struct drm_device *dev, struct i915_page_dma *p)
{
	struct pci_dev *pdev = dev->pdev;

	if (WARN_ON(!p->page))
		return;

	dma_unmap_page(&pdev->dev, p->daddr, 4096, PCI_DMA_BIDIRECTIONAL);
	__free_page(p->page);
	memset(p, 0, sizeof(*p));
}

static void *kmap_page_dma(struct i915_page_dma *p)
{
	return kmap_atomic(p->page);
}

/* We use the flushing unmap only with ppgtt structures:
 * page directories, page tables and scratch pages.
 */
static void kunmap_page_dma(struct drm_i915_private *dev_priv, void *vaddr)
{
	/* There are only few exceptions for gen >=6. chv and bxt.
	 * And we are not sure about the latter so play safe for now.
	 */
	if (IS_CHERRYVIEW(dev_priv) || IS_BROXTON(dev_priv))
		drm_clflush_virt_range(vaddr, PAGE_SIZE);

	kunmap_atomic(vaddr);
}

#define kmap_px(px) kmap_page_dma(px_base(px))
#define kunmap_px(ppgtt, vaddr) \
		kunmap_page_dma(to_i915((ppgtt)->base.dev), (vaddr))

#define setup_px(dev, px) setup_page_dma((dev), px_base(px))
#define cleanup_px(dev, px) cleanup_page_dma((dev), px_base(px))
#define fill_px(dev_priv, px, v) fill_page_dma((dev_priv), px_base(px), (v))
#define fill32_px(dev_priv, px, v) \
		fill_page_dma_32((dev_priv), px_base(px), (v))

static void fill_page_dma(struct drm_i915_private *dev_priv,
			  struct i915_page_dma *p, const uint64_t val)
{
	int i;
	uint64_t * const vaddr = kmap_page_dma(p);

	for (i = 0; i < 512; i++)
		vaddr[i] = val;

	kunmap_page_dma(dev_priv, vaddr);
}

static void fill_page_dma_32(struct drm_i915_private *dev_priv,
			     struct i915_page_dma *p, const uint32_t val32)
{
	uint64_t v = val32;

	v = v << 32 | val32;

	fill_page_dma(dev_priv, p, v);
}

static int
setup_scratch_page(struct drm_device *dev,
		   struct i915_page_dma *scratch,
		   gfp_t gfp)
{
	return __setup_page_dma(dev, scratch, gfp | __GFP_ZERO);
}

static void cleanup_scratch_page(struct drm_device *dev,
				 struct i915_page_dma *scratch)
{
	cleanup_page_dma(dev, scratch);
}

static struct i915_page_table *alloc_pt(struct drm_device *dev)
{
	struct i915_page_table *pt;
	const size_t count = INTEL_INFO(dev)->gen >= 8 ?
		GEN8_PTES : GEN6_PTES;
	int ret = -ENOMEM;

	pt = kzalloc(sizeof(*pt), GFP_KERNEL);
	if (!pt)
		return ERR_PTR(-ENOMEM);

	pt->used_ptes = kcalloc(BITS_TO_LONGS(count), sizeof(*pt->used_ptes),
				GFP_KERNEL);

	if (!pt->used_ptes)
		goto fail_bitmap;

	ret = setup_px(dev, pt);
	if (ret)
		goto fail_page_m;

	return pt;

fail_page_m:
	kfree(pt->used_ptes);
fail_bitmap:
	kfree(pt);

	return ERR_PTR(ret);
}

static void free_pt(struct drm_device *dev, struct i915_page_table *pt)
{
	cleanup_px(dev, pt);
	kfree(pt->used_ptes);
	kfree(pt);
}

static void gen8_initialize_pt(struct i915_address_space *vm,
			       struct i915_page_table *pt)
{
	gen8_pte_t scratch_pte;

	scratch_pte = gen8_pte_encode(vm->scratch_page.daddr,
				      I915_CACHE_LLC);

	fill_px(to_i915(vm->dev), pt, scratch_pte);
}

static void gen6_initialize_pt(struct i915_address_space *vm,
			       struct i915_page_table *pt)
{
	gen6_pte_t scratch_pte;

	WARN_ON(vm->scratch_page.daddr == 0);

	scratch_pte = vm->pte_encode(vm->scratch_page.daddr,
				     I915_CACHE_LLC, 0);

	fill32_px(to_i915(vm->dev), pt, scratch_pte);
}

static struct i915_page_directory *alloc_pd(struct drm_device *dev)
{
	struct i915_page_directory *pd;
	int ret = -ENOMEM;

	pd = kzalloc(sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return ERR_PTR(-ENOMEM);

	pd->used_pdes = kcalloc(BITS_TO_LONGS(I915_PDES),
				sizeof(*pd->used_pdes), GFP_KERNEL);
	if (!pd->used_pdes)
		goto fail_bitmap;

	ret = setup_px(dev, pd);
	if (ret)
		goto fail_page_m;

	return pd;

fail_page_m:
	kfree(pd->used_pdes);
fail_bitmap:
	kfree(pd);

	return ERR_PTR(ret);
}

static void free_pd(struct drm_device *dev, struct i915_page_directory *pd)
{
	if (px_page(pd)) {
		cleanup_px(dev, pd);
		kfree(pd->used_pdes);
		kfree(pd);
	}
}

static void gen8_initialize_pd(struct i915_address_space *vm,
			       struct i915_page_directory *pd)
{
	gen8_pde_t scratch_pde;

	scratch_pde = gen8_pde_encode(px_dma(vm->scratch_pt), I915_CACHE_LLC);

	fill_px(to_i915(vm->dev), pd, scratch_pde);
}

static int __pdp_init(struct drm_device *dev,
		      struct i915_page_directory_pointer *pdp)
{
	size_t pdpes = I915_PDPES_PER_PDP(dev);

	pdp->used_pdpes = kcalloc(BITS_TO_LONGS(pdpes),
				  sizeof(unsigned long),
				  GFP_KERNEL);
	if (!pdp->used_pdpes)
		return -ENOMEM;

	pdp->page_directory = kcalloc(pdpes, sizeof(*pdp->page_directory),
				      GFP_KERNEL);
	if (!pdp->page_directory) {
		kfree(pdp->used_pdpes);
		/* the PDP might be the statically allocated top level. Keep it
		 * as clean as possible */
		pdp->used_pdpes = NULL;
		return -ENOMEM;
	}

	return 0;
}

static void __pdp_fini(struct i915_page_directory_pointer *pdp)
{
	kfree(pdp->used_pdpes);
	kfree(pdp->page_directory);
	pdp->page_directory = NULL;
}

static struct
i915_page_directory_pointer *alloc_pdp(struct drm_device *dev)
{
	struct i915_page_directory_pointer *pdp;
	int ret = -ENOMEM;

	WARN_ON(!USES_FULL_48BIT_PPGTT(dev));

	pdp = kzalloc(sizeof(*pdp), GFP_KERNEL);
	if (!pdp)
		return ERR_PTR(-ENOMEM);

	ret = __pdp_init(dev, pdp);
	if (ret)
		goto fail_bitmap;

	ret = setup_px(dev, pdp);
	if (ret)
		goto fail_page_m;

	return pdp;

fail_page_m:
	__pdp_fini(pdp);
fail_bitmap:
	kfree(pdp);

	return ERR_PTR(ret);
}

static void free_pdp(struct drm_device *dev,
		     struct i915_page_directory_pointer *pdp)
{
	__pdp_fini(pdp);
	if (USES_FULL_48BIT_PPGTT(dev)) {
		cleanup_px(dev, pdp);
		kfree(pdp);
	}
}

static void gen8_initialize_pdp(struct i915_address_space *vm,
				struct i915_page_directory_pointer *pdp)
{
	gen8_ppgtt_pdpe_t scratch_pdpe;

	scratch_pdpe = gen8_pdpe_encode(px_dma(vm->scratch_pd), I915_CACHE_LLC);

	fill_px(to_i915(vm->dev), pdp, scratch_pdpe);
}

static void gen8_initialize_pml4(struct i915_address_space *vm,
				 struct i915_pml4 *pml4)
{
	gen8_ppgtt_pml4e_t scratch_pml4e;

	scratch_pml4e = gen8_pml4e_encode(px_dma(vm->scratch_pdp),
					  I915_CACHE_LLC);

	fill_px(to_i915(vm->dev), pml4, scratch_pml4e);
}

static void
gen8_setup_page_directory(struct i915_hw_ppgtt *ppgtt,
			  struct i915_page_directory_pointer *pdp,
			  struct i915_page_directory *pd,
			  int index)
{
	gen8_ppgtt_pdpe_t *page_directorypo;

	if (!USES_FULL_48BIT_PPGTT(ppgtt->base.dev))
		return;

	page_directorypo = kmap_px(pdp);
	page_directorypo[index] = gen8_pdpe_encode(px_dma(pd), I915_CACHE_LLC);
	kunmap_px(ppgtt, page_directorypo);
}

static void
gen8_setup_page_directory_pointer(struct i915_hw_ppgtt *ppgtt,
				  struct i915_pml4 *pml4,
				  struct i915_page_directory_pointer *pdp,
				  int index)
{
	gen8_ppgtt_pml4e_t *pagemap = kmap_px(pml4);

	WARN_ON(!USES_FULL_48BIT_PPGTT(ppgtt->base.dev));
	pagemap[index] = gen8_pml4e_encode(px_dma(pdp), I915_CACHE_LLC);
	kunmap_px(ppgtt, pagemap);
}

/* Broadwell Page Directory Pointer Descriptors */
static int gen8_write_pdp(struct drm_i915_gem_request *req,
			  unsigned entry,
			  dma_addr_t addr)
{
	struct intel_ring *ring = req->ring;
	struct intel_engine_cs *engine = req->engine;
	int ret;

	BUG_ON(entry >= 4);

	ret = intel_ring_begin(req, 6);
	if (ret)
		return ret;

	intel_ring_emit(ring, MI_LOAD_REGISTER_IMM(1));
	intel_ring_emit_reg(ring, GEN8_RING_PDP_UDW(engine, entry));
	intel_ring_emit(ring, upper_32_bits(addr));
	intel_ring_emit(ring, MI_LOAD_REGISTER_IMM(1));
	intel_ring_emit_reg(ring, GEN8_RING_PDP_LDW(engine, entry));
	intel_ring_emit(ring, lower_32_bits(addr));
	intel_ring_advance(ring);

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
	ppgtt->pd_dirty_rings = INTEL_INFO(ppgtt->base.dev)->ring_mask;
}

/* Removes entries from a single page table, releasing it if it's empty.
 * Caller can use the return value to update higher-level entries.
 */
static bool gen8_ppgtt_clear_pt(struct i915_address_space *vm,
				struct i915_page_table *pt,
				uint64_t start,
				uint64_t length)
{
	struct i915_hw_ppgtt *ppgtt = i915_vm_to_ppgtt(vm);
	unsigned int num_entries = gen8_pte_count(start, length);
	unsigned int pte = gen8_pte_index(start);
	unsigned int pte_end = pte + num_entries;
	gen8_pte_t *pt_vaddr;
	gen8_pte_t scratch_pte = gen8_pte_encode(vm->scratch_page.daddr,
						 I915_CACHE_LLC);

	if (WARN_ON(!px_page(pt)))
		return false;

	GEM_BUG_ON(pte_end > GEN8_PTES);

	bitmap_clear(pt->used_ptes, pte, num_entries);

	if (bitmap_empty(pt->used_ptes, GEN8_PTES)) {
		free_pt(vm->dev, pt);
		return true;
	}

	pt_vaddr = kmap_px(pt);

	while (pte < pte_end)
		pt_vaddr[pte++] = scratch_pte;

	kunmap_px(ppgtt, pt_vaddr);

	return false;
}

/* Removes entries from a single page dir, releasing it if it's empty.
 * Caller can use the return value to update higher-level entries
 */
static bool gen8_ppgtt_clear_pd(struct i915_address_space *vm,
				struct i915_page_directory *pd,
				uint64_t start,
				uint64_t length)
{
	struct i915_hw_ppgtt *ppgtt = i915_vm_to_ppgtt(vm);
	struct i915_page_table *pt;
	uint64_t pde;
	gen8_pde_t *pde_vaddr;
	gen8_pde_t scratch_pde = gen8_pde_encode(px_dma(vm->scratch_pt),
						 I915_CACHE_LLC);

	gen8_for_each_pde(pt, pd, start, length, pde) {
		if (WARN_ON(!pd->page_table[pde]))
			break;

		if (gen8_ppgtt_clear_pt(vm, pt, start, length)) {
			__clear_bit(pde, pd->used_pdes);
			pde_vaddr = kmap_px(pd);
			pde_vaddr[pde] = scratch_pde;
			kunmap_px(ppgtt, pde_vaddr);
		}
	}

	if (bitmap_empty(pd->used_pdes, I915_PDES)) {
		free_pd(vm->dev, pd);
		return true;
	}

	return false;
}

/* Removes entries from a single page dir pointer, releasing it if it's empty.
 * Caller can use the return value to update higher-level entries
 */
static bool gen8_ppgtt_clear_pdp(struct i915_address_space *vm,
				 struct i915_page_directory_pointer *pdp,
				 uint64_t start,
				 uint64_t length)
{
	struct i915_hw_ppgtt *ppgtt = i915_vm_to_ppgtt(vm);
	struct i915_page_directory *pd;
	uint64_t pdpe;
	gen8_ppgtt_pdpe_t *pdpe_vaddr;
	gen8_ppgtt_pdpe_t scratch_pdpe =
		gen8_pdpe_encode(px_dma(vm->scratch_pd), I915_CACHE_LLC);

	gen8_for_each_pdpe(pd, pdp, start, length, pdpe) {
		if (WARN_ON(!pdp->page_directory[pdpe]))
			break;

		if (gen8_ppgtt_clear_pd(vm, pd, start, length)) {
			__clear_bit(pdpe, pdp->used_pdpes);
			if (USES_FULL_48BIT_PPGTT(vm->dev)) {
				pdpe_vaddr = kmap_px(pdp);
				pdpe_vaddr[pdpe] = scratch_pdpe;
				kunmap_px(ppgtt, pdpe_vaddr);
			}
		}
	}

	mark_tlbs_dirty(ppgtt);

	if (USES_FULL_48BIT_PPGTT(vm->dev) &&
	    bitmap_empty(pdp->used_pdpes, I915_PDPES_PER_PDP(vm->dev))) {
		free_pdp(vm->dev, pdp);
		return true;
	}

	return false;
}

/* Removes entries from a single pml4.
 * This is the top-level structure in 4-level page tables used on gen8+.
 * Empty entries are always scratch pml4e.
 */
static void gen8_ppgtt_clear_pml4(struct i915_address_space *vm,
				  struct i915_pml4 *pml4,
				  uint64_t start,
				  uint64_t length)
{
	struct i915_hw_ppgtt *ppgtt = i915_vm_to_ppgtt(vm);
	struct i915_page_directory_pointer *pdp;
	uint64_t pml4e;
	gen8_ppgtt_pml4e_t *pml4e_vaddr;
	gen8_ppgtt_pml4e_t scratch_pml4e =
		gen8_pml4e_encode(px_dma(vm->scratch_pdp), I915_CACHE_LLC);

	GEM_BUG_ON(!USES_FULL_48BIT_PPGTT(vm->dev));

	gen8_for_each_pml4e(pdp, pml4, start, length, pml4e) {
		if (WARN_ON(!pml4->pdps[pml4e]))
			break;

		if (gen8_ppgtt_clear_pdp(vm, pdp, start, length)) {
			__clear_bit(pml4e, pml4->used_pml4es);
			pml4e_vaddr = kmap_px(pml4);
			pml4e_vaddr[pml4e] = scratch_pml4e;
			kunmap_px(ppgtt, pml4e_vaddr);
		}
	}
}

static void gen8_ppgtt_clear_range(struct i915_address_space *vm,
				   uint64_t start, uint64_t length)
{
	struct i915_hw_ppgtt *ppgtt = i915_vm_to_ppgtt(vm);

	if (USES_FULL_48BIT_PPGTT(vm->dev))
		gen8_ppgtt_clear_pml4(vm, &ppgtt->pml4, start, length);
	else
		gen8_ppgtt_clear_pdp(vm, &ppgtt->pdp, start, length);
}

static void
gen8_ppgtt_insert_pte_entries(struct i915_address_space *vm,
			      struct i915_page_directory_pointer *pdp,
			      struct sg_page_iter *sg_iter,
			      uint64_t start,
			      enum i915_cache_level cache_level)
{
	struct i915_hw_ppgtt *ppgtt = i915_vm_to_ppgtt(vm);
	gen8_pte_t *pt_vaddr;
	unsigned pdpe = gen8_pdpe_index(start);
	unsigned pde = gen8_pde_index(start);
	unsigned pte = gen8_pte_index(start);

	pt_vaddr = NULL;

	while (__sg_page_iter_next(sg_iter)) {
		if (pt_vaddr == NULL) {
			struct i915_page_directory *pd = pdp->page_directory[pdpe];
			struct i915_page_table *pt = pd->page_table[pde];
			pt_vaddr = kmap_px(pt);
		}

		pt_vaddr[pte] =
			gen8_pte_encode(sg_page_iter_dma_address(sg_iter),
					cache_level);
		if (++pte == GEN8_PTES) {
			kunmap_px(ppgtt, pt_vaddr);
			pt_vaddr = NULL;
			if (++pde == I915_PDES) {
				if (++pdpe == I915_PDPES_PER_PDP(vm->dev))
					break;
				pde = 0;
			}
			pte = 0;
		}
	}

	if (pt_vaddr)
		kunmap_px(ppgtt, pt_vaddr);
}

static void gen8_ppgtt_insert_entries(struct i915_address_space *vm,
				      struct sg_table *pages,
				      uint64_t start,
				      enum i915_cache_level cache_level,
				      u32 unused)
{
	struct i915_hw_ppgtt *ppgtt = i915_vm_to_ppgtt(vm);
	struct sg_page_iter sg_iter;

	__sg_page_iter_start(&sg_iter, pages->sgl, sg_nents(pages->sgl), 0);

	if (!USES_FULL_48BIT_PPGTT(vm->dev)) {
		gen8_ppgtt_insert_pte_entries(vm, &ppgtt->pdp, &sg_iter, start,
					      cache_level);
	} else {
		struct i915_page_directory_pointer *pdp;
		uint64_t pml4e;
		uint64_t length = (uint64_t)pages->orig_nents << PAGE_SHIFT;

		gen8_for_each_pml4e(pdp, &ppgtt->pml4, start, length, pml4e) {
			gen8_ppgtt_insert_pte_entries(vm, pdp, &sg_iter,
						      start, cache_level);
		}
	}
}

static void gen8_free_page_tables(struct drm_device *dev,
				  struct i915_page_directory *pd)
{
	int i;

	if (!px_page(pd))
		return;

	for_each_set_bit(i, pd->used_pdes, I915_PDES) {
		if (WARN_ON(!pd->page_table[i]))
			continue;

		free_pt(dev, pd->page_table[i]);
		pd->page_table[i] = NULL;
	}
}

static int gen8_init_scratch(struct i915_address_space *vm)
{
	struct drm_device *dev = vm->dev;
	int ret;

	ret = setup_scratch_page(dev, &vm->scratch_page, I915_GFP_DMA);
	if (ret)
		return ret;

	vm->scratch_pt = alloc_pt(dev);
	if (IS_ERR(vm->scratch_pt)) {
		ret = PTR_ERR(vm->scratch_pt);
		goto free_scratch_page;
	}

	vm->scratch_pd = alloc_pd(dev);
	if (IS_ERR(vm->scratch_pd)) {
		ret = PTR_ERR(vm->scratch_pd);
		goto free_pt;
	}

	if (USES_FULL_48BIT_PPGTT(dev)) {
		vm->scratch_pdp = alloc_pdp(dev);
		if (IS_ERR(vm->scratch_pdp)) {
			ret = PTR_ERR(vm->scratch_pdp);
			goto free_pd;
		}
	}

	gen8_initialize_pt(vm, vm->scratch_pt);
	gen8_initialize_pd(vm, vm->scratch_pd);
	if (USES_FULL_48BIT_PPGTT(dev))
		gen8_initialize_pdp(vm, vm->scratch_pdp);

	return 0;

free_pd:
	free_pd(dev, vm->scratch_pd);
free_pt:
	free_pt(dev, vm->scratch_pt);
free_scratch_page:
	cleanup_scratch_page(dev, &vm->scratch_page);

	return ret;
}

static int gen8_ppgtt_notify_vgt(struct i915_hw_ppgtt *ppgtt, bool create)
{
	enum vgt_g2v_type msg;
	struct drm_i915_private *dev_priv = to_i915(ppgtt->base.dev);
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
	struct drm_device *dev = vm->dev;

	if (USES_FULL_48BIT_PPGTT(dev))
		free_pdp(dev, vm->scratch_pdp);
	free_pd(dev, vm->scratch_pd);
	free_pt(dev, vm->scratch_pt);
	cleanup_scratch_page(dev, &vm->scratch_page);
}

static void gen8_ppgtt_cleanup_3lvl(struct drm_device *dev,
				    struct i915_page_directory_pointer *pdp)
{
	int i;

	for_each_set_bit(i, pdp->used_pdpes, I915_PDPES_PER_PDP(dev)) {
		if (WARN_ON(!pdp->page_directory[i]))
			continue;

		gen8_free_page_tables(dev, pdp->page_directory[i]);
		free_pd(dev, pdp->page_directory[i]);
	}

	free_pdp(dev, pdp);
}

static void gen8_ppgtt_cleanup_4lvl(struct i915_hw_ppgtt *ppgtt)
{
	int i;

	for_each_set_bit(i, ppgtt->pml4.used_pml4es, GEN8_PML4ES_PER_PML4) {
		if (WARN_ON(!ppgtt->pml4.pdps[i]))
			continue;

		gen8_ppgtt_cleanup_3lvl(ppgtt->base.dev, ppgtt->pml4.pdps[i]);
	}

	cleanup_px(ppgtt->base.dev, &ppgtt->pml4);
}

static void gen8_ppgtt_cleanup(struct i915_address_space *vm)
{
	struct i915_hw_ppgtt *ppgtt = i915_vm_to_ppgtt(vm);

	if (intel_vgpu_active(to_i915(vm->dev)))
		gen8_ppgtt_notify_vgt(ppgtt, false);

	if (!USES_FULL_48BIT_PPGTT(ppgtt->base.dev))
		gen8_ppgtt_cleanup_3lvl(ppgtt->base.dev, &ppgtt->pdp);
	else
		gen8_ppgtt_cleanup_4lvl(ppgtt);

	gen8_free_scratch(vm);
}

/**
 * gen8_ppgtt_alloc_pagetabs() - Allocate page tables for VA range.
 * @vm:	Master vm structure.
 * @pd:	Page directory for this address range.
 * @start:	Starting virtual address to begin allocations.
 * @length:	Size of the allocations.
 * @new_pts:	Bitmap set by function with new allocations. Likely used by the
 *		caller to free on error.
 *
 * Allocate the required number of page tables. Extremely similar to
 * gen8_ppgtt_alloc_page_directories(). The main difference is here we are limited by
 * the page directory boundary (instead of the page directory pointer). That
 * boundary is 1GB virtual. Therefore, unlike gen8_ppgtt_alloc_page_directories(), it is
 * possible, and likely that the caller will need to use multiple calls of this
 * function to achieve the appropriate allocation.
 *
 * Return: 0 if success; negative error code otherwise.
 */
static int gen8_ppgtt_alloc_pagetabs(struct i915_address_space *vm,
				     struct i915_page_directory *pd,
				     uint64_t start,
				     uint64_t length,
				     unsigned long *new_pts)
{
	struct drm_device *dev = vm->dev;
	struct i915_page_table *pt;
	uint32_t pde;

	gen8_for_each_pde(pt, pd, start, length, pde) {
		/* Don't reallocate page tables */
		if (test_bit(pde, pd->used_pdes)) {
			/* Scratch is never allocated this way */
			WARN_ON(pt == vm->scratch_pt);
			continue;
		}

		pt = alloc_pt(dev);
		if (IS_ERR(pt))
			goto unwind_out;

		gen8_initialize_pt(vm, pt);
		pd->page_table[pde] = pt;
		__set_bit(pde, new_pts);
		trace_i915_page_table_entry_alloc(vm, pde, start, GEN8_PDE_SHIFT);
	}

	return 0;

unwind_out:
	for_each_set_bit(pde, new_pts, I915_PDES)
		free_pt(dev, pd->page_table[pde]);

	return -ENOMEM;
}

/**
 * gen8_ppgtt_alloc_page_directories() - Allocate page directories for VA range.
 * @vm:	Master vm structure.
 * @pdp:	Page directory pointer for this address range.
 * @start:	Starting virtual address to begin allocations.
 * @length:	Size of the allocations.
 * @new_pds:	Bitmap set by function with new allocations. Likely used by the
 *		caller to free on error.
 *
 * Allocate the required number of page directories starting at the pde index of
 * @start, and ending at the pde index @start + @length. This function will skip
 * over already allocated page directories within the range, and only allocate
 * new ones, setting the appropriate pointer within the pdp as well as the
 * correct position in the bitmap @new_pds.
 *
 * The function will only allocate the pages within the range for a give page
 * directory pointer. In other words, if @start + @length straddles a virtually
 * addressed PDP boundary (512GB for 4k pages), there will be more allocations
 * required by the caller, This is not currently possible, and the BUG in the
 * code will prevent it.
 *
 * Return: 0 if success; negative error code otherwise.
 */
static int
gen8_ppgtt_alloc_page_directories(struct i915_address_space *vm,
				  struct i915_page_directory_pointer *pdp,
				  uint64_t start,
				  uint64_t length,
				  unsigned long *new_pds)
{
	struct drm_device *dev = vm->dev;
	struct i915_page_directory *pd;
	uint32_t pdpe;
	uint32_t pdpes = I915_PDPES_PER_PDP(dev);

	WARN_ON(!bitmap_empty(new_pds, pdpes));

	gen8_for_each_pdpe(pd, pdp, start, length, pdpe) {
		if (test_bit(pdpe, pdp->used_pdpes))
			continue;

		pd = alloc_pd(dev);
		if (IS_ERR(pd))
			goto unwind_out;

		gen8_initialize_pd(vm, pd);
		pdp->page_directory[pdpe] = pd;
		__set_bit(pdpe, new_pds);
		trace_i915_page_directory_entry_alloc(vm, pdpe, start, GEN8_PDPE_SHIFT);
	}

	return 0;

unwind_out:
	for_each_set_bit(pdpe, new_pds, pdpes)
		free_pd(dev, pdp->page_directory[pdpe]);

	return -ENOMEM;
}

/**
 * gen8_ppgtt_alloc_page_dirpointers() - Allocate pdps for VA range.
 * @vm:	Master vm structure.
 * @pml4:	Page map level 4 for this address range.
 * @start:	Starting virtual address to begin allocations.
 * @length:	Size of the allocations.
 * @new_pdps:	Bitmap set by function with new allocations. Likely used by the
 *		caller to free on error.
 *
 * Allocate the required number of page directory pointers. Extremely similar to
 * gen8_ppgtt_alloc_page_directories() and gen8_ppgtt_alloc_pagetabs().
 * The main difference is here we are limited by the pml4 boundary (instead of
 * the page directory pointer).
 *
 * Return: 0 if success; negative error code otherwise.
 */
static int
gen8_ppgtt_alloc_page_dirpointers(struct i915_address_space *vm,
				  struct i915_pml4 *pml4,
				  uint64_t start,
				  uint64_t length,
				  unsigned long *new_pdps)
{
	struct drm_device *dev = vm->dev;
	struct i915_page_directory_pointer *pdp;
	uint32_t pml4e;

	WARN_ON(!bitmap_empty(new_pdps, GEN8_PML4ES_PER_PML4));

	gen8_for_each_pml4e(pdp, pml4, start, length, pml4e) {
		if (!test_bit(pml4e, pml4->used_pml4es)) {
			pdp = alloc_pdp(dev);
			if (IS_ERR(pdp))
				goto unwind_out;

			gen8_initialize_pdp(vm, pdp);
			pml4->pdps[pml4e] = pdp;
			__set_bit(pml4e, new_pdps);
			trace_i915_page_directory_pointer_entry_alloc(vm,
								      pml4e,
								      start,
								      GEN8_PML4E_SHIFT);
		}
	}

	return 0;

unwind_out:
	for_each_set_bit(pml4e, new_pdps, GEN8_PML4ES_PER_PML4)
		free_pdp(dev, pml4->pdps[pml4e]);

	return -ENOMEM;
}

static void
free_gen8_temp_bitmaps(unsigned long *new_pds, unsigned long *new_pts)
{
	kfree(new_pts);
	kfree(new_pds);
}

/* Fills in the page directory bitmap, and the array of page tables bitmap. Both
 * of these are based on the number of PDPEs in the system.
 */
static
int __must_check alloc_gen8_temp_bitmaps(unsigned long **new_pds,
					 unsigned long **new_pts,
					 uint32_t pdpes)
{
	unsigned long *pds;
	unsigned long *pts;

	pds = kcalloc(BITS_TO_LONGS(pdpes), sizeof(unsigned long), GFP_TEMPORARY);
	if (!pds)
		return -ENOMEM;

	pts = kcalloc(pdpes, BITS_TO_LONGS(I915_PDES) * sizeof(unsigned long),
		      GFP_TEMPORARY);
	if (!pts)
		goto err_out;

	*new_pds = pds;
	*new_pts = pts;

	return 0;

err_out:
	free_gen8_temp_bitmaps(pds, pts);
	return -ENOMEM;
}

static int gen8_alloc_va_range_3lvl(struct i915_address_space *vm,
				    struct i915_page_directory_pointer *pdp,
				    uint64_t start,
				    uint64_t length)
{
	struct i915_hw_ppgtt *ppgtt = i915_vm_to_ppgtt(vm);
	unsigned long *new_page_dirs, *new_page_tables;
	struct drm_device *dev = vm->dev;
	struct i915_page_directory *pd;
	const uint64_t orig_start = start;
	const uint64_t orig_length = length;
	uint32_t pdpe;
	uint32_t pdpes = I915_PDPES_PER_PDP(dev);
	int ret;

	/* Wrap is never okay since we can only represent 48b, and we don't
	 * actually use the other side of the canonical address space.
	 */
	if (WARN_ON(start + length < start))
		return -ENODEV;

	if (WARN_ON(start + length > vm->total))
		return -ENODEV;

	ret = alloc_gen8_temp_bitmaps(&new_page_dirs, &new_page_tables, pdpes);
	if (ret)
		return ret;

	/* Do the allocations first so we can easily bail out */
	ret = gen8_ppgtt_alloc_page_directories(vm, pdp, start, length,
						new_page_dirs);
	if (ret) {
		free_gen8_temp_bitmaps(new_page_dirs, new_page_tables);
		return ret;
	}

	/* For every page directory referenced, allocate page tables */
	gen8_for_each_pdpe(pd, pdp, start, length, pdpe) {
		ret = gen8_ppgtt_alloc_pagetabs(vm, pd, start, length,
						new_page_tables + pdpe * BITS_TO_LONGS(I915_PDES));
		if (ret)
			goto err_out;
	}

	start = orig_start;
	length = orig_length;

	/* Allocations have completed successfully, so set the bitmaps, and do
	 * the mappings. */
	gen8_for_each_pdpe(pd, pdp, start, length, pdpe) {
		gen8_pde_t *const page_directory = kmap_px(pd);
		struct i915_page_table *pt;
		uint64_t pd_len = length;
		uint64_t pd_start = start;
		uint32_t pde;

		/* Every pd should be allocated, we just did that above. */
		WARN_ON(!pd);

		gen8_for_each_pde(pt, pd, pd_start, pd_len, pde) {
			/* Same reasoning as pd */
			WARN_ON(!pt);
			WARN_ON(!pd_len);
			WARN_ON(!gen8_pte_count(pd_start, pd_len));

			/* Set our used ptes within the page table */
			bitmap_set(pt->used_ptes,
				   gen8_pte_index(pd_start),
				   gen8_pte_count(pd_start, pd_len));

			/* Our pde is now pointing to the pagetable, pt */
			__set_bit(pde, pd->used_pdes);

			/* Map the PDE to the page table */
			page_directory[pde] = gen8_pde_encode(px_dma(pt),
							      I915_CACHE_LLC);
			trace_i915_page_table_entry_map(&ppgtt->base, pde, pt,
							gen8_pte_index(start),
							gen8_pte_count(start, length),
							GEN8_PTES);

			/* NB: We haven't yet mapped ptes to pages. At this
			 * point we're still relying on insert_entries() */
		}

		kunmap_px(ppgtt, page_directory);
		__set_bit(pdpe, pdp->used_pdpes);
		gen8_setup_page_directory(ppgtt, pdp, pd, pdpe);
	}

	free_gen8_temp_bitmaps(new_page_dirs, new_page_tables);
	mark_tlbs_dirty(ppgtt);
	return 0;

err_out:
	while (pdpe--) {
		unsigned long temp;

		for_each_set_bit(temp, new_page_tables + pdpe *
				BITS_TO_LONGS(I915_PDES), I915_PDES)
			free_pt(dev, pdp->page_directory[pdpe]->page_table[temp]);
	}

	for_each_set_bit(pdpe, new_page_dirs, pdpes)
		free_pd(dev, pdp->page_directory[pdpe]);

	free_gen8_temp_bitmaps(new_page_dirs, new_page_tables);
	mark_tlbs_dirty(ppgtt);
	return ret;
}

static int gen8_alloc_va_range_4lvl(struct i915_address_space *vm,
				    struct i915_pml4 *pml4,
				    uint64_t start,
				    uint64_t length)
{
	DECLARE_BITMAP(new_pdps, GEN8_PML4ES_PER_PML4);
	struct i915_hw_ppgtt *ppgtt = i915_vm_to_ppgtt(vm);
	struct i915_page_directory_pointer *pdp;
	uint64_t pml4e;
	int ret = 0;

	/* Do the pml4 allocations first, so we don't need to track the newly
	 * allocated tables below the pdp */
	bitmap_zero(new_pdps, GEN8_PML4ES_PER_PML4);

	/* The pagedirectory and pagetable allocations are done in the shared 3
	 * and 4 level code. Just allocate the pdps.
	 */
	ret = gen8_ppgtt_alloc_page_dirpointers(vm, pml4, start, length,
						new_pdps);
	if (ret)
		return ret;

	WARN(bitmap_weight(new_pdps, GEN8_PML4ES_PER_PML4) > 2,
	     "The allocation has spanned more than 512GB. "
	     "It is highly likely this is incorrect.");

	gen8_for_each_pml4e(pdp, pml4, start, length, pml4e) {
		WARN_ON(!pdp);

		ret = gen8_alloc_va_range_3lvl(vm, pdp, start, length);
		if (ret)
			goto err_out;

		gen8_setup_page_directory_pointer(ppgtt, pml4, pdp, pml4e);
	}

	bitmap_or(pml4->used_pml4es, new_pdps, pml4->used_pml4es,
		  GEN8_PML4ES_PER_PML4);

	return 0;

err_out:
	for_each_set_bit(pml4e, new_pdps, GEN8_PML4ES_PER_PML4)
		gen8_ppgtt_cleanup_3lvl(vm->dev, pml4->pdps[pml4e]);

	return ret;
}

static int gen8_alloc_va_range(struct i915_address_space *vm,
			       uint64_t start, uint64_t length)
{
	struct i915_hw_ppgtt *ppgtt = i915_vm_to_ppgtt(vm);

	if (USES_FULL_48BIT_PPGTT(vm->dev))
		return gen8_alloc_va_range_4lvl(vm, &ppgtt->pml4, start, length);
	else
		return gen8_alloc_va_range_3lvl(vm, &ppgtt->pdp, start, length);
}

static void gen8_dump_pdp(struct i915_page_directory_pointer *pdp,
			  uint64_t start, uint64_t length,
			  gen8_pte_t scratch_pte,
			  struct seq_file *m)
{
	struct i915_page_directory *pd;
	uint32_t pdpe;

	gen8_for_each_pdpe(pd, pdp, start, length, pdpe) {
		struct i915_page_table *pt;
		uint64_t pd_len = length;
		uint64_t pd_start = start;
		uint32_t pde;

		if (!test_bit(pdpe, pdp->used_pdpes))
			continue;

		seq_printf(m, "\tPDPE #%d\n", pdpe);
		gen8_for_each_pde(pt, pd, pd_start, pd_len, pde) {
			uint32_t  pte;
			gen8_pte_t *pt_vaddr;

			if (!test_bit(pde, pd->used_pdes))
				continue;

			pt_vaddr = kmap_px(pt);
			for (pte = 0; pte < GEN8_PTES; pte += 4) {
				uint64_t va =
					(pdpe << GEN8_PDPE_SHIFT) |
					(pde << GEN8_PDE_SHIFT) |
					(pte << GEN8_PTE_SHIFT);
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
			/* don't use kunmap_px, it could trigger
			 * an unnecessary flush.
			 */
			kunmap_atomic(pt_vaddr);
		}
	}
}

static void gen8_dump_ppgtt(struct i915_hw_ppgtt *ppgtt, struct seq_file *m)
{
	struct i915_address_space *vm = &ppgtt->base;
	uint64_t start = ppgtt->base.start;
	uint64_t length = ppgtt->base.total;
	gen8_pte_t scratch_pte = gen8_pte_encode(vm->scratch_page.daddr,
						 I915_CACHE_LLC);

	if (!USES_FULL_48BIT_PPGTT(vm->dev)) {
		gen8_dump_pdp(&ppgtt->pdp, start, length, scratch_pte, m);
	} else {
		uint64_t pml4e;
		struct i915_pml4 *pml4 = &ppgtt->pml4;
		struct i915_page_directory_pointer *pdp;

		gen8_for_each_pml4e(pdp, pml4, start, length, pml4e) {
			if (!test_bit(pml4e, pml4->used_pml4es))
				continue;

			seq_printf(m, "    PML4E #%llu\n", pml4e);
			gen8_dump_pdp(pdp, start, length, scratch_pte, m);
		}
	}
}

static int gen8_preallocate_top_level_pdps(struct i915_hw_ppgtt *ppgtt)
{
	unsigned long *new_page_dirs, *new_page_tables;
	uint32_t pdpes = I915_PDPES_PER_PDP(dev);
	int ret;

	/* We allocate temp bitmap for page tables for no gain
	 * but as this is for init only, lets keep the things simple
	 */
	ret = alloc_gen8_temp_bitmaps(&new_page_dirs, &new_page_tables, pdpes);
	if (ret)
		return ret;

	/* Allocate for all pdps regardless of how the ppgtt
	 * was defined.
	 */
	ret = gen8_ppgtt_alloc_page_directories(&ppgtt->base, &ppgtt->pdp,
						0, 1ULL << 32,
						new_page_dirs);
	if (!ret)
		*ppgtt->pdp.used_pdpes = *new_page_dirs;

	free_gen8_temp_bitmaps(new_page_dirs, new_page_tables);

	return ret;
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
	int ret;

	ret = gen8_init_scratch(&ppgtt->base);
	if (ret)
		return ret;

	ppgtt->base.start = 0;
	ppgtt->base.cleanup = gen8_ppgtt_cleanup;
	ppgtt->base.allocate_va_range = gen8_alloc_va_range;
	ppgtt->base.insert_entries = gen8_ppgtt_insert_entries;
	ppgtt->base.clear_range = gen8_ppgtt_clear_range;
	ppgtt->base.unbind_vma = ppgtt_unbind_vma;
	ppgtt->base.bind_vma = ppgtt_bind_vma;
	ppgtt->debug_dump = gen8_dump_ppgtt;

	if (USES_FULL_48BIT_PPGTT(ppgtt->base.dev)) {
		ret = setup_px(ppgtt->base.dev, &ppgtt->pml4);
		if (ret)
			goto free_scratch;

		gen8_initialize_pml4(&ppgtt->base, &ppgtt->pml4);

		ppgtt->base.total = 1ULL << 48;
		ppgtt->switch_mm = gen8_48b_mm_switch;
	} else {
		ret = __pdp_init(ppgtt->base.dev, &ppgtt->pdp);
		if (ret)
			goto free_scratch;

		ppgtt->base.total = 1ULL << 32;
		ppgtt->switch_mm = gen8_legacy_mm_switch;
		trace_i915_page_directory_pointer_entry_alloc(&ppgtt->base,
							      0, 0,
							      GEN8_PML4E_SHIFT);

		if (intel_vgpu_active(to_i915(ppgtt->base.dev))) {
			ret = gen8_preallocate_top_level_pdps(ppgtt);
			if (ret)
				goto free_scratch;
		}
	}

	if (intel_vgpu_active(to_i915(ppgtt->base.dev)))
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
	uint32_t pd_entry;
	uint32_t  pte, pde;
	uint32_t start = ppgtt->base.start, length = ppgtt->base.total;

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

		pt_vaddr = kmap_px(ppgtt->pd.page_table[pde]);

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
		kunmap_px(ppgtt, pt_vaddr);
	}
}

/* Write pde (index) from the page directory @pd to the page table @pt */
static void gen6_write_pde(struct i915_page_directory *pd,
			    const int pde, struct i915_page_table *pt)
{
	/* Caller needs to make sure the write completes if necessary */
	struct i915_hw_ppgtt *ppgtt =
		container_of(pd, struct i915_hw_ppgtt, pd);
	u32 pd_entry;

	pd_entry = GEN6_PDE_ADDR_ENCODE(px_dma(pt));
	pd_entry |= GEN6_PDE_VALID;

	writel(pd_entry, ppgtt->pd_addr + pde);
}

/* Write all the page tables found in the ppgtt structure to incrementing page
 * directories. */
static void gen6_write_page_range(struct drm_i915_private *dev_priv,
				  struct i915_page_directory *pd,
				  uint32_t start, uint32_t length)
{
	struct i915_ggtt *ggtt = &dev_priv->ggtt;
	struct i915_page_table *pt;
	uint32_t pde;

	gen6_for_each_pde(pt, pd, start, length, pde)
		gen6_write_pde(pd, pde, pt);

	/* Make sure write is complete before other code can use this page
	 * table. Also require for WC mapped PTEs */
	readl(ggtt->gsm);
}

static uint32_t get_pd_offset(struct i915_hw_ppgtt *ppgtt)
{
	BUG_ON(ppgtt->pd.base.ggtt_offset & 0x3f);

	return (ppgtt->pd.base.ggtt_offset / 64) << 16;
}

static int hsw_mm_switch(struct i915_hw_ppgtt *ppgtt,
			 struct drm_i915_gem_request *req)
{
	struct intel_ring *ring = req->ring;
	struct intel_engine_cs *engine = req->engine;
	int ret;

	/* NB: TLBs must be flushed and invalidated before a switch */
	ret = engine->emit_flush(req, EMIT_INVALIDATE | EMIT_FLUSH);
	if (ret)
		return ret;

	ret = intel_ring_begin(req, 6);
	if (ret)
		return ret;

	intel_ring_emit(ring, MI_LOAD_REGISTER_IMM(2));
	intel_ring_emit_reg(ring, RING_PP_DIR_DCLV(engine));
	intel_ring_emit(ring, PP_DIR_DCLV_2G);
	intel_ring_emit_reg(ring, RING_PP_DIR_BASE(engine));
	intel_ring_emit(ring, get_pd_offset(ppgtt));
	intel_ring_emit(ring, MI_NOOP);
	intel_ring_advance(ring);

	return 0;
}

static int gen7_mm_switch(struct i915_hw_ppgtt *ppgtt,
			  struct drm_i915_gem_request *req)
{
	struct intel_ring *ring = req->ring;
	struct intel_engine_cs *engine = req->engine;
	int ret;

	/* NB: TLBs must be flushed and invalidated before a switch */
	ret = engine->emit_flush(req, EMIT_INVALIDATE | EMIT_FLUSH);
	if (ret)
		return ret;

	ret = intel_ring_begin(req, 6);
	if (ret)
		return ret;

	intel_ring_emit(ring, MI_LOAD_REGISTER_IMM(2));
	intel_ring_emit_reg(ring, RING_PP_DIR_DCLV(engine));
	intel_ring_emit(ring, PP_DIR_DCLV_2G);
	intel_ring_emit_reg(ring, RING_PP_DIR_BASE(engine));
	intel_ring_emit(ring, get_pd_offset(ppgtt));
	intel_ring_emit(ring, MI_NOOP);
	intel_ring_advance(ring);

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

static void gen8_ppgtt_enable(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	for_each_engine(engine, dev_priv, id) {
		u32 four_level = USES_FULL_48BIT_PPGTT(dev) ? GEN8_GFX_PPGTT_48B : 0;
		I915_WRITE(RING_MODE_GEN7(engine),
			   _MASKED_BIT_ENABLE(GFX_PPGTT_ENABLE | four_level));
	}
}

static void gen7_ppgtt_enable(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_engine_cs *engine;
	uint32_t ecochk, ecobits;
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

static void gen6_ppgtt_enable(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	uint32_t ecochk, gab_ctl, ecobits;

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
				   uint64_t start,
				   uint64_t length)
{
	struct i915_hw_ppgtt *ppgtt = i915_vm_to_ppgtt(vm);
	gen6_pte_t *pt_vaddr, scratch_pte;
	unsigned first_entry = start >> PAGE_SHIFT;
	unsigned num_entries = length >> PAGE_SHIFT;
	unsigned act_pt = first_entry / GEN6_PTES;
	unsigned first_pte = first_entry % GEN6_PTES;
	unsigned last_pte, i;

	scratch_pte = vm->pte_encode(vm->scratch_page.daddr,
				     I915_CACHE_LLC, 0);

	while (num_entries) {
		last_pte = first_pte + num_entries;
		if (last_pte > GEN6_PTES)
			last_pte = GEN6_PTES;

		pt_vaddr = kmap_px(ppgtt->pd.page_table[act_pt]);

		for (i = first_pte; i < last_pte; i++)
			pt_vaddr[i] = scratch_pte;

		kunmap_px(ppgtt, pt_vaddr);

		num_entries -= last_pte - first_pte;
		first_pte = 0;
		act_pt++;
	}
}

static void gen6_ppgtt_insert_entries(struct i915_address_space *vm,
				      struct sg_table *pages,
				      uint64_t start,
				      enum i915_cache_level cache_level, u32 flags)
{
	struct i915_hw_ppgtt *ppgtt = i915_vm_to_ppgtt(vm);
	unsigned first_entry = start >> PAGE_SHIFT;
	unsigned act_pt = first_entry / GEN6_PTES;
	unsigned act_pte = first_entry % GEN6_PTES;
	gen6_pte_t *pt_vaddr = NULL;
	struct sgt_iter sgt_iter;
	dma_addr_t addr;

	for_each_sgt_dma(addr, sgt_iter, pages) {
		if (pt_vaddr == NULL)
			pt_vaddr = kmap_px(ppgtt->pd.page_table[act_pt]);

		pt_vaddr[act_pte] =
			vm->pte_encode(addr, cache_level, flags);

		if (++act_pte == GEN6_PTES) {
			kunmap_px(ppgtt, pt_vaddr);
			pt_vaddr = NULL;
			act_pt++;
			act_pte = 0;
		}
	}

	if (pt_vaddr)
		kunmap_px(ppgtt, pt_vaddr);
}

static int gen6_alloc_va_range(struct i915_address_space *vm,
			       uint64_t start_in, uint64_t length_in)
{
	DECLARE_BITMAP(new_page_tables, I915_PDES);
	struct drm_device *dev = vm->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct i915_ggtt *ggtt = &dev_priv->ggtt;
	struct i915_hw_ppgtt *ppgtt = i915_vm_to_ppgtt(vm);
	struct i915_page_table *pt;
	uint32_t start, length, start_save, length_save;
	uint32_t pde;
	int ret;

	if (WARN_ON(start_in + length_in > ppgtt->base.total))
		return -ENODEV;

	start = start_save = start_in;
	length = length_save = length_in;

	bitmap_zero(new_page_tables, I915_PDES);

	/* The allocation is done in two stages so that we can bail out with
	 * minimal amount of pain. The first stage finds new page tables that
	 * need allocation. The second stage marks use ptes within the page
	 * tables.
	 */
	gen6_for_each_pde(pt, &ppgtt->pd, start, length, pde) {
		if (pt != vm->scratch_pt) {
			WARN_ON(bitmap_empty(pt->used_ptes, GEN6_PTES));
			continue;
		}

		/* We've already allocated a page table */
		WARN_ON(!bitmap_empty(pt->used_ptes, GEN6_PTES));

		pt = alloc_pt(dev);
		if (IS_ERR(pt)) {
			ret = PTR_ERR(pt);
			goto unwind_out;
		}

		gen6_initialize_pt(vm, pt);

		ppgtt->pd.page_table[pde] = pt;
		__set_bit(pde, new_page_tables);
		trace_i915_page_table_entry_alloc(vm, pde, start, GEN6_PDE_SHIFT);
	}

	start = start_save;
	length = length_save;

	gen6_for_each_pde(pt, &ppgtt->pd, start, length, pde) {
		DECLARE_BITMAP(tmp_bitmap, GEN6_PTES);

		bitmap_zero(tmp_bitmap, GEN6_PTES);
		bitmap_set(tmp_bitmap, gen6_pte_index(start),
			   gen6_pte_count(start, length));

		if (__test_and_clear_bit(pde, new_page_tables))
			gen6_write_pde(&ppgtt->pd, pde, pt);

		trace_i915_page_table_entry_map(vm, pde, pt,
					 gen6_pte_index(start),
					 gen6_pte_count(start, length),
					 GEN6_PTES);
		bitmap_or(pt->used_ptes, tmp_bitmap, pt->used_ptes,
				GEN6_PTES);
	}

	WARN_ON(!bitmap_empty(new_page_tables, I915_PDES));

	/* Make sure write is complete before other code can use this page
	 * table. Also require for WC mapped PTEs */
	readl(ggtt->gsm);

	mark_tlbs_dirty(ppgtt);
	return 0;

unwind_out:
	for_each_set_bit(pde, new_page_tables, I915_PDES) {
		struct i915_page_table *pt = ppgtt->pd.page_table[pde];

		ppgtt->pd.page_table[pde] = vm->scratch_pt;
		free_pt(vm->dev, pt);
	}

	mark_tlbs_dirty(ppgtt);
	return ret;
}

static int gen6_init_scratch(struct i915_address_space *vm)
{
	struct drm_device *dev = vm->dev;
	int ret;

	ret = setup_scratch_page(dev, &vm->scratch_page, I915_GFP_DMA);
	if (ret)
		return ret;

	vm->scratch_pt = alloc_pt(dev);
	if (IS_ERR(vm->scratch_pt)) {
		cleanup_scratch_page(dev, &vm->scratch_page);
		return PTR_ERR(vm->scratch_pt);
	}

	gen6_initialize_pt(vm, vm->scratch_pt);

	return 0;
}

static void gen6_free_scratch(struct i915_address_space *vm)
{
	struct drm_device *dev = vm->dev;

	free_pt(dev, vm->scratch_pt);
	cleanup_scratch_page(dev, &vm->scratch_page);
}

static void gen6_ppgtt_cleanup(struct i915_address_space *vm)
{
	struct i915_hw_ppgtt *ppgtt = i915_vm_to_ppgtt(vm);
	struct i915_page_directory *pd = &ppgtt->pd;
	struct drm_device *dev = vm->dev;
	struct i915_page_table *pt;
	uint32_t pde;

	drm_mm_remove_node(&ppgtt->node);

	gen6_for_all_pdes(pt, pd, pde)
		if (pt != vm->scratch_pt)
			free_pt(dev, pt);

	gen6_free_scratch(vm);
}

static int gen6_ppgtt_allocate_page_directories(struct i915_hw_ppgtt *ppgtt)
{
	struct i915_address_space *vm = &ppgtt->base;
	struct drm_device *dev = ppgtt->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct i915_ggtt *ggtt = &dev_priv->ggtt;
	bool retried = false;
	int ret;

	/* PPGTT PDEs reside in the GGTT and consists of 512 entries. The
	 * allocator works in address space sizes, so it's multiplied by page
	 * size. We allocate at the top of the GTT to avoid fragmentation.
	 */
	BUG_ON(!drm_mm_initialized(&ggtt->base.mm));

	ret = gen6_init_scratch(vm);
	if (ret)
		return ret;

alloc:
	ret = drm_mm_insert_node_in_range_generic(&ggtt->base.mm,
						  &ppgtt->node, GEN6_PD_SIZE,
						  GEN6_PD_ALIGN, 0,
						  0, ggtt->base.total,
						  DRM_MM_TOPDOWN);
	if (ret == -ENOSPC && !retried) {
		ret = i915_gem_evict_something(&ggtt->base,
					       GEN6_PD_SIZE, GEN6_PD_ALIGN,
					       I915_CACHE_NONE,
					       0, ggtt->base.total,
					       0);
		if (ret)
			goto err_out;

		retried = true;
		goto alloc;
	}

	if (ret)
		goto err_out;


	if (ppgtt->node.start < ggtt->mappable_end)
		DRM_DEBUG("Forced to use aperture for PDEs\n");

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
				  uint64_t start, uint64_t length)
{
	struct i915_page_table *unused;
	uint32_t pde;

	gen6_for_each_pde(unused, &ppgtt->pd, start, length, pde)
		ppgtt->pd.page_table[pde] = ppgtt->base.scratch_pt;
}

static int gen6_ppgtt_init(struct i915_hw_ppgtt *ppgtt)
{
	struct drm_device *dev = ppgtt->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
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

	ppgtt->base.allocate_va_range = gen6_alloc_va_range;
	ppgtt->base.clear_range = gen6_ppgtt_clear_range;
	ppgtt->base.insert_entries = gen6_ppgtt_insert_entries;
	ppgtt->base.unbind_vma = ppgtt_unbind_vma;
	ppgtt->base.bind_vma = ppgtt_bind_vma;
	ppgtt->base.cleanup = gen6_ppgtt_cleanup;
	ppgtt->base.start = 0;
	ppgtt->base.total = I915_PDES * GEN6_PTES * PAGE_SIZE;
	ppgtt->debug_dump = gen6_dump_ppgtt;

	ppgtt->pd.base.ggtt_offset =
		ppgtt->node.start / PAGE_SIZE * sizeof(gen6_pte_t);

	ppgtt->pd_addr = (gen6_pte_t __iomem *)ggtt->gsm +
		ppgtt->pd.base.ggtt_offset / sizeof(gen6_pte_t);

	gen6_scratch_va_range(ppgtt, 0, ppgtt->base.total);

	gen6_write_page_range(dev_priv, &ppgtt->pd, 0, ppgtt->base.total);

	DRM_DEBUG_DRIVER("Allocated pde space (%lldM) at GTT entry: %llx\n",
			 ppgtt->node.size >> 20,
			 ppgtt->node.start / PAGE_SIZE);

	DRM_DEBUG("Adding PPGTT at offset %x\n",
		  ppgtt->pd.base.ggtt_offset << 10);

	return 0;
}

static int __hw_ppgtt_init(struct i915_hw_ppgtt *ppgtt,
			   struct drm_i915_private *dev_priv)
{
	ppgtt->base.dev = &dev_priv->drm;

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
	drm_mm_init(&vm->mm, vm->start, vm->total);
	INIT_LIST_HEAD(&vm->active_list);
	INIT_LIST_HEAD(&vm->inactive_list);
	INIT_LIST_HEAD(&vm->unbound_list);
	list_add_tail(&vm->global_link, &dev_priv->vm_list);
}

static void gtt_write_workarounds(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);

	/* This function is for gtt related workarounds. This function is
	 * called on driver load and after a GPU reset, so you can place
	 * workarounds here even if they get overwritten by GPU reset.
	 */
	/* WaIncreaseDefaultTLBEntries:chv,bdw,skl,bxt */
	if (IS_BROADWELL(dev_priv))
		I915_WRITE(GEN8_L3_LRA_1_GPGPU, GEN8_L3_LRA_1_GPGPU_DEFAULT_VALUE_BDW);
	else if (IS_CHERRYVIEW(dev_priv))
		I915_WRITE(GEN8_L3_LRA_1_GPGPU, GEN8_L3_LRA_1_GPGPU_DEFAULT_VALUE_CHV);
	else if (IS_SKYLAKE(dev_priv))
		I915_WRITE(GEN8_L3_LRA_1_GPGPU, GEN9_L3_LRA_1_GPGPU_DEFAULT_VALUE_SKL);
	else if (IS_BROXTON(dev_priv))
		I915_WRITE(GEN8_L3_LRA_1_GPGPU, GEN9_L3_LRA_1_GPGPU_DEFAULT_VALUE_BXT);
}

static int i915_ppgtt_init(struct i915_hw_ppgtt *ppgtt,
			   struct drm_i915_private *dev_priv,
			   struct drm_i915_file_private *file_priv,
			   const char *name)
{
	int ret;

	ret = __hw_ppgtt_init(ppgtt, dev_priv);
	if (ret == 0) {
		kref_init(&ppgtt->ref);
		i915_address_space_init(&ppgtt->base, dev_priv, name);
		ppgtt->base.file = file_priv;
	}

	return ret;
}

int i915_ppgtt_init_hw(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);

	gtt_write_workarounds(dev);

	/* In the case of execlists, PPGTT is enabled by the context descriptor
	 * and the PDPs are contained within the context itself.  We don't
	 * need to do anything here. */
	if (i915.enable_execlists)
		return 0;

	if (!USES_PPGTT(dev))
		return 0;

	if (IS_GEN6(dev_priv))
		gen6_ppgtt_enable(dev);
	else if (IS_GEN7(dev_priv))
		gen7_ppgtt_enable(dev);
	else if (INTEL_INFO(dev)->gen >= 8)
		gen8_ppgtt_enable(dev);
	else
		MISSING_CASE(INTEL_INFO(dev)->gen);

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

	ret = i915_ppgtt_init(ppgtt, dev_priv, fpriv, name);
	if (ret) {
		kfree(ppgtt);
		return ERR_PTR(ret);
	}

	trace_i915_ppgtt_create(&ppgtt->base);

	return ppgtt;
}

void  i915_ppgtt_release(struct kref *kref)
{
	struct i915_hw_ppgtt *ppgtt =
		container_of(kref, struct i915_hw_ppgtt, ref);

	trace_i915_ppgtt_release(&ppgtt->base);

	/* vmas should already be unbound and destroyed */
	WARN_ON(!list_empty(&ppgtt->base.active_list));
	WARN_ON(!list_empty(&ppgtt->base.inactive_list));
	WARN_ON(!list_empty(&ppgtt->base.unbound_list));

	i915_gem_timeline_fini(&ppgtt->base.timeline);
	list_del(&ppgtt->base.global_link);
	drm_mm_takedown(&ppgtt->base.mm);

	ppgtt->base.cleanup(&ppgtt->base);
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

static void i915_ggtt_flush(struct drm_i915_private *dev_priv)
{
	if (INTEL_INFO(dev_priv)->gen < 6) {
		intel_gtt_chipset_flush();
	} else {
		I915_WRITE(GFX_FLSH_CNTL_GEN6, GFX_FLSH_CNTL_EN);
		POSTING_READ(GFX_FLSH_CNTL_GEN6);
	}
}

void i915_gem_suspend_gtt_mappings(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct i915_ggtt *ggtt = &dev_priv->ggtt;

	/* Don't bother messing with faults pre GEN6 as we have little
	 * documentation supporting that it's a good idea.
	 */
	if (INTEL_INFO(dev)->gen < 6)
		return;

	i915_check_and_clear_faults(dev_priv);

	ggtt->base.clear_range(&ggtt->base, ggtt->base.start, ggtt->base.total);

	i915_ggtt_flush(dev_priv);
}

int i915_gem_gtt_prepare_pages(struct drm_i915_gem_object *obj,
			       struct sg_table *pages)
{
	if (dma_map_sg(&obj->base.dev->pdev->dev,
		       pages->sgl, pages->nents,
		       PCI_DMA_BIDIRECTIONAL))
		return 0;

	return -ENOSPC;
}

static void gen8_set_pte(void __iomem *addr, gen8_pte_t pte)
{
	writeq(pte, addr);
}

static void gen8_ggtt_insert_page(struct i915_address_space *vm,
				  dma_addr_t addr,
				  uint64_t offset,
				  enum i915_cache_level level,
				  u32 unused)
{
	struct drm_i915_private *dev_priv = to_i915(vm->dev);
	gen8_pte_t __iomem *pte =
		(gen8_pte_t __iomem *)dev_priv->ggtt.gsm +
		(offset >> PAGE_SHIFT);

	gen8_set_pte(pte, gen8_pte_encode(addr, level));

	I915_WRITE(GFX_FLSH_CNTL_GEN6, GFX_FLSH_CNTL_EN);
	POSTING_READ(GFX_FLSH_CNTL_GEN6);
}

static void gen8_ggtt_insert_entries(struct i915_address_space *vm,
				     struct sg_table *st,
				     uint64_t start,
				     enum i915_cache_level level, u32 unused)
{
	struct drm_i915_private *dev_priv = to_i915(vm->dev);
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
	struct sgt_iter sgt_iter;
	gen8_pte_t __iomem *gtt_entries;
	gen8_pte_t gtt_entry;
	dma_addr_t addr;
	int i = 0;

	gtt_entries = (gen8_pte_t __iomem *)ggtt->gsm + (start >> PAGE_SHIFT);

	for_each_sgt_dma(addr, sgt_iter, st) {
		gtt_entry = gen8_pte_encode(addr, level);
		gen8_set_pte(&gtt_entries[i++], gtt_entry);
	}

	/*
	 * XXX: This serves as a posting read to make sure that the PTE has
	 * actually been updated. There is some concern that even though
	 * registers and PTEs are within the same BAR that they are potentially
	 * of NUMA access patterns. Therefore, even with the way we assume
	 * hardware should work, we must keep this posting read for paranoia.
	 */
	if (i != 0)
		WARN_ON(readq(&gtt_entries[i-1]) != gtt_entry);

	/* This next bit makes the above posting read even more important. We
	 * want to flush the TLBs only after we're certain all the PTE updates
	 * have finished.
	 */
	I915_WRITE(GFX_FLSH_CNTL_GEN6, GFX_FLSH_CNTL_EN);
	POSTING_READ(GFX_FLSH_CNTL_GEN6);
}

struct insert_entries {
	struct i915_address_space *vm;
	struct sg_table *st;
	uint64_t start;
	enum i915_cache_level level;
	u32 flags;
};

static int gen8_ggtt_insert_entries__cb(void *_arg)
{
	struct insert_entries *arg = _arg;
	gen8_ggtt_insert_entries(arg->vm, arg->st,
				 arg->start, arg->level, arg->flags);
	return 0;
}

static void gen8_ggtt_insert_entries__BKL(struct i915_address_space *vm,
					  struct sg_table *st,
					  uint64_t start,
					  enum i915_cache_level level,
					  u32 flags)
{
	struct insert_entries arg = { vm, st, start, level, flags };
	stop_machine(gen8_ggtt_insert_entries__cb, &arg, NULL);
}

static void gen6_ggtt_insert_page(struct i915_address_space *vm,
				  dma_addr_t addr,
				  uint64_t offset,
				  enum i915_cache_level level,
				  u32 flags)
{
	struct drm_i915_private *dev_priv = to_i915(vm->dev);
	gen6_pte_t __iomem *pte =
		(gen6_pte_t __iomem *)dev_priv->ggtt.gsm +
		(offset >> PAGE_SHIFT);

	iowrite32(vm->pte_encode(addr, level, flags), pte);

	I915_WRITE(GFX_FLSH_CNTL_GEN6, GFX_FLSH_CNTL_EN);
	POSTING_READ(GFX_FLSH_CNTL_GEN6);
}

/*
 * Binds an object into the global gtt with the specified cache level. The object
 * will be accessible to the GPU via commands whose operands reference offsets
 * within the global GTT as well as accessible by the GPU through the GMADR
 * mapped BAR (dev_priv->mm.gtt->gtt).
 */
static void gen6_ggtt_insert_entries(struct i915_address_space *vm,
				     struct sg_table *st,
				     uint64_t start,
				     enum i915_cache_level level, u32 flags)
{
	struct drm_i915_private *dev_priv = to_i915(vm->dev);
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
	struct sgt_iter sgt_iter;
	gen6_pte_t __iomem *gtt_entries;
	gen6_pte_t gtt_entry;
	dma_addr_t addr;
	int i = 0;

	gtt_entries = (gen6_pte_t __iomem *)ggtt->gsm + (start >> PAGE_SHIFT);

	for_each_sgt_dma(addr, sgt_iter, st) {
		gtt_entry = vm->pte_encode(addr, level, flags);
		iowrite32(gtt_entry, &gtt_entries[i++]);
	}

	/* XXX: This serves as a posting read to make sure that the PTE has
	 * actually been updated. There is some concern that even though
	 * registers and PTEs are within the same BAR that they are potentially
	 * of NUMA access patterns. Therefore, even with the way we assume
	 * hardware should work, we must keep this posting read for paranoia.
	 */
	if (i != 0)
		WARN_ON(readl(&gtt_entries[i-1]) != gtt_entry);

	/* This next bit makes the above posting read even more important. We
	 * want to flush the TLBs only after we're certain all the PTE updates
	 * have finished.
	 */
	I915_WRITE(GFX_FLSH_CNTL_GEN6, GFX_FLSH_CNTL_EN);
	POSTING_READ(GFX_FLSH_CNTL_GEN6);
}

static void nop_clear_range(struct i915_address_space *vm,
			    uint64_t start, uint64_t length)
{
}

static void gen8_ggtt_clear_range(struct i915_address_space *vm,
				  uint64_t start, uint64_t length)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
	unsigned first_entry = start >> PAGE_SHIFT;
	unsigned num_entries = length >> PAGE_SHIFT;
	gen8_pte_t scratch_pte, __iomem *gtt_base =
		(gen8_pte_t __iomem *)ggtt->gsm + first_entry;
	const int max_entries = ggtt_total_entries(ggtt) - first_entry;
	int i;

	if (WARN(num_entries > max_entries,
		 "First entry = %d; Num entries = %d (max=%d)\n",
		 first_entry, num_entries, max_entries))
		num_entries = max_entries;

	scratch_pte = gen8_pte_encode(vm->scratch_page.daddr,
				      I915_CACHE_LLC);
	for (i = 0; i < num_entries; i++)
		gen8_set_pte(&gtt_base[i], scratch_pte);
	readl(gtt_base);
}

static void gen6_ggtt_clear_range(struct i915_address_space *vm,
				  uint64_t start,
				  uint64_t length)
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
	readl(gtt_base);
}

static void i915_ggtt_insert_page(struct i915_address_space *vm,
				  dma_addr_t addr,
				  uint64_t offset,
				  enum i915_cache_level cache_level,
				  u32 unused)
{
	unsigned int flags = (cache_level == I915_CACHE_NONE) ?
		AGP_USER_MEMORY : AGP_USER_CACHED_MEMORY;

	intel_gtt_insert_page(addr, offset >> PAGE_SHIFT, flags);
}

static void i915_ggtt_insert_entries(struct i915_address_space *vm,
				     struct sg_table *pages,
				     uint64_t start,
				     enum i915_cache_level cache_level, u32 unused)
{
	unsigned int flags = (cache_level == I915_CACHE_NONE) ?
		AGP_USER_MEMORY : AGP_USER_CACHED_MEMORY;

	intel_gtt_insert_sg_entries(pages, start >> PAGE_SHIFT, flags);

}

static void i915_ggtt_clear_range(struct i915_address_space *vm,
				  uint64_t start,
				  uint64_t length)
{
	intel_gtt_clear_range(start >> PAGE_SHIFT, length >> PAGE_SHIFT);
}

static int ggtt_bind_vma(struct i915_vma *vma,
			 enum i915_cache_level cache_level,
			 u32 flags)
{
	struct drm_i915_private *i915 = to_i915(vma->vm->dev);
	struct drm_i915_gem_object *obj = vma->obj;
	u32 pte_flags = 0;
	int ret;

	ret = i915_get_ggtt_vma_pages(vma);
	if (ret)
		return ret;

	/* Currently applicable only to VLV */
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

static int aliasing_gtt_bind_vma(struct i915_vma *vma,
				 enum i915_cache_level cache_level,
				 u32 flags)
{
	struct drm_i915_private *i915 = to_i915(vma->vm->dev);
	u32 pte_flags;
	int ret;

	ret = i915_get_ggtt_vma_pages(vma);
	if (ret)
		return ret;

	/* Currently applicable only to VLV */
	pte_flags = 0;
	if (vma->obj->gt_ro)
		pte_flags |= PTE_READ_ONLY;


	if (flags & I915_VMA_GLOBAL_BIND) {
		intel_runtime_pm_get(i915);
		vma->vm->insert_entries(vma->vm,
					vma->pages, vma->node.start,
					cache_level, pte_flags);
		intel_runtime_pm_put(i915);
	}

	if (flags & I915_VMA_LOCAL_BIND) {
		struct i915_hw_ppgtt *appgtt = i915->mm.aliasing_ppgtt;
		appgtt->base.insert_entries(&appgtt->base,
					    vma->pages, vma->node.start,
					    cache_level, pte_flags);
	}

	return 0;
}

static void ggtt_unbind_vma(struct i915_vma *vma)
{
	struct drm_i915_private *i915 = to_i915(vma->vm->dev);
	struct i915_hw_ppgtt *appgtt = i915->mm.aliasing_ppgtt;
	const u64 size = min(vma->size, vma->node.size);

	if (vma->flags & I915_VMA_GLOBAL_BIND) {
		intel_runtime_pm_get(i915);
		vma->vm->clear_range(vma->vm,
				     vma->node.start, size);
		intel_runtime_pm_put(i915);
	}

	if (vma->flags & I915_VMA_LOCAL_BIND && appgtt)
		appgtt->base.clear_range(&appgtt->base,
					 vma->node.start, size);
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

static void i915_gtt_color_adjust(struct drm_mm_node *node,
				  unsigned long color,
				  u64 *start,
				  u64 *end)
{
	if (node->color != color)
		*start += 4096;

	node = list_first_entry_or_null(&node->node_list,
					struct drm_mm_node,
					node_list);
	if (node && node->allocated && node->color != color)
		*end -= 4096;
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
	struct i915_hw_ppgtt *ppgtt;
	struct drm_mm_node *entry;
	int ret;

	ret = intel_vgt_balloon(dev_priv);
	if (ret)
		return ret;

	/* Reserve a mappable slot for our lockless error capture */
	ret = drm_mm_insert_node_in_range_generic(&ggtt->base.mm,
						  &ggtt->error_capture,
						  4096, 0, -1,
						  0, ggtt->mappable_end,
						  0, 0);
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
		ppgtt = kzalloc(sizeof(*ppgtt), GFP_KERNEL);
		if (!ppgtt) {
			ret = -ENOMEM;
			goto err;
		}

		ret = __hw_ppgtt_init(ppgtt, dev_priv);
		if (ret)
			goto err_ppgtt;

		if (ppgtt->base.allocate_va_range) {
			ret = ppgtt->base.allocate_va_range(&ppgtt->base, 0,
							    ppgtt->base.total);
			if (ret)
				goto err_ppgtt_cleanup;
		}

		ppgtt->base.clear_range(&ppgtt->base,
					ppgtt->base.start,
					ppgtt->base.total);

		dev_priv->mm.aliasing_ppgtt = ppgtt;
		WARN_ON(ggtt->base.bind_vma != ggtt_bind_vma);
		ggtt->base.bind_vma = aliasing_gtt_bind_vma;
	}

	return 0;

err_ppgtt_cleanup:
	ppgtt->base.cleanup(&ppgtt->base);
err_ppgtt:
	kfree(ppgtt);
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

	if (dev_priv->mm.aliasing_ppgtt) {
		struct i915_hw_ppgtt *ppgtt = dev_priv->mm.aliasing_ppgtt;
		ppgtt->base.cleanup(&ppgtt->base);
		kfree(ppgtt);
	}

	i915_gem_cleanup_stolen(&dev_priv->drm);

	if (drm_mm_node_allocated(&ggtt->error_capture))
		drm_mm_remove_node(&ggtt->error_capture);

	if (drm_mm_initialized(&ggtt->base.mm)) {
		intel_vgt_deballoon(dev_priv);

		drm_mm_takedown(&ggtt->base.mm);
		list_del(&ggtt->base.global_link);
	}

	ggtt->base.cleanup(&ggtt->base);

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
	struct pci_dev *pdev = ggtt->base.dev->pdev;
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
	if (IS_BROXTON(to_i915(ggtt->base.dev)))
		ggtt->gsm = ioremap_nocache(phys_addr, size);
	else
		ggtt->gsm = ioremap_wc(phys_addr, size);
	if (!ggtt->gsm) {
		DRM_ERROR("Failed to map the ggtt page table\n");
		return -ENOMEM;
	}

	ret = setup_scratch_page(ggtt->base.dev,
				 &ggtt->base.scratch_page,
				 GFP_DMA32);
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
	uint64_t pat;

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
	uint64_t pat;

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
	cleanup_scratch_page(vm->dev, &vm->scratch_page);
}

static int gen8_gmch_probe(struct i915_ggtt *ggtt)
{
	struct drm_i915_private *dev_priv = to_i915(ggtt->base.dev);
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

	if (IS_CHERRYVIEW(dev_priv) || IS_BROXTON(dev_priv))
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
	if (IS_CHERRYVIEW(dev_priv))
		ggtt->base.insert_entries = gen8_ggtt_insert_entries__BKL;

	return ggtt_probe_common(ggtt, size);
}

static int gen6_gmch_probe(struct i915_ggtt *ggtt)
{
	struct drm_i915_private *dev_priv = to_i915(ggtt->base.dev);
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
	struct drm_i915_private *dev_priv = to_i915(ggtt->base.dev);
	int ret;

	ret = intel_gmch_probe(dev_priv->bridge_dev, dev_priv->drm.pdev, NULL);
	if (!ret) {
		DRM_ERROR("failed to set up gmch\n");
		return -EIO;
	}

	intel_gtt_get(&ggtt->base.total, &ggtt->stolen_size,
		      &ggtt->mappable_base, &ggtt->mappable_end);

	ggtt->do_idle_maps = needs_idle_maps(dev_priv);
	ggtt->base.insert_page = i915_ggtt_insert_page;
	ggtt->base.insert_entries = i915_ggtt_insert_entries;
	ggtt->base.clear_range = i915_ggtt_clear_range;
	ggtt->base.bind_vma = ggtt_bind_vma;
	ggtt->base.unbind_vma = ggtt_unbind_vma;
	ggtt->base.cleanup = i915_gmch_remove;

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

	ggtt->base.dev = &dev_priv->drm;

	if (INTEL_GEN(dev_priv) <= 5)
		ret = i915_gmch_probe(ggtt);
	else if (INTEL_GEN(dev_priv) < 8)
		ret = gen6_gmch_probe(ggtt);
	else
		ret = gen8_gmch_probe(ggtt);
	if (ret)
		return ret;

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
	DRM_DEBUG_DRIVER("GTT stolen size = %zdM\n", ggtt->stolen_size >> 20);
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

	/* Subtract the guard page before address space initialization to
	 * shrink the range used by drm_mm.
	 */
	mutex_lock(&dev_priv->drm.struct_mutex);
	ggtt->base.total -= PAGE_SIZE;
	i915_address_space_init(&ggtt->base, dev_priv, "[global]");
	ggtt->base.total += PAGE_SIZE;
	if (!HAS_LLC(dev_priv))
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
	ret = i915_gem_init_stolen(&dev_priv->drm);
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

void i915_gem_restore_gtt_mappings(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct i915_ggtt *ggtt = &dev_priv->ggtt;
	struct drm_i915_gem_object *obj, *on;

	i915_check_and_clear_faults(dev_priv);

	/* First fill our portion of the GTT with scratch pages */
	ggtt->base.clear_range(&ggtt->base, ggtt->base.start, ggtt->base.total);

	ggtt->base.closed = true; /* skip rewriting PTE on VMA unbind */

	/* clflush objects bound into the GGTT and rebind them. */
	list_for_each_entry_safe(obj, on,
				 &dev_priv->mm.bound_list, global_list) {
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

	if (INTEL_INFO(dev)->gen >= 8) {
		if (IS_CHERRYVIEW(dev_priv) || IS_BROXTON(dev_priv))
			chv_setup_private_ppat(dev_priv);
		else
			bdw_setup_private_ppat(dev_priv);

		return;
	}

	if (USES_PPGTT(dev)) {
		struct i915_address_space *vm;

		list_for_each_entry(vm, &dev_priv->vm_list, global_link) {
			/* TODO: Perhaps it shouldn't be gen6 specific */

			struct i915_hw_ppgtt *ppgtt;

			if (i915_is_ggtt(vm))
				ppgtt = dev_priv->mm.aliasing_ppgtt;
			else
				ppgtt = i915_vm_to_ppgtt(vm);

			gen6_write_page_range(dev_priv, &ppgtt->pd,
					      0, ppgtt->base.total);
		}
	}

	i915_ggtt_flush(dev_priv);
}

static void
i915_vma_retire(struct i915_gem_active *active,
		struct drm_i915_gem_request *rq)
{
	const unsigned int idx = rq->engine->id;
	struct i915_vma *vma =
		container_of(active, struct i915_vma, last_read[idx]);
	struct drm_i915_gem_object *obj = vma->obj;

	GEM_BUG_ON(!i915_vma_has_active_engine(vma, idx));

	i915_vma_clear_active(vma, idx);
	if (i915_vma_is_active(vma))
		return;

	list_move_tail(&vma->vm_link, &vma->vm->inactive_list);
	if (unlikely(i915_vma_is_closed(vma) && !i915_vma_is_pinned(vma)))
		WARN_ON(i915_vma_unbind(vma));

	GEM_BUG_ON(!i915_gem_object_is_active(obj));
	if (--obj->active_count)
		return;

	/* Bump our place on the bound list to keep it roughly in LRU order
	 * so that we don't steal from recently used but inactive objects
	 * (unless we are forced to ofc!)
	 */
	if (obj->bind_count)
		list_move_tail(&obj->global_list, &rq->i915->mm.bound_list);

	obj->mm.dirty = true; /* be paranoid  */

	if (i915_gem_object_has_active_reference(obj)) {
		i915_gem_object_clear_active_reference(obj);
		i915_gem_object_put(obj);
	}
}

static void
i915_ggtt_retire__write(struct i915_gem_active *active,
			struct drm_i915_gem_request *request)
{
	struct i915_vma *vma =
		container_of(active, struct i915_vma, last_write);

	intel_fb_obj_flush(vma->obj, true, ORIGIN_CS);
}

void i915_vma_destroy(struct i915_vma *vma)
{
	GEM_BUG_ON(vma->node.allocated);
	GEM_BUG_ON(i915_vma_is_active(vma));
	GEM_BUG_ON(!i915_vma_is_closed(vma));
	GEM_BUG_ON(vma->fence);

	rb_erase(&vma->obj_node, &vma->obj->vma_tree);
	list_del(&vma->vm_link);
	if (!i915_vma_is_ggtt(vma))
		i915_ppgtt_put(i915_vm_to_ppgtt(vma->vm));

	kmem_cache_free(to_i915(vma->obj->base.dev)->vmas, vma);
}

void i915_vma_close(struct i915_vma *vma)
{
	GEM_BUG_ON(i915_vma_is_closed(vma));
	vma->flags |= I915_VMA_CLOSED;

	list_del_init(&vma->obj_link);
	if (!i915_vma_is_active(vma) && !i915_vma_is_pinned(vma))
		WARN_ON(i915_vma_unbind(vma));
}

static inline long vma_compare(struct i915_vma *vma,
			       struct i915_address_space *vm,
			       const struct i915_ggtt_view *view)
{
	GEM_BUG_ON(view && !i915_vma_is_ggtt(vma));

	if (vma->vm != vm)
		return vma->vm - vm;

	if (!view)
		return vma->ggtt_view.type;

	if (vma->ggtt_view.type != view->type)
		return vma->ggtt_view.type - view->type;

	return memcmp(&vma->ggtt_view.params,
		      &view->params,
		      sizeof(view->params));
}

static struct i915_vma *
__i915_vma_create(struct drm_i915_gem_object *obj,
		  struct i915_address_space *vm,
		  const struct i915_ggtt_view *view)
{
	struct i915_vma *vma;
	struct rb_node *rb, **p;
	int i;

	GEM_BUG_ON(vm->closed);

	vma = kmem_cache_zalloc(to_i915(obj->base.dev)->vmas, GFP_KERNEL);
	if (vma == NULL)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&vma->exec_list);
	for (i = 0; i < ARRAY_SIZE(vma->last_read); i++)
		init_request_active(&vma->last_read[i], i915_vma_retire);
	init_request_active(&vma->last_write,
			    i915_is_ggtt(vm) ? i915_ggtt_retire__write : NULL);
	init_request_active(&vma->last_fence, NULL);
	list_add(&vma->vm_link, &vm->unbound_list);
	vma->vm = vm;
	vma->obj = obj;
	vma->size = obj->base.size;

	if (view) {
		vma->ggtt_view = *view;
		if (view->type == I915_GGTT_VIEW_PARTIAL) {
			vma->size = view->params.partial.size;
			vma->size <<= PAGE_SHIFT;
		} else if (view->type == I915_GGTT_VIEW_ROTATED) {
			vma->size =
				intel_rotation_info_size(&view->params.rotated);
			vma->size <<= PAGE_SHIFT;
		}
	}

	if (i915_is_ggtt(vm)) {
		vma->flags |= I915_VMA_GGTT;
		list_add(&vma->obj_link, &obj->vma_list);
	} else {
		i915_ppgtt_get(i915_vm_to_ppgtt(vm));
		list_add_tail(&vma->obj_link, &obj->vma_list);
	}

	rb = NULL;
	p = &obj->vma_tree.rb_node;
	while (*p) {
		struct i915_vma *pos;

		rb = *p;
		pos = rb_entry(rb, struct i915_vma, obj_node);
		if (vma_compare(pos, vm, view) < 0)
			p = &rb->rb_right;
		else
			p = &rb->rb_left;
	}
	rb_link_node(&vma->obj_node, rb, p);
	rb_insert_color(&vma->obj_node, &obj->vma_tree);

	return vma;
}

struct i915_vma *
i915_vma_create(struct drm_i915_gem_object *obj,
		struct i915_address_space *vm,
		const struct i915_ggtt_view *view)
{
	lockdep_assert_held(&obj->base.dev->struct_mutex);
	GEM_BUG_ON(view && !i915_is_ggtt(vm));
	GEM_BUG_ON(i915_gem_obj_to_vma(obj, vm, view));

	return __i915_vma_create(obj, vm, view);
}

struct i915_vma *
i915_gem_obj_to_vma(struct drm_i915_gem_object *obj,
		    struct i915_address_space *vm,
		    const struct i915_ggtt_view *view)
{
	struct rb_node *rb;

	rb = obj->vma_tree.rb_node;
	while (rb) {
		struct i915_vma *vma = rb_entry(rb, struct i915_vma, obj_node);
		long cmp;

		cmp = vma_compare(vma, vm, view);
		if (cmp == 0)
			return vma;

		if (cmp < 0)
			rb = rb->rb_right;
		else
			rb = rb->rb_left;
	}

	return NULL;
}

struct i915_vma *
i915_gem_obj_lookup_or_create_vma(struct drm_i915_gem_object *obj,
				  struct i915_address_space *vm,
				  const struct i915_ggtt_view *view)
{
	struct i915_vma *vma;

	lockdep_assert_held(&obj->base.dev->struct_mutex);
	GEM_BUG_ON(view && !i915_is_ggtt(vm));

	vma = i915_gem_obj_to_vma(obj, vm, view);
	if (!vma) {
		vma = __i915_vma_create(obj, vm, view);
		GEM_BUG_ON(vma != i915_gem_obj_to_vma(obj, vm, view));
	}

	GEM_BUG_ON(i915_vma_is_closed(vma));
	return vma;
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

static struct sg_table *
intel_rotate_fb_obj_pages(const struct intel_rotation_info *rot_info,
			  struct drm_i915_gem_object *obj)
{
	const size_t n_pages = obj->base.size / PAGE_SIZE;
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

static struct sg_table *
intel_partial_pages(const struct i915_ggtt_view *view,
		    struct drm_i915_gem_object *obj)
{
	struct sg_table *st;
	struct scatterlist *sg, *iter;
	unsigned int count = view->params.partial.size;
	unsigned int offset;
	int ret = -ENOMEM;

	st = kmalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		goto err_st_alloc;

	ret = sg_alloc_table(st, count, GFP_KERNEL);
	if (ret)
		goto err_sg_alloc;

	iter = i915_gem_object_get_sg(obj,
				      view->params.partial.offset,
				      &offset);
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
	int ret = 0;

	if (vma->pages)
		return 0;

	if (vma->ggtt_view.type == I915_GGTT_VIEW_NORMAL)
		vma->pages = vma->obj->mm.pages;
	else if (vma->ggtt_view.type == I915_GGTT_VIEW_ROTATED)
		vma->pages =
			intel_rotate_fb_obj_pages(&vma->ggtt_view.params.rotated, vma->obj);
	else if (vma->ggtt_view.type == I915_GGTT_VIEW_PARTIAL)
		vma->pages = intel_partial_pages(&vma->ggtt_view, vma->obj);
	else
		WARN_ONCE(1, "GGTT view %u not implemented!\n",
			  vma->ggtt_view.type);

	if (!vma->pages) {
		DRM_ERROR("Failed to get pages for GGTT view type %u!\n",
			  vma->ggtt_view.type);
		ret = -EINVAL;
	} else if (IS_ERR(vma->pages)) {
		ret = PTR_ERR(vma->pages);
		vma->pages = NULL;
		DRM_ERROR("Failed to get pages for VMA view type %u (%d)!\n",
			  vma->ggtt_view.type, ret);
	}

	return ret;
}

/**
 * i915_vma_bind - Sets up PTEs for an VMA in it's corresponding address space.
 * @vma: VMA to map
 * @cache_level: mapping cache level
 * @flags: flags like global or local mapping
 *
 * DMA addresses are taken from the scatter-gather table of this object (or of
 * this VMA in case of non-default GGTT views) and PTE entries set up.
 * Note that DMA addresses are also the only part of the SG table we care about.
 */
int i915_vma_bind(struct i915_vma *vma, enum i915_cache_level cache_level,
		  u32 flags)
{
	u32 bind_flags;
	u32 vma_flags;
	int ret;

	if (WARN_ON(flags == 0))
		return -EINVAL;

	bind_flags = 0;
	if (flags & PIN_GLOBAL)
		bind_flags |= I915_VMA_GLOBAL_BIND;
	if (flags & PIN_USER)
		bind_flags |= I915_VMA_LOCAL_BIND;

	vma_flags = vma->flags & (I915_VMA_GLOBAL_BIND | I915_VMA_LOCAL_BIND);
	if (flags & PIN_UPDATE)
		bind_flags |= vma_flags;
	else
		bind_flags &= ~vma_flags;
	if (bind_flags == 0)
		return 0;

	if (vma_flags == 0 && vma->vm->allocate_va_range) {
		trace_i915_va_alloc(vma);
		ret = vma->vm->allocate_va_range(vma->vm,
						 vma->node.start,
						 vma->node.size);
		if (ret)
			return ret;
	}

	ret = vma->vm->bind_vma(vma, cache_level, bind_flags);
	if (ret)
		return ret;

	vma->flags |= bind_flags;
	return 0;
}

void __iomem *i915_vma_pin_iomap(struct i915_vma *vma)
{
	void __iomem *ptr;

	/* Access through the GTT requires the device to be awake. */
	assert_rpm_wakelock_held(to_i915(vma->vm->dev));

	lockdep_assert_held(&vma->vm->dev->struct_mutex);
	if (WARN_ON(!i915_vma_is_map_and_fenceable(vma)))
		return IO_ERR_PTR(-ENODEV);

	GEM_BUG_ON(!i915_vma_is_ggtt(vma));
	GEM_BUG_ON((vma->flags & I915_VMA_GLOBAL_BIND) == 0);

	ptr = vma->iomap;
	if (ptr == NULL) {
		ptr = io_mapping_map_wc(&i915_vm_to_ggtt(vma->vm)->mappable,
					vma->node.start,
					vma->node.size);
		if (ptr == NULL)
			return IO_ERR_PTR(-ENOMEM);

		vma->iomap = ptr;
	}

	__i915_vma_pin(vma);
	return ptr;
}

void i915_vma_unpin_and_release(struct i915_vma **p_vma)
{
	struct i915_vma *vma;
	struct drm_i915_gem_object *obj;

	vma = fetch_and_zero(p_vma);
	if (!vma)
		return;

	obj = vma->obj;

	i915_vma_unpin(vma);
	i915_vma_close(vma);

	__i915_gem_object_release_unless_active(obj);
}
