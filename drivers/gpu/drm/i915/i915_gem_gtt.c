/*
 * Copyright © 2010 Daniel Vetter
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

#include <drm/drmP.h>
#include <drm/i915_drm.h>
#include "i915_drv.h"
#include "i915_trace.h"
#include "intel_drv.h"

#define GEN6_PPGTT_PD_ENTRIES 512
#define I915_PPGTT_PT_ENTRIES (PAGE_SIZE / sizeof(gen6_gtt_pte_t))
typedef uint64_t gen8_gtt_pte_t;
typedef gen8_gtt_pte_t gen8_ppgtt_pde_t;

/* PPGTT stuff */
#define GEN6_GTT_ADDR_ENCODE(addr)	((addr) | (((addr) >> 28) & 0xff0))
#define HSW_GTT_ADDR_ENCODE(addr)	((addr) | (((addr) >> 28) & 0x7f0))

#define GEN6_PDE_VALID			(1 << 0)
/* gen6+ has bit 11-4 for physical addr bit 39-32 */
#define GEN6_PDE_ADDR_ENCODE(addr)	GEN6_GTT_ADDR_ENCODE(addr)

#define GEN6_PTE_VALID			(1 << 0)
#define GEN6_PTE_UNCACHED		(1 << 1)
#define HSW_PTE_UNCACHED		(0)
#define GEN6_PTE_CACHE_LLC		(2 << 1)
#define GEN7_PTE_CACHE_L3_LLC		(3 << 1)
#define GEN6_PTE_ADDR_ENCODE(addr)	GEN6_GTT_ADDR_ENCODE(addr)
#define HSW_PTE_ADDR_ENCODE(addr)	HSW_GTT_ADDR_ENCODE(addr)

/* Cacheability Control is a 4-bit value. The low three bits are stored in *
 * bits 3:1 of the PTE, while the fourth bit is stored in bit 11 of the PTE.
 */
#define HSW_CACHEABILITY_CONTROL(bits)	((((bits) & 0x7) << 1) | \
					 (((bits) & 0x8) << (11 - 3)))
#define HSW_WB_LLC_AGE3			HSW_CACHEABILITY_CONTROL(0x2)
#define HSW_WB_LLC_AGE0			HSW_CACHEABILITY_CONTROL(0x3)
#define HSW_WB_ELLC_LLC_AGE0		HSW_CACHEABILITY_CONTROL(0xb)
#define HSW_WB_ELLC_LLC_AGE3		HSW_CACHEABILITY_CONTROL(0x8)
#define HSW_WT_ELLC_LLC_AGE0		HSW_CACHEABILITY_CONTROL(0x6)
#define HSW_WT_ELLC_LLC_AGE3		HSW_CACHEABILITY_CONTROL(0x7)

#define GEN8_PTES_PER_PAGE		(PAGE_SIZE / sizeof(gen8_gtt_pte_t))
#define GEN8_PDES_PER_PAGE		(PAGE_SIZE / sizeof(gen8_ppgtt_pde_t))
#define GEN8_LEGACY_PDPS		4

#define PPAT_UNCACHED_INDEX		(_PAGE_PWT | _PAGE_PCD)
#define PPAT_CACHED_PDE_INDEX		0 /* WB LLC */
#define PPAT_CACHED_INDEX		_PAGE_PAT /* WB LLCeLLC */
#define PPAT_DISPLAY_ELLC_INDEX		_PAGE_PCD /* WT eLLC */

static inline gen8_gtt_pte_t gen8_pte_encode(dma_addr_t addr,
					     enum i915_cache_level level,
					     bool valid)
{
	gen8_gtt_pte_t pte = valid ? _PAGE_PRESENT | _PAGE_RW : 0;
	pte |= addr;
	if (level != I915_CACHE_NONE)
		pte |= PPAT_CACHED_INDEX;
	else
		pte |= PPAT_UNCACHED_INDEX;
	return pte;
}

static inline gen8_ppgtt_pde_t gen8_pde_encode(struct drm_device *dev,
					     dma_addr_t addr,
					     enum i915_cache_level level)
{
	gen8_ppgtt_pde_t pde = _PAGE_PRESENT | _PAGE_RW;
	pde |= addr;
	if (level != I915_CACHE_NONE)
		pde |= PPAT_CACHED_PDE_INDEX;
	else
		pde |= PPAT_UNCACHED_INDEX;
	return pde;
}

static gen6_gtt_pte_t snb_pte_encode(dma_addr_t addr,
				     enum i915_cache_level level,
				     bool valid)
{
	gen6_gtt_pte_t pte = valid ? GEN6_PTE_VALID : 0;
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
		WARN_ON(1);
	}

	return pte;
}

static gen6_gtt_pte_t ivb_pte_encode(dma_addr_t addr,
				     enum i915_cache_level level,
				     bool valid)
{
	gen6_gtt_pte_t pte = valid ? GEN6_PTE_VALID : 0;
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
		WARN_ON(1);
	}

	return pte;
}

#define BYT_PTE_WRITEABLE		(1 << 1)
#define BYT_PTE_SNOOPED_BY_CPU_CACHES	(1 << 2)

static gen6_gtt_pte_t byt_pte_encode(dma_addr_t addr,
				     enum i915_cache_level level,
				     bool valid)
{
	gen6_gtt_pte_t pte = valid ? GEN6_PTE_VALID : 0;
	pte |= GEN6_PTE_ADDR_ENCODE(addr);

	/* Mark the page as writeable.  Other platforms don't have a
	 * setting for read-only/writable, so this matches that behavior.
	 */
	pte |= BYT_PTE_WRITEABLE;

	if (level != I915_CACHE_NONE)
		pte |= BYT_PTE_SNOOPED_BY_CPU_CACHES;

	return pte;
}

static gen6_gtt_pte_t hsw_pte_encode(dma_addr_t addr,
				     enum i915_cache_level level,
				     bool valid)
{
	gen6_gtt_pte_t pte = valid ? GEN6_PTE_VALID : 0;
	pte |= HSW_PTE_ADDR_ENCODE(addr);

	if (level != I915_CACHE_NONE)
		pte |= HSW_WB_LLC_AGE3;

	return pte;
}

