/*
 * Copyright (C) The Weather Channel, Inc.  2002.  All Rights Reserved.
 * Copyright 2005 Stephane Marchesin
 *
 * The Weather Channel (TM) funded Tungsten Graphics to develop the
 * initial release of the Radeon 8500 driver under the XFree86 license.
 * This notice must be preserved.
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
 * THE AUTHORS AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Keith Whitwell <keith@tungstengraphics.com>
 */


#include "drmP.h"
#include "drm.h"
#include "drm_sarea.h"
#include "nouveau_drv.h"

static struct mem_block *
split_block(struct mem_block *p, uint64_t start, uint64_t size,
	    struct drm_file *file_priv)
{
	/* Maybe cut off the start of an existing block */
	if (start > p->start) {
		struct mem_block *newblock =
			kmalloc(sizeof(*newblock), GFP_KERNEL);
		if (!newblock)
			goto out;
		newblock->start = start;
		newblock->size = p->size - (start - p->start);
		newblock->file_priv = NULL;
		newblock->next = p->next;
		newblock->prev = p;
		p->next->prev = newblock;
		p->next = newblock;
		p->size -= newblock->size;
		p = newblock;
	}

	/* Maybe cut off the end of an existing block */
	if (size < p->size) {
		struct mem_block *newblock =
			kmalloc(sizeof(*newblock), GFP_KERNEL);
		if (!newblock)
			goto out;
		newblock->start = start + size;
		newblock->size = p->size - size;
		newblock->file_priv = NULL;
		newblock->next = p->next;
		newblock->prev = p;
		p->next->prev = newblock;
		p->next = newblock;
		p->size = size;
	}

out:
	/* Our block is in the middle */
	p->file_priv = file_priv;
	return p;
}

struct mem_block *
nouveau_mem_alloc_block(struct mem_block *heap, uint64_t size,
			int align2, struct drm_file *file_priv, int tail)
{
	struct mem_block *p;
	uint64_t mask = (1 << align2) - 1;

	if (!heap)
		return NULL;

	if (tail) {
		list_for_each_prev(p, heap) {
			uint64_t start = ((p->start + p->size) - size) & ~mask;

			if (p->file_priv == NULL && start >= p->start &&
			    start + size <= p->start + p->size)
				return split_block(p, start, size, file_priv);
		}
	} else {
		list_for_each(p, heap) {
			uint64_t start = (p->start + mask) & ~mask;

			if (p->file_priv == NULL &&
			    start + size <= p->start + p->size)
				return split_block(p, start, size, file_priv);
		}
	}

	return NULL;
}

void nouveau_mem_free_block(struct mem_block *p)
{
	p->file_priv = NULL;

	/* Assumes a single contiguous range.  Needs a special file_priv in
	 * 'heap' to stop it being subsumed.
	 */
	if (p->next->file_priv == NULL) {
		struct mem_block *q = p->next;
		p->size += q->size;
		p->next = q->next;
		p->next->prev = p;
		kfree(q);
	}

	if (p->prev->file_priv == NULL) {
		struct mem_block *q = p->prev;
		q->size += p->size;
		q->next = p->next;
		q->next->prev = q;
		kfree(p);
	}
}

/* Initialize.  How to check for an uninitialized heap?
 */
int nouveau_mem_init_heap(struct mem_block **heap, uint64_t start,
			  uint64_t size)
{
	struct mem_block *blocks = kmalloc(sizeof(*blocks), GFP_KERNEL);

	if (!blocks)
		return -ENOMEM;

	*heap = kmalloc(sizeof(**heap), GFP_KERNEL);
	if (!*heap) {
		kfree(blocks);
		return -ENOMEM;
	}

	blocks->start = start;
	blocks->size = size;
	blocks->file_priv = NULL;
	blocks->next = blocks->prev = *heap;

	memset(*heap, 0, sizeof(**heap));
	(*heap)->file_priv = (struct drm_file *) -1;
	(*heap)->next = (*heap)->prev = blocks;
	return 0;
}

/*
 * Free all blocks associated with the releasing file_priv
 */
