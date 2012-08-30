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

#include "drmP.h"
#include "drm.h"
#include "i915_drm.h"
#include "i915_drv.h"
#include "i915_trace.h"
#include "intel_drv.h"

/* PPGTT support for Sandybdrige/Gen6 and later */
static void i915_ppgtt_clear_range(struct i915_hw_ppgtt *ppgtt,
				   unsigned first_entry,
				   unsigned num_entries)
{
	uint32_t *pt_vaddr;
	uint32_t scratch_pte;
	unsigned act_pd = first_entry / I915_PPGTT_PT_ENTRIES;
	unsigned first_pte = first_entry % I915_PPGTT_PT_ENTRIES;
	unsigned last_pte, i;

	scratch_pte = GEN6_PTE_ADDR_ENCODE(ppgtt->scratch_page_dma_addr);
	scratch_pte |= GEN6_PTE_VALID | GEN6_PTE_CACHE_LLC;

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

	ppgtt->pd_offset = (first_pd_entry_in_global_pt)*sizeof(uint32_t);

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
					 struct scatterlist *sg_list,
					 unsigned sg_len,
					 unsigned first_entry,
					 uint32_t pte_flags)
{
	uint32_t *pt_vaddr, pte;
	unsigned act_pd = first_entry / I915_PPGTT_PT_ENTRIES;
	unsigned first_pte = first_entry % I915_PPGTT_PT_ENTRIES;
	unsigned i, j, m, segment_len;
	dma_addr_t page_addr;
	struct scatterlist *sg;

	/* init sg walking */
	sg = sg_list;
	i = 0;
	segment_len = sg_dma_len(sg) >> PAGE_SHIFT;
	m = 0;

	while (i < sg_len) {
		pt_vaddr = kmap_atomic(ppgtt->pt_pages[act_pd]);

		for (j = first_pte; j < I915_PPGTT_PT_ENTRIES; j++) {
			page_addr = sg_dma_address(sg) + (m << PAGE_SHIFT);
			pte = GEN6_PTE_ADDR_ENCODE(page_addr);
			pt_vaddr[j] = pte | pte_flags;

			/* grab the next page */
			m++;
			if (m == segment_len) {
				sg = sg_next(sg);
				i++;
				if (i == sg_len)
					break;

				segment_len = sg_dma_len(sg) >> PAGE_SHIFT;
				m = 0;
			}
		}

		kunmap_atomic(pt_vaddr);

		first_pte = 0;
		act_pd++;
	}
}

static void i915_ppgtt_insert_pages(struct i915_hw_ppgtt *ppgtt,
				    unsigned first_entry, unsigned num_entries,
				    struct page **pages, uint32_t pte_flags)
{
	uint32_t *pt_vaddr, pte;
	unsigned act_pd = first_entry / I915_PPGTT_PT_ENTRIES;
	unsigned first_pte = first_entry % I915_PPGTT_PT_ENTRIES;
	unsigned last_pte, i;
	dma_addr_t page_addr;

	while (num_entries) {
		last_pte = first_pte + num_entries;
		last_pte = min_t(unsigned, last_pte, I915_PPGTT_PT_ENTRIES);

		pt_vaddr = kmap_atomic(ppgtt->pt_pages[act_pd]);

		for (i = first_pte; i < last_pte; i++) {
			page_addr = page_to_phys(*pages);
			pte = GEN6_PTE_ADDR_ENCODE(page_addr);
			pt_vaddr[i] = pte | pte_flags;

			pages++;
		}

		kunmap_atomic(pt_vaddr);

		num_entries -= last_pte - first_pte;
		first_pte = 0;
		act_pd++;
	}
}

void i915_ppgtt_bind_object(struct i915_hw_ppgtt *ppgtt,
			    struct drm_i915_gem_object *obj,
			    enum i915_cache_level cache_level)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t pte_flags = GEN6_PTE_VALID;

	switch (cache_level) {
	case I915_CACHE_LLC_MLC:
		pte_flags |= GEN6_PTE_CACHE_LLC_MLC;
		break;
	case I915_CACHE_LLC:
		pte_flags |= GEN6_PTE_CACHE_LLC;
		break;
	case I915_CACHE_NONE:
		if (IS_HASWELL(dev))
			pte_flags |= HSW_PTE_UNCACHED;
		else
			pte_flags |= GEN6_PTE_UNCACHED;
		break;
	default:
		BUG();
	}

	if (obj->sg_table) {
		i915_ppgtt_insert_sg_entries(ppgtt,
					     obj->sg_table->sgl,
					     obj->sg_table->nents,
					     obj->gtt_space->start >> PAGE_SHIFT,
					     pte_flags);
	} else if (dev_priv->mm.gtt->needs_dmar) {
		BUG_ON(!obj->sg_list);

		i915_ppgtt_insert_sg_entries(ppgtt,
					     obj->sg_list,
					     obj->num_sg,
					     obj->gtt_space->start >> PAGE_SHIFT,
					     pte_flags);
	} else
		i915_ppgtt_insert_pages(ppgtt,
					obj->gtt_space->start >> PAGE_SHIFT,
					obj->base.size >> PAGE_SHIFT,
					obj->pages,
					pte_flags);
}