static gen6_gtt_pte_t iris_pte_encode(dma_addr_t addr,
				      enum i915_cache_level level,
				      bool valid)
{
	gen6_gtt_pte_t pte = valid ? GEN6_PTE_VALID : 0;
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

/* Broadwell Page Directory Pointer Descriptors */
static int gen8_write_pdp(struct intel_ring_buffer *ring, unsigned entry,
			   uint64_t val)
{
	int ret;

	BUG_ON(entry >= 4);

	ret = intel_ring_begin(ring, 6);
	if (ret)
		return ret;

	intel_ring_emit(ring, MI_LOAD_REGISTER_IMM(1));
	intel_ring_emit(ring, GEN8_RING_PDP_UDW(ring, entry));
	intel_ring_emit(ring, (u32)(val >> 32));
	intel_ring_emit(ring, MI_LOAD_REGISTER_IMM(1));
	intel_ring_emit(ring, GEN8_RING_PDP_LDW(ring, entry));
	intel_ring_emit(ring, (u32)(val));
	intel_ring_advance(ring);

	return 0;
}

static int gen8_ppgtt_enable(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_ring_buffer *ring;
	struct i915_hw_ppgtt *ppgtt = dev_priv->mm.aliasing_ppgtt;
	int i, j, ret;

	/* bit of a hack to find the actual last used pd */
	int used_pd = ppgtt->num_pd_entries / GEN8_PDES_PER_PAGE;

	for_each_ring(ring, dev_priv, j) {
		I915_WRITE(RING_MODE_GEN7(ring),
			   _MASKED_BIT_ENABLE(GFX_PPGTT_ENABLE));
	}

	for (i = used_pd - 1; i >= 0; i--) {
		dma_addr_t addr = ppgtt->pd_dma_addr[i];
		for_each_ring(ring, dev_priv, j) {
			ret = gen8_write_pdp(ring, i, addr);
			if (ret)
				return ret;
		}
	}
	return 0;
}

static void gen8_ppgtt_clear_range(struct i915_address_space *vm,
				   unsigned first_entry,
				   unsigned num_entries,
				   bool use_scratch)
{
	struct i915_hw_ppgtt *ppgtt =
		container_of(vm, struct i915_hw_ppgtt, base);
	gen8_gtt_pte_t *pt_vaddr, scratch_pte;
	unsigned act_pt = first_entry / GEN8_PTES_PER_PAGE;
	unsigned first_pte = first_entry % GEN8_PTES_PER_PAGE;
	unsigned last_pte, i;

	scratch_pte = gen8_pte_encode(ppgtt->base.scratch.addr,
				      I915_CACHE_LLC, use_scratch);

	while (num_entries) {
		struct page *page_table = &ppgtt->gen8_pt_pages[act_pt];

		last_pte = first_pte + num_entries;
		if (last_pte > GEN8_PTES_PER_PAGE)
			last_pte = GEN8_PTES_PER_PAGE;

		pt_vaddr = kmap_atomic(page_table);

		for (i = first_pte; i < last_pte; i++)
			pt_vaddr[i] = scratch_pte;

		kunmap_atomic(pt_vaddr);

		num_entries -= last_pte - first_pte;
		first_pte = 0;
		act_pt++;
	}
}

static void gen8_ppgtt_insert_entries(struct i915_address_space *vm,
				      struct sg_table *pages,
				      unsigned first_entry,
				      enum i915_cache_level cache_level)
{
	struct i915_hw_ppgtt *ppgtt =
		container_of(vm, struct i915_hw_ppgtt, base);
	gen8_gtt_pte_t *pt_vaddr;
	unsigned act_pt = first_entry / GEN8_PTES_PER_PAGE;
	unsigned act_pte = first_entry % GEN8_PTES_PER_PAGE;
	struct sg_page_iter sg_iter;

	pt_vaddr = kmap_atomic(&ppgtt->gen8_pt_pages[act_pt]);
	for_each_sg_page(pages->sgl, &sg_iter, pages->nents, 0) {
		dma_addr_t page_addr;

		page_addr = sg_dma_address(sg_iter.sg) +
				(sg_iter.sg_pgoffset << PAGE_SHIFT);
		pt_vaddr[act_pte] = gen8_pte_encode(page_addr, cache_level,
						    true);
		if (++act_pte == GEN8_PTES_PER_PAGE) {
			kunmap_atomic(pt_vaddr);
			act_pt++;
			pt_vaddr = kmap_atomic(&ppgtt->gen8_pt_pages[act_pt]);
			act_pte = 0;

		}
	}
	kunmap_atomic(pt_vaddr);
}

static void gen8_ppgtt_cleanup(struct i915_address_space *vm)
{
	struct i915_hw_ppgtt *ppgtt =
		container_of(vm, struct i915_hw_ppgtt, base);
	int i, j;

	for (i = 0; i < ppgtt->num_pd_pages ; i++) {
		if (ppgtt->pd_dma_addr[i]) {
			pci_unmap_page(ppgtt->base.dev->pdev,
				       ppgtt->pd_dma_addr[i],
				       PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);

			for (j = 0; j < GEN8_PDES_PER_PAGE; j++) {
				dma_addr_t addr = ppgtt->gen8_pt_dma_addr[i][j];
				if (addr)
					pci_unmap_page(ppgtt->base.dev->pdev,
						       addr,
						       PAGE_SIZE,
						       PCI_DMA_BIDIRECTIONAL);

			}
		}
		kfree(ppgtt->gen8_pt_dma_addr[i]);
	}

	__free_pages(ppgtt->gen8_pt_pages, ppgtt->num_pt_pages << PAGE_SHIFT);
	__free_pages(ppgtt->pd_pages, ppgtt->num_pd_pages << PAGE_SHIFT);
}

/**
 * GEN8 legacy ppgtt programming is accomplished through 4 PDP registers with a
 * net effect resembling a 2-level page table in normal x86 terms. Each PDP
 * represents 1GB of memory
 * 4 * 512 * 512 * 4096 = 4GB legacy 32b address space.
 *
 * TODO: Do something with the size parameter
 **/
static int gen8_ppgtt_init(struct i915_hw_ppgtt *ppgtt, uint64_t size)
{
	struct page *pt_pages;
	int i, j, ret = -ENOMEM;
	const int max_pdp = DIV_ROUND_UP(size, 1 << 30);
	const int num_pt_pages = GEN8_PDES_PER_PAGE * max_pdp;

	if (size % (1<<30))
		DRM_INFO("Pages will be wasted unless GTT size (%llu) is divisible by 1GB\n", size);

	/* FIXME: split allocation into smaller pieces. For now we only ever do
	 * this once, but with full PPGTT, the multiple contiguous allocations
	 * will be bad.
	 */
	ppgtt->pd_pages = alloc_pages(GFP_KERNEL, get_order(max_pdp << PAGE_SHIFT));
	if (!ppgtt->pd_pages)
		return -ENOMEM;

	pt_pages = alloc_pages(GFP_KERNEL, get_order(num_pt_pages << PAGE_SHIFT));
	if (!pt_pages) {
		__free_pages(ppgtt->pd_pages, get_order(max_pdp << PAGE_SHIFT));
		return -ENOMEM;
	}

	ppgtt->gen8_pt_pages = pt_pages;
	ppgtt->num_pd_pages = 1 << get_order(max_pdp << PAGE_SHIFT);
	ppgtt->num_pt_pages = 1 << get_order(num_pt_pages << PAGE_SHIFT);
	ppgtt->num_pd_entries = max_pdp * GEN8_PDES_PER_PAGE;
	ppgtt->enable = gen8_ppgtt_enable;
	ppgtt->base.clear_range = gen8_ppgtt_clear_range;
	ppgtt->base.insert_entries = gen8_ppgtt_insert_entries;
	ppgtt->base.cleanup = gen8_ppgtt_cleanup;

	BUG_ON(ppgtt->num_pd_pages > GEN8_LEGACY_PDPS);

	/*
	 * - Create a mapping for the page directories.
	 * - For each page directory:
	 *      allocate space for page table mappings.
	 *      map each page table
	 */
	for (i = 0; i < max_pdp; i++) {
		dma_addr_t temp;
		temp = pci_map_page(ppgtt->base.dev->pdev,
				    &ppgtt->pd_pages[i], 0,
				    PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
		if (pci_dma_mapping_error(ppgtt->base.dev->pdev, temp))
			goto err_out;

		ppgtt->pd_dma_addr[i] = temp;

		ppgtt->gen8_pt_dma_addr[i] = kmalloc(sizeof(dma_addr_t) * GEN8_PDES_PER_PAGE, GFP_KERNEL);
		if (!ppgtt->gen8_pt_dma_addr[i])
			goto err_out;

		for (j = 0; j < GEN8_PDES_PER_PAGE; j++) {
			struct page *p = &pt_pages[i * GEN8_PDES_PER_PAGE + j];
			temp = pci_map_page(ppgtt->base.dev->pdev,
					    p, 0, PAGE_SIZE,
					    PCI_DMA_BIDIRECTIONAL);

			if (pci_dma_mapping_error(ppgtt->base.dev->pdev, temp))
				goto err_out;

			ppgtt->gen8_pt_dma_addr[i][j] = temp;
		}
	}

	/* For now, the PPGTT helper functions all require that the PDEs are
	 * plugged in correctly. So we do that now/here. For aliasing PPGTT, we
	 * will never need to touch the PDEs again */
	for (i = 0; i < max_pdp; i++) {
		gen8_ppgtt_pde_t *pd_vaddr;
		pd_vaddr = kmap_atomic(&ppgtt->pd_pages[i]);
		for (j = 0; j < GEN8_PDES_PER_PAGE; j++) {
			dma_addr_t addr = ppgtt->gen8_pt_dma_addr[i][j];
			pd_vaddr[j] = gen8_pde_encode(ppgtt->base.dev, addr,
						      I915_CACHE_LLC);
		}
		kunmap_atomic(pd_vaddr);
	}

	ppgtt->base.clear_range(&ppgtt->base, 0,
				ppgtt->num_pd_entries * GEN8_PTES_PER_PAGE,
				true);

	DRM_DEBUG_DRIVER("Allocated %d pages for page directories (%d wasted)\n",
			 ppgtt->num_pd_pages, ppgtt->num_pd_pages - max_pdp);
	DRM_DEBUG_DRIVER("Allocated %d pages for page tables (%lld wasted)\n",
			 ppgtt->num_pt_pages,
			 (ppgtt->num_pt_pages - num_pt_pages) +
			 size % (1<<30));
	return 0;

err_out:
	ppgtt->base.cleanup(&ppgtt->base);
	return ret;
}

static void gen6_write_pdes(struct i915_hw_ppgtt *ppgtt)
{
	struct drm_i915_private *dev_priv = ppgtt->base.dev->dev_private;
	gen6_gtt_pte_t __iomem *pd_addr;
	uint32_t pd_entry;
	int i;

	WARN_ON(ppgtt->pd_offset & 0x3f);
	pd_addr = (gen6_gtt_pte_t __iomem*)dev_priv->gtt.gsm +
		ppgtt->pd_offset / sizeof(gen6_gtt_pte_t);
	for (i = 0; i < ppgtt->num_pd_entries; i++) {
		dma_addr_t pt_addr;

		pt_addr = ppgtt->pt_dma_addr[i];
		pd_entry = GEN6_PDE_ADDR_ENCODE(pt_addr);
		pd_entry |= GEN6_PDE_VALID;

		writel(pd_entry, pd_addr + i);
	}
	readl(pd_addr);
}

static int gen6_ppgtt_enable(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	uint32_t pd_offset;
	struct intel_ring_buffer *ring;
	struct i915_hw_ppgtt *ppgtt = dev_priv->mm.aliasing_ppgtt;
	int i;

	BUG_ON(ppgtt->pd_offset & 0x3f);

	gen6_write_pdes(ppgtt);

	pd_offset = ppgtt->pd_offset;
	pd_offset /= 64; /* in cachelines, */
	pd_offset <<= 16;

	if (INTEL_INFO(dev)->gen == 6) {
		uint32_t ecochk, gab_ctl, ecobits;

		ecobits = I915_READ(GAC_ECO_BITS);
		I915_WRITE(GAC_ECO_BITS, ecobits | ECOBITS_SNB_BIT |
					 ECOBITS_PPGTT_CACHE64B);

		gab_ctl = I915_READ(GAB_CTL);
		I915_WRITE(GAB_CTL, gab_ctl | GAB_CTL_CONT_AFTER_PAGEFAULT);

		ecochk = I915_READ(GAM_ECOCHK);
		I915_WRITE(GAM_ECOCHK, ecochk | ECOCHK_SNB_BIT |
				       ECOCHK_PPGTT_CACHE64B);
		I915_WRITE(GFX_MODE, _MASKED_BIT_ENABLE(GFX_PPGTT_ENABLE));
	} else if (INTEL_INFO(dev)->gen >= 7) {
		uint32_t ecochk, ecobits;

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
		/* GFX_MODE is per-ring on gen7+ */
	}

	for_each_ring(ring, dev_priv, i) {
		if (INTEL_INFO(dev)->gen >= 7)
			I915_WRITE(RING_MODE_GEN7(ring),
				   _MASKED_BIT_ENABLE(GFX_PPGTT_ENABLE));

		I915_WRITE(RING_PP_DIR_DCLV(ring), PP_DIR_DCLV_2G);
		I915_WRITE(RING_PP_DIR_BASE(ring), pd_offset);
	}
	return 0;
}

/* PPGTT support for Sandybdrige/Gen6 and later */
static void gen6_ppgtt_clear_range(struct i915_address_space *vm,
				   unsigned first_entry,
				   unsigned num_entries,
				   bool use_scratch)
{
	struct i915_hw_ppgtt *ppgtt =
		container_of(vm, struct i915_hw_ppgtt, base);
	gen6_gtt_pte_t *pt_vaddr, scratch_pte;
	unsigned act_pt = first_entry / I915_PPGTT_PT_ENTRIES;
	unsigned first_pte = first_entry % I915_PPGTT_PT_ENTRIES;
	unsigned last_pte, i;

	scratch_pte = vm->pte_encode(vm->scratch.addr, I915_CACHE_LLC, true);

	while (num_entries) {
		last_pte = first_pte + num_entries;
		if (last_pte > I915_PPGTT_PT_ENTRIES)
			last_pte = I915_PPGTT_PT_ENTRIES;

		pt_vaddr = kmap_atomic(ppgtt->pt_pages[act_pt]);

		for (i = first_pte; i < last_pte; i++)
			pt_vaddr[i] = scratch_pte;

		kunmap_atomic(pt_vaddr);

		num_entries -= last_pte - first_pte;
		first_pte = 0;
		act_pt++;
	}
}

static void gen6_ppgtt_insert_entries(struct i915_address_space *vm,
				      struct sg_table *pages,
				      unsigned first_entry,
				      enum i915_cache_level cache_level)
{
	struct i915_hw_ppgtt *ppgtt =
		container_of(vm, struct i915_hw_ppgtt, base);
	gen6_gtt_pte_t *pt_vaddr;
	unsigned act_pt = first_entry / I915_PPGTT_PT_ENTRIES;
	unsigned act_pte = first_entry % I915_PPGTT_PT_ENTRIES;
	struct sg_page_iter sg_iter;

	pt_vaddr = kmap_atomic(ppgtt->pt_pages[act_pt]);
	for_each_sg_page(pages->sgl, &sg_iter, pages->nents, 0) {
		dma_addr_t page_addr;

		page_addr = sg_page_iter_dma_address(&sg_iter);
		pt_vaddr[act_pte] = vm->pte_encode(page_addr, cache_level, true);
		if (++act_pte == I915_PPGTT_PT_ENTRIES) {
			kunmap_atomic(pt_vaddr);
			act_pt++;
			pt_vaddr = kmap_atomic(ppgtt->pt_pages[act_pt]);
			act_pte = 0;

		}
	}
	kunmap_atomic(pt_vaddr);
}

static void gen6_ppgtt_cleanup(struct i915_address_space *vm)
{
	struct i915_hw_ppgtt *ppgtt =
		container_of(vm, struct i915_hw_ppgtt, base);
	int i;

	drm_mm_takedown(&ppgtt->base.mm);

	if (ppgtt->pt_dma_addr) {
		for (i = 0; i < ppgtt->num_pd_entries; i++)
			pci_unmap_page(ppgtt->base.dev->pdev,
				       ppgtt->pt_dma_addr[i],
				       4096, PCI_DMA_BIDIRECTIONAL);
	}

	kfree(ppgtt->pt_dma_addr);
	for (i = 0; i < ppgtt->num_pd_entries; i++)
		__free_page(ppgtt->pt_pages[i]);
	kfree(ppgtt->pt_pages);
	kfree(ppgtt);
}

static int gen6_ppgtt_init(struct i915_hw_ppgtt *ppgtt)
{
	struct drm_device *dev = ppgtt->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned first_pd_entry_in_global_pt;
	int i;
	int ret = -ENOMEM;

	/* ppgtt PDEs reside in the global gtt pagetable, which has 512*1024
	 * entries. For aliasing ppgtt support we just steal them at the end for
	 * now. */
	first_pd_entry_in_global_pt = gtt_total_entries(dev_priv->gtt);

	ppgtt->base.pte_encode = dev_priv->gtt.base.pte_encode;
	ppgtt->num_pd_entries = GEN6_PPGTT_PD_ENTRIES;
	ppgtt->enable = gen6_ppgtt_enable;
	ppgtt->base.clear_range = gen6_ppgtt_clear_range;
	ppgtt->base.insert_entries = gen6_ppgtt_insert_entries;
	ppgtt->base.cleanup = gen6_ppgtt_cleanup;
	ppgtt->base.scratch = dev_priv->gtt.base.scratch;
	ppgtt->pt_pages = kcalloc(ppgtt->num_pd_entries, sizeof(struct page *),
				  GFP_KERNEL);
	if (!ppgtt->pt_pages)
		return -ENOMEM;

	for (i = 0; i < ppgtt->num_pd_entries; i++) {
		ppgtt->pt_pages[i] = alloc_page(GFP_KERNEL);
		if (!ppgtt->pt_pages[i])
			goto err_pt_alloc;
	}

	ppgtt->pt_dma_addr = kcalloc(ppgtt->num_pd_entries, sizeof(dma_addr_t),
				     GFP_KERNEL);
	if (!ppgtt->pt_dma_addr)
		goto err_pt_alloc;

	for (i = 0; i < ppgtt->num_pd_entries; i++) {
		dma_addr_t pt_addr;

		pt_addr = pci_map_page(dev->pdev, ppgtt->pt_pages[i], 0, 4096,
				       PCI_DMA_BIDIRECTIONAL);

		if (pci_dma_mapping_error(dev->pdev, pt_addr)) {
			ret = -EIO;
			goto err_pd_pin;

		}
		ppgtt->pt_dma_addr[i] = pt_addr;
	}

	ppgtt->base.clear_range(&ppgtt->base, 0,
				ppgtt->num_pd_entries * I915_PPGTT_PT_ENTRIES, true);

	ppgtt->pd_offset = first_pd_entry_in_global_pt * sizeof(gen6_gtt_pte_t);

	return 0;

err_pd_pin:
	if (ppgtt->pt_dma_addr) {
		for (i--; i >= 0; i--)
			pci_unmap_page(dev->pdev, ppgtt->pt_dma_addr[i],
				       4096, PCI_DMA_BIDIRECTIONAL);
	}
err_pt_alloc:
	kfree(ppgtt->pt_dma_addr);
	for (i = 0; i < ppgtt->num_pd_entries; i++) {
		if (ppgtt->pt_pages[i])
			__free_page(ppgtt->pt_pages[i]);
	}
	kfree(ppgtt->pt_pages);

	return ret;
}

static int i915_gem_init_aliasing_ppgtt(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct i915_hw_ppgtt *ppgtt;
	int ret;

	ppgtt = kzalloc(sizeof(*ppgtt), GFP_KERNEL);
	if (!ppgtt)
		return -ENOMEM;

	ppgtt->base.dev = dev;

	if (INTEL_INFO(dev)->gen < 8)
		ret = gen6_ppgtt_init(ppgtt);
	else if (IS_GEN8(dev))
		ret = gen8_ppgtt_init(ppgtt, dev_priv->gtt.base.total);
	else
		BUG();

	if (ret)
		kfree(ppgtt);
	else {
		dev_priv->mm.aliasing_ppgtt = ppgtt;
		drm_mm_init(&ppgtt->base.mm, ppgtt->base.start,
			    ppgtt->base.total);
	}

	return ret;
}

void i915_gem_cleanup_aliasing_ppgtt(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct i915_hw_ppgtt *ppgtt = dev_priv->mm.aliasing_ppgtt;

	if (!ppgtt)
		return;

	ppgtt->base.cleanup(&ppgtt->base);
	dev_priv->mm.aliasing_ppgtt = NULL;
}

void i915_ppgtt_bind_object(struct i915_hw_ppgtt *ppgtt,
			    struct drm_i915_gem_object *obj,
			    enum i915_cache_level cache_level)
{
	ppgtt->base.insert_entries(&ppgtt->base, obj->pages,
				   i915_gem_obj_ggtt_offset(obj) >> PAGE_SHIFT,
				   cache_level);
}

void i915_ppgtt_unbind_object(struct i915_hw_ppgtt *ppgtt,
			      struct drm_i915_gem_object *obj)
{
	ppgtt->base.clear_range(&ppgtt->base,
				i915_gem_obj_ggtt_offset(obj) >> PAGE_SHIFT,
				obj->base.size >> PAGE_SHIFT,
				true);
}

extern int intel_iommu_gfx_mapped;
/* Certain Gen5 chipsets require require idling the GPU before
 * unmapping anything from the GTT when VT-d is enabled.
 */
static inline bool needs_idle_maps(struct drm_device *dev)
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
	struct intel_ring_buffer *ring;
	int i;

	if (INTEL_INFO(dev)->gen < 6)
		return;

	for_each_ring(ring, dev_priv, i) {
		u32 fault_reg;
		fault_reg = I915_READ(RING_FAULT_REG(ring));
		if (fault_reg & RING_FAULT_VALID) {
			DRM_DEBUG_DRIVER("Unexpected fault\n"
					 "\tAddr: 0x%08lx\\n"
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
				       dev_priv->gtt.base.start / PAGE_SIZE,
				       dev_priv->gtt.base.total / PAGE_SIZE,
				       false);
}

void i915_gem_restore_gtt_mappings(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj;

	i915_check_and_clear_faults(dev);

	/* First fill our portion of the GTT with scratch pages */
	dev_priv->gtt.base.clear_range(&dev_priv->gtt.base,
				       dev_priv->gtt.base.start / PAGE_SIZE,
				       dev_priv->gtt.base.total / PAGE_SIZE,
				       true);

	list_for_each_entry(obj, &dev_priv->mm.bound_list, global_list) {
		i915_gem_clflush_object(obj, obj->pin_display);
		i915_gem_gtt_bind_object(obj, obj->cache_level);
	}

	i915_gem_chipset_flush(dev);
}

int i915_gem_gtt_prepare_object(struct drm_i915_gem_object *obj)
{
	if (obj->has_dma_mapping)
		return 0;

	if (!dma_map_sg(&obj->base.dev->pdev->dev,
			obj->pages->sgl, obj->pages->nents,
			PCI_DMA_BIDIRECTIONAL))
		return -ENOSPC;

	return 0;
}

static inline void gen8_set_pte(void __iomem *addr, gen8_gtt_pte_t pte)
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
				     unsigned int first_entry,
				     enum i915_cache_level level)
{
	struct drm_i915_private *dev_priv = vm->dev->dev_private;
	gen8_gtt_pte_t __iomem *gtt_entries =
		(gen8_gtt_pte_t __iomem *)dev_priv->gtt.gsm + first_entry;
	int i = 0;
	struct sg_page_iter sg_iter;
	dma_addr_t addr;

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

#if 0 /* TODO: Still needed on GEN8? */
	/* This next bit makes the above posting read even more important. We
	 * want to flush the TLBs only after we're certain all the PTE updates
	 * have finished.
	 */
	I915_WRITE(GFX_FLSH_CNTL_GEN6, GFX_FLSH_CNTL_EN);
	POSTING_READ(GFX_FLSH_CNTL_GEN6);
#endif
}

/*
 * Binds an object into the global gtt with the specified cache level. The object
 * will be accessible to the GPU via commands whose operands reference offsets
 * within the global GTT as well as accessible by the GPU through the GMADR
 * mapped BAR (dev_priv->mm.gtt->gtt).
 */
static void gen6_ggtt_insert_entries(struct i915_address_space *vm,
				     struct sg_table *st,
				     unsigned int first_entry,
				     enum i915_cache_level level)
{
	struct drm_i915_private *dev_priv = vm->dev->dev_private;
	gen6_gtt_pte_t __iomem *gtt_entries =
		(gen6_gtt_pte_t __iomem *)dev_priv->gtt.gsm + first_entry;
	int i = 0;
	struct sg_page_iter sg_iter;
	dma_addr_t addr;

	for_each_sg_page(st->sgl, &sg_iter, st->nents, 0) {
		addr = sg_page_iter_dma_address(&sg_iter);
		iowrite32(vm->pte_encode(addr, level, true), &gtt_entries[i]);
		i++;
	}

	/* XXX: This serves as a posting read to make sure that the PTE has
	 * actually been updated. There is some concern that even though
	 * registers and PTEs are within the same BAR that they are potentially
	 * of NUMA access patterns. Therefore, even with the way we assume
	 * hardware should work, we must keep this posting read for paranoia.
	 */
	if (i != 0)
		WARN_ON(readl(&gtt_entries[i-1]) !=
			vm->pte_encode(addr, level, true));

	/* This next bit makes the above posting read even more important. We
	 * want to flush the TLBs only after we're certain all the PTE updates
	 * have finished.
	 */
	I915_WRITE(GFX_FLSH_CNTL_GEN6, GFX_FLSH_CNTL_EN);
	POSTING_READ(GFX_FLSH_CNTL_GEN6);
}

static void gen8_ggtt_clear_range(struct i915_address_space *vm,
				  unsigned int first_entry,
				  unsigned int num_entries,
				  bool use_scratch)
{
	struct drm_i915_private *dev_priv = vm->dev->dev_private;
	gen8_gtt_pte_t scratch_pte, __iomem *gtt_base =
		(gen8_gtt_pte_t __iomem *) dev_priv->gtt.gsm + first_entry;
	const int max_entries = gtt_total_entries(dev_priv->gtt) - first_entry;
	int i;

	if (WARN(num_entries > max_entries,
		 "First entry = %d; Num entries = %d (max=%d)\n",
		 first_entry, num_entries, max_entries))
		num_entries = max_entries;

	scratch_pte = gen8_pte_encode(vm->scratch.addr,
				      I915_CACHE_LLC,
				      use_scratch);
	for (i = 0; i < num_entries; i++)
		gen8_set_pte(&gtt_base[i], scratch_pte);
	readl(gtt_base);
}

static void gen6_ggtt_clear_range(struct i915_address_space *vm,
				  unsigned int first_entry,
				  unsigned int num_entries,
				  bool use_scratch)
{
	struct drm_i915_private *dev_priv = vm->dev->dev_private;
	gen6_gtt_pte_t scratch_pte, __iomem *gtt_base =
		(gen6_gtt_pte_t __iomem *) dev_priv->gtt.gsm + first_entry;
	const int max_entries = gtt_total_entries(dev_priv->gtt) - first_entry;
	int i;

	if (WARN(num_entries > max_entries,
		 "First entry = %d; Num entries = %d (max=%d)\n",
		 first_entry, num_entries, max_entries))
		num_entries = max_entries;

	scratch_pte = vm->pte_encode(vm->scratch.addr, I915_CACHE_LLC, use_scratch);

	for (i = 0; i < num_entries; i++)
		iowrite32(scratch_pte, &gtt_base[i]);
	readl(gtt_base);
}

static void i915_ggtt_insert_entries(struct i915_address_space *vm,
				     struct sg_table *st,
				     unsigned int pg_start,
				     enum i915_cache_level cache_level)
{
	unsigned int flags = (cache_level == I915_CACHE_NONE) ?
		AGP_USER_MEMORY : AGP_USER_CACHED_MEMORY;

	intel_gtt_insert_sg_entries(st, pg_start, flags);

}

static void i915_ggtt_clear_range(struct i915_address_space *vm,
				  unsigned int first_entry,
				  unsigned int num_entries,
				  bool unused)
{
	intel_gtt_clear_range(first_entry, num_entries);
}


void i915_gem_gtt_bind_object(struct drm_i915_gem_object *obj,
			      enum i915_cache_level cache_level)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	const unsigned long entry = i915_gem_obj_ggtt_offset(obj) >> PAGE_SHIFT;

	dev_priv->gtt.base.insert_entries(&dev_priv->gtt.base, obj->pages,
					  entry,
					  cache_level);

	obj->has_global_gtt_mapping = 1;
}

void i915_gem_gtt_unbind_object(struct drm_i915_gem_object *obj)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	const unsigned long entry = i915_gem_obj_ggtt_offset(obj) >> PAGE_SHIFT;

	dev_priv->gtt.base.clear_range(&dev_priv->gtt.base,
				       entry,
				       obj->base.size >> PAGE_SHIFT,
				       true);

	obj->has_global_gtt_mapping = 0;
}

