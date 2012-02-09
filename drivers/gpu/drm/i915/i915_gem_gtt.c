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
	int i, j;
	uint32_t *pt_vaddr;
	uint32_t scratch_pte;

	scratch_pte = GEN6_PTE_ADDR_ENCODE(ppgtt->scratch_page_dma_addr);
	scratch_pte |= GEN6_PTE_VALID | GEN6_PTE_CACHE_LLC;

	for (i = 0; i < ppgtt->num_pd_entries; i++) {
		pt_vaddr = kmap_atomic(ppgtt->pt_pages[i]);

		for (j = 0; j < I915_PPGTT_PT_ENTRIES; j++)
			pt_vaddr[j] = scratch_pte;

		kunmap_atomic(pt_vaddr);
	}

}

int i915_gem_init_aliasing_ppgtt(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct i915_hw_ppgtt *ppgtt;
	uint32_t pd_entry;
	unsigned first_pd_entry_in_global_pt;
	uint32_t __iomem *pd_addr;
	int i;
	int ret = -ENOMEM;

	/* ppgtt PDEs reside in the global gtt pagetable, which has 512*1024
	 * entries. For aliasing ppgtt support we just steal them at the end for
	 * now. */
	first_pd_entry_in_global_pt = 512*1024 - I915_PPGTT_PD_ENTRIES;

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
	}

	pd_addr = dev_priv->mm.gtt->gtt + first_pd_entry_in_global_pt;
	for (i = 0; i < ppgtt->num_pd_entries; i++) {
		dma_addr_t pt_addr;
		if (dev_priv->mm.gtt->needs_dmar) {
			pt_addr = pci_map_page(dev->pdev, ppgtt->pt_pages[i],
					       0, 4096,
					       PCI_DMA_BIDIRECTIONAL);

			if (pci_dma_mapping_error(dev->pdev,
						  pt_addr)) {
				ret = -EIO;
				goto err_pd_pin;

			}
			ppgtt->pt_dma_addr[i] = pt_addr;
		} else
			pt_addr = page_to_phys(ppgtt->pt_pages[i]);

		pd_entry = GEN6_PDE_ADDR_ENCODE(pt_addr);
		pd_entry |= GEN6_PDE_VALID;

		writel(pd_entry, pd_addr + i);
	}
	readl(pd_addr);

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
		if (i915_gpu_idle(dev_priv->dev, false)) {
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
		i915_gem_gtt_rebind_object(obj, obj->cache_level);
	}

	intel_gtt_chipset_flush();
}

int i915_gem_gtt_bind_object(struct drm_i915_gem_object *obj)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned int agp_type = cache_level_to_agp_type(dev, obj->cache_level);
	int ret;

	if (dev_priv->mm.gtt->needs_dmar) {
		ret = intel_gtt_map_memory(obj->pages,
					   obj->base.size >> PAGE_SHIFT,
					   &obj->sg_list,
					   &obj->num_sg);
		if (ret != 0)
			return ret;

		intel_gtt_insert_sg_entries(obj->sg_list,
					    obj->num_sg,
					    obj->gtt_space->start >> PAGE_SHIFT,
					    agp_type);
	} else
		intel_gtt_insert_pages(obj->gtt_space->start >> PAGE_SHIFT,
				       obj->base.size >> PAGE_SHIFT,
				       obj->pages,
				       agp_type);

	return 0;
}

void i915_gem_gtt_rebind_object(struct drm_i915_gem_object *obj,
				enum i915_cache_level cache_level)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned int agp_type = cache_level_to_agp_type(dev, cache_level);

	if (dev_priv->mm.gtt->needs_dmar) {
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
}

void i915_gem_gtt_unbind_object(struct drm_i915_gem_object *obj)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	bool interruptible;

	interruptible = do_idling(dev_priv);

	intel_gtt_clear_range(obj->gtt_space->start >> PAGE_SHIFT,
			      obj->base.size >> PAGE_SHIFT);

	if (obj->sg_list) {
		intel_gtt_unmap_memory(obj->sg_list, obj->num_sg);
		obj->sg_list = NULL;
	}

	undo_idling(dev_priv, interruptible);
}