void i915_ppgtt_unbind_object(struct i915_hw_ppgtt *ppgtt,
			      struct drm_i915_gem_object *obj)
{
	i915_ppgtt_clear_range(ppgtt,
			       obj->gtt_space->start >> PAGE_SHIFT,
			       obj->base.size >> PAGE_SHIFT);
}

/* XXX kill agp_type! */
static unsigned int cache_level_to_agp_type(struct drm_device *dev,
					    enum i915_cache_level cache_level)
{
	switch (cache_level) {
	case I915_CACHE_LLC_MLC:
		if (INTEL_INFO(dev)->gen >= 6)
			return AGP_USER_CACHED_MEMORY_LLC_MLC;
		/* Older chipsets do not have this extra level of CPU
		 * cacheing, so fallthrough and request the PTE simply
		 * as cached.
		 */
	case I915_CACHE_LLC:
		return AGP_USER_CACHED_MEMORY;
	default:
	case I915_CACHE_NONE:
		return AGP_USER_MEMORY;
	}
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

void i915_gem_restore_gtt_mappings(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj;

	/* First fill our portion of the GTT with scratch pages */
	intel_gtt_clear_range(dev_priv->mm.gtt_start / PAGE_SIZE,
			      (dev_priv->mm.gtt_end - dev_priv->mm.gtt_start) / PAGE_SIZE);

	list_for_each_entry(obj, &dev_priv->mm.gtt_list, gtt_list) {
		i915_gem_clflush_object(obj);
		i915_gem_gtt_bind_object(obj, obj->cache_level);
	}

	intel_gtt_chipset_flush();
}

int i915_gem_gtt_prepare_object(struct drm_i915_gem_object *obj)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	/* don't map imported dma buf objects */
	if (dev_priv->mm.gtt->needs_dmar && !obj->sg_table)
		return intel_gtt_map_memory(obj->pages,
					    obj->base.size >> PAGE_SHIFT,
					    &obj->sg_list,
					    &obj->num_sg);
	else
		return 0;
}

void i915_gem_gtt_bind_object(struct drm_i915_gem_object *obj,
			      enum i915_cache_level cache_level)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned int agp_type = cache_level_to_agp_type(dev, cache_level);

	if (obj->sg_table) {
		intel_gtt_insert_sg_entries(obj->sg_table->sgl,
					    obj->sg_table->nents,
					    obj->gtt_space->start >> PAGE_SHIFT,
					    agp_type);
	} else if (dev_priv->mm.gtt->needs_dmar) {
		BUG_ON(!obj->sg_list);

		intel_gtt_insert_sg_entries(obj->sg_list,
					    obj->num_sg,
					    obj->gtt_space->start >> PAGE_SHIFT,
					    agp_type);
	} else
		intel_gtt_insert_pages(obj->gtt_space->start >> PAGE_SHIFT,
				       obj->base.size >> PAGE_SHIFT,
				       obj->pages,
				       agp_type);

	obj->has_global_gtt_mapping = 1;
}

void i915_gem_gtt_unbind_object(struct drm_i915_gem_object *obj)
{
	intel_gtt_clear_range(obj->gtt_space->start >> PAGE_SHIFT,
			      obj->base.size >> PAGE_SHIFT);

	obj->has_global_gtt_mapping = 0;
}

void i915_gem_gtt_finish_object(struct drm_i915_gem_object *obj)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	bool interruptible;

	interruptible = do_idling(dev_priv);

	if (obj->sg_list) {
		intel_gtt_unmap_memory(obj->sg_list, obj->num_sg);
		obj->sg_list = NULL;
	}

	undo_idling(dev_priv, interruptible);
}

void i915_gem_init_global_gtt(struct drm_device *dev,
			      unsigned long start,
			      unsigned long mappable_end,
			      unsigned long end)
{
	drm_i915_private_t *dev_priv = dev->dev_private;

	/* Substract the guard page ... */
	drm_mm_init(&dev_priv->mm.gtt_space, start, end - start - PAGE_SIZE);

	dev_priv->mm.gtt_start = start;
	dev_priv->mm.gtt_mappable_end = mappable_end;
	dev_priv->mm.gtt_end = end;
	dev_priv->mm.gtt_total = end - start;
	dev_priv->mm.mappable_gtt_total = min(end, mappable_end) - start;

	/* ... but ensure that we clear the entire range. */
	intel_gtt_clear_range(start / PAGE_SIZE, (end-start) / PAGE_SIZE);
}