void i915_gem_gtt_finish_object(struct drm_i915_gem_object *obj)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	bool interruptible;

	interruptible = do_idling(dev_priv);

	if (!obj->has_dma_mapping)
		dma_unmap_sg(&dev->pdev->dev,
			     obj->pages->sgl, obj->pages->nents,
			     PCI_DMA_BIDIRECTIONAL);

	undo_idling(dev_priv, interruptible);
}

static void i915_gtt_color_adjust(struct drm_mm_node *node,
				  unsigned long color,
				  unsigned long *start,
				  unsigned long *end)
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

void i915_gem_setup_global_gtt(struct drm_device *dev,
			       unsigned long start,
			       unsigned long mappable_end,
			       unsigned long end)
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

	BUG_ON(mappable_end > end);

	/* Subtract the guard page ... */
	drm_mm_init(&ggtt_vm->mm, start, end - start - PAGE_SIZE);
	if (!HAS_LLC(dev))
		dev_priv->gtt.base.mm.color_adjust = i915_gtt_color_adjust;

	/* Mark any preallocated objects as occupied */
	list_for_each_entry(obj, &dev_priv->mm.bound_list, global_list) {
		struct i915_vma *vma = i915_gem_obj_to_vma(obj, ggtt_vm);
		int ret;
		DRM_DEBUG_KMS("reserving preallocated space: %lx + %zx\n",
			      i915_gem_obj_ggtt_offset(obj), obj->base.size);

		WARN_ON(i915_gem_obj_ggtt_bound(obj));
		ret = drm_mm_reserve_node(&ggtt_vm->mm, &vma->node);
		if (ret)
			DRM_DEBUG_KMS("Reservation failed\n");
		obj->has_global_gtt_mapping = 1;
		list_add(&vma->vma_link, &obj->vma_list);
	}

	dev_priv->gtt.base.start = start;
	dev_priv->gtt.base.total = end - start;

	/* Clear any non-preallocated blocks */
	drm_mm_for_each_hole(entry, &ggtt_vm->mm, hole_start, hole_end) {
		const unsigned long count = (hole_end - hole_start) / PAGE_SIZE;
		DRM_DEBUG_KMS("clearing unused GTT space: [%lx, %lx]\n",
			      hole_start, hole_end);
		ggtt_vm->clear_range(ggtt_vm, hole_start / PAGE_SIZE, count, true);
	}

	/* And finally clear the reserved guard page */
	ggtt_vm->clear_range(ggtt_vm, end / PAGE_SIZE - 1, 1, true);
}

