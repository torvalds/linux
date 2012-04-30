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
 *    Ben Skeggs <bskeggs@redhat.com>
 *    Roy Spliet <r.spliet@student.tudelft.nl>
 */


#include "drmP.h"
#include "drm.h"
#include "drm_sarea.h"

#include "nouveau_drv.h"
#include "nouveau_pm.h"
#include "nouveau_mm.h"
#include "nouveau_vm.h"
#include "nouveau_fence.h"

/*
 * NV10-NV40 tiling helpers
 */

static void
nv10_mem_update_tile_region(struct drm_device *dev,
			    struct nouveau_tile_reg *tile, uint32_t addr,
			    uint32_t size, uint32_t pitch, uint32_t flags)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_fifo_engine *pfifo = &dev_priv->engine.fifo;
	struct nouveau_fb_engine *pfb = &dev_priv->engine.fb;
	int i = tile - dev_priv->tile.reg, j;
	unsigned long save;

	nouveau_fence_unref(&tile->fence);

	if (tile->pitch)
		pfb->free_tile_region(dev, i);

	if (pitch)
		pfb->init_tile_region(dev, i, addr, size, pitch, flags);

	spin_lock_irqsave(&dev_priv->context_switch_lock, save);
	pfifo->reassign(dev, false);
	pfifo->cache_pull(dev, false);

	nouveau_wait_for_idle(dev);

	pfb->set_tile_region(dev, i);
	for (j = 0; j < NVOBJ_ENGINE_NR; j++) {
		if (dev_priv->eng[j] && dev_priv->eng[j]->set_tile_region)
			dev_priv->eng[j]->set_tile_region(dev, i);
	}

	pfifo->cache_pull(dev, true);
	pfifo->reassign(dev, true);
	spin_unlock_irqrestore(&dev_priv->context_switch_lock, save);
}

static struct nouveau_tile_reg *
nv10_mem_get_tile_region(struct drm_device *dev, int i)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_tile_reg *tile = &dev_priv->tile.reg[i];

	spin_lock(&dev_priv->tile.lock);

	if (!tile->used &&
	    (!tile->fence || nouveau_fence_done(tile->fence)))
		tile->used = true;
	else
		tile = NULL;

	spin_unlock(&dev_priv->tile.lock);
	return tile;
}

void
nv10_mem_put_tile_region(struct drm_device *dev, struct nouveau_tile_reg *tile,
			 struct nouveau_fence *fence)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (tile) {
		spin_lock(&dev_priv->tile.lock);
		if (fence) {
			/* Mark it as pending. */
			tile->fence = fence;
			nouveau_fence_ref(fence);
		}

		tile->used = false;
		spin_unlock(&dev_priv->tile.lock);
	}
}

struct nouveau_tile_reg *
nv10_mem_set_tiling(struct drm_device *dev, uint32_t addr, uint32_t size,
		    uint32_t pitch, uint32_t flags)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_fb_engine *pfb = &dev_priv->engine.fb;
	struct nouveau_tile_reg *tile, *found = NULL;
	int i;

	for (i = 0; i < pfb->num_tiles; i++) {
		tile = nv10_mem_get_tile_region(dev, i);

		if (pitch && !found) {
			found = tile;
			continue;

		} else if (tile && tile->pitch) {
			/* Kill an unused tile region. */
			nv10_mem_update_tile_region(dev, tile, 0, 0, 0, 0);
		}

		nv10_mem_put_tile_region(dev, tile, NULL);
	}

	if (found)
		nv10_mem_update_tile_region(dev, found, addr, size,
					    pitch, flags);
	return found;
}

/*
 * Cleanup everything
 */
void
nouveau_mem_vram_fini(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	ttm_bo_device_release(&dev_priv->ttm.bdev);

	nouveau_ttm_global_release(dev_priv);

	if (dev_priv->fb_mtrr >= 0) {
		drm_mtrr_del(dev_priv->fb_mtrr,
			     pci_resource_start(dev->pdev, 1),
			     pci_resource_len(dev->pdev, 1), DRM_MTRR_WC);
		dev_priv->fb_mtrr = -1;
	}
}

void
nouveau_mem_gart_fini(struct drm_device *dev)
{
	nouveau_sgdma_takedown(dev);

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
}

bool
nouveau_mem_flags_valid(struct drm_device *dev, u32 tile_flags)
{
	if (!(tile_flags & NOUVEAU_GEM_TILE_LAYOUT_MASK))
		return true;

	return false;
}

#if __OS_HAS_AGP
static unsigned long
get_agp_mode(struct drm_device *dev, unsigned long mode)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	/*
	 * FW seems to be broken on nv18, it makes the card lock up
	 * randomly.
	 */
	if (dev_priv->chipset == 0x18)
		mode &= ~PCI_AGP_COMMAND_FW;

	/*
	 * AGP mode set in the command line.
	 */
	if (nouveau_agpmode > 0) {
		bool agpv3 = mode & 0x8;
		int rate = agpv3 ? nouveau_agpmode / 4 : nouveau_agpmode;

		mode = (mode & ~0x7) | (rate & 0x7);
	}

	return mode;
}
#endif