void nouveau_mem_release(struct drm_file *file_priv, struct mem_block *heap)
{
	struct mem_block *p;

	if (!heap || !heap->next)
		return;

	list_for_each(p, heap) {
		if (p->file_priv == file_priv)
			p->file_priv = NULL;
	}

	/* Assumes a single contiguous range.  Needs a special file_priv in
	 * 'heap' to stop it being subsumed.
	 */
	list_for_each(p, heap) {
		while ((p->file_priv == NULL) &&
					(p->next->file_priv == NULL) &&
					(p->next != heap)) {
			struct mem_block *q = p->next;
			p->size += q->size;
			p->next = q->next;
			p->next->prev = p;
			kfree(q);
		}
	}
}

/*
 * NV10-NV40 tiling helpers
 */

static void
nv10_mem_set_region_tiling(struct drm_device *dev, int i, uint32_t addr,
			   uint32_t size, uint32_t pitch)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_fifo_engine *pfifo = &dev_priv->engine.fifo;
	struct nouveau_fb_engine *pfb = &dev_priv->engine.fb;
	struct nouveau_pgraph_engine *pgraph = &dev_priv->engine.graph;
	struct nouveau_tile_reg *tile = &dev_priv->tile.reg[i];

	tile->addr = addr;
	tile->size = size;
	tile->used = !!pitch;
	nouveau_fence_unref((void **)&tile->fence);

	if (!pfifo->cache_flush(dev))
		return;

	pfifo->reassign(dev, false);
	pfifo->cache_flush(dev);
	pfifo->cache_pull(dev, false);

	nouveau_wait_for_idle(dev);

	pgraph->set_region_tiling(dev, i, addr, size, pitch);
	pfb->set_region_tiling(dev, i, addr, size, pitch);

	pfifo->cache_pull(dev, true);
	pfifo->reassign(dev, true);
}

struct nouveau_tile_reg *
nv10_mem_set_tiling(struct drm_device *dev, uint32_t addr, uint32_t size,
		    uint32_t pitch)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_fb_engine *pfb = &dev_priv->engine.fb;
	struct nouveau_tile_reg *tile = dev_priv->tile.reg, *found = NULL;
	int i;

	spin_lock(&dev_priv->tile.lock);

	for (i = 0; i < pfb->num_tiles; i++) {
		if (tile[i].used)
			/* Tile region in use. */
			continue;

		if (tile[i].fence &&
		    !nouveau_fence_signalled(tile[i].fence, NULL))
			/* Pending tile region. */
			continue;

		if (max(tile[i].addr, addr) <
		    min(tile[i].addr + tile[i].size, addr + size))
			/* Kill an intersecting tile region. */
			nv10_mem_set_region_tiling(dev, i, 0, 0, 0);

		if (pitch && !found) {
			/* Free tile region. */
			nv10_mem_set_region_tiling(dev, i, addr, size, pitch);
			found = &tile[i];
		}
	}

	spin_unlock(&dev_priv->tile.lock);

	return found;
}

void
nv10_mem_expire_tiling(struct drm_device *dev, struct nouveau_tile_reg *tile,
		       struct nouveau_fence *fence)
{
	if (fence) {
		/* Mark it as pending. */
		tile->fence = fence;
		nouveau_fence_ref(fence);
	}

	tile->used = false;
}

/*
 * NV50 VM helpers
 */