static bool
intel_enable_ppgtt(struct drm_device *dev)
{
	if (i915_enable_ppgtt >= 0)
		return i915_enable_ppgtt;

#ifdef CONFIG_INTEL_IOMMU
	/* Disable ppgtt on SNB if VT-d is on. */
	if (INTEL_INFO(dev)->gen == 6 && intel_iommu_gfx_mapped)
		return false;
#endif

	return true;
}

void i915_gem_init_global_gtt(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long gtt_size, mappable_size;

	gtt_size = dev_priv->gtt.base.total;
	mappable_size = dev_priv->gtt.mappable_end;

	if (intel_enable_ppgtt(dev) && HAS_ALIASING_PPGTT(dev)) {
		int ret;

		if (INTEL_INFO(dev)->gen <= 7) {
			/* PPGTT pdes are stolen from global gtt ptes, so shrink the
			 * aperture accordingly when using aliasing ppgtt. */
			gtt_size -= GEN6_PPGTT_PD_ENTRIES * PAGE_SIZE;
		}

		i915_gem_setup_global_gtt(dev, 0, mappable_size, gtt_size);

		ret = i915_gem_init_aliasing_ppgtt(dev);
		if (!ret)
			return;

		DRM_ERROR("Aliased PPGTT setup failed %d\n", ret);
		drm_mm_takedown(&dev_priv->gtt.base.mm);
		if (INTEL_INFO(dev)->gen < 8)
			gtt_size += GEN6_PPGTT_PD_ENTRIES*PAGE_SIZE;
	}
	i915_gem_setup_global_gtt(dev, 0, mappable_size, gtt_size);
}