int
nouveau_mem_reset_agp(struct drm_device *dev)
{
#if __OS_HAS_AGP
	uint32_t saved_pci_nv_1, pmc_enable;
	int ret;

	/* First of all, disable fast writes, otherwise if it's
	 * already enabled in the AGP bridge and we disable the card's
	 * AGP controller we might be locking ourselves out of it. */
	if ((nv_rd32(dev, NV04_PBUS_PCI_NV_19) |
	     dev->agp->mode) & PCI_AGP_COMMAND_FW) {
		struct drm_agp_info info;
		struct drm_agp_mode mode;

		ret = drm_agp_info(dev, &info);
		if (ret)
			return ret;

		mode.mode = get_agp_mode(dev, info.mode) & ~PCI_AGP_COMMAND_FW;
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
	mode.mode = get_agp_mode(dev, info.mode);
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

static const struct vram_types {
	int value;
	const char *name;
} vram_type_map[] = {
	{ NV_MEM_TYPE_STOLEN , "stolen system memory" },
	{ NV_MEM_TYPE_SGRAM  , "SGRAM" },
	{ NV_MEM_TYPE_SDRAM  , "SDRAM" },
	{ NV_MEM_TYPE_DDR1   , "DDR1" },
	{ NV_MEM_TYPE_DDR2   , "DDR2" },
	{ NV_MEM_TYPE_DDR3   , "DDR3" },
	{ NV_MEM_TYPE_GDDR2  , "GDDR2" },
	{ NV_MEM_TYPE_GDDR3  , "GDDR3" },
	{ NV_MEM_TYPE_GDDR4  , "GDDR4" },
	{ NV_MEM_TYPE_GDDR5  , "GDDR5" },
	{ NV_MEM_TYPE_UNKNOWN, "unknown type" }
};

int
nouveau_mem_vram_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct ttm_bo_device *bdev = &dev_priv->ttm.bdev;
	const struct vram_types *vram_type;
	int ret, dma_bits;

	dma_bits = 32;
	if (dev_priv->card_type >= NV_50) {
		if (pci_dma_supported(dev->pdev, DMA_BIT_MASK(40)))
			dma_bits = 40;
	} else
	if (0 && pci_is_pcie(dev->pdev) &&
	    dev_priv->chipset  > 0x40 &&
	    dev_priv->chipset != 0x45) {
		if (pci_dma_supported(dev->pdev, DMA_BIT_MASK(39)))
			dma_bits = 39;
	}

	ret = pci_set_dma_mask(dev->pdev, DMA_BIT_MASK(dma_bits));
	if (ret)
		return ret;
	ret = pci_set_consistent_dma_mask(dev->pdev, DMA_BIT_MASK(dma_bits));
	if (ret) {
		/* Reset to default value. */
		pci_set_consistent_dma_mask(dev->pdev, DMA_BIT_MASK(32));
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

	vram_type = vram_type_map;
	while (vram_type->value != NV_MEM_TYPE_UNKNOWN) {
		if (nouveau_vram_type) {
			if (!strcasecmp(nouveau_vram_type, vram_type->name))
				break;
			dev_priv->vram_type = vram_type->value;
		} else {
			if (vram_type->value == dev_priv->vram_type)
				break;
		}
		vram_type++;
	}

	NV_INFO(dev, "Detected %dMiB VRAM (%s)\n",
		(int)(dev_priv->vram_size >> 20), vram_type->name);
	if (dev_priv->vram_sys_base) {
		NV_INFO(dev, "Stolen system memory at: 0x%010llx\n",
			dev_priv->vram_sys_base);
	}

	dev_priv->fb_available_size = dev_priv->vram_size;
	dev_priv->fb_mappable_pages = dev_priv->fb_available_size;
	if (dev_priv->fb_mappable_pages > pci_resource_len(dev->pdev, 1))
		dev_priv->fb_mappable_pages = pci_resource_len(dev->pdev, 1);
	dev_priv->fb_mappable_pages >>= PAGE_SHIFT;

	dev_priv->fb_available_size -= dev_priv->ramin_rsvd_vram;
	dev_priv->fb_aper_free = dev_priv->fb_available_size;

	/* mappable vram */
	ret = ttm_bo_init_mm(bdev, TTM_PL_VRAM,
			     dev_priv->fb_available_size >> PAGE_SHIFT);
	if (ret) {
		NV_ERROR(dev, "Failed VRAM mm init: %d\n", ret);
		return ret;
	}

	if (dev_priv->card_type < NV_50) {
		ret = nouveau_bo_new(dev, 256*1024, 0, TTM_PL_FLAG_VRAM,
				     0, 0, NULL, &dev_priv->vga_ram);
		if (ret == 0)
			ret = nouveau_bo_pin(dev_priv->vga_ram,
					     TTM_PL_FLAG_VRAM);

		if (ret) {
			NV_WARN(dev, "failed to reserve VGA memory\n");
			nouveau_bo_ref(NULL, &dev_priv->vga_ram);
		}
	}

	dev_priv->fb_mtrr = drm_mtrr_add(pci_resource_start(dev->pdev, 1),
					 pci_resource_len(dev->pdev, 1),
					 DRM_MTRR_WC);
	return 0;
}

int
nouveau_mem_gart_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct ttm_bo_device *bdev = &dev_priv->ttm.bdev;
	int ret;

	dev_priv->gart_info.type = NOUVEAU_GART_NONE;

#if !defined(__powerpc__) && !defined(__ia64__)
	if (drm_pci_device_is_agp(dev) && dev->agp && nouveau_agpmode) {
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

	return 0;
}

static int
nv40_mem_timing_calc(struct drm_device *dev, u32 freq,
		     struct nouveau_pm_tbl_entry *e, u8 len,
		     struct nouveau_pm_memtiming *boot,
		     struct nouveau_pm_memtiming *t)
{
	t->reg[0] = (e->tRP << 24 | e->tRAS << 16 | e->tRFC << 8 | e->tRC);

	/* XXX: I don't trust the -1's and +1's... they must come
	 *      from somewhere! */
	t->reg[1] = (e->tWR + 2 + (t->tCWL - 1)) << 24 |
		    1 << 16 |
		    (e->tWTR + 2 + (t->tCWL - 1)) << 8 |
		    (e->tCL + 2 - (t->tCWL - 1));

	t->reg[2] = 0x20200000 |
		    ((t->tCWL - 1) << 24 |
		     e->tRRD << 16 |
		     e->tRCDWR << 8 |
		     e->tRCDRD);

	NV_DEBUG(dev, "Entry %d: 220: %08x %08x %08x\n", t->id,
		 t->reg[0], t->reg[1], t->reg[2]);
	return 0;
}

static int
nv50_mem_timing_calc(struct drm_device *dev, u32 freq,
		     struct nouveau_pm_tbl_entry *e, u8 len,
		     struct nouveau_pm_memtiming *boot,
		     struct nouveau_pm_memtiming *t)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct bit_entry P;
	uint8_t unk18 = 1, unk20 = 0, unk21 = 0, tmp7_3;

	if (bit_table(dev, 'P', &P))
		return -EINVAL;

	switch (min(len, (u8) 22)) {
	case 22:
		unk21 = e->tUNK_21;
	case 21:
		unk20 = e->tUNK_20;
	case 20:
		if (e->tCWL > 0)
			t->tCWL = e->tCWL;
	case 19:
		unk18 = e->tUNK_18;
		break;
	}

	t->reg[0] = (e->tRP << 24 | e->tRAS << 16 | e->tRFC << 8 | e->tRC);

	t->reg[1] = (e->tWR + 2 + (t->tCWL - 1)) << 24 |
				max(unk18, (u8) 1) << 16 |
				(e->tWTR + 2 + (t->tCWL - 1)) << 8;

	t->reg[2] = ((t->tCWL - 1) << 24 |
		    e->tRRD << 16 |
		    e->tRCDWR << 8 |
		    e->tRCDRD);

	t->reg[4] = e->tUNK_13 << 8  | e->tUNK_13;

	t->reg[5] = (e->tRFC << 24 | max(e->tRCDRD, e->tRCDWR) << 16 | e->tRP);

	t->reg[8] = boot->reg[8] & 0xffffff00;

	if (P.version == 1) {
		t->reg[1] |= (e->tCL + 2 - (t->tCWL - 1));

		t->reg[3] = (0x14 + e->tCL) << 24 |
			    0x16 << 16 |
			    (e->tCL - 1) << 8 |
			    (e->tCL - 1);

		t->reg[4] |= boot->reg[4] & 0xffff0000;

		t->reg[6] = (0x33 - t->tCWL) << 16 |
			    t->tCWL << 8 |
			    (0x2e + e->tCL - t->tCWL);

		t->reg[7] = 0x4000202 | (e->tCL - 1) << 16;

		/* XXX: P.version == 1 only has DDR2 and GDDR3? */
		if (dev_priv->vram_type == NV_MEM_TYPE_DDR2) {
			t->reg[5] |= (e->tCL + 3) << 8;
			t->reg[6] |= (t->tCWL - 2) << 8;
			t->reg[8] |= (e->tCL - 4);
		} else {
			t->reg[5] |= (e->tCL + 2) << 8;
			t->reg[6] |= t->tCWL << 8;
			t->reg[8] |= (e->tCL - 2);
		}
	} else {
		t->reg[1] |= (5 + e->tCL - (t->tCWL));

		/* XXX: 0xb? 0x30? */
		t->reg[3] = (0x30 + e->tCL) << 24 |
			    (boot->reg[3] & 0x00ff0000)|
			    (0xb + e->tCL) << 8 |
			    (e->tCL - 1);

		t->reg[4] |= (unk20 << 24 | unk21 << 16);

		/* XXX: +6? */
		t->reg[5] |= (t->tCWL + 6) << 8;

		t->reg[6] = (0x5a + e->tCL) << 16 |
			    (6 - e->tCL + t->tCWL) << 8 |
			    (0x50 + e->tCL - t->tCWL);

		tmp7_3 = (boot->reg[7] & 0xff000000) >> 24;
		t->reg[7] = (tmp7_3 << 24) |
			    ((tmp7_3 - 6 + e->tCL) << 16) |
			    0x202;
	}

	NV_DEBUG(dev, "Entry %d: 220: %08x %08x %08x %08x\n", t->id,
		 t->reg[0], t->reg[1], t->reg[2], t->reg[3]);
	NV_DEBUG(dev, "         230: %08x %08x %08x %08x\n",
		 t->reg[4], t->reg[5], t->reg[6], t->reg[7]);
	NV_DEBUG(dev, "         240: %08x\n", t->reg[8]);
	return 0;
}

static int
nvc0_mem_timing_calc(struct drm_device *dev, u32 freq,
		     struct nouveau_pm_tbl_entry *e, u8 len,
		     struct nouveau_pm_memtiming *boot,
		     struct nouveau_pm_memtiming *t)
{
	if (e->tCWL > 0)
		t->tCWL = e->tCWL;

	t->reg[0] = (e->tRP << 24 | (e->tRAS & 0x7f) << 17 |
		     e->tRFC << 8 | e->tRC);

	t->reg[1] = (boot->reg[1] & 0xff000000) |
		    (e->tRCDWR & 0x0f) << 20 |
		    (e->tRCDRD & 0x0f) << 14 |
		    (t->tCWL << 7) |
		    (e->tCL & 0x0f);

	t->reg[2] = (boot->reg[2] & 0xff0000ff) |
		    e->tWR << 16 | e->tWTR << 8;

	t->reg[3] = (e->tUNK_20 & 0x1f) << 9 |
		    (e->tUNK_21 & 0xf) << 5 |
		    (e->tUNK_13 & 0x1f);

	t->reg[4] = (boot->reg[4] & 0xfff00fff) |
		    (e->tRRD&0x1f) << 15;

	NV_DEBUG(dev, "Entry %d: 290: %08x %08x %08x %08x\n", t->id,
		 t->reg[0], t->reg[1], t->reg[2], t->reg[3]);
	NV_DEBUG(dev, "         2a0: %08x\n", t->reg[4]);
	return 0;
}

/**
 * MR generation methods
 */

static int
nouveau_mem_ddr2_mr(struct drm_device *dev, u32 freq,
		    struct nouveau_pm_tbl_entry *e, u8 len,
		    struct nouveau_pm_memtiming *boot,
		    struct nouveau_pm_memtiming *t)
{
	t->drive_strength = 0;
	if (len < 15) {
		t->odt = boot->odt;
	} else {
		t->odt = e->RAM_FT1 & 0x07;
	}

	if (e->tCL >= NV_MEM_CL_DDR2_MAX) {
		NV_WARN(dev, "(%u) Invalid tCL: %u", t->id, e->tCL);
		return -ERANGE;
	}

	if (e->tWR >= NV_MEM_WR_DDR2_MAX) {
		NV_WARN(dev, "(%u) Invalid tWR: %u", t->id, e->tWR);
		return -ERANGE;
	}

	if (t->odt > 3) {
		NV_WARN(dev, "(%u) Invalid odt value, assuming disabled: %x",
			t->id, t->odt);
		t->odt = 0;
	}

	t->mr[0] = (boot->mr[0] & 0x100f) |
		   (e->tCL) << 4 |
		   (e->tWR - 1) << 9;
	t->mr[1] = (boot->mr[1] & 0x101fbb) |
		   (t->odt & 0x1) << 2 |
		   (t->odt & 0x2) << 5;

	NV_DEBUG(dev, "(%u) MR: %08x", t->id, t->mr[0]);
	return 0;
}

uint8_t nv_mem_wr_lut_ddr3[NV_MEM_WR_DDR3_MAX] = {
	0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 5, 6, 6, 7, 7, 0, 0};

static int
nouveau_mem_ddr3_mr(struct drm_device *dev, u32 freq,
		    struct nouveau_pm_tbl_entry *e, u8 len,
		    struct nouveau_pm_memtiming *boot,
		    struct nouveau_pm_memtiming *t)
{
	u8 cl = e->tCL - 4;

	t->drive_strength = 0;
	if (len < 15) {
		t->odt = boot->odt;
	} else {
		t->odt = e->RAM_FT1 & 0x07;
	}

	if (e->tCL >= NV_MEM_CL_DDR3_MAX || e->tCL < 4) {
		NV_WARN(dev, "(%u) Invalid tCL: %u", t->id, e->tCL);
		return -ERANGE;
	}

	if (e->tWR >= NV_MEM_WR_DDR3_MAX || e->tWR < 4) {
		NV_WARN(dev, "(%u) Invalid tWR: %u", t->id, e->tWR);
		return -ERANGE;
	}

	if (e->tCWL < 5) {
		NV_WARN(dev, "(%u) Invalid tCWL: %u", t->id, e->tCWL);
		return -ERANGE;
	}

	t->mr[0] = (boot->mr[0] & 0x180b) |
		   /* CAS */
		   (cl & 0x7) << 4 |
		   (cl & 0x8) >> 1 |
		   (nv_mem_wr_lut_ddr3[e->tWR]) << 9;
	t->mr[1] = (boot->mr[1] & 0x101dbb) |
		   (t->odt & 0x1) << 2 |
		   (t->odt & 0x2) << 5 |
		   (t->odt & 0x4) << 7;
	t->mr[2] = (boot->mr[2] & 0x20ffb7) | (e->tCWL - 5) << 3;

	NV_DEBUG(dev, "(%u) MR: %08x %08x", t->id, t->mr[0], t->mr[2]);
	return 0;
}

uint8_t nv_mem_cl_lut_gddr3[NV_MEM_CL_GDDR3_MAX] = {
	0, 0, 0, 0, 4, 5, 6, 7, 0, 1, 2, 3, 8, 9, 10, 11};
uint8_t nv_mem_wr_lut_gddr3[NV_MEM_WR_GDDR3_MAX] = {
	0, 0, 0, 0, 0, 2, 3, 8, 9, 10, 11, 0, 0, 1, 1, 0, 3};

static int
nouveau_mem_gddr3_mr(struct drm_device *dev, u32 freq,
		     struct nouveau_pm_tbl_entry *e, u8 len,
		     struct nouveau_pm_memtiming *boot,
		     struct nouveau_pm_memtiming *t)
{
	if (len < 15) {
		t->drive_strength = boot->drive_strength;
		t->odt = boot->odt;
	} else {
		t->drive_strength = (e->RAM_FT1 & 0x30) >> 4;
		t->odt = e->RAM_FT1 & 0x07;
	}

	if (e->tCL >= NV_MEM_CL_GDDR3_MAX) {
		NV_WARN(dev, "(%u) Invalid tCL: %u", t->id, e->tCL);
		return -ERANGE;
	}

	if (e->tWR >= NV_MEM_WR_GDDR3_MAX) {
		NV_WARN(dev, "(%u) Invalid tWR: %u", t->id, e->tWR);
		return -ERANGE;
	}

	if (t->odt > 3) {
		NV_WARN(dev, "(%u) Invalid odt value, assuming autocal: %x",
			t->id, t->odt);
		t->odt = 0;
	}

	t->mr[0] = (boot->mr[0] & 0xe0b) |
		   /* CAS */
		   ((nv_mem_cl_lut_gddr3[e->tCL] & 0x7) << 4) |
		   ((nv_mem_cl_lut_gddr3[e->tCL] & 0x8) >> 2);
	t->mr[1] = (boot->mr[1] & 0x100f40) | t->drive_strength |
		   (t->odt << 2) |
		   (nv_mem_wr_lut_gddr3[e->tWR] & 0xf) << 4;
	t->mr[2] = boot->mr[2];

	NV_DEBUG(dev, "(%u) MR: %08x %08x %08x", t->id,
		      t->mr[0], t->mr[1], t->mr[2]);
	return 0;
}

static int
nouveau_mem_gddr5_mr(struct drm_device *dev, u32 freq,
		     struct nouveau_pm_tbl_entry *e, u8 len,
		     struct nouveau_pm_memtiming *boot,
		     struct nouveau_pm_memtiming *t)
{
	if (len < 15) {
		t->drive_strength = boot->drive_strength;
		t->odt = boot->odt;
	} else {
		t->drive_strength = (e->RAM_FT1 & 0x30) >> 4;
		t->odt = e->RAM_FT1 & 0x03;
	}

	if (e->tCL >= NV_MEM_CL_GDDR5_MAX) {
		NV_WARN(dev, "(%u) Invalid tCL: %u", t->id, e->tCL);
		return -ERANGE;
	}

	if (e->tWR >= NV_MEM_WR_GDDR5_MAX) {
		NV_WARN(dev, "(%u) Invalid tWR: %u", t->id, e->tWR);
		return -ERANGE;
	}

	if (t->odt > 3) {
		NV_WARN(dev, "(%u) Invalid odt value, assuming autocal: %x",
			t->id, t->odt);
		t->odt = 0;
	}

	t->mr[0] = (boot->mr[0] & 0x007) |
		   ((e->tCL - 5) << 3) |
		   ((e->tWR - 4) << 8);
	t->mr[1] = (boot->mr[1] & 0x1007f0) |
		   t->drive_strength |
		   (t->odt << 2);

	NV_DEBUG(dev, "(%u) MR: %08x %08x", t->id, t->mr[0], t->mr[1]);
	return 0;
}

int
nouveau_mem_timing_calc(struct drm_device *dev, u32 freq,
			struct nouveau_pm_memtiming *t)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;
	struct nouveau_pm_memtiming *boot = &pm->boot.timing;
	struct nouveau_pm_tbl_entry *e;
	u8 ver, len, *ptr, *ramcfg;
	int ret;

	ptr = nouveau_perf_timing(dev, freq, &ver, &len);
	if (!ptr || ptr[0] == 0x00) {
		*t = *boot;
		return 0;
	}
	e = (struct nouveau_pm_tbl_entry *)ptr;

	t->tCWL = boot->tCWL;

	switch (dev_priv->card_type) {
	case NV_40:
		ret = nv40_mem_timing_calc(dev, freq, e, len, boot, t);
		break;
	case NV_50:
		ret = nv50_mem_timing_calc(dev, freq, e, len, boot, t);
		break;
	case NV_C0:
	case NV_D0:
		ret = nvc0_mem_timing_calc(dev, freq, e, len, boot, t);
		break;
	default:
		ret = -ENODEV;
		break;
	}

	switch (dev_priv->vram_type * !ret) {
	case NV_MEM_TYPE_GDDR3:
		ret = nouveau_mem_gddr3_mr(dev, freq, e, len, boot, t);
		break;
	case NV_MEM_TYPE_GDDR5:
		ret = nouveau_mem_gddr5_mr(dev, freq, e, len, boot, t);
		break;
	case NV_MEM_TYPE_DDR2:
		ret = nouveau_mem_ddr2_mr(dev, freq, e, len, boot, t);
		break;
	case NV_MEM_TYPE_DDR3:
		ret = nouveau_mem_ddr3_mr(dev, freq, e, len, boot, t);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	ramcfg = nouveau_perf_ramcfg(dev, freq, &ver, &len);
	if (ramcfg) {
		int dll_off;

		if (ver == 0x00)
			dll_off = !!(ramcfg[3] & 0x04);
		else
			dll_off = !!(ramcfg[2] & 0x40);

		switch (dev_priv->vram_type) {
		case NV_MEM_TYPE_GDDR3:
			t->mr[1] &= ~0x00000040;
			t->mr[1] |=  0x00000040 * dll_off;
			break;
		default:
			t->mr[1] &= ~0x00000001;
			t->mr[1] |=  0x00000001 * dll_off;
			break;
		}
	}

	return ret;
}

void
nouveau_mem_timing_read(struct drm_device *dev, struct nouveau_pm_memtiming *t)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	u32 timing_base, timing_regs, mr_base;
	int i;

	if (dev_priv->card_type >= 0xC0) {
		timing_base = 0x10f290;
		mr_base = 0x10f300;
	} else {
		timing_base = 0x100220;
		mr_base = 0x1002c0;
	}

	t->id = -1;

	switch (dev_priv->card_type) {
	case NV_50:
		timing_regs = 9;
		break;
	case NV_C0:
	case NV_D0:
		timing_regs = 5;
		break;
	case NV_30:
	case NV_40:
		timing_regs = 3;
		break;
	default:
		timing_regs = 0;
		return;
	}
	for(i = 0; i < timing_regs; i++)
		t->reg[i] = nv_rd32(dev, timing_base + (0x04 * i));

	t->tCWL = 0;
	if (dev_priv->card_type < NV_C0) {
		t->tCWL = ((nv_rd32(dev, 0x100228) & 0x0f000000) >> 24) + 1;
	} else if (dev_priv->card_type <= NV_D0) {
		t->tCWL = ((nv_rd32(dev, 0x10f294) & 0x00000f80) >> 7);
	}

	t->mr[0] = nv_rd32(dev, mr_base);
	t->mr[1] = nv_rd32(dev, mr_base + 0x04);
	t->mr[2] = nv_rd32(dev, mr_base + 0x20);
	t->mr[3] = nv_rd32(dev, mr_base + 0x24);

	t->odt = 0;
	t->drive_strength = 0;

	switch (dev_priv->vram_type) {
	case NV_MEM_TYPE_DDR3:
		t->odt |= (t->mr[1] & 0x200) >> 7;
	case NV_MEM_TYPE_DDR2:
		t->odt |= (t->mr[1] & 0x04) >> 2 |
			  (t->mr[1] & 0x40) >> 5;
		break;
	case NV_MEM_TYPE_GDDR3:
	case NV_MEM_TYPE_GDDR5:
		t->drive_strength = t->mr[1] & 0x03;
		t->odt = (t->mr[1] & 0x0c) >> 2;
		break;
	default:
		break;
	}
}