int
nv50_mem_vm_bind_linear(struct drm_device *dev, uint64_t virt, uint32_t size,
			uint32_t flags, uint64_t phys)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj **pgt;
	unsigned psz, pfl, pages;

	if (virt >= dev_priv->vm_gart_base &&
	    (virt + size) < (dev_priv->vm_gart_base + dev_priv->vm_gart_size)) {
		psz = 12;
		pgt = &dev_priv->gart_info.sg_ctxdma;
		pfl = 0x21;
		virt -= dev_priv->vm_gart_base;
	} else
	if (virt >= dev_priv->vm_vram_base &&
	    (virt + size) < (dev_priv->vm_vram_base + dev_priv->vm_vram_size)) {
		psz = 16;
		pgt = dev_priv->vm_vram_pt;
		pfl = 0x01;
		virt -= dev_priv->vm_vram_base;
	} else {
		NV_ERROR(dev, "Invalid address: 0x%16llx-0x%16llx\n",
			 virt, virt + size - 1);
		return -EINVAL;
	}

	pages = size >> psz;

	dev_priv->engine.instmem.prepare_access(dev, true);
	if (flags & 0x80000000) {
		while (pages--) {
			struct nouveau_gpuobj *pt = pgt[virt >> 29];
			unsigned pte = ((virt & 0x1fffffffULL) >> psz) << 1;

			nv_wo32(dev, pt, pte++, 0x00000000);
			nv_wo32(dev, pt, pte++, 0x00000000);

			virt += (1 << psz);
		}
	} else {
		while (pages--) {
			struct nouveau_gpuobj *pt = pgt[virt >> 29];
			unsigned pte = ((virt & 0x1fffffffULL) >> psz) << 1;
			unsigned offset_h = upper_32_bits(phys) & 0xff;
			unsigned offset_l = lower_32_bits(phys);

			nv_wo32(dev, pt, pte++, offset_l | pfl);
			nv_wo32(dev, pt, pte++, offset_h | flags);

			phys += (1 << psz);
			virt += (1 << psz);
		}
	}
	dev_priv->engine.instmem.finish_access(dev);

	nv_wr32(dev, 0x100c80, 0x00050001);
	if (!nv_wait(0x100c80, 0x00000001, 0x00000000)) {
		NV_ERROR(dev, "timeout: (0x100c80 & 1) == 0 (2)\n");
		NV_ERROR(dev, "0x100c80 = 0x%08x\n", nv_rd32(dev, 0x100c80));
		return -EBUSY;
	}

	nv_wr32(dev, 0x100c80, 0x00000001);
	if (!nv_wait(0x100c80, 0x00000001, 0x00000000)) {
		NV_ERROR(dev, "timeout: (0x100c80 & 1) == 0 (2)\n");
		NV_ERROR(dev, "0x100c80 = 0x%08x\n", nv_rd32(dev, 0x100c80));
		return -EBUSY;
	}

	return 0;
}

void
nv50_mem_vm_unbind(struct drm_device *dev, uint64_t virt, uint32_t size)
{
	nv50_mem_vm_bind_linear(dev, virt, size, 0x80000000, 0);
}

/*
 * Cleanup everything
 */
void nouveau_mem_takedown(struct mem_block **heap)
{
	struct mem_block *p;

	if (!*heap)
		return;

	for (p = (*heap)->next; p != *heap;) {
		struct mem_block *q = p;
		p = p->next;
		kfree(q);
	}

	kfree(*heap);
	*heap = NULL;
}

void nouveau_mem_close(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (dev_priv->ttm.bdev.man[TTM_PL_PRIV0].has_type)
		ttm_bo_clean_mm(&dev_priv->ttm.bdev, TTM_PL_PRIV0);
	ttm_bo_clean_mm(&dev_priv->ttm.bdev, TTM_PL_VRAM);

	ttm_bo_device_release(&dev_priv->ttm.bdev);

	nouveau_ttm_global_release(dev_priv);

	if (drm_core_has_AGP(dev) && dev->agp &&
	    drm_core_check_feature(dev, DRIVER_MODESET)) {
		struct drm_agp_mem *entry, *tempe;

		/* Remove AGP resources, but leave dev->agp
		   intact until drv_cleanup is called. */
		list_for_each_entry_safe(entry, tempe, &dev->agp->memory, head) {
			if (entry->bound)
				drm_unbind_agp(entry->memory);
			drm_free_agp(entry->memory, entry->pages);
			kfree(entry);
		}
		INIT_LIST_HEAD(&dev->agp->memory);

		if (dev->agp->acquired)
			drm_agp_release(dev);

		dev->agp->acquired = 0;
		dev->agp->enabled = 0;
	}

	if (dev_priv->fb_mtrr) {
		drm_mtrr_del(dev_priv->fb_mtrr, drm_get_resource_start(dev, 1),
			     drm_get_resource_len(dev, 1), DRM_MTRR_WC);
		dev_priv->fb_mtrr = 0;
	}
}

/*XXX won't work on BSD because of pci_read_config_dword */
static uint32_t
nouveau_mem_fb_amount_igp(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pci_dev *bridge;
	uint32_t mem;

	bridge = pci_get_bus_and_slot(0, PCI_DEVFN(0, 1));
	if (!bridge) {
		NV_ERROR(dev, "no bridge device\n");
		return 0;
	}

	if (dev_priv->flags&NV_NFORCE) {
		pci_read_config_dword(bridge, 0x7C, &mem);
		return (uint64_t)(((mem >> 6) & 31) + 1)*1024*1024;
	} else
	if (dev_priv->flags&NV_NFORCE2) {
		pci_read_config_dword(bridge, 0x84, &mem);
		return (uint64_t)(((mem >> 4) & 127) + 1)*1024*1024;
	}

	NV_ERROR(dev, "impossible!\n");
	return 0;
}

