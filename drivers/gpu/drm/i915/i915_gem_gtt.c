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

typedef uint32_t gtt_pte_t;

static inline gtt_pte_t pte_encode(struct drm_device *dev,
				   dma_addr_t addr,
				   enum i915_cache_level level)
{
	gtt_pte_t pte = GEN6_PTE_VALID;
	pte |= GEN6_PTE_ADDR_ENCODE(addr);

	switch (level) {
	case I915_CACHE_LLC_MLC:
		/* Haswell doesn't set L3 this way */
		if (IS_HASWELL(dev))
			pte |= GEN6_PTE_CACHE_LLC;
		else
			pte |= GEN6_PTE_CACHE_LLC_MLC;
		break;
	case I915_CACHE_LLC:
		pte |= GEN6_PTE_CACHE_LLC;
		break;
	case I915_CACHE_NONE:
		if (IS_HASWELL(dev))
			pte |= HSW_PTE_UNCACHED;
		else
			pte |= GEN6_PTE_UNCACHED;
		break;
	default:
		BUG();
	}


	return pte;
}

/* PPGTT support for Sandybdrige/Gen6 and later */
static void i915_ppgtt_clear_range(struct i915_hw_ppgtt *ppgtt,
				   unsigned first_entry,
				   unsigned num_entries)
{
	gtt_pte_t *pt_vaddr;
	gtt_pte_t scratch_pte;
	unsigned act_pd = first_entry / I915_PPGTT_PT_ENTRIES;
	unsigned first_pte = first_entry % I915_PPGTT_PT_ENTRIES;
	unsigned last_pte, i;

	scratch_pte = pte_encode(ppgtt->dev, ppgtt->scratch_page_dma_addr,
				 I915_CACHE_LLC);

	while (num_entries) {
		last_pte = first_pte + num_entries;
		if (last_pte > I915_PPGTT_PT_ENTRIES)
			last_pte = I915_PPGTT_PT_ENTRIES;

		pt_vaddr = kmap_atomic(ppgtt->pt_pages[act_pd]);

		for (i = first_pte; i < last_pte; i++)
			pt_vaddr[i] = scratch_pte;

		kunmap_atomic(pt_vaddr);

		num_entries -= last_pte - first_pte;
		first_pte = 0;
		act_pd++;
	}
}

int i915_gem_init_aliasing_ppgtt(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct i915_hw_ppgtt *ppgtt;
	unsigned first_pd_entry_in_global_pt;
	int i;
	int ret = -ENOMEM;

	/* ppgtt PDEs reside in the global gtt pagetable, which has 512*1024
	 * entries. For aliasing ppgtt support we just steal them at the end for
	 * now. */
	first_pd_entry_in_global_pt = dev_priv->mm.gtt->gtt_total_entries - I915_PPGTT_PD_ENTRIES;

	ppgtt = kzalloc(sizeof(*ppgtt), GFP_KERNEL);
	if (!ppgtt)
		return ret;

	ppgtt->dev = dev;
	ppgtt->num_pd_entries = I915_PPGTT_PD_ENTRIES;
	ppgtt->pt_pages = kzalloc(sizeof(struct page *)*ppgtt->num_pd_entries,
				  GFP_KERNEL);
	if (!ppgtt->pt_pages)
		goto err_ppgtt;

	for (i = 0; i < ppgtt->num_pd_entries; i++) {
		ppgtt->pt_pages[i] = alloc_page(GFP_KERNEL);
		if (!ppgtt->pt_pages[i])
			goto err_pt_alloc;
	}

	if (dev_priv->mm.gtt->needs_dmar) {
		ppgtt->pt_dma_addr = kzalloc(sizeof(dma_addr_t)
						*ppgtt->num_pd_entries,
					     GFP_KERNEL);
		if (!ppgtt->pt_dma_addr)
			goto err_pt_alloc;

		for (i = 0; i < ppgtt->num_pd_entries; i++) {
			dma_addr_t pt_addr;

			pt_addr = pci_map_page(dev->pdev, ppgtt->pt_pages[i],
					       0, 4096,
					       PCI_DMA_BIDIRECTIONAL);

			if (pci_dma_mapping_error(dev->pdev,
						  pt_addr)) {
				ret = -EIO;
				goto err_pd_pin;

			}
			ppgtt->pt_dma_addr[i] = pt_addr;
		}
	}

	ppgtt->scratch_page_dma_addr = dev_priv->mm.gtt->scratch_page_dma;

	i915_ppgtt_clear_range(ppgtt, 0,
			       ppgtt->num_pd_entries*I915_PPGTT_PT_ENTRIES);

	ppgtt->pd_offset = (first_pd_entry_in_global_pt)*sizeof(gtt_pte_t);

	dev_priv->mm.aliasing_ppgtt = ppgtt;

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
err_ppgtt:
	kfree(ppgtt);

	return ret;
}