int
nouveau_mem_exec(struct nouveau_mem_exec_func *exec,
		 struct nouveau_pm_level *perflvl)
{
	struct drm_nouveau_private *dev_priv = exec->dev->dev_private;
	struct nouveau_pm_memtiming *info = &perflvl->timing;
	u32 tMRD = 1000, tCKSRE = 0, tCKSRX = 0, tXS = 0, tDLLK = 0;
	u32 mr[3] = { info->mr[0], info->mr[1], info->mr[2] };
	u32 mr1_dlloff;

	switch (dev_priv->vram_type) {
	case NV_MEM_TYPE_DDR2:
		tDLLK = 2000;
		mr1_dlloff = 0x00000001;
		break;
	case NV_MEM_TYPE_DDR3:
		tDLLK = 12000;
		tCKSRE = 2000;
		tXS = 1000;
		mr1_dlloff = 0x00000001;
		break;
	case NV_MEM_TYPE_GDDR3:
		tDLLK = 40000;
		mr1_dlloff = 0x00000040;
		break;
	default:
		NV_ERROR(exec->dev, "cannot reclock unsupported memtype\n");
		return -ENODEV;
	}

	/* fetch current MRs */
	switch (dev_priv->vram_type) {
	case NV_MEM_TYPE_GDDR3:
	case NV_MEM_TYPE_DDR3:
		mr[2] = exec->mrg(exec, 2);
	default:
		mr[1] = exec->mrg(exec, 1);
		mr[0] = exec->mrg(exec, 0);
		break;
	}