static int setup_scratch_page(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct page *page;
	dma_addr_t dma_addr;

	page = alloc_page(GFP_KERNEL | GFP_DMA32 | __GFP_ZERO);
	if (page == NULL)
		return -ENOMEM;
	get_page(page);
	set_pages_uc(page, 1);

#ifdef CONFIG_INTEL_IOMMU
	dma_addr = pci_map_page(dev->pdev, page, 0, PAGE_SIZE,
				PCI_DMA_BIDIRECTIONAL);
	if (pci_dma_mapping_error(dev->pdev, dma_addr))
		return -EINVAL;
#else
	dma_addr = page_to_phys(page);
#endif
	dev_priv->gtt.base.scratch.page = page;
	dev_priv->gtt.base.scratch.addr = dma_addr;

	return 0;
}

static void teardown_scratch_page(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct page *page = dev_priv->gtt.base.scratch.page;

	set_pages_wb(page, 1);
	pci_unmap_page(dev->pdev, dev_priv->gtt.base.scratch.addr,
		       PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
	put_page(page);
	__free_page(page);
}

static inline unsigned int gen6_get_total_gtt_size(u16 snb_gmch_ctl)
{
	snb_gmch_ctl >>= SNB_GMCH_GGMS_SHIFT;
	snb_gmch_ctl &= SNB_GMCH_GGMS_MASK;
	return snb_gmch_ctl << 20;
}

static inline unsigned int gen8_get_total_gtt_size(u16 bdw_gmch_ctl)
{
	bdw_gmch_ctl >>= BDW_GMCH_GGMS_SHIFT;
	bdw_gmch_ctl &= BDW_GMCH_GGMS_MASK;
	if (bdw_gmch_ctl)
		bdw_gmch_ctl = 1 << bdw_gmch_ctl;
	return bdw_gmch_ctl << 20;
}

static inline size_t gen6_get_stolen_size(u16 snb_gmch_ctl)
{
	snb_gmch_ctl >>= SNB_GMCH_GMS_SHIFT;
	snb_gmch_ctl &= SNB_GMCH_GMS_MASK;
	return snb_gmch_ctl << 25; /* 32 MB units */
}

static inline size_t gen8_get_stolen_size(u16 bdw_gmch_ctl)
{
	bdw_gmch_ctl >>= BDW_GMCH_GMS_SHIFT;
	bdw_gmch_ctl &= BDW_GMCH_GMS_MASK;
	return bdw_gmch_ctl << 25; /* 32 MB units */
}

static int ggtt_probe_common(struct drm_device *dev,
			     size_t gtt_size)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	phys_addr_t gtt_bus_addr;
	int ret;

	/* For Modern GENs the PTEs and register space are split in the BAR */
	gtt_bus_addr = pci_resource_start(dev->pdev, 0) +
		(pci_resource_len(dev->pdev, 0) / 2);

	dev_priv->gtt.gsm = ioremap_wc(gtt_bus_addr, gtt_size);
	if (!dev_priv->gtt.gsm) {
		DRM_ERROR("Failed to map the gtt page table\n");
		return -ENOMEM;
	}

	ret = setup_scratch_page(dev);
	if (ret) {
		DRM_ERROR("Scratch setup failed\n");
		/* iounmap will also get called at remove, but meh */
		iounmap(dev_priv->gtt.gsm);
	}

	return ret;
}