/* returns the amount of FB ram in bytes */
uint64_t nouveau_mem_fb_amount(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t boot0;

	switch (dev_priv->card_type) {
	case NV_04:
		boot0 = nv_rd32(dev, NV03_BOOT_0);
		if (boot0 & 0x00000100)
			return (((boot0 >> 12) & 0xf) * 2 + 2) * 1024 * 1024;

		switch (boot0 & NV03_BOOT_0_RAM_AMOUNT) {
		case NV04_BOOT_0_RAM_AMOUNT_32MB:
			return 32 * 1024 * 1024;
		case NV04_BOOT_0_RAM_AMOUNT_16MB:
			return 16 * 1024 * 1024;
		case NV04_BOOT_0_RAM_AMOUNT_8MB:
			return 8 * 1024 * 1024;
		case NV04_BOOT_0_RAM_AMOUNT_4MB:
			return 4 * 1024 * 1024;
		}
		break;
	case NV_10:
	case NV_20:
	case NV_30:
	case NV_40:
	case NV_50:
	default:
		if (dev_priv->flags & (NV_NFORCE | NV_NFORCE2)) {
			return nouveau_mem_fb_amount_igp(dev);
		} else {
			uint64_t mem;
			mem = (nv_rd32(dev, NV04_FIFO_DATA) &
					NV10_FIFO_DATA_RAM_AMOUNT_MB_MASK) >>
					NV10_FIFO_DATA_RAM_AMOUNT_MB_SHIFT;
			return mem * 1024 * 1024;
		}
		break;
	}

	NV_ERROR(dev,
		"Unable to detect video ram size. Please report your setup to "
							DRIVER_EMAIL "\n");
	return 0;
}

#if __OS_HAS_AGP
static void nouveau_mem_reset_agp(struct drm_device *dev)
{
	uint32_t saved_pci_nv_1, saved_pci_nv_19, pmc_enable;

	saved_pci_nv_1 = nv_rd32(dev, NV04_PBUS_PCI_NV_1);
	saved_pci_nv_19 = nv_rd32(dev, NV04_PBUS_PCI_NV_19);

	/* clear busmaster bit */
	nv_wr32(dev, NV04_PBUS_PCI_NV_1, saved_pci_nv_1 & ~0x4);
	/* clear SBA and AGP bits */
	nv_wr32(dev, NV04_PBUS_PCI_NV_19, saved_pci_nv_19 & 0xfffff0ff);

	/* power cycle pgraph, if enabled */
	pmc_enable = nv_rd32(dev, NV03_PMC_ENABLE);
	if (pmc_enable & NV_PMC_ENABLE_PGRAPH) {
		nv_wr32(dev, NV03_PMC_ENABLE,
				pmc_enable & ~NV_PMC_ENABLE_PGRAPH);
		nv_wr32(dev, NV03_PMC_ENABLE, nv_rd32(dev, NV03_PMC_ENABLE) |
				NV_PMC_ENABLE_PGRAPH);
	}

	/* and restore (gives effect of resetting AGP) */
	nv_wr32(dev, NV04_PBUS_PCI_NV_19, saved_pci_nv_19);
	nv_wr32(dev, NV04_PBUS_PCI_NV_1, saved_pci_nv_1);
}
#endif

int
nouveau_mem_init_agp(struct drm_device *dev)
{
#if __OS_HAS_AGP
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_agp_info info;
	struct drm_agp_mode mode;
	int ret;

	if (nouveau_noagp)
		return 0;

	nouveau_mem_reset_agp(dev);

	if (!dev->agp->acquired) {
		ret = drm_agp_acquire(dev);
		if (ret) {
			NV_ERROR(dev, "Unable to acquire AGP: %d\n", ret);
			return ret;
		}
	}

	ret = drm_agp_info(dev, &info);
	if (ret) {
		NV_ERROR(dev, "Unable to get AGP info: %d\n", ret);
		return ret;
	}

	/* see agp.h for the AGPSTAT_* modes available */
	mode.mode = info.mode;
	ret = drm_agp_enable(dev, mode);
	if (ret) {
		NV_ERROR(dev, "Unable to enable AGP: %d\n", ret);
		return ret;
	}

	dev_priv->gart_info.type	= NOUVEAU_GART_AGP;
	dev_priv->gart_info.aper_base	= info.aperture_base;
	dev_priv->gart_info.aper_size	= info.aperture_size;
#endif
	return 0;
}