	/* DLL 'on' -> DLL 'off' mode, disable before entering self-refresh  */
	if (!(mr[1] & mr1_dlloff) && (info->mr[1] & mr1_dlloff)) {
		exec->precharge(exec);
		exec->mrs (exec, 1, mr[1] | mr1_dlloff);
		exec->wait(exec, tMRD);
	}

	/* enter self-refresh mode */
	exec->precharge(exec);
	exec->refresh(exec);
	exec->refresh(exec);
	exec->refresh_auto(exec, false);
	exec->refresh_self(exec, true);
	exec->wait(exec, tCKSRE);

	/* modify input clock frequency */
	exec->clock_set(exec);

	/* exit self-refresh mode */
	exec->wait(exec, tCKSRX);
	exec->precharge(exec);
	exec->refresh_self(exec, false);
	exec->refresh_auto(exec, true);
	exec->wait(exec, tXS);
	exec->wait(exec, tXS);

	/* update MRs */
	if (mr[2] != info->mr[2]) {
		exec->mrs (exec, 2, info->mr[2]);
		exec->wait(exec, tMRD);
	}

	if (mr[1] != info->mr[1]) {
		/* need to keep DLL off until later, at least on GDDR3 */
		exec->mrs (exec, 1, info->mr[1] | (mr[1] & mr1_dlloff));
		exec->wait(exec, tMRD);
	}