/* The GGTT and PPGTT need a private PPAT setup in order to handle cacheability
 * bits. When using advanced contexts each context stores its own PAT, but
 * writing this data shouldn't be harmful even in those cases. */
static void gen8_setup_private_ppat(struct drm_i915_private *dev_priv)
{
#define GEN8_PPAT_UC		(0<<0)
#define GEN8_PPAT_WC		(1<<0)
#define GEN8_PPAT_WT		(2<<0)
#define GEN8_PPAT_WB		(3<<0)
#define GEN8_PPAT_ELLC_OVERRIDE	(0<<2)
/* FIXME(BDW): Bspec is completely confused about cache control bits. */
#define GEN8_PPAT_LLC		(1<<2)
#define GEN8_PPAT_LLCELLC	(2<<2)
#define GEN8_PPAT_LLCeLLC	(3<<2)
#define GEN8_PPAT_AGE(x)	(x<<4)
#define GEN8_PPAT(i, x) ((uint64_t) (x) << ((i) * 8))
	uint64_t pat;

	pat = GEN8_PPAT(0, GEN8_PPAT_WB | GEN8_PPAT_LLC)     | /* for normal objects, no eLLC */
	      GEN8_PPAT(1, GEN8_PPAT_WC | GEN8_PPAT_LLCELLC) | /* for something pointing to ptes? */
	      GEN8_PPAT(2, GEN8_PPAT_WT | GEN8_PPAT_LLCELLC) | /* for scanout with eLLC */
	      GEN8_PPAT(3, GEN8_PPAT_UC)                     | /* Uncached objects, mostly for scanout */
	      GEN8_PPAT(4, GEN8_PPAT_WB | GEN8_PPAT_LLCELLC | GEN8_PPAT_AGE(0)) |
	      GEN8_PPAT(5, GEN8_PPAT_WB | GEN8_PPAT_LLCELLC | GEN8_PPAT_AGE(1)) |
	      GEN8_PPAT(6, GEN8_PPAT_WB | GEN8_PPAT_LLCELLC | GEN8_PPAT_AGE(2)) |
	      GEN8_PPAT(7, GEN8_PPAT_WB | GEN8_PPAT_LLCELLC | GEN8_PPAT_AGE(3));

	/* XXX: spec defines this as 2 distinct registers. It's unclear if a 64b
	 * write would work. */
	I915_WRITE(GEN8_PRIVATE_PAT, pat);
	I915_WRITE(GEN8_PRIVATE_PAT + 4, pat >> 32);
}

