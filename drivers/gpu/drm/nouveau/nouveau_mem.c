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
	struct nouveau_gpuobj *pgt;
	unsigned block;
	int i;

	virt = ((virt - dev_priv->vm_vram_base) >> 16) << 1;
	size = (size >> 16) << 1;

	phys |= ((uint64_t)flags << 32);
	phys |= 1;
	if (dev_priv->vram_sys_base) {
		phys += dev_priv->vram_sys_base;
		phys |= 0x30;
	}

	while (size) {
		unsigned offset_h = upper_32_bits(phys);
		unsigned offset_l = lower_32_bits(phys);
		unsigned pte, end;

		for (i = 7; i >= 0; i--) {
			block = 1 << (i + 1);
			if (size >= block && !(virt & (block - 1)))
				break;
		}
		offset_l |= (i << 7);

		phys += block << 15;
		size -= block;

		while (block) {
			pgt = dev_priv->vm_vram_pt[virt >> 14];
			pte = virt & 0x3ffe;

			end = pte + block;
			if (end > 16384)
				end = 16384;
			block -= (end - pte);
			virt  += (end - pte);

			while (pte < end) {
				nv_wo32(dev, pgt, pte++, offset_l);
				nv_wo32(dev, pgt, pte++, offset_h);
			}
		}
	}
	dev_priv->engine.instmem.flush(dev);

	nv50_vm_flush(dev, 5);
	nv50_vm_flush(dev, 0);
	nv50_vm_flush(dev, 4);
	nv50_vm_flush(dev, 6);
	return 0;
}

void
nv50_mem_vm_unbind(struct drm_device *dev, uint64_t virt, uint32_t size)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *pgt;
	unsigned pages, pte, end;

	virt -= dev_priv->vm_vram_base;
	pages = (size >> 16) << 1;

	while (pages) {
		pgt = dev_priv->vm_vram_pt[virt >> 29];
		pte = (virt & 0x1ffe0000ULL) >> 15;

		end = pte + pages;
		if (end > 16384)
			end = 16384;
		pages -= (end - pte);
		virt  += (end - pte) << 15;

		while (pte < end)
			nv_wo32(dev, pgt, pte++, 0);
	}
	dev_priv->engine.instmem.flush(dev);

	nv50_vm_flush(dev, 5);
	nv50_vm_flush(dev, 0);
	nv50_vm_flush(dev, 4);
	nv50_vm_flush(dev, 6);
}

/*
 * Cleanup everything
 */
void
nouveau_mem_close(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	nouveau_bo_unpin(dev_priv->vga_ram);
	nouveau_bo_ref(NULL, &dev_priv->vga_ram);

	ttm_bo_device_release(&dev_priv->ttm.bdev);

	nouveau_ttm_global_release(dev_priv);

	if (drm_core_has_AGP(dev) && dev->agp) {
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
		drm_mtrr_del(dev_priv->fb_mtrr,
			     pci_resource_start(dev->pdev, 1),
			     pci_resource_len(dev->pdev, 1), DRM_MTRR_WC);
		dev_priv->fb_mtrr = -1;
	}
}

static uint32_t
nouveau_mem_detect_nv04(struct drm_device *dev)
{
	uint32_t boot0 = nv_rd32(dev, NV04_PFB_BOOT_0);

	if (boot0 & 0x00000100)
		return (((boot0 >> 12) & 0xf) * 2 + 2) * 1024 * 1024;

	switch (boot0 & NV04_PFB_BOOT_0_RAM_AMOUNT) {
	case NV04_PFB_BOOT_0_RAM_AMOUNT_32MB:
		return 32 * 1024 * 1024;
	case NV04_PFB_BOOT_0_RAM_AMOUNT_16MB:
		return 16 * 1024 * 1024;
	case NV04_PFB_BOOT_0_RAM_AMOUNT_8MB:
		return 8 * 1024 * 1024;
	case NV04_PFB_BOOT_0_RAM_AMOUNT_4MB:
		return 4 * 1024 * 1024;
	}

	return 0;
}

static uint32_t
nouveau_mem_detect_nforce(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pci_dev *bridge;
	uint32_t mem;

	bridge = pci_get_bus_and_slot(0, PCI_DEVFN(0, 1));
	if (!bridge) {
		NV_ERROR(dev, "no bridge device\n");
		return 0;
	}

	if (dev_priv->flags & NV_NFORCE) {
		pci_read_config_dword(bridge, 0x7C, &mem);
		return (uint64_t)(((mem >> 6) & 31) + 1)*1024*1024;
	} else
	if (dev_priv->flags & NV_NFORCE2) {
		pci_read_config_dword(bridge, 0x84, &mem);
		return (uint64_t)(((mem >> 4) & 127) + 1)*1024*1024;
	}

	NV_ERROR(dev, "impossible!\n");
	return 0;
}