	if (mr[0] != info->mr[0]) {
		exec->mrs (exec, 0, info->mr[0]);
		exec->wait(exec, tMRD);
	}

	/* update PFB timing registers */
	exec->timing_set(exec);

	/* DLL (enable + ) reset */
	if (!(info->mr[1] & mr1_dlloff)) {
		if (mr[1] & mr1_dlloff) {
			exec->mrs (exec, 1, info->mr[1]);
			exec->wait(exec, tMRD);
		}
		exec->mrs (exec, 0, info->mr[0] | 0x00000100);
		exec->wait(exec, tMRD);
		exec->mrs (exec, 0, info->mr[0] | 0x00000000);
		exec->wait(exec, tMRD);
		exec->wait(exec, tDLLK);
		if (dev_priv->vram_type == NV_MEM_TYPE_GDDR3)
			exec->precharge(exec);
	}

	return 0;
}

int
nouveau_mem_vbios_type(struct drm_device *dev)
{
	struct bit_entry M;
	u8 ramcfg = (nv_rd32(dev, 0x101000) & 0x0000003c) >> 2;
	if (!bit_table(dev, 'M', &M) || M.version != 2 || M.length < 5) {
		u8 *table = ROMPTR(dev, M.data[3]);
		if (table && table[0] == 0x10 && ramcfg < table[3]) {
			u8 *entry = table + table[1] + (ramcfg * table[2]);
			switch (entry[0] & 0x0f) {
			case 0: return NV_MEM_TYPE_DDR2;
			case 1: return NV_MEM_TYPE_DDR3;
			case 2: return NV_MEM_TYPE_GDDR3;
			case 3: return NV_MEM_TYPE_GDDR5;
			default:
				break;
			}

		}
	}
	return NV_MEM_TYPE_UNKNOWN;
}