static int gen8_gmch_probe(struct drm_device *dev,
			   size_t *gtt_total,
			   size_t *stolen,
			   phys_addr_t *mappable_base,
			   unsigned long *mappable_end)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned int gtt_size;
	u16 snb_gmch_ctl;
	int ret;

	/* TODO: We're not aware of mappable constraints on gen8 yet */
	*mappable_base = pci_resource_start(dev->pdev, 2);
	*mappable_end = pci_resource_len(dev->pdev, 2);

	if (!pci_set_dma_mask(dev->pdev, DMA_BIT_MASK(39)))
		pci_set_consistent_dma_mask(dev->pdev, DMA_BIT_MASK(39));

	pci_read_config_word(dev->pdev, SNB_GMCH_CTRL, &snb_gmch_ctl);

	*stolen = gen8_get_stolen_size(snb_gmch_ctl);

	gtt_size = gen8_get_total_gtt_size(snb_gmch_ctl);
	*gtt_total = (gtt_size / sizeof(gen8_gtt_pte_t)) << PAGE_SHIFT;

	gen8_setup_private_ppat(dev_priv);

	ret = ggtt_probe_common(dev, gtt_size);

	dev_priv->gtt.base.clear_range = gen8_ggtt_clear_range;
	dev_priv->gtt.base.insert_entries = gen8_ggtt_insert_entries;

	return ret;
}