/* returns the amount of FB ram in bytes */
int
nouveau_mem_detect(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (dev_priv->card_type == NV_04) {
		dev_priv->vram_size = nouveau_mem_detect_nv04(dev);
	} else
	if (dev_priv->flags & (NV_NFORCE | NV_NFORCE2)) {
		dev_priv->vram_size = nouveau_mem_detect_nforce(dev);
	} else
	if (dev_priv->card_type < NV_50) {
		dev_priv->vram_size  = nv_rd32(dev, NV04_PFB_FIFO_DATA);
		dev_priv->vram_size &= NV10_PFB_FIFO_DATA_RAM_AMOUNT_MB_MASK;
	} else
	if (dev_priv->card_type < NV_C0) {
		dev_priv->vram_size = nv_rd32(dev, NV04_PFB_FIFO_DATA);
		dev_priv->vram_size |= (dev_priv->vram_size & 0xff) << 32;
		dev_priv->vram_size &= 0xffffffff00ll;
		if (dev_priv->chipset == 0xaa || dev_priv->chipset == 0xac) {
			dev_priv->vram_sys_base = nv_rd32(dev, 0x100e10);
			dev_priv->vram_sys_base <<= 12;
		}
	} else {
		dev_priv->vram_size  = nv_rd32(dev, 0x10f20c) << 20;
		dev_priv->vram_size *= nv_rd32(dev, 0x121c74);
	}

	NV_INFO(dev, "Detected %dMiB VRAM\n", (int)(dev_priv->vram_size >> 20));
	if (dev_priv->vram_sys_base) {
		NV_INFO(dev, "Stolen system memory at: 0x%010llx\n",
			dev_priv->vram_sys_base);
	}

	if (dev_priv->vram_size)
		return 0;
	return -ENOMEM;
}

int
nouveau_mem_reset_agp(struct drm_device *dev)
{
#if __OS_HAS_AGP
	uint32_t saved_pci_nv_1, pmc_enable;
	int ret;

	/* First of all, disable fast writes, otherwise if it's
	 * already enabled in the AGP bridge and we disable the card's
	 * AGP controller we might be locking ourselves out of it. */
	if (nv_rd32(dev, NV04_PBUS_PCI_NV_19) & PCI_AGP_COMMAND_FW) {
		struct drm_agp_info info;
		struct drm_agp_mode mode;

		ret = drm_agp_info(dev, &info);
		if (ret)
			return ret;

		mode.mode = info.mode & ~PCI_AGP_COMMAND_FW;
		ret = drm_agp_enable(dev, mode);
		if (ret)
			return ret;
	}

	saved_pci_nv_1 = nv_rd32(dev, NV04_PBUS_PCI_NV_1);

	/* clear busmaster bit */
	nv_wr32(dev, NV04_PBUS_PCI_NV_1, saved_pci_nv_1 & ~0x4);
	/* disable AGP */
	nv_wr32(dev, NV04_PBUS_PCI_NV_19, 0);

	/* power cycle pgraph, if enabled */
	pmc_enable = nv_rd32(dev, NV03_PMC_ENABLE);
	if (pmc_enable & NV_PMC_ENABLE_PGRAPH) {
		nv_wr32(dev, NV03_PMC_ENABLE,
				pmc_enable & ~NV_PMC_ENABLE_PGRAPH);
		nv_wr32(dev, NV03_PMC_ENABLE, nv_rd32(dev, NV03_PMC_ENABLE) |
				NV_PMC_ENABLE_PGRAPH);
	}

	/* and restore (gives effect of resetting AGP) */
	nv_wr32(dev, NV04_PBUS_PCI_NV_1, saved_pci_nv_1);
#endif

	return 0;
}

int
nouveau_mem_init_agp(struct drm_device *dev)
{
#if __OS_HAS_AGP
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_agp_info info;
	struct drm_agp_mode mode;
	int ret;

	if (!dev->agp->acquired) {
		ret = drm_agp_acquire(dev);
		if (ret) {
			NV_ERROR(dev, "Unable to acquire AGP: %d\n", ret);
			return ret;
		}
	}

	nouveau_mem_reset_agp(dev);

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

	dev_priv->fb_phys = pci_resource_start(dev->pdev, 1);
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

	spin_lock_init(&dev_priv->tile.lock);

	dev_priv->fb_available_size = dev_priv->vram_size;
	dev_priv->fb_mappable_pages = dev_priv->fb_available_size;
	if (dev_priv->fb_mappable_pages > pci_resource_len(dev->pdev, 1))
		dev_priv->fb_mappable_pages =
			pci_resource_len(dev->pdev, 1);
	dev_priv->fb_mappable_pages >>= PAGE_SHIFT;

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

	ret = nouveau_bo_new(dev, NULL, 256*1024, 0, TTM_PL_FLAG_VRAM,
			     0, 0, true, true, &dev_priv->vga_ram);
	if (ret == 0)
		ret = nouveau_bo_pin(dev_priv->vga_ram, TTM_PL_FLAG_VRAM);
	if (ret) {
		NV_WARN(dev, "failed to reserve VGA memory\n");
		nouveau_bo_ref(NULL, &dev_priv->vga_ram);
	}

	/* GART */
#if !defined(__powerpc__) && !defined(__ia64__)
	if (drm_device_is_agp(dev) && dev->agp && !nouveau_noagp) {
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

	dev_priv->fb_mtrr = drm_mtrr_add(pci_resource_start(dev->pdev, 1),
					 pci_resource_len(dev->pdev, 1),
					 DRM_MTRR_WC);

	return 0;
}