void i915_gem_cleanup_aliasing_ppgtt(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct i915_hw_ppgtt *ppgtt = dev_priv->mm.aliasing_ppgtt;
	int i;

	if (!ppgtt)
		return;

	if (ppgtt->pt_dma_addr) {
		for (i = 0; i < ppgtt->num_pd_entries; i++)
			pci_unmap_page(dev->pdev, ppgtt->pt_dma_addr[i],
				       4096, PCI_DMA_BIDIRECTIONAL);
	}

	kfree(ppgtt->pt_dma_addr);
	for (i = 0; i < ppgtt->num_pd_entries; i++)
		__free_page(ppgtt->pt_pages[i]);
	kfree(ppgtt->pt_pages);
	kfree(ppgtt);
}

static void i915_ppgtt_insert_sg_entries(struct i915_hw_ppgtt *ppgtt,
					 const struct sg_table *pages,
					 unsigned first_entry,
					 enum i915_cache_level cache_level)
{
	gtt_pte_t *pt_vaddr;
	unsigned act_pd = first_entry / I915_PPGTT_PT_ENTRIES;
	unsigned first_pte = first_entry % I915_PPGTT_PT_ENTRIES;
	unsigned i, j, m, segment_len;
	dma_addr_t page_addr;
	struct scatterlist *sg;

	/* init sg walking */
	sg = pages->sgl;
	i = 0;
	segment_len = sg_dma_len(sg) >> PAGE_SHIFT;
	m = 0;

	while (i < pages->nents) {
		pt_vaddr = kmap_atomic(ppgtt->pt_pages[act_pd]);

		for (j = first_pte; j < I915_PPGTT_PT_ENTRIES; j++) {
			page_addr = sg_dma_address(sg) + (m << PAGE_SHIFT);
			pt_vaddr[j] = pte_encode(ppgtt->dev, page_addr,
						 cache_level);

			/* grab the next page */
			if (++m == segment_len) {
				if (++i == pages->nents)
					break;

				sg = sg_next(sg);
				segment_len = sg_dma_len(sg) >> PAGE_SHIFT;
				m = 0;
			}
		}

		kunmap_atomic(pt_vaddr);

		first_pte = 0;
		act_pd++;
	}
}

void i915_ppgtt_bind_object(struct i915_hw_ppgtt *ppgtt,
			    struct drm_i915_gem_object *obj,
			    enum i915_cache_level cache_level)
{
	i915_ppgtt_insert_sg_entries(ppgtt,
				     obj->pages,
				     obj->gtt_space->start >> PAGE_SHIFT,
				     cache_level);
}

void i915_ppgtt_unbind_object(struct i915_hw_ppgtt *ppgtt,
			      struct drm_i915_gem_object *obj)
{
	i915_ppgtt_clear_range(ppgtt,
			       obj->gtt_space->start >> PAGE_SHIFT,
			       obj->base.size >> PAGE_SHIFT);
}