int
nouveau_mem_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct ttm_bo_device *bdev = &dev_priv->ttm.bdev;
	int ret, dma_bits = 32;

	dev_priv->fb_phys = drm_get_resource_start(dev, 1);
	dev_priv->gart_info.type = NOUVEAU_GART_NONE;

	if (dev_priv->card_type >= NV_50 &&
	    pci_dma_supported(dev->pdev, DMA_BIT_MASK(40)))
		dma_bits = 40;

	ret = pci_set_dma_mask(dev->pdev, DMA_BIT_MASK(dma_bits));
	if (ret) {
		NV_ERROR(dev, "Error setting DMA mask: %d\n", ret);
		return ret;
	}

	ret = nouveau_ttm_global_init(dev_priv);
	if (ret)
		return ret;

	ret = ttm_bo_device_init(&dev_priv->ttm.bdev,
				 dev_priv->ttm.bo_global_ref.ref.object,
				 &nouveau_bo_driver, DRM_FILE_PAGE_OFFSET,
				 dma_bits <= 32 ? true : false);
	if (ret) {
		NV_ERROR(dev, "Error initialising bo driver: %d\n", ret);
		return ret;
	}

	INIT_LIST_HEAD(&dev_priv->ttm.bo_list);
	spin_lock_init(&dev_priv->ttm.bo_list_lock);
	spin_lock_init(&dev_priv->tile.lock);

	dev_priv->fb_available_size = nouveau_mem_fb_amount(dev);

	dev_priv->fb_mappable_pages = dev_priv->fb_available_size;
	if (dev_priv->fb_mappable_pages > drm_get_resource_len(dev, 1))
		dev_priv->fb_mappable_pages = drm_get_resource_len(dev, 1);
	dev_priv->fb_mappable_pages >>= PAGE_SHIFT;

	NV_INFO(dev, "%d MiB VRAM\n", (int)(dev_priv->fb_available_size >> 20));

	/* remove reserved space at end of vram from available amount */
	dev_priv->fb_available_size -= dev_priv->ramin_rsvd_vram;
	dev_priv->fb_aper_free = dev_priv->fb_available_size;

	/* mappable vram */
	ret = ttm_bo_init_mm(bdev, TTM_PL_VRAM,
			     dev_priv->fb_available_size >> PAGE_SHIFT);
	if (ret) {
		NV_ERROR(dev, "Failed VRAM mm init: %d\n", ret);
		return ret;
	}

	/* GART */
#if !defined(__powerpc__) && !defined(__ia64__)
	if (drm_device_is_agp(dev) && dev->agp) {
		ret = nouveau_mem_init_agp(dev);
		if (ret)
			NV_ERROR(dev, "Error initialising AGP: %d\n", ret);
	}
#endif

	if (dev_priv->gart_info.type == NOUVEAU_GART_NONE) {
		ret = nouveau_sgdma_init(dev);
		if (ret) {
			NV_ERROR(dev, "Error initialising PCI(E): %d\n", ret);
			return ret;
		}
	}

	NV_INFO(dev, "%d MiB GART (aperture)\n",
		(int)(dev_priv->gart_info.aper_size >> 20));
	dev_priv->gart_info.aper_free = dev_priv->gart_info.aper_size;

	ret = ttm_bo_init_mm(bdev, TTM_PL_TT,
			     dev_priv->gart_info.aper_size >> PAGE_SHIFT);
	if (ret) {
		NV_ERROR(dev, "Failed TT mm init: %d\n", ret);
		return ret;
	}

	dev_priv->fb_mtrr = drm_mtrr_add(drm_get_resource_start(dev, 1),
					 drm_get_resource_len(dev, 1),
					 DRM_MTRR_WC);
	return 0;
}


