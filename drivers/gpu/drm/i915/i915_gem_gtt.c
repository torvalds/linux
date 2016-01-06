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
#include <drm/drmP.h>
#include <drm/i915_drm.h>
#include "i915_drv.h"
#include "i915_vgpu.h"
#include "i915_trace.h"
#include "intel_drv.h"

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

const struct i915_ggtt_view i915_ggtt_view_normal;
const struct i915_ggtt_view i915_ggtt_view_rotated = {
        .type = I915_GGTT_VIEW_ROTATED
};

static int sanitize_enable_ppgtt(struct drm_device *dev, int enable_ppgtt)
{
	bool has_aliasing_ppgtt;
	bool has_full_ppgtt;

	has_aliasing_ppgtt = INTEL_INFO(dev)->gen >= 6;
	has_full_ppgtt = INTEL_INFO(dev)->gen >= 7;

	if (intel_vgpu_active(dev))
		has_full_ppgtt = false; /* emulation is too hard */

	/*
	 * We don't allow disabling PPGTT for gen9+ as it's a requirement for
	 * execlists, the sole mechanism available to submit work.
	 */
	if (INTEL_INFO(dev)->gen < 9 &&
	    (enable_ppgtt == 0 || !has_aliasing_ppgtt))
		return 0;

	if (enable_ppgtt == 1)
		return 1;

	if (enable_ppgtt == 2 && has_full_ppgtt)
		return 2;

#ifdef CONFIG_INTEL_IOMMU
	/* Disable ppgtt on SNB if VT-d is on. */
	if (INTEL_INFO(dev)->gen == 6 && intel_iommu_gfx_mapped) {
		DRM_INFO("Disabling PPGTT because VT-d is on\n");
		return 0;
	}
#endif

	/* Early VLV doesn't have this */
	if (IS_VALLEYVIEW(dev) && !IS_CHERRYVIEW(dev) &&
	    dev->pdev->revision < 0xb) {
		DRM_DEBUG_DRIVER("disabling PPGTT on pre-B3 step VLV\n");
		return 0;
	}

	if (INTEL_INFO(dev)->gen >= 8 && i915.enable_execlists)
		return 2;
	else
		return has_aliasing_ppgtt ? 1 : 0;
}

static int ppgtt_bind_vma(struct i915_vma *vma,
			  enum i915_cache_level cache_level,
			  u32 unused)
{
	u32 pte_flags = 0;

	/* Currently applicable only to VLV */
	if (vma->obj->gt_ro)
		pte_flags |= PTE_READ_ONLY;

	vma->vm->insert_entries(vma->vm, vma->obj->pages, vma->node.start,
				cache_level, pte_flags);

	return 0;
}

static void ppgtt_unbind_vma(struct i915_vma *vma)
{
	vma->vm->clear_range(vma->vm,
			     vma->node.start,
			     vma->obj->base.size,
			     true);
}

static gen8_pte_t gen8_pte_encode(dma_addr_t addr,
				  enum i915_cache_level level,
				  bool valid)
{
	gen8_pte_t pte = valid ? _PAGE_PRESENT | _PAGE_RW : 0;
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
				 bool valid, u32 unused)
{
	gen6_pte_t pte = valid ? GEN6_PTE_VALID : 0;
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
				 bool valid, u32 unused)
{
	gen6_pte_t pte = valid ? GEN6_PTE_VALID : 0;
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
				 bool valid, u32 flags)
{
	gen6_pte_t pte = valid ? GEN6_PTE_VALID : 0;
	pte |= GEN6_PTE_ADDR_ENCODE(addr);

	if (!(flags & PTE_READ_ONLY))
		pte |= BYT_PTE_WRITEABLE;

	if (level != I915_CACHE_NONE)
		pte |= BYT_PTE_SNOOPED_BY_CPU_CACHES;

	return pte;
}

static gen6_pte_t hsw_pte_encode(dma_addr_t addr,
				 enum i915_cache_level level,
				 bool valid, u32 unused)
{
	gen6_pte_t pte = valid ? GEN6_PTE_VALID : 0;
	pte |= HSW_PTE_ADDR_ENCODE(addr);

	if (level != I915_CACHE_NONE)
		pte |= HSW_WB_LLC_AGE3;

	return pte;
}

static gen6_pte_t iris_pte_encode(dma_addr_t addr,
				  enum i915_cache_level level,
				  bool valid, u32 unused)
{
	gen6_pte_t pte = valid ? GEN6_PTE_VALID : 0;
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
	struct device *device = &dev->pdev->dev;

	p->page = alloc_page(flags);
	if (!p->page)
		return -ENOMEM;

	p->daddr = dma_map_page(device,
				p->page, 0, 4096, PCI_DMA_BIDIRECTIONAL);

	if (dma_mapping_error(device, p->daddr)) {
		__free_page(p->page);
		return -EINVAL;
	}

	return 0;
}

static int setup_page_dma(struct drm_device *dev, struct i915_page_dma *p)
{
	return __setup_page_dma(dev, p, GFP_KERNEL);
}