static int gen6_gmch_probe(struct drm_device *dev,
			   size_t *gtt_total,
			   size_t *stolen,
			   phys_addr_t *mappable_base,
			   unsigned long *mappable_end)
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
		DRM_ERROR("Unknown GMADR size (%lx)\n",
			  dev_priv->gtt.mappable_end);
		return -ENXIO;
	}

	if (!pci_set_dma_mask(dev->pdev, DMA_BIT_MASK(40)))
		pci_set_consistent_dma_mask(dev->pdev, DMA_BIT_MASK(40));
	pci_read_config_word(dev->pdev, SNB_GMCH_CTRL, &snb_gmch_ctl);

	*stolen = gen6_get_stolen_size(snb_gmch_ctl);

	gtt_size = gen6_get_total_gtt_size(snb_gmch_ctl);
	*gtt_total = (gtt_size / sizeof(gen6_gtt_pte_t)) << PAGE_SHIFT;

	ret = ggtt_probe_common(dev, gtt_size);

	dev_priv->gtt.base.clear_range = gen6_ggtt_clear_range;
	dev_priv->gtt.base.insert_entries = gen6_ggtt_insert_entries;

	return ret;
}

static void gen6_gmch_remove(struct i915_address_space *vm)
{

	struct i915_gtt *gtt = container_of(vm, struct i915_gtt, base);
	iounmap(gtt->gsm);
	teardown_scratch_page(vm->dev);
}

static int i915_gmch_probe(struct drm_device *dev,
			   size_t *gtt_total,
			   size_t *stolen,
			   phys_addr_t *mappable_base,
			   unsigned long *mappable_end)
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
	dev_priv->gtt.base.clear_range = i915_ggtt_clear_range;
	dev_priv->gtt.base.insert_entries = i915_ggtt_insert_entries;

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

	ret = gtt->gtt_probe(dev, &gtt->base.total, &gtt->stolen_size,
			     &gtt->mappable_base, &gtt->mappable_end);
	if (ret)
		return ret;

	gtt->base.dev = dev;

	/* GMADR is the PCI mmio aperture into the global GTT. */
	DRM_INFO("Memory usable by graphics device = %zdM\n",
		 gtt->base.total >> 20);
	DRM_DEBUG_DRIVER("GMADR size = %ldM\n", gtt->mappable_end >> 20);
	DRM_DEBUG_DRIVER("GTT stolen size = %zdM\n", gtt->stolen_size >> 20);

	return 0;
}