static bool do_idling(struct drm_i915_private *dev_priv)
{
	bool ret = dev_priv->mm.interruptible;

	if (unlikely(dev_priv->mm.gtt->do_idle_maps)) {
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
	if (unlikely(dev_priv->mm.gtt->do_idle_maps))
		dev_priv->mm.interruptible = interruptible;
}


static void i915_ggtt_clear_range(struct drm_device *dev,
				 unsigned first_entry,
				 unsigned num_entries)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	gtt_pte_t scratch_pte;
	volatile void __iomem *gtt_base = dev_priv->mm.gtt->gtt + first_entry;
	const int max_entries = dev_priv->mm.gtt->gtt_total_entries - first_entry;

	if (INTEL_INFO(dev)->gen < 6) {
		intel_gtt_clear_range(first_entry, num_entries);
		return;
	}

	if (WARN(num_entries > max_entries,
		 "First entry = %d; Num entries = %d (max=%d)\n",
		 first_entry, num_entries, max_entries))
		num_entries = max_entries;

	scratch_pte = pte_encode(dev, dev_priv->mm.gtt->scratch_page_dma, I915_CACHE_LLC);
	memset_io(gtt_base, scratch_pte, num_entries * sizeof(scratch_pte));
	readl(gtt_base);
}

void i915_gem_restore_gtt_mappings(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj;

	/* First fill our portion of the GTT with scratch pages */
	i915_ggtt_clear_range(dev, dev_priv->mm.gtt_start / PAGE_SIZE,
			      (dev_priv->mm.gtt_end - dev_priv->mm.gtt_start) / PAGE_SIZE);

	list_for_each_entry(obj, &dev_priv->mm.bound_list, gtt_list) {
		i915_gem_clflush_object(obj);
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

/*
 * Binds an object into the global gtt with the specified cache level. The object
 * will be accessible to the GPU via commands whose operands reference offsets
 * within the global GTT as well as accessible by the GPU through the GMADR
 * mapped BAR (dev_priv->mm.gtt->gtt).
 */
static void gen6_ggtt_bind_object(struct drm_i915_gem_object *obj,
				  enum i915_cache_level level)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct sg_table *st = obj->pages;
	struct scatterlist *sg = st->sgl;
	const int first_entry = obj->gtt_space->start >> PAGE_SHIFT;
	const int max_entries = dev_priv->mm.gtt->gtt_total_entries - first_entry;
	gtt_pte_t __iomem *gtt_entries = dev_priv->mm.gtt->gtt + first_entry;
	int unused, i = 0;
	unsigned int len, m = 0;
	dma_addr_t addr;

	for_each_sg(st->sgl, sg, st->nents, unused) {
		len = sg_dma_len(sg) >> PAGE_SHIFT;
		for (m = 0; m < len; m++) {
			addr = sg_dma_address(sg) + (m << PAGE_SHIFT);
			gtt_entries[i] = pte_encode(dev, addr, level);
			i++;
		}
	}

	BUG_ON(i > max_entries);
	BUG_ON(i != obj->base.size / PAGE_SIZE);

	/* XXX: This serves as a posting read to make sure that the PTE has
	 * actually been updated. There is some concern that even though
	 * registers and PTEs are within the same BAR that they are potentially
	 * of NUMA access patterns. Therefore, even with the way we assume
	 * hardware should work, we must keep this posting read for paranoia.
	 */
	if (i != 0)
		WARN_ON(readl(&gtt_entries[i-1]) != pte_encode(dev, addr, level));

	/* This next bit makes the above posting read even more important. We
	 * want to flush the TLBs only after we're certain all the PTE updates
	 * have finished.
	 */
	I915_WRITE(GFX_FLSH_CNTL_GEN6, GFX_FLSH_CNTL_EN);
	POSTING_READ(GFX_FLSH_CNTL_GEN6);
}

void i915_gem_gtt_bind_object(struct drm_i915_gem_object *obj,
			      enum i915_cache_level cache_level)
{
	struct drm_device *dev = obj->base.dev;
	if (INTEL_INFO(dev)->gen < 6) {
		unsigned int flags = (cache_level == I915_CACHE_NONE) ?
			AGP_USER_MEMORY : AGP_USER_CACHED_MEMORY;
		intel_gtt_insert_sg_entries(obj->pages,
					    obj->gtt_space->start >> PAGE_SHIFT,
					    flags);
	} else {
		gen6_ggtt_bind_object(obj, cache_level);
	}

	obj->has_global_gtt_mapping = 1;
}

void i915_gem_gtt_unbind_object(struct drm_i915_gem_object *obj)
{
	i915_ggtt_clear_range(obj->base.dev,
			      obj->gtt_space->start >> PAGE_SHIFT,
			      obj->base.size >> PAGE_SHIFT);

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

void i915_gem_init_global_gtt(struct drm_device *dev,
			      unsigned long start,
			      unsigned long mappable_end,
			      unsigned long end)
{
	drm_i915_private_t *dev_priv = dev->dev_private;

	/* Substract the guard page ... */
	drm_mm_init(&dev_priv->mm.gtt_space, start, end - start - PAGE_SIZE);
	if (!HAS_LLC(dev))
		dev_priv->mm.gtt_space.color_adjust = i915_gtt_color_adjust;

	dev_priv->mm.gtt_start = start;
	dev_priv->mm.gtt_mappable_end = mappable_end;
	dev_priv->mm.gtt_end = end;
	dev_priv->mm.gtt_total = end - start;
	dev_priv->mm.mappable_gtt_total = min(end, mappable_end) - start;

	/* ... but ensure that we clear the entire range. */
	i915_ggtt_clear_range(dev, start / PAGE_SIZE, (end-start) / PAGE_SIZE);
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
	dev_priv->mm.gtt->scratch_page = page;
	dev_priv->mm.gtt->scratch_page_dma = dma_addr;

	return 0;
}

static void teardown_scratch_page(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	set_pages_wb(dev_priv->mm.gtt->scratch_page, 1);
	pci_unmap_page(dev->pdev, dev_priv->mm.gtt->scratch_page_dma,
		       PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
	put_page(dev_priv->mm.gtt->scratch_page);
	__free_page(dev_priv->mm.gtt->scratch_page);
}

static inline unsigned int gen6_get_total_gtt_size(u16 snb_gmch_ctl)
{
	snb_gmch_ctl >>= SNB_GMCH_GGMS_SHIFT;
	snb_gmch_ctl &= SNB_GMCH_GGMS_MASK;
	return snb_gmch_ctl << 20;
}

static inline unsigned int gen6_get_stolen_size(u16 snb_gmch_ctl)
{
	snb_gmch_ctl >>= SNB_GMCH_GMS_SHIFT;
	snb_gmch_ctl &= SNB_GMCH_GMS_MASK;
	return snb_gmch_ctl << 25; /* 32 MB units */
}

static inline unsigned int gen7_get_stolen_size(u16 snb_gmch_ctl)
{
	static const int stolen_decoder[] = {
		0, 0, 0, 0, 0, 32, 48, 64, 128, 256, 96, 160, 224, 352};
	snb_gmch_ctl >>= IVB_GMCH_GMS_SHIFT;
	snb_gmch_ctl &= IVB_GMCH_GMS_MASK;
	return stolen_decoder[snb_gmch_ctl] << 20;
}

int i915_gem_gtt_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	phys_addr_t gtt_bus_addr;
	u16 snb_gmch_ctl;
	u32 tmp;
	int ret;

	/* On modern platforms we need not worry ourself with the legacy
	 * hostbridge query stuff. Skip it entirely
	 */
	if (INTEL_INFO(dev)->gen < 6) {
		ret = intel_gmch_probe(dev_priv->bridge_dev, dev->pdev, NULL);
		if (!ret) {
			DRM_ERROR("failed to set up gmch\n");
			return -EIO;
		}

		dev_priv->mm.gtt = intel_gtt_get();
		if (!dev_priv->mm.gtt) {
			DRM_ERROR("Failed to initialize GTT\n");
			intel_gmch_remove();
			return -ENODEV;
		}
		return 0;
	}

	dev_priv->mm.gtt = kzalloc(sizeof(*dev_priv->mm.gtt), GFP_KERNEL);
	if (!dev_priv->mm.gtt)
		return -ENOMEM;

	if (!pci_set_dma_mask(dev->pdev, DMA_BIT_MASK(40)))
		pci_set_consistent_dma_mask(dev->pdev, DMA_BIT_MASK(40));

	pci_read_config_dword(dev->pdev, PCI_BASE_ADDRESS_0, &tmp);
	/* For GEN6+ the PTEs for the ggtt live at 2MB + BAR0 */
	gtt_bus_addr = (tmp & PCI_BASE_ADDRESS_MEM_MASK) + (2<<20);

	pci_read_config_dword(dev->pdev, PCI_BASE_ADDRESS_2, &tmp);
	dev_priv->mm.gtt->gma_bus_addr = tmp & PCI_BASE_ADDRESS_MEM_MASK;

	/* i9xx_setup */
	pci_read_config_word(dev->pdev, SNB_GMCH_CTRL, &snb_gmch_ctl);
	dev_priv->mm.gtt->gtt_total_entries =
		gen6_get_total_gtt_size(snb_gmch_ctl) / sizeof(gtt_pte_t);
	if (INTEL_INFO(dev)->gen < 7)
		dev_priv->mm.gtt->stolen_size = gen6_get_stolen_size(snb_gmch_ctl);
	else
		dev_priv->mm.gtt->stolen_size = gen7_get_stolen_size(snb_gmch_ctl);

	dev_priv->mm.gtt->gtt_mappable_entries = pci_resource_len(dev->pdev, 2) >> PAGE_SHIFT;
	/* 64/512MB is the current min/max we actually know of, but this is just a
	 * coarse sanity check.
	 */
	if ((dev_priv->mm.gtt->gtt_mappable_entries >> 8) < 64 ||
	    dev_priv->mm.gtt->gtt_mappable_entries > dev_priv->mm.gtt->gtt_total_entries) {
		DRM_ERROR("Unknown GMADR entries (%d)\n",
			  dev_priv->mm.gtt->gtt_mappable_entries);
		ret = -ENXIO;
		goto err_out;
	}

	ret = setup_scratch_page(dev);
	if (ret) {
		DRM_ERROR("Scratch setup failed\n");
		goto err_out;
	}

	dev_priv->mm.gtt->gtt = ioremap_wc(gtt_bus_addr,
					   dev_priv->mm.gtt->gtt_total_entries * sizeof(gtt_pte_t));
	if (!dev_priv->mm.gtt->gtt) {
		DRM_ERROR("Failed to map the gtt page table\n");
		teardown_scratch_page(dev);
		ret = -ENOMEM;
		goto err_out;
	}

	/* GMADR is the PCI aperture used by SW to access tiled GFX surfaces in a linear fashion. */
	DRM_INFO("Memory Usable by graphics device = %dK\n", dev_priv->mm.gtt->gtt_total_entries >> 10);
	DRM_DEBUG_DRIVER("GMADR size = %dM\n", dev_priv->mm.gtt->gtt_mappable_entries >> 8);
	DRM_DEBUG_DRIVER("GTT stolen size = %dM\n", dev_priv->mm.gtt->stolen_size >> 20);

	return 0;

err_out:
	kfree(dev_priv->mm.gtt);
	if (INTEL_INFO(dev)->gen < 6)
		intel_gmch_remove();
	return ret;
}

void i915_gem_gtt_fini(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	iounmap(dev_priv->mm.gtt->gtt);
	teardown_scratch_page(dev);
	if (INTEL_INFO(dev)->gen < 6)
		intel_gmch_remove();
	kfree(dev_priv->mm.gtt);
}