static int
nouveau_vram_manager_init(struct ttm_mem_type_manager *man, unsigned long psize)
{
	/* nothing to do */
	return 0;
}

static int
nouveau_vram_manager_fini(struct ttm_mem_type_manager *man)
{
	/* nothing to do */
	return 0;
}

static inline void
nouveau_mem_node_cleanup(struct nouveau_mem *node)
{
	if (node->vma[0].node) {
		nouveau_vm_unmap(&node->vma[0]);
		nouveau_vm_put(&node->vma[0]);
	}

	if (node->vma[1].node) {
		nouveau_vm_unmap(&node->vma[1]);
		nouveau_vm_put(&node->vma[1]);
	}
}

static void
nouveau_vram_manager_del(struct ttm_mem_type_manager *man,
			 struct ttm_mem_reg *mem)
{
	struct drm_nouveau_private *dev_priv = nouveau_bdev(man->bdev);
	struct nouveau_vram_engine *vram = &dev_priv->engine.vram;
	struct drm_device *dev = dev_priv->dev;

	nouveau_mem_node_cleanup(mem->mm_node);
	vram->put(dev, (struct nouveau_mem **)&mem->mm_node);
}

static int
nouveau_vram_manager_new(struct ttm_mem_type_manager *man,
			 struct ttm_buffer_object *bo,
			 struct ttm_placement *placement,
			 struct ttm_mem_reg *mem)
{
	struct drm_nouveau_private *dev_priv = nouveau_bdev(man->bdev);
	struct nouveau_vram_engine *vram = &dev_priv->engine.vram;
	struct drm_device *dev = dev_priv->dev;
	struct nouveau_bo *nvbo = nouveau_bo(bo);
	struct nouveau_mem *node;
	u32 size_nc = 0;
	int ret;