static void cleanup_page_dma(struct drm_device *dev, struct i915_page_dma *p)
{
	if (WARN_ON(!p->page))
		return;

	dma_unmap_page(&dev->pdev->dev, p->daddr, 4096, PCI_DMA_BIDIRECTIONAL);
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
static void kunmap_page_dma(struct drm_device *dev, void *vaddr)
{
	/* There are only few exceptions for gen >=6. chv and bxt.
	 * And we are not sure about the latter so play safe for now.
	 */
	if (IS_CHERRYVIEW(dev) || IS_BROXTON(dev))
		drm_clflush_virt_range(vaddr, PAGE_SIZE);

	kunmap_atomic(vaddr);
}

#define kmap_px(px) kmap_page_dma(px_base(px))
#define kunmap_px(ppgtt, vaddr) kunmap_page_dma((ppgtt)->base.dev, (vaddr))

#define setup_px(dev, px) setup_page_dma((dev), px_base(px))
#define cleanup_px(dev, px) cleanup_page_dma((dev), px_base(px))
#define fill_px(dev, px, v) fill_page_dma((dev), px_base(px), (v))
#define fill32_px(dev, px, v) fill_page_dma_32((dev), px_base(px), (v))

static void fill_page_dma(struct drm_device *dev, struct i915_page_dma *p,
			  const uint64_t val)
{
	int i;
	uint64_t * const vaddr = kmap_page_dma(p);

	for (i = 0; i < 512; i++)
		vaddr[i] = val;

	kunmap_page_dma(dev, vaddr);
}

static void fill_page_dma_32(struct drm_device *dev, struct i915_page_dma *p,
			     const uint32_t val32)
{
	uint64_t v = val32;

	v = v << 32 | val32;

	fill_page_dma(dev, p, v);
}

static struct i915_page_scratch *alloc_scratch_page(struct drm_device *dev)
{
	struct i915_page_scratch *sp;
	int ret;

	sp = kzalloc(sizeof(*sp), GFP_KERNEL);
	if (sp == NULL)
		return ERR_PTR(-ENOMEM);

	ret = __setup_page_dma(dev, px_base(sp), GFP_DMA32 | __GFP_ZERO);
	if (ret) {
		kfree(sp);
		return ERR_PTR(ret);
	}

	set_pages_uc(px_page(sp), 1);

	return sp;
}

static void free_scratch_page(struct drm_device *dev,
			      struct i915_page_scratch *sp)
{
	set_pages_wb(px_page(sp), 1);

	cleanup_px(dev, sp);
	kfree(sp);
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

	scratch_pte = gen8_pte_encode(px_dma(vm->scratch_page),
				      I915_CACHE_LLC, true);

	fill_px(vm->dev, pt, scratch_pte);
}

static void gen6_initialize_pt(struct i915_address_space *vm,
			       struct i915_page_table *pt)
{
	gen6_pte_t scratch_pte;

	WARN_ON(px_dma(vm->scratch_page) == 0);

	scratch_pte = vm->pte_encode(px_dma(vm->scratch_page),
				     I915_CACHE_LLC, true, 0);

	fill32_px(vm->dev, pt, scratch_pte);
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

	fill_px(vm->dev, pd, scratch_pde);
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

	fill_px(vm->dev, pdp, scratch_pdpe);
}

static void gen8_initialize_pml4(struct i915_address_space *vm,
				 struct i915_pml4 *pml4)
{
	gen8_ppgtt_pml4e_t scratch_pml4e;

	scratch_pml4e = gen8_pml4e_encode(px_dma(vm->scratch_pdp),
					  I915_CACHE_LLC);

	fill_px(vm->dev, pml4, scratch_pml4e);
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
	struct intel_engine_cs *ring = req->ring;
	int ret;

	BUG_ON(entry >= 4);

	ret = intel_ring_begin(req, 6);
	if (ret)
		return ret;

	intel_ring_emit(ring, MI_LOAD_REGISTER_IMM(1));
	intel_ring_emit(ring, GEN8_RING_PDP_UDW(ring, entry));
	intel_ring_emit(ring, upper_32_bits(addr));
	intel_ring_emit(ring, MI_LOAD_REGISTER_IMM(1));
	intel_ring_emit(ring, GEN8_RING_PDP_LDW(ring, entry));
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

static void gen8_ppgtt_clear_pte_range(struct i915_address_space *vm,
				       struct i915_page_directory_pointer *pdp,
				       uint64_t start,
				       uint64_t length,
				       gen8_pte_t scratch_pte)
{
	struct i915_hw_ppgtt *ppgtt =
		container_of(vm, struct i915_hw_ppgtt, base);
	gen8_pte_t *pt_vaddr;
	unsigned pdpe = gen8_pdpe_index(start);
	unsigned pde = gen8_pde_index(start);
	unsigned pte = gen8_pte_index(start);
	unsigned num_entries = length >> PAGE_SHIFT;
	unsigned last_pte, i;

	if (WARN_ON(!pdp))
		return;

	while (num_entries) {
		struct i915_page_directory *pd;
		struct i915_page_table *pt;

		if (WARN_ON(!pdp->page_directory[pdpe]))
			break;

		pd = pdp->page_directory[pdpe];

		if (WARN_ON(!pd->page_table[pde]))
			break;

		pt = pd->page_table[pde];

		if (WARN_ON(!px_page(pt)))
			break;

		last_pte = pte + num_entries;
		if (last_pte > GEN8_PTES)
			last_pte = GEN8_PTES;

		pt_vaddr = kmap_px(pt);

		for (i = pte; i < last_pte; i++) {
			pt_vaddr[i] = scratch_pte;
			num_entries--;
		}

		kunmap_px(ppgtt, pt);

		pte = 0;
		if (++pde == I915_PDES) {
			if (++pdpe == I915_PDPES_PER_PDP(vm->dev))
				break;
			pde = 0;
		}
	}
}

static void gen8_ppgtt_clear_range(struct i915_address_space *vm,
				   uint64_t start,
				   uint64_t length,
				   bool use_scratch)
{
	struct i915_hw_ppgtt *ppgtt =
		container_of(vm, struct i915_hw_ppgtt, base);
	gen8_pte_t scratch_pte = gen8_pte_encode(px_dma(vm->scratch_page),
						 I915_CACHE_LLC, use_scratch);

	if (!USES_FULL_48BIT_PPGTT(vm->dev)) {
		gen8_ppgtt_clear_pte_range(vm, &ppgtt->pdp, start, length,
					   scratch_pte);
	} else {
		uint64_t templ4, pml4e;
		struct i915_page_directory_pointer *pdp;

		gen8_for_each_pml4e(pdp, &ppgtt->pml4, start, length, templ4, pml4e) {
			gen8_ppgtt_clear_pte_range(vm, pdp, start, length,
						   scratch_pte);
		}
	}
}

static void
gen8_ppgtt_insert_pte_entries(struct i915_address_space *vm,
			      struct i915_page_directory_pointer *pdp,
			      struct sg_page_iter *sg_iter,
			      uint64_t start,
			      enum i915_cache_level cache_level)
{
	struct i915_hw_ppgtt *ppgtt =
		container_of(vm, struct i915_hw_ppgtt, base);
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
					cache_level, true);
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
	struct i915_hw_ppgtt *ppgtt =
		container_of(vm, struct i915_hw_ppgtt, base);
	struct sg_page_iter sg_iter;

	__sg_page_iter_start(&sg_iter, pages->sgl, sg_nents(pages->sgl), 0);

	if (!USES_FULL_48BIT_PPGTT(vm->dev)) {
		gen8_ppgtt_insert_pte_entries(vm, &ppgtt->pdp, &sg_iter, start,
					      cache_level);
	} else {
		struct i915_page_directory_pointer *pdp;
		uint64_t templ4, pml4e;
		uint64_t length = (uint64_t)pages->orig_nents << PAGE_SHIFT;

		gen8_for_each_pml4e(pdp, &ppgtt->pml4, start, length, templ4, pml4e) {
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

	vm->scratch_page = alloc_scratch_page(dev);
	if (IS_ERR(vm->scratch_page))
		return PTR_ERR(vm->scratch_page);

	vm->scratch_pt = alloc_pt(dev);
	if (IS_ERR(vm->scratch_pt)) {
		free_scratch_page(dev, vm->scratch_page);
		return PTR_ERR(vm->scratch_pt);
	}

	vm->scratch_pd = alloc_pd(dev);
	if (IS_ERR(vm->scratch_pd)) {
		free_pt(dev, vm->scratch_pt);
		free_scratch_page(dev, vm->scratch_page);
		return PTR_ERR(vm->scratch_pd);
	}

	if (USES_FULL_48BIT_PPGTT(dev)) {
		vm->scratch_pdp = alloc_pdp(dev);
		if (IS_ERR(vm->scratch_pdp)) {
			free_pd(dev, vm->scratch_pd);
			free_pt(dev, vm->scratch_pt);
			free_scratch_page(dev, vm->scratch_page);
			return PTR_ERR(vm->scratch_pdp);
		}
	}

	gen8_initialize_pt(vm, vm->scratch_pt);
	gen8_initialize_pd(vm, vm->scratch_pd);
	if (USES_FULL_48BIT_PPGTT(dev))
		gen8_initialize_pdp(vm, vm->scratch_pdp);

	return 0;
}

static int gen8_ppgtt_notify_vgt(struct i915_hw_ppgtt *ppgtt, bool create)
{
	enum vgt_g2v_type msg;
	struct drm_device *dev = ppgtt->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned int offset = vgtif_reg(pdp0_lo);
	int i;

	if (USES_FULL_48BIT_PPGTT(dev)) {
		u64 daddr = px_dma(&ppgtt->pml4);

		I915_WRITE(offset, lower_32_bits(daddr));
		I915_WRITE(offset + 4, upper_32_bits(daddr));

		msg = (create ? VGT_G2V_PPGTT_L4_PAGE_TABLE_CREATE :
				VGT_G2V_PPGTT_L4_PAGE_TABLE_DESTROY);
	} else {
		for (i = 0; i < GEN8_LEGACY_PDPES; i++) {
			u64 daddr = i915_page_dir_dma_addr(ppgtt, i);

			I915_WRITE(offset, lower_32_bits(daddr));
			I915_WRITE(offset + 4, upper_32_bits(daddr));

			offset += 8;
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
	free_scratch_page(dev, vm->scratch_page);
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
	struct i915_hw_ppgtt *ppgtt =
		container_of(vm, struct i915_hw_ppgtt, base);

	if (intel_vgpu_active(vm->dev))
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
	uint64_t temp;
	uint32_t pde;

	gen8_for_each_pde(pt, pd, start, length, temp, pde) {
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
	uint64_t temp;
	uint32_t pdpe;
	uint32_t pdpes = I915_PDPES_PER_PDP(dev);

	WARN_ON(!bitmap_empty(new_pds, pdpes));

	gen8_for_each_pdpe(pd, pdp, start, length, temp, pdpe) {
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
	uint64_t temp;
	uint32_t pml4e;

	WARN_ON(!bitmap_empty(new_pdps, GEN8_PML4ES_PER_PML4));

	gen8_for_each_pml4e(pdp, pml4, start, length, temp, pml4e) {
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

/* PDE TLBs are a pain to invalidate on GEN8+. When we modify
 * the page table structures, we mark them dirty so that
 * context switching/execlist queuing code takes extra steps
 * to ensure that tlbs are flushed.
 */
static void mark_tlbs_dirty(struct i915_hw_ppgtt *ppgtt)
{
	ppgtt->pd_dirty_rings = INTEL_INFO(ppgtt->base.dev)->ring_mask;
}

static int gen8_alloc_va_range_3lvl(struct i915_address_space *vm,
				    struct i915_page_directory_pointer *pdp,
				    uint64_t start,
				    uint64_t length)
{
	struct i915_hw_ppgtt *ppgtt =
		container_of(vm, struct i915_hw_ppgtt, base);
	unsigned long *new_page_dirs, *new_page_tables;
	struct drm_device *dev = vm->dev;
	struct i915_page_directory *pd;
	const uint64_t orig_start = start;
	const uint64_t orig_length = length;
	uint64_t temp;
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
	gen8_for_each_pdpe(pd, pdp, start, length, temp, pdpe) {
		ret = gen8_ppgtt_alloc_pagetabs(vm, pd, start, length,
						new_page_tables + pdpe * BITS_TO_LONGS(I915_PDES));
		if (ret)
			goto err_out;
	}

	start = orig_start;
	length = orig_length;

	/* Allocations have completed successfully, so set the bitmaps, and do
	 * the mappings. */
	gen8_for_each_pdpe(pd, pdp, start, length, temp, pdpe) {
		gen8_pde_t *const page_directory = kmap_px(pd);
		struct i915_page_table *pt;
		uint64_t pd_len = length;
		uint64_t pd_start = start;
		uint32_t pde;

		/* Every pd should be allocated, we just did that above. */
		WARN_ON(!pd);

		gen8_for_each_pde(pt, pd, pd_start, pd_len, temp, pde) {
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
	struct i915_hw_ppgtt *ppgtt =
			container_of(vm, struct i915_hw_ppgtt, base);
	struct i915_page_directory_pointer *pdp;
	uint64_t temp, pml4e;
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

	gen8_for_each_pml4e(pdp, pml4, start, length, temp, pml4e) {
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
	struct i915_hw_ppgtt *ppgtt =
		container_of(vm, struct i915_hw_ppgtt, base);

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
	uint64_t temp;
	uint32_t pdpe;

	gen8_for_each_pdpe(pd, pdp, start, length, temp, pdpe) {
		struct i915_page_table *pt;
		uint64_t pd_len = length;
		uint64_t pd_start = start;
		uint32_t pde;

		if (!test_bit(pdpe, pdp->used_pdpes))
			continue;

		seq_printf(m, "\tPDPE #%d\n", pdpe);
		gen8_for_each_pde(pt, pd, pd_start, pd_len, temp, pde) {
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
	gen8_pte_t scratch_pte = gen8_pte_encode(px_dma(vm->scratch_page),
						 I915_CACHE_LLC, true);

	if (!USES_FULL_48BIT_PPGTT(vm->dev)) {
		gen8_dump_pdp(&ppgtt->pdp, start, length, scratch_pte, m);
	} else {
		uint64_t templ4, pml4e;
		struct i915_pml4 *pml4 = &ppgtt->pml4;
		struct i915_page_directory_pointer *pdp;

		gen8_for_each_pml4e(pdp, pml4, start, length, templ4, pml4e) {
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

		if (intel_vgpu_active(ppgtt->base.dev)) {
			ret = gen8_preallocate_top_level_pdps(ppgtt);
			if (ret)
				goto free_scratch;
		}
	}

	if (intel_vgpu_active(ppgtt->base.dev))
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
	uint32_t  pte, pde, temp;
	uint32_t start = ppgtt->base.start, length = ppgtt->base.total;

	scratch_pte = vm->pte_encode(px_dma(vm->scratch_page),
				     I915_CACHE_LLC, true, 0);

	gen6_for_each_pde(unused, &ppgtt->pd, start, length, temp, pde) {
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
	struct i915_page_table *pt;
	uint32_t pde, temp;

	gen6_for_each_pde(pt, pd, start, length, temp, pde)
		gen6_write_pde(pd, pde, pt);

	/* Make sure write is complete before other code can use this page
	 * table. Also require for WC mapped PTEs */
	readl(dev_priv->gtt.gsm);
}

static uint32_t get_pd_offset(struct i915_hw_ppgtt *ppgtt)
{
	BUG_ON(ppgtt->pd.base.ggtt_offset & 0x3f);

	return (ppgtt->pd.base.ggtt_offset / 64) << 16;
}

static int hsw_mm_switch(struct i915_hw_ppgtt *ppgtt,
			 struct drm_i915_gem_request *req)
{
	struct intel_engine_cs *ring = req->ring;
	int ret;

	/* NB: TLBs must be flushed and invalidated before a switch */
	ret = ring->flush(req, I915_GEM_GPU_DOMAINS, I915_GEM_GPU_DOMAINS);
	if (ret)
		return ret;

	ret = intel_ring_begin(req, 6);
	if (ret)
		return ret;

	intel_ring_emit(ring, MI_LOAD_REGISTER_IMM(2));
	intel_ring_emit(ring, RING_PP_DIR_DCLV(ring));
	intel_ring_emit(ring, PP_DIR_DCLV_2G);
	intel_ring_emit(ring, RING_PP_DIR_BASE(ring));
	intel_ring_emit(ring, get_pd_offset(ppgtt));
	intel_ring_emit(ring, MI_NOOP);
	intel_ring_advance(ring);

	return 0;
}

static int vgpu_mm_switch(struct i915_hw_ppgtt *ppgtt,
			  struct drm_i915_gem_request *req)
{
	struct intel_engine_cs *ring = req->ring;
	struct drm_i915_private *dev_priv = to_i915(ppgtt->base.dev);

	I915_WRITE(RING_PP_DIR_DCLV(ring), PP_DIR_DCLV_2G);
	I915_WRITE(RING_PP_DIR_BASE(ring), get_pd_offset(ppgtt));
	return 0;
}

static int gen7_mm_switch(struct i915_hw_ppgtt *ppgtt,
			  struct drm_i915_gem_request *req)
{
	struct intel_engine_cs *ring = req->ring;
	int ret;

	/* NB: TLBs must be flushed and invalidated before a switch */
	ret = ring->flush(req, I915_GEM_GPU_DOMAINS, I915_GEM_GPU_DOMAINS);
	if (ret)
		return ret;

	ret = intel_ring_begin(req, 6);
	if (ret)
		return ret;

	intel_ring_emit(ring, MI_LOAD_REGISTER_IMM(2));
	intel_ring_emit(ring, RING_PP_DIR_DCLV(ring));
	intel_ring_emit(ring, PP_DIR_DCLV_2G);
	intel_ring_emit(ring, RING_PP_DIR_BASE(ring));
	intel_ring_emit(ring, get_pd_offset(ppgtt));
	intel_ring_emit(ring, MI_NOOP);
	intel_ring_advance(ring);

	/* XXX: RCS is the only one to auto invalidate the TLBs? */
	if (ring->id != RCS) {
		ret = ring->flush(req, I915_GEM_GPU_DOMAINS, I915_GEM_GPU_DOMAINS);
		if (ret)
			return ret;
	}

	return 0;
}

static int gen6_mm_switch(struct i915_hw_ppgtt *ppgtt,
			  struct drm_i915_gem_request *req)
{
	struct intel_engine_cs *ring = req->ring;
	struct drm_device *dev = ppgtt->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;


	I915_WRITE(RING_PP_DIR_DCLV(ring), PP_DIR_DCLV_2G);
	I915_WRITE(RING_PP_DIR_BASE(ring), get_pd_offset(ppgtt));

	POSTING_READ(RING_PP_DIR_DCLV(ring));

	return 0;
}

static void gen8_ppgtt_enable(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_engine_cs *ring;
	int j;

	for_each_ring(ring, dev_priv, j) {
		u32 four_level = USES_FULL_48BIT_PPGTT(dev) ? GEN8_GFX_PPGTT_48B : 0;
		I915_WRITE(RING_MODE_GEN7(ring),
			   _MASKED_BIT_ENABLE(GFX_PPGTT_ENABLE | four_level));
	}
}

static void gen7_ppgtt_enable(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_engine_cs *ring;
	uint32_t ecochk, ecobits;
	int i;

	ecobits = I915_READ(GAC_ECO_BITS);
	I915_WRITE(GAC_ECO_BITS, ecobits | ECOBITS_PPGTT_CACHE64B);

	ecochk = I915_READ(GAM_ECOCHK);
	if (IS_HASWELL(dev)) {
		ecochk |= ECOCHK_PPGTT_WB_HSW;
	} else {
		ecochk |= ECOCHK_PPGTT_LLC_IVB;
		ecochk &= ~ECOCHK_PPGTT_GFDT_IVB;
	}
	I915_WRITE(GAM_ECOCHK, ecochk);

	for_each_ring(ring, dev_priv, i) {
		/* GFX_MODE is per-ring on gen7+ */
		I915_WRITE(RING_MODE_GEN7(ring),
			   _MASKED_BIT_ENABLE(GFX_PPGTT_ENABLE));
	}
}

static void gen6_ppgtt_enable(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
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
				   uint64_t length,
				   bool use_scratch)
{
	struct i915_hw_ppgtt *ppgtt =
		container_of(vm, struct i915_hw_ppgtt, base);
	gen6_pte_t *pt_vaddr, scratch_pte;
	unsigned first_entry = start >> PAGE_SHIFT;
	unsigned num_entries = length >> PAGE_SHIFT;
	unsigned act_pt = first_entry / GEN6_PTES;
	unsigned first_pte = first_entry % GEN6_PTES;
	unsigned last_pte, i;

	scratch_pte = vm->pte_encode(px_dma(vm->scratch_page),
				     I915_CACHE_LLC, true, 0);

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
	struct i915_hw_ppgtt *ppgtt =
		container_of(vm, struct i915_hw_ppgtt, base);
	gen6_pte_t *pt_vaddr;
	unsigned first_entry = start >> PAGE_SHIFT;
	unsigned act_pt = first_entry / GEN6_PTES;
	unsigned act_pte = first_entry % GEN6_PTES;
	struct sg_page_iter sg_iter;

	pt_vaddr = NULL;
	for_each_sg_page(pages->sgl, &sg_iter, pages->nents, 0) {
		if (pt_vaddr == NULL)
			pt_vaddr = kmap_px(ppgtt->pd.page_table[act_pt]);

		pt_vaddr[act_pte] =
			vm->pte_encode(sg_page_iter_dma_address(&sg_iter),
				       cache_level, true, flags);

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
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct i915_hw_ppgtt *ppgtt =
				container_of(vm, struct i915_hw_ppgtt, base);
	struct i915_page_table *pt;
	uint32_t start, length, start_save, length_save;
	uint32_t pde, temp;
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
	gen6_for_each_pde(pt, &ppgtt->pd, start, length, temp, pde) {
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

	gen6_for_each_pde(pt, &ppgtt->pd, start, length, temp, pde) {
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
	readl(dev_priv->gtt.gsm);

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

	vm->scratch_page = alloc_scratch_page(dev);
	if (IS_ERR(vm->scratch_page))
		return PTR_ERR(vm->scratch_page);

	vm->scratch_pt = alloc_pt(dev);
	if (IS_ERR(vm->scratch_pt)) {
		free_scratch_page(dev, vm->scratch_page);
		return PTR_ERR(vm->scratch_pt);
	}

	gen6_initialize_pt(vm, vm->scratch_pt);

	return 0;
}

static void gen6_free_scratch(struct i915_address_space *vm)
{
	struct drm_device *dev = vm->dev;

	free_pt(dev, vm->scratch_pt);
	free_scratch_page(dev, vm->scratch_page);
}

static void gen6_ppgtt_cleanup(struct i915_address_space *vm)
{
	struct i915_hw_ppgtt *ppgtt =
		container_of(vm, struct i915_hw_ppgtt, base);
	struct i915_page_table *pt;
	uint32_t pde;

	drm_mm_remove_node(&ppgtt->node);

	gen6_for_all_pdes(pt, ppgtt, pde) {
		if (pt != vm->scratch_pt)
			free_pt(ppgtt->base.dev, pt);
	}

	gen6_free_scratch(vm);
}

static int gen6_ppgtt_allocate_page_directories(struct i915_hw_ppgtt *ppgtt)
{
	struct i915_address_space *vm = &ppgtt->base;
	struct drm_device *dev = ppgtt->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	bool retried = false;
	int ret;

	/* PPGTT PDEs reside in the GGTT and consists of 512 entries. The
	 * allocator works in address space sizes, so it's multiplied by page
	 * size. We allocate at the top of the GTT to avoid fragmentation.
	 */
	BUG_ON(!drm_mm_initialized(&dev_priv->gtt.base.mm));

	ret = gen6_init_scratch(vm);
	if (ret)
		return ret;

alloc:
	ret = drm_mm_insert_node_in_range_generic(&dev_priv->gtt.base.mm,
						  &ppgtt->node, GEN6_PD_SIZE,
						  GEN6_PD_ALIGN, 0,
						  0, dev_priv->gtt.base.total,
						  DRM_MM_TOPDOWN);
	if (ret == -ENOSPC && !retried) {
		ret = i915_gem_evict_something(dev, &dev_priv->gtt.base,
					       GEN6_PD_SIZE, GEN6_PD_ALIGN,
					       I915_CACHE_NONE,
					       0, dev_priv->gtt.base.total,
					       0);
		if (ret)
			goto err_out;

		retried = true;
		goto alloc;
	}

	if (ret)
		goto err_out;


	if (ppgtt->node.start < dev_priv->gtt.mappable_end)
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
	uint32_t pde, temp;

	gen6_for_each_pde(unused, &ppgtt->pd, start, length, temp, pde)
		ppgtt->pd.page_table[pde] = ppgtt->base.scratch_pt;
}

static int gen6_ppgtt_init(struct i915_hw_ppgtt *ppgtt)
{
	struct drm_device *dev = ppgtt->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	ppgtt->base.pte_encode = dev_priv->gtt.base.pte_encode;
	if (IS_GEN6(dev)) {
		ppgtt->switch_mm = gen6_mm_switch;
	} else if (IS_HASWELL(dev)) {
		ppgtt->switch_mm = hsw_mm_switch;
	} else if (IS_GEN7(dev)) {
		ppgtt->switch_mm = gen7_mm_switch;
	} else
		BUG();

	if (intel_vgpu_active(dev))
		ppgtt->switch_mm = vgpu_mm_switch;

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

	ppgtt->pd_addr = (gen6_pte_t __iomem *)dev_priv->gtt.gsm +
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

static int __hw_ppgtt_init(struct drm_device *dev, struct i915_hw_ppgtt *ppgtt)
{
	ppgtt->base.dev = dev;

	if (INTEL_INFO(dev)->gen < 8)
		return gen6_ppgtt_init(ppgtt);
	else
		return gen8_ppgtt_init(ppgtt);
}

static void i915_address_space_init(struct i915_address_space *vm,
				    struct drm_i915_private *dev_priv)
{
	drm_mm_init(&vm->mm, vm->start, vm->total);
	vm->dev = dev_priv->dev;
	INIT_LIST_HEAD(&vm->active_list);
	INIT_LIST_HEAD(&vm->inactive_list);
	list_add_tail(&vm->global_link, &dev_priv->vm_list);
}

int i915_ppgtt_init(struct drm_device *dev, struct i915_hw_ppgtt *ppgtt)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret = 0;

	ret = __hw_ppgtt_init(dev, ppgtt);
	if (ret == 0) {
		kref_init(&ppgtt->ref);
		i915_address_space_init(&ppgtt->base, dev_priv);
	}

	return ret;
}

int i915_ppgtt_init_hw(struct drm_device *dev)
{
	/* In the case of execlists, PPGTT is enabled by the context descriptor
	 * and the PDPs are contained within the context itself.  We don't
	 * need to do anything here. */
	if (i915.enable_execlists)
		return 0;

	if (!USES_PPGTT(dev))
		return 0;

	if (IS_GEN6(dev))
		gen6_ppgtt_enable(dev);
	else if (IS_GEN7(dev))
		gen7_ppgtt_enable(dev);
	else if (INTEL_INFO(dev)->gen >= 8)
		gen8_ppgtt_enable(dev);
	else
		MISSING_CASE(INTEL_INFO(dev)->gen);

	return 0;
}

int i915_ppgtt_init_ring(struct drm_i915_gem_request *req)
{
	struct drm_i915_private *dev_priv = req->ring->dev->dev_private;
	struct i915_hw_ppgtt *ppgtt = dev_priv->mm.aliasing_ppgtt;

	if (i915.enable_execlists)
		return 0;

	if (!ppgtt)
		return 0;

	return ppgtt->switch_mm(ppgtt, req);
}

struct i915_hw_ppgtt *
i915_ppgtt_create(struct drm_device *dev, struct drm_i915_file_private *fpriv)
{
	struct i915_hw_ppgtt *ppgtt;
	int ret;

	ppgtt = kzalloc(sizeof(*ppgtt), GFP_KERNEL);
	if (!ppgtt)
		return ERR_PTR(-ENOMEM);

	ret = i915_ppgtt_init(dev, ppgtt);
	if (ret) {
		kfree(ppgtt);
		return ERR_PTR(ret);
	}

	ppgtt->file_priv = fpriv;

	trace_i915_ppgtt_create(&ppgtt->base);

	return ppgtt;
}

void  i915_ppgtt_release(struct kref *kref)
{
	struct i915_hw_ppgtt *ppgtt =
		container_of(kref, struct i915_hw_ppgtt, ref);

	trace_i915_ppgtt_release(&ppgtt->base);

	/* vmas should already be unbound */
	WARN_ON(!list_empty(&ppgtt->base.active_list));
	WARN_ON(!list_empty(&ppgtt->base.inactive_list));

	list_del(&ppgtt->base.global_link);
	drm_mm_takedown(&ppgtt->base.mm);

	ppgtt->base.cleanup(&ppgtt->base);
	kfree(ppgtt);
}

extern int intel_iommu_gfx_mapped;
/* Certain Gen5 chipsets require require idling the GPU before
 * unmapping anything from the GTT when VT-d is enabled.
 */
static bool needs_idle_maps(struct drm_device *dev)
{
#ifdef CONFIG_INTEL_IOMMU
	/* Query intel_iommu to see if we need the workaround. Presumably that
	 * was loaded first.
	 */
	if (IS_GEN5(dev) && IS_MOBILE(dev) && intel_iommu_gfx_mapped)
		return true;
#endif
	return false;
}

static bool do_idling(struct drm_i915_private *dev_priv)
{
	bool ret = dev_priv->mm.interruptible;

	if (unlikely(dev_priv->gtt.do_idle_maps)) {
		dev_priv->mm.interruptible = false;
		if (i915_gpu_idle(dev_priv->dev)) {
			DRM_ERROR("Couldn't idle GPU\n");
			/* Wait a bit, in hopes it avoids the hang */
			udelay(10);
		}
	}

	return ret;
}

static void undo_idling(struct drm_i915_private *dev_priv, bool interruptible)
{
	if (unlikely(dev_priv->gtt.do_idle_maps))
		dev_priv->mm.interruptible = interruptible;
}

void i915_check_and_clear_faults(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_engine_cs *ring;
	int i;

	if (INTEL_INFO(dev)->gen < 6)
		return;

	for_each_ring(ring, dev_priv, i) {
		u32 fault_reg;
		fault_reg = I915_READ(RING_FAULT_REG(ring));
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
			I915_WRITE(RING_FAULT_REG(ring),
				   fault_reg & ~RING_FAULT_VALID);
		}
	}
	POSTING_READ(RING_FAULT_REG(&dev_priv->ring[RCS]));
}

static void i915_ggtt_flush(struct drm_i915_private *dev_priv)
{
	if (INTEL_INFO(dev_priv->dev)->gen < 6) {
		intel_gtt_chipset_flush();
	} else {
		I915_WRITE(GFX_FLSH_CNTL_GEN6, GFX_FLSH_CNTL_EN);
		POSTING_READ(GFX_FLSH_CNTL_GEN6);
	}
}

void i915_gem_suspend_gtt_mappings(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	/* Don't bother messing with faults pre GEN6 as we have little
	 * documentation supporting that it's a good idea.
	 */
	if (INTEL_INFO(dev)->gen < 6)
		return;

	i915_check_and_clear_faults(dev);

	dev_priv->gtt.base.clear_range(&dev_priv->gtt.base,
				       dev_priv->gtt.base.start,
				       dev_priv->gtt.base.total,
				       true);

	i915_ggtt_flush(dev_priv);
}

int i915_gem_gtt_prepare_object(struct drm_i915_gem_object *obj)
{
	if (!dma_map_sg(&obj->base.dev->pdev->dev,
			obj->pages->sgl, obj->pages->nents,
			PCI_DMA_BIDIRECTIONAL))
		return -ENOSPC;

	return 0;
}

static void gen8_set_pte(void __iomem *addr, gen8_pte_t pte)
{
#ifdef writeq
	writeq(pte, addr);
#else
	iowrite32((u32)pte, addr);
	iowrite32(pte >> 32, addr + 4);
#endif
}

static void gen8_ggtt_insert_entries(struct i915_address_space *vm,
				     struct sg_table *st,
				     uint64_t start,
				     enum i915_cache_level level, u32 unused)
{
	struct drm_i915_private *dev_priv = vm->dev->dev_private;
	unsigned first_entry = start >> PAGE_SHIFT;
	gen8_pte_t __iomem *gtt_entries =
		(gen8_pte_t __iomem *)dev_priv->gtt.gsm + first_entry;
	int i = 0;
	struct sg_page_iter sg_iter;
	dma_addr_t addr = 0; /* shut up gcc */

	for_each_sg_page(st->sgl, &sg_iter, st->nents, 0) {
		addr = sg_dma_address(sg_iter.sg) +
			(sg_iter.sg_pgoffset << PAGE_SHIFT);
		gen8_set_pte(&gtt_entries[i],
			     gen8_pte_encode(addr, level, true));
		i++;
	}

	/*
	 * XXX: This serves as a posting read to make sure that the PTE has
	 * actually been updated. There is some concern that even though
	 * registers and PTEs are within the same BAR that they are potentially
	 * of NUMA access patterns. Therefore, even with the way we assume
	 * hardware should work, we must keep this posting read for paranoia.
	 */
	if (i != 0)
		WARN_ON(readq(&gtt_entries[i-1])
			!= gen8_pte_encode(addr, level, true));

	/* This next bit makes the above posting read even more important. We
	 * want to flush the TLBs only after we're certain all the PTE updates
	 * have finished.
	 */
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
	struct drm_i915_private *dev_priv = vm->dev->dev_private;
	unsigned first_entry = start >> PAGE_SHIFT;
	gen6_pte_t __iomem *gtt_entries =
		(gen6_pte_t __iomem *)dev_priv->gtt.gsm + first_entry;
	int i = 0;
	struct sg_page_iter sg_iter;
	dma_addr_t addr = 0;

	for_each_sg_page(st->sgl, &sg_iter, st->nents, 0) {
		addr = sg_page_iter_dma_address(&sg_iter);
		iowrite32(vm->pte_encode(addr, level, true, flags), &gtt_entries[i]);
		i++;
	}

	/* XXX: This serves as a posting read to make sure that the PTE has
	 * actually been updated. There is some concern that even though
	 * registers and PTEs are within the same BAR that they are potentially
	 * of NUMA access patterns. Therefore, even with the way we assume
	 * hardware should work, we must keep this posting read for paranoia.
	 */
	if (i != 0) {
		unsigned long gtt = readl(&gtt_entries[i-1]);
		WARN_ON(gtt != vm->pte_encode(addr, level, true, flags));
	}

	/* This next bit makes the above posting read even more important. We
	 * want to flush the TLBs only after we're certain all the PTE updates
	 * have finished.
	 */
	I915_WRITE(GFX_FLSH_CNTL_GEN6, GFX_FLSH_CNTL_EN);
	POSTING_READ(GFX_FLSH_CNTL_GEN6);
}

static void gen8_ggtt_clear_range(struct i915_address_space *vm,
				  uint64_t start,
				  uint64_t length,
				  bool use_scratch)
{
	struct drm_i915_private *dev_priv = vm->dev->dev_private;
	unsigned first_entry = start >> PAGE_SHIFT;
	unsigned num_entries = length >> PAGE_SHIFT;
	gen8_pte_t scratch_pte, __iomem *gtt_base =
		(gen8_pte_t __iomem *) dev_priv->gtt.gsm + first_entry;
	const int max_entries = gtt_total_entries(dev_priv->gtt) - first_entry;
	int i;

	if (WARN(num_entries > max_entries,
		 "First entry = %d; Num entries = %d (max=%d)\n",
		 first_entry, num_entries, max_entries))
		num_entries = max_entries;

	scratch_pte = gen8_pte_encode(px_dma(vm->scratch_page),
				      I915_CACHE_LLC,
				      use_scratch);
	for (i = 0; i < num_entries; i++)
		gen8_set_pte(&gtt_base[i], scratch_pte);
	readl(gtt_base);
}

static void gen6_ggtt_clear_range(struct i915_address_space *vm,
				  uint64_t start,
				  uint64_t length,
				  bool use_scratch)
{
	struct drm_i915_private *dev_priv = vm->dev->dev_private;
	unsigned first_entry = start >> PAGE_SHIFT;
	unsigned num_entries = length >> PAGE_SHIFT;
	gen6_pte_t scratch_pte, __iomem *gtt_base =
		(gen6_pte_t __iomem *) dev_priv->gtt.gsm + first_entry;
	const int max_entries = gtt_total_entries(dev_priv->gtt) - first_entry;
	int i;

	if (WARN(num_entries > max_entries,
		 "First entry = %d; Num entries = %d (max=%d)\n",
		 first_entry, num_entries, max_entries))
		num_entries = max_entries;

	scratch_pte = vm->pte_encode(px_dma(vm->scratch_page),
				     I915_CACHE_LLC, use_scratch, 0);

	for (i = 0; i < num_entries; i++)
		iowrite32(scratch_pte, &gtt_base[i]);
	readl(gtt_base);
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
				  uint64_t length,
				  bool unused)
{
	unsigned first_entry = start >> PAGE_SHIFT;
	unsigned num_entries = length >> PAGE_SHIFT;
	intel_gtt_clear_range(first_entry, num_entries);
}

static int ggtt_bind_vma(struct i915_vma *vma,
			 enum i915_cache_level cache_level,
			 u32 flags)
{
	struct drm_i915_gem_object *obj = vma->obj;
	u32 pte_flags = 0;
	int ret;

	ret = i915_get_ggtt_vma_pages(vma);
	if (ret)
		return ret;

	/* Currently applicable only to VLV */
	if (obj->gt_ro)
		pte_flags |= PTE_READ_ONLY;

	vma->vm->insert_entries(vma->vm, vma->ggtt_view.pages,
				vma->node.start,
				cache_level, pte_flags);

	/*
	 * Without aliasing PPGTT there's no difference between
	 * GLOBAL/LOCAL_BIND, it's all the same ptes. Hence unconditionally
	 * upgrade to both bound if we bind either to avoid double-binding.
	 */
	vma->bound |= GLOBAL_BIND | LOCAL_BIND;

	return 0;
}

static int aliasing_gtt_bind_vma(struct i915_vma *vma,
				 enum i915_cache_level cache_level,
				 u32 flags)
{
	struct drm_device *dev = vma->vm->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj = vma->obj;
	struct sg_table *pages = obj->pages;
	u32 pte_flags = 0;
	int ret;

	ret = i915_get_ggtt_vma_pages(vma);
	if (ret)
		return ret;
	pages = vma->ggtt_view.pages;

	/* Currently applicable only to VLV */
	if (obj->gt_ro)
		pte_flags |= PTE_READ_ONLY;


	if (flags & GLOBAL_BIND) {
		vma->vm->insert_entries(vma->vm, pages,
					vma->node.start,
					cache_level, pte_flags);
	}

	if (flags & LOCAL_BIND) {
		struct i915_hw_ppgtt *appgtt = dev_priv->mm.aliasing_ppgtt;
		appgtt->base.insert_entries(&appgtt->base, pages,
					    vma->node.start,
					    cache_level, pte_flags);
	}

	return 0;
}

static void ggtt_unbind_vma(struct i915_vma *vma)
{
	struct drm_device *dev = vma->vm->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj = vma->obj;
	const uint64_t size = min_t(uint64_t,
				    obj->base.size,
				    vma->node.size);

	if (vma->bound & GLOBAL_BIND) {
		vma->vm->clear_range(vma->vm,
				     vma->node.start,
				     size,
				     true);
	}

	if (dev_priv->mm.aliasing_ppgtt && vma->bound & LOCAL_BIND) {
		struct i915_hw_ppgtt *appgtt = dev_priv->mm.aliasing_ppgtt;

		appgtt->base.clear_range(&appgtt->base,
					 vma->node.start,
					 size,
					 true);
	}
}

void i915_gem_gtt_finish_object(struct drm_i915_gem_object *obj)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	bool interruptible;

	interruptible = do_idling(dev_priv);

	dma_unmap_sg(&dev->pdev->dev, obj->pages->sgl, obj->pages->nents,
		     PCI_DMA_BIDIRECTIONAL);

	undo_idling(dev_priv, interruptible);
}

static void i915_gtt_color_adjust(struct drm_mm_node *node,
				  unsigned long color,
				  u64 *start,
				  u64 *end)
{
	if (node->color != color)
		*start += 4096;

	if (!list_empty(&node->node_list)) {
		node = list_entry(node->node_list.next,
				  struct drm_mm_node,
				  node_list);
		if (node->allocated && node->color != color)
			*end -= 4096;
	}
}

static int i915_gem_setup_global_gtt(struct drm_device *dev,
				     u64 start,
				     u64 mappable_end,
				     u64 end)
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
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct i915_address_space *ggtt_vm = &dev_priv->gtt.base;
	struct drm_mm_node *entry;
	struct drm_i915_gem_object *obj;
	unsigned long hole_start, hole_end;
	int ret;

	BUG_ON(mappable_end > end);

	ggtt_vm->start = start;

	/* Subtract the guard page before address space initialization to
	 * shrink the range used by drm_mm */
	ggtt_vm->total = end - start - PAGE_SIZE;
	i915_address_space_init(ggtt_vm, dev_priv);
	ggtt_vm->total += PAGE_SIZE;

	if (intel_vgpu_active(dev)) {
		ret = intel_vgt_balloon(dev);
		if (ret)
			return ret;
	}

	if (!HAS_LLC(dev))
		ggtt_vm->mm.color_adjust = i915_gtt_color_adjust;

	/* Mark any preallocated objects as occupied */
	list_for_each_entry(obj, &dev_priv->mm.bound_list, global_list) {
		struct i915_vma *vma = i915_gem_obj_to_vma(obj, ggtt_vm);

		DRM_DEBUG_KMS("reserving preallocated space: %llx + %zx\n",
			      i915_gem_obj_ggtt_offset(obj), obj->base.size);

		WARN_ON(i915_gem_obj_ggtt_bound(obj));
		ret = drm_mm_reserve_node(&ggtt_vm->mm, &vma->node);
		if (ret) {
			DRM_DEBUG_KMS("Reservation failed: %i\n", ret);
			return ret;
		}
		vma->bound |= GLOBAL_BIND;
		__i915_vma_set_map_and_fenceable(vma);
		list_add_tail(&vma->mm_list, &ggtt_vm->inactive_list);
	}

	/* Clear any non-preallocated blocks */
	drm_mm_for_each_hole(entry, &ggtt_vm->mm, hole_start, hole_end) {
		DRM_DEBUG_KMS("clearing unused GTT space: [%lx, %lx]\n",
			      hole_start, hole_end);
		ggtt_vm->clear_range(ggtt_vm, hole_start,
				     hole_end - hole_start, true);
	}

	/* And finally clear the reserved guard page */
	ggtt_vm->clear_range(ggtt_vm, end - PAGE_SIZE, PAGE_SIZE, true);

	if (USES_PPGTT(dev) && !USES_FULL_PPGTT(dev)) {
		struct i915_hw_ppgtt *ppgtt;

		ppgtt = kzalloc(sizeof(*ppgtt), GFP_KERNEL);
		if (!ppgtt)
			return -ENOMEM;

		ret = __hw_ppgtt_init(dev, ppgtt);
		if (ret) {
			ppgtt->base.cleanup(&ppgtt->base);
			kfree(ppgtt);
			return ret;
		}

		if (ppgtt->base.allocate_va_range)
			ret = ppgtt->base.allocate_va_range(&ppgtt->base, 0,
							    ppgtt->base.total);
		if (ret) {
			ppgtt->base.cleanup(&ppgtt->base);
			kfree(ppgtt);
			return ret;
		}

		ppgtt->base.clear_range(&ppgtt->base,
					ppgtt->base.start,
					ppgtt->base.total,
					true);

		dev_priv->mm.aliasing_ppgtt = ppgtt;
		WARN_ON(dev_priv->gtt.base.bind_vma != ggtt_bind_vma);
		dev_priv->gtt.base.bind_vma = aliasing_gtt_bind_vma;
	}

	return 0;
}

void i915_gem_init_global_gtt(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u64 gtt_size, mappable_size;

	gtt_size = dev_priv->gtt.base.total;
	mappable_size = dev_priv->gtt.mappable_end;

	i915_gem_setup_global_gtt(dev, 0, mappable_size, gtt_size);
}

void i915_global_gtt_cleanup(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct i915_address_space *vm = &dev_priv->gtt.base;

	if (dev_priv->mm.aliasing_ppgtt) {
		struct i915_hw_ppgtt *ppgtt = dev_priv->mm.aliasing_ppgtt;

		ppgtt->base.cleanup(&ppgtt->base);
	}

	if (drm_mm_initialized(&vm->mm)) {
		if (intel_vgpu_active(dev))
			intel_vgt_deballoon();

		drm_mm_takedown(&vm->mm);
		list_del(&vm->global_link);
	}

	vm->cleanup(vm);
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

static int ggtt_probe_common(struct drm_device *dev,
			     size_t gtt_size)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct i915_page_scratch *scratch_page;
	phys_addr_t gtt_phys_addr;

	/* For Modern GENs the PTEs and register space are split in the BAR */
	gtt_phys_addr = pci_resource_start(dev->pdev, 0) +
		(pci_resource_len(dev->pdev, 0) / 2);

	/*
	 * On BXT writes larger than 64 bit to the GTT pagetable range will be
	 * dropped. For WC mappings in general we have 64 byte burst writes
	 * when the WC buffer is flushed, so we can't use it, but have to
	 * resort to an uncached mapping. The WC issue is easily caught by the
	 * readback check when writing GTT PTE entries.
	 */
	if (IS_BROXTON(dev))
		dev_priv->gtt.gsm = ioremap_nocache(gtt_phys_addr, gtt_size);
	else
		dev_priv->gtt.gsm = ioremap_wc(gtt_phys_addr, gtt_size);
	if (!dev_priv->gtt.gsm) {
		DRM_ERROR("Failed to map the gtt page table\n");
		return -ENOMEM;
	}

	scratch_page = alloc_scratch_page(dev);
	if (IS_ERR(scratch_page)) {
		DRM_ERROR("Scratch setup failed\n");
		/* iounmap will also get called at remove, but meh */
		iounmap(dev_priv->gtt.gsm);
		return PTR_ERR(scratch_page);
	}

	dev_priv->gtt.base.scratch_page = scratch_page;

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

	if (!USES_PPGTT(dev_priv->dev))
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

static int gen8_gmch_probe(struct drm_device *dev,
			   u64 *gtt_total,
			   size_t *stolen,
			   phys_addr_t *mappable_base,
			   u64 *mappable_end)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u64 gtt_size;
	u16 snb_gmch_ctl;
	int ret;

	/* TODO: We're not aware of mappable constraints on gen8 yet */
	*mappable_base = pci_resource_start(dev->pdev, 2);
	*mappable_end = pci_resource_len(dev->pdev, 2);

	if (!pci_set_dma_mask(dev->pdev, DMA_BIT_MASK(39)))
		pci_set_consistent_dma_mask(dev->pdev, DMA_BIT_MASK(39));

	pci_read_config_word(dev->pdev, SNB_GMCH_CTRL, &snb_gmch_ctl);

	if (INTEL_INFO(dev)->gen >= 9) {
		*stolen = gen9_get_stolen_size(snb_gmch_ctl);
		gtt_size = gen8_get_total_gtt_size(snb_gmch_ctl);
	} else if (IS_CHERRYVIEW(dev)) {
		*stolen = chv_get_stolen_size(snb_gmch_ctl);
		gtt_size = chv_get_total_gtt_size(snb_gmch_ctl);
	} else {
		*stolen = gen8_get_stolen_size(snb_gmch_ctl);
		gtt_size = gen8_get_total_gtt_size(snb_gmch_ctl);
	}

	*gtt_total = (gtt_size / sizeof(gen8_pte_t)) << PAGE_SHIFT;

	if (IS_CHERRYVIEW(dev) || IS_BROXTON(dev))
		chv_setup_private_ppat(dev_priv);
	else
		bdw_setup_private_ppat(dev_priv);

	ret = ggtt_probe_common(dev, gtt_size);

	dev_priv->gtt.base.clear_range = gen8_ggtt_clear_range;
	dev_priv->gtt.base.insert_entries = gen8_ggtt_insert_entries;
	dev_priv->gtt.base.bind_vma = ggtt_bind_vma;
	dev_priv->gtt.base.unbind_vma = ggtt_unbind_vma;

	return ret;
}

static int gen6_gmch_probe(struct drm_device *dev,
			   u64 *gtt_total,
			   size_t *stolen,
			   phys_addr_t *mappable_base,
			   u64 *mappable_end)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned int gtt_size;
	u16 snb_gmch_ctl;
	int ret;

	*mappable_base = pci_resource_start(dev->pdev, 2);
	*mappable_end = pci_resource_len(dev->pdev, 2);

	/* 64/512MB is the current min/max we actually know of, but this is just
	 * a coarse sanity check.
	 */
	if ((*mappable_end < (64<<20) || (*mappable_end > (512<<20)))) {
		DRM_ERROR("Unknown GMADR size (%llx)\n",
			  dev_priv->gtt.mappable_end);
		return -ENXIO;
	}

	if (!pci_set_dma_mask(dev->pdev, DMA_BIT_MASK(40)))
		pci_set_consistent_dma_mask(dev->pdev, DMA_BIT_MASK(40));
	pci_read_config_word(dev->pdev, SNB_GMCH_CTRL, &snb_gmch_ctl);

	*stolen = gen6_get_stolen_size(snb_gmch_ctl);

	gtt_size = gen6_get_total_gtt_size(snb_gmch_ctl);
	*gtt_total = (gtt_size / sizeof(gen6_pte_t)) << PAGE_SHIFT;

	ret = ggtt_probe_common(dev, gtt_size);

	dev_priv->gtt.base.clear_range = gen6_ggtt_clear_range;
	dev_priv->gtt.base.insert_entries = gen6_ggtt_insert_entries;
	dev_priv->gtt.base.bind_vma = ggtt_bind_vma;
	dev_priv->gtt.base.unbind_vma = ggtt_unbind_vma;

	return ret;
}

static void gen6_gmch_remove(struct i915_address_space *vm)
{

	struct i915_gtt *gtt = container_of(vm, struct i915_gtt, base);

	iounmap(gtt->gsm);
	free_scratch_page(vm->dev, vm->scratch_page);
}

static int i915_gmch_probe(struct drm_device *dev,
			   u64 *gtt_total,
			   size_t *stolen,
			   phys_addr_t *mappable_base,
			   u64 *mappable_end)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	ret = intel_gmch_probe(dev_priv->bridge_dev, dev_priv->dev->pdev, NULL);
	if (!ret) {
		DRM_ERROR("failed to set up gmch\n");
		return -EIO;
	}

	intel_gtt_get(gtt_total, stolen, mappable_base, mappable_end);

	dev_priv->gtt.do_idle_maps = needs_idle_maps(dev_priv->dev);
	dev_priv->gtt.base.insert_entries = i915_ggtt_insert_entries;
	dev_priv->gtt.base.clear_range = i915_ggtt_clear_range;
	dev_priv->gtt.base.bind_vma = ggtt_bind_vma;
	dev_priv->gtt.base.unbind_vma = ggtt_unbind_vma;

	if (unlikely(dev_priv->gtt.do_idle_maps))
		DRM_INFO("applying Ironlake quirks for intel_iommu\n");

	return 0;
}

static void i915_gmch_remove(struct i915_address_space *vm)
{
	intel_gmch_remove();
}

int i915_gem_gtt_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct i915_gtt *gtt = &dev_priv->gtt;
	int ret;

	if (INTEL_INFO(dev)->gen <= 5) {
		gtt->gtt_probe = i915_gmch_probe;
		gtt->base.cleanup = i915_gmch_remove;
	} else if (INTEL_INFO(dev)->gen < 8) {
		gtt->gtt_probe = gen6_gmch_probe;
		gtt->base.cleanup = gen6_gmch_remove;
		if (IS_HASWELL(dev) && dev_priv->ellc_size)
			gtt->base.pte_encode = iris_pte_encode;
		else if (IS_HASWELL(dev))
			gtt->base.pte_encode = hsw_pte_encode;
		else if (IS_VALLEYVIEW(dev))
			gtt->base.pte_encode = byt_pte_encode;
		else if (INTEL_INFO(dev)->gen >= 7)
			gtt->base.pte_encode = ivb_pte_encode;
		else
			gtt->base.pte_encode = snb_pte_encode;
	} else {
		dev_priv->gtt.gtt_probe = gen8_gmch_probe;
		dev_priv->gtt.base.cleanup = gen6_gmch_remove;
	}

	gtt->base.dev = dev;

	ret = gtt->gtt_probe(dev, &gtt->base.total, &gtt->stolen_size,
			     &gtt->mappable_base, &gtt->mappable_end);
	if (ret)
		return ret;

	/* GMADR is the PCI mmio aperture into the global GTT. */
	DRM_INFO("Memory usable by graphics device = %lluM\n",
		 gtt->base.total >> 20);
	DRM_DEBUG_DRIVER("GMADR size = %lldM\n", gtt->mappable_end >> 20);
	DRM_DEBUG_DRIVER("GTT stolen size = %zdM\n", gtt->stolen_size >> 20);
#ifdef CONFIG_INTEL_IOMMU
	if (intel_iommu_gfx_mapped)
		DRM_INFO("VT-d active for gfx access\n");
#endif
	/*
	 * i915.enable_ppgtt is read-only, so do an early pass to validate the
	 * user's requested state against the hardware/driver capabilities.  We
	 * do this now so that we can print out any log messages once rather
	 * than every time we check intel_enable_ppgtt().
	 */
	i915.enable_ppgtt = sanitize_enable_ppgtt(dev, i915.enable_ppgtt);
	DRM_DEBUG_DRIVER("ppgtt mode: %i\n", i915.enable_ppgtt);

	return 0;
}

void i915_gem_restore_gtt_mappings(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj;
	struct i915_address_space *vm;
	struct i915_vma *vma;
	bool flush;

	i915_check_and_clear_faults(dev);

	/* First fill our portion of the GTT with scratch pages */
	dev_priv->gtt.base.clear_range(&dev_priv->gtt.base,
				       dev_priv->gtt.base.start,
				       dev_priv->gtt.base.total,
				       true);

	/* Cache flush objects bound into GGTT and rebind them. */
	vm = &dev_priv->gtt.base;
	list_for_each_entry(obj, &dev_priv->mm.bound_list, global_list) {
		flush = false;
		list_for_each_entry(vma, &obj->vma_list, vma_link) {
			if (vma->vm != vm)
				continue;

			WARN_ON(i915_vma_bind(vma, obj->cache_level,
					      PIN_UPDATE));

			flush = true;
		}

		if (flush)
			i915_gem_clflush_object(obj, obj->pin_display);
	}

	if (INTEL_INFO(dev)->gen >= 8) {
		if (IS_CHERRYVIEW(dev) || IS_BROXTON(dev))
			chv_setup_private_ppat(dev_priv);
		else
			bdw_setup_private_ppat(dev_priv);

		return;
	}

	if (USES_PPGTT(dev)) {
		list_for_each_entry(vm, &dev_priv->vm_list, global_link) {
			/* TODO: Perhaps it shouldn't be gen6 specific */

			struct i915_hw_ppgtt *ppgtt =
					container_of(vm, struct i915_hw_ppgtt,
						     base);

			if (i915_is_ggtt(vm))
				ppgtt = dev_priv->mm.aliasing_ppgtt;

			gen6_write_page_range(dev_priv, &ppgtt->pd,
					      0, ppgtt->base.total);
		}
	}

	i915_ggtt_flush(dev_priv);
}

static struct i915_vma *
__i915_gem_vma_create(struct drm_i915_gem_object *obj,
		      struct i915_address_space *vm,
		      const struct i915_ggtt_view *ggtt_view)
{
	struct i915_vma *vma;

	if (WARN_ON(i915_is_ggtt(vm) != !!ggtt_view))
		return ERR_PTR(-EINVAL);

	vma = kmem_cache_zalloc(to_i915(obj->base.dev)->vmas, GFP_KERNEL);
	if (vma == NULL)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&vma->vma_link);
	INIT_LIST_HEAD(&vma->mm_list);
	INIT_LIST_HEAD(&vma->exec_list);
	vma->vm = vm;
	vma->obj = obj;

	if (i915_is_ggtt(vm))
		vma->ggtt_view = *ggtt_view;

	list_add_tail(&vma->vma_link, &obj->vma_list);
	if (!i915_is_ggtt(vm))
		i915_ppgtt_get(i915_vm_to_ppgtt(vm));

	return vma;
}

struct i915_vma *
i915_gem_obj_lookup_or_create_vma(struct drm_i915_gem_object *obj,
				  struct i915_address_space *vm)
{
	struct i915_vma *vma;

	vma = i915_gem_obj_to_vma(obj, vm);
	if (!vma)
		vma = __i915_gem_vma_create(obj, vm,
					    i915_is_ggtt(vm) ? &i915_ggtt_view_normal : NULL);

	return vma;
}

struct i915_vma *
i915_gem_obj_lookup_or_create_ggtt_vma(struct drm_i915_gem_object *obj,
				       const struct i915_ggtt_view *view)
{
	struct i915_address_space *ggtt = i915_obj_to_ggtt(obj);
	struct i915_vma *vma;

	if (WARN_ON(!view))
		return ERR_PTR(-EINVAL);

	vma = i915_gem_obj_to_ggtt_view(obj, view);

	if (IS_ERR(vma))
		return vma;

	if (!vma)
		vma = __i915_gem_vma_create(obj, ggtt, view);

	return vma;

}

static struct scatterlist *
rotate_pages(dma_addr_t *in, unsigned int offset,
	     unsigned int width, unsigned int height,
	     struct sg_table *st, struct scatterlist *sg)
{
	unsigned int column, row;
	unsigned int src_idx;

	if (!sg) {
		st->nents = 0;
		sg = st->sgl;
	}

	for (column = 0; column < width; column++) {
		src_idx = width * (height - 1) + column;
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
			src_idx -= width;
		}
	}

	return sg;
}

static struct sg_table *
intel_rotate_fb_obj_pages(struct i915_ggtt_view *ggtt_view,
			  struct drm_i915_gem_object *obj)
{
	struct intel_rotation_info *rot_info = &ggtt_view->rotation_info;
	unsigned int size_pages = rot_info->size >> PAGE_SHIFT;
	unsigned int size_pages_uv;
	struct sg_page_iter sg_iter;
	unsigned long i;
	dma_addr_t *page_addr_list;
	struct sg_table *st;
	unsigned int uv_start_page;
	struct scatterlist *sg;
	int ret = -ENOMEM;

	/* Allocate a temporary list of source pages for random access. */
	page_addr_list = drm_malloc_ab(obj->base.size / PAGE_SIZE,
				       sizeof(dma_addr_t));
	if (!page_addr_list)
		return ERR_PTR(ret);

	/* Account for UV plane with NV12. */
	if (rot_info->pixel_format == DRM_FORMAT_NV12)
		size_pages_uv = rot_info->size_uv >> PAGE_SHIFT;
	else
		size_pages_uv = 0;

	/* Allocate target SG list. */
	st = kmalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		goto err_st_alloc;

	ret = sg_alloc_table(st, size_pages + size_pages_uv, GFP_KERNEL);
	if (ret)
		goto err_sg_alloc;

	/* Populate source page list from the object. */
	i = 0;
	for_each_sg_page(obj->pages->sgl, &sg_iter, obj->pages->nents, 0) {
		page_addr_list[i] = sg_page_iter_dma_address(&sg_iter);
		i++;
	}

	/* Rotate the pages. */
	sg = rotate_pages(page_addr_list, 0,
		     rot_info->width_pages, rot_info->height_pages,
		     st, NULL);

	/* Append the UV plane if NV12. */
	if (rot_info->pixel_format == DRM_FORMAT_NV12) {
		uv_start_page = size_pages;

		/* Check for tile-row un-alignment. */
		if (offset_in_page(rot_info->uv_offset))
			uv_start_page--;

		rot_info->uv_start_page = uv_start_page;

		rotate_pages(page_addr_list, uv_start_page,
			     rot_info->width_pages_uv,
			     rot_info->height_pages_uv,
			     st, sg);
	}

	DRM_DEBUG_KMS(
		      "Created rotated page mapping for object size %zu (pitch=%u, height=%u, pixel_format=0x%x, %ux%u tiles, %u pages (%u plane 0)).\n",
		      obj->base.size, rot_info->pitch, rot_info->height,
		      rot_info->pixel_format, rot_info->width_pages,
		      rot_info->height_pages, size_pages + size_pages_uv,
		      size_pages);

	drm_free_large(page_addr_list);

	return st;

err_sg_alloc:
	kfree(st);
err_st_alloc:
	drm_free_large(page_addr_list);

	DRM_DEBUG_KMS(
		      "Failed to create rotated mapping for object size %zu! (%d) (pitch=%u, height=%u, pixel_format=0x%x, %ux%u tiles, %u pages (%u plane 0))\n",
		      obj->base.size, ret, rot_info->pitch, rot_info->height,
		      rot_info->pixel_format, rot_info->width_pages,
		      rot_info->height_pages, size_pages + size_pages_uv,
		      size_pages);
	return ERR_PTR(ret);
}

static struct sg_table *
intel_partial_pages(const struct i915_ggtt_view *view,
		    struct drm_i915_gem_object *obj)
{
	struct sg_table *st;
	struct scatterlist *sg;
	struct sg_page_iter obj_sg_iter;
	int ret = -ENOMEM;

	st = kmalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		goto err_st_alloc;

	ret = sg_alloc_table(st, view->params.partial.size, GFP_KERNEL);
	if (ret)
		goto err_sg_alloc;

	sg = st->sgl;
	st->nents = 0;
	for_each_sg_page(obj->pages->sgl, &obj_sg_iter, obj->pages->nents,
		view->params.partial.offset)
	{
		if (st->nents >= view->params.partial.size)
			break;

		sg_set_page(sg, NULL, PAGE_SIZE, 0);
		sg_dma_address(sg) = sg_page_iter_dma_address(&obj_sg_iter);
		sg_dma_len(sg) = PAGE_SIZE;

		sg = sg_next(sg);
		st->nents++;
	}

	return st;

err_sg_alloc:
	kfree(st);
err_st_alloc:
	return ERR_PTR(ret);
}

static int
i915_get_ggtt_vma_pages(struct i915_vma *vma)
{
	int ret = 0;

	if (vma->ggtt_view.pages)
		return 0;

	if (vma->ggtt_view.type == I915_GGTT_VIEW_NORMAL)
		vma->ggtt_view.pages = vma->obj->pages;
	else if (vma->ggtt_view.type == I915_GGTT_VIEW_ROTATED)
		vma->ggtt_view.pages =
			intel_rotate_fb_obj_pages(&vma->ggtt_view, vma->obj);
	else if (vma->ggtt_view.type == I915_GGTT_VIEW_PARTIAL)
		vma->ggtt_view.pages =
			intel_partial_pages(&vma->ggtt_view, vma->obj);
	else
		WARN_ONCE(1, "GGTT view %u not implemented!\n",
			  vma->ggtt_view.type);

	if (!vma->ggtt_view.pages) {
		DRM_ERROR("Failed to get pages for GGTT view type %u!\n",
			  vma->ggtt_view.type);
		ret = -EINVAL;
	} else if (IS_ERR(vma->ggtt_view.pages)) {
		ret = PTR_ERR(vma->ggtt_view.pages);
		vma->ggtt_view.pages = NULL;
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
	int ret;
	u32 bind_flags;

	if (WARN_ON(flags == 0))
		return -EINVAL;

	bind_flags = 0;
	if (flags & PIN_GLOBAL)
		bind_flags |= GLOBAL_BIND;
	if (flags & PIN_USER)
		bind_flags |= LOCAL_BIND;

	if (flags & PIN_UPDATE)
		bind_flags |= vma->bound;
	else
		bind_flags &= ~vma->bound;

	if (bind_flags == 0)
		return 0;

	if (vma->bound == 0 && vma->vm->allocate_va_range) {
		trace_i915_va_alloc(vma->vm,
				    vma->node.start,
				    vma->node.size,
				    VM_TO_TRACE_NAME(vma->vm));

		/* XXX: i915_vma_pin() will fix this +- hack */
		vma->pin_count++;
		ret = vma->vm->allocate_va_range(vma->vm,
						 vma->node.start,
						 vma->node.size);
		vma->pin_count--;
		if (ret)
			return ret;
	}

	ret = vma->vm->bind_vma(vma, cache_level, bind_flags);
	if (ret)
		return ret;

	vma->bound |= bind_flags;

	return 0;
}

/**
 * i915_ggtt_view_size - Get the size of a GGTT view.
 * @obj: Object the view is of.
 * @view: The view in question.
 *
 * @return The size of the GGTT view in bytes.
 */
size_t
i915_ggtt_view_size(struct drm_i915_gem_object *obj,
		    const struct i915_ggtt_view *view)
{
	if (view->type == I915_GGTT_VIEW_NORMAL) {
		return obj->base.size;
	} else if (view->type == I915_GGTT_VIEW_ROTATED) {
		return view->rotation_info.size;
	} else if (view->type == I915_GGTT_VIEW_PARTIAL) {
		return view->params.partial.size << PAGE_SHIFT;
	} else {
		WARN_ONCE(1, "GGTT view %u not implemented!\n", view->type);
		return obj->base.size;
	}
}