	if (nvbo->tile_flags & NOUVEAU_GEM_TILE_NONCONTIG)
		size_nc = 1 << nvbo->page_shift;

	ret = vram->get(dev, mem->num_pages << PAGE_SHIFT,
			mem->page_alignment << PAGE_SHIFT, size_nc,
			(nvbo->tile_flags >> 8) & 0x3ff, &node);
	if (ret) {
		mem->mm_node = NULL;
		return (ret == -ENOSPC) ? 0 : ret;
	}

	node->page_shift = nvbo->page_shift;

	mem->mm_node = node;
	mem->start   = node->offset >> PAGE_SHIFT;
	return 0;
}

void
nouveau_vram_manager_debug(struct ttm_mem_type_manager *man, const char *prefix)
{
	struct nouveau_mm *mm = man->priv;
	struct nouveau_mm_node *r;
	u32 total = 0, free = 0;

	mutex_lock(&mm->mutex);
	list_for_each_entry(r, &mm->nodes, nl_entry) {
		printk(KERN_DEBUG "%s %d: 0x%010llx 0x%010llx\n",
		       prefix, r->type, ((u64)r->offset << 12),
		       (((u64)r->offset + r->length) << 12));

		total += r->length;
		if (!r->type)
			free += r->length;
	}
	mutex_unlock(&mm->mutex);

	printk(KERN_DEBUG "%s  total: 0x%010llx free: 0x%010llx\n",
	       prefix, (u64)total << 12, (u64)free << 12);
	printk(KERN_DEBUG "%s  block: 0x%08x\n",
	       prefix, mm->block_size << 12);
}

const struct ttm_mem_type_manager_func nouveau_vram_manager = {
	nouveau_vram_manager_init,
	nouveau_vram_manager_fini,
	nouveau_vram_manager_new,
	nouveau_vram_manager_del,
	nouveau_vram_manager_debug
};

static int
nouveau_gart_manager_init(struct ttm_mem_type_manager *man, unsigned long psize)
{
	return 0;
}

static int
nouveau_gart_manager_fini(struct ttm_mem_type_manager *man)
{
	return 0;
}

static void
nouveau_gart_manager_del(struct ttm_mem_type_manager *man,
			 struct ttm_mem_reg *mem)
{
	nouveau_mem_node_cleanup(mem->mm_node);
	kfree(mem->mm_node);
	mem->mm_node = NULL;
}

static int
nouveau_gart_manager_new(struct ttm_mem_type_manager *man,
			 struct ttm_buffer_object *bo,
			 struct ttm_placement *placement,
			 struct ttm_mem_reg *mem)
{
	struct drm_nouveau_private *dev_priv = nouveau_bdev(bo->bdev);
	struct nouveau_mem *node;

	if (unlikely((mem->num_pages << PAGE_SHIFT) >=
		     dev_priv->gart_info.aper_size))
		return -ENOMEM;

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;
	node->page_shift = 12;

	mem->mm_node = node;
	mem->start   = 0;
	return 0;
}

void
nouveau_gart_manager_debug(struct ttm_mem_type_manager *man, const char *prefix)
{
}

const struct ttm_mem_type_manager_func nouveau_gart_manager = {
	nouveau_gart_manager_init,
	nouveau_gart_manager_fini,
	nouveau_gart_manager_new,
	nouveau_gart_manager_del,
	nouveau_gart_manager_debug
};
