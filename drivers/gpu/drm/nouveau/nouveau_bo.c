/*
 * Copyright 2007 Dave Airlied
 * All Rights Reserved.
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
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
/*
 * Authors: Dave Airlied <airlied@linux.ie>
 *	    Ben Skeggs   <darktama@iinet.net.au>
 *	    Jeremy Kolb  <jkolb@brandeis.edu>
 */

#include "drmP.h"

#include "nouveau_drm.h"
#include "nouveau_drv.h"
#include "nouveau_dma.h"

#include <linux/log2.h>

static void
nouveau_bo_del_ttm(struct ttm_buffer_object *bo)
{
	struct drm_nouveau_private *dev_priv = nouveau_bdev(bo->bdev);
	struct drm_device *dev = dev_priv->dev;
	struct nouveau_bo *nvbo = nouveau_bo(bo);

	ttm_bo_kunmap(&nvbo->kmap);

	if (unlikely(nvbo->gem))
		DRM_ERROR("bo %p still attached to GEM object\n", bo);

	if (nvbo->tile)
		nv10_mem_expire_tiling(dev, nvbo->tile, NULL);

	spin_lock(&dev_priv->ttm.bo_list_lock);
	list_del(&nvbo->head);
	spin_unlock(&dev_priv->ttm.bo_list_lock);
	kfree(nvbo);
}

static void
nouveau_bo_fixup_align(struct drm_device *dev,
		       uint32_t tile_mode, uint32_t tile_flags,
		       int *align, int *size)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	/*
	 * Some of the tile_flags have a periodic structure of N*4096 bytes,
	 * align to to that as well as the page size. Overallocate memory to
	 * avoid corruption of other buffer objects.
	 */
	if (dev_priv->card_type == NV_50) {
		uint32_t block_size = nouveau_mem_fb_amount(dev) >> 15;
		int i;

		switch (tile_flags) {
		case 0x1800:
		case 0x2800:
		case 0x4800:
		case 0x7a00:
			*size = roundup(*size, block_size);
			if (is_power_of_2(block_size)) {
				*size += 3 * block_size;
				for (i = 1; i < 10; i++) {
					*align = 12 * i * block_size;
					if (!(*align % 65536))
						break;
				}
			} else {
				*size += 6 * block_size;
				for (i = 1; i < 10; i++) {
					*align = 8 * i * block_size;
					if (!(*align % 65536))
						break;
				}
			}
			break;
		default:
			break;
		}

	} else {
		if (tile_mode) {
			if (dev_priv->chipset >= 0x40) {
				*align = 65536;
				*size = roundup(*size, 64 * tile_mode);

			} else if (dev_priv->chipset >= 0x30) {
				*align = 32768;
				*size = roundup(*size, 64 * tile_mode);

			} else if (dev_priv->chipset >= 0x20) {
				*align = 16384;
				*size = roundup(*size, 64 * tile_mode);

			} else if (dev_priv->chipset >= 0x10) {
				*align = 16384;
				*size = roundup(*size, 32 * tile_mode);
			}
		}
	}

	/* ALIGN works only on powers of two. */
	*size = roundup(*size, PAGE_SIZE);

	if (dev_priv->card_type == NV_50) {
		*size = roundup(*size, 65536);
		*align = max(65536, *align);
	}
}

int
nouveau_bo_new(struct drm_device *dev, struct nouveau_channel *chan,
	       int size, int align, uint32_t flags, uint32_t tile_mode,
	       uint32_t tile_flags, bool no_vm, bool mappable,
	       struct nouveau_bo **pnvbo)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_bo *nvbo;
	int ret = 0;

	nvbo = kzalloc(sizeof(struct nouveau_bo), GFP_KERNEL);
	if (!nvbo)
		return -ENOMEM;
	INIT_LIST_HEAD(&nvbo->head);
	INIT_LIST_HEAD(&nvbo->entry);
	nvbo->mappable = mappable;
	nvbo->no_vm = no_vm;
	nvbo->tile_mode = tile_mode;
	nvbo->tile_flags = tile_flags;

	nouveau_bo_fixup_align(dev, tile_mode, tile_flags, &align, &size);
	align >>= PAGE_SHIFT;

	nvbo->placement.fpfn = 0;
	nvbo->placement.lpfn = mappable ? dev_priv->fb_mappable_pages : 0;
	nouveau_bo_placement_set(nvbo, flags);

	nvbo->channel = chan;
	ret = ttm_bo_init(&dev_priv->ttm.bdev, &nvbo->bo, size,
			  ttm_bo_type_device, &nvbo->placement, align, 0,
			  false, NULL, size, nouveau_bo_del_ttm);
	nvbo->channel = NULL;
	if (ret) {
		/* ttm will call nouveau_bo_del_ttm if it fails.. */
		return ret;
	}

	spin_lock(&dev_priv->ttm.bo_list_lock);
	list_add_tail(&nvbo->head, &dev_priv->ttm.bo_list);
	spin_unlock(&dev_priv->ttm.bo_list_lock);
	*pnvbo = nvbo;
	return 0;
}

void
nouveau_bo_placement_set(struct nouveau_bo *nvbo, uint32_t memtype)
{
	int n = 0;

	if (memtype & TTM_PL_FLAG_VRAM)
		nvbo->placements[n++] = TTM_PL_FLAG_VRAM | TTM_PL_MASK_CACHING;
	if (memtype & TTM_PL_FLAG_TT)
		nvbo->placements[n++] = TTM_PL_FLAG_TT | TTM_PL_MASK_CACHING;
	if (memtype & TTM_PL_FLAG_SYSTEM)
		nvbo->placements[n++] = TTM_PL_FLAG_SYSTEM | TTM_PL_MASK_CACHING;
	nvbo->placement.placement = nvbo->placements;
	nvbo->placement.busy_placement = nvbo->placements;
	nvbo->placement.num_placement = n;
	nvbo->placement.num_busy_placement = n;

	if (nvbo->pin_refcnt) {
		while (n--)
			nvbo->placements[n] |= TTM_PL_FLAG_NO_EVICT;
	}
}

int
nouveau_bo_pin(struct nouveau_bo *nvbo, uint32_t memtype)
{
	struct drm_nouveau_private *dev_priv = nouveau_bdev(nvbo->bo.bdev);
	struct ttm_buffer_object *bo = &nvbo->bo;
	int ret, i;

	if (nvbo->pin_refcnt && !(memtype & (1 << bo->mem.mem_type))) {
		NV_ERROR(nouveau_bdev(bo->bdev)->dev,
			 "bo %p pinned elsewhere: 0x%08x vs 0x%08x\n", bo,
			 1 << bo->mem.mem_type, memtype);
		return -EINVAL;
	}

	if (nvbo->pin_refcnt++)
		return 0;

	ret = ttm_bo_reserve(bo, false, false, false, 0);
	if (ret)
		goto out;

	nouveau_bo_placement_set(nvbo, memtype);
	for (i = 0; i < nvbo->placement.num_placement; i++)
		nvbo->placements[i] |= TTM_PL_FLAG_NO_EVICT;

	ret = ttm_bo_validate(bo, &nvbo->placement, false, false);
	if (ret == 0) {
		switch (bo->mem.mem_type) {
		case TTM_PL_VRAM:
			dev_priv->fb_aper_free -= bo->mem.size;
			break;
		case TTM_PL_TT:
			dev_priv->gart_info.aper_free -= bo->mem.size;
			break;
		default:
			break;
		}
	}
	ttm_bo_unreserve(bo);
out:
	if (unlikely(ret))
		nvbo->pin_refcnt--;
	return ret;
}

int
nouveau_bo_unpin(struct nouveau_bo *nvbo)
{
	struct drm_nouveau_private *dev_priv = nouveau_bdev(nvbo->bo.bdev);
	struct ttm_buffer_object *bo = &nvbo->bo;
	int ret, i;

	if (--nvbo->pin_refcnt)
		return 0;

	ret = ttm_bo_reserve(bo, false, false, false, 0);
	if (ret)
		return ret;

	for (i = 0; i < nvbo->placement.num_placement; i++)
		nvbo->placements[i] &= ~TTM_PL_FLAG_NO_EVICT;

	ret = ttm_bo_validate(bo, &nvbo->placement, false, false);
	if (ret == 0) {
		switch (bo->mem.mem_type) {
		case TTM_PL_VRAM:
			dev_priv->fb_aper_free += bo->mem.size;
			break;
		case TTM_PL_TT:
			dev_priv->gart_info.aper_free += bo->mem.size;
			break;
		default:
			break;
		}
	}

	ttm_bo_unreserve(bo);
	return ret;
}

int
nouveau_bo_map(struct nouveau_bo *nvbo)
{
	int ret;

	ret = ttm_bo_reserve(&nvbo->bo, false, false, false, 0);
	if (ret)
		return ret;

	ret = ttm_bo_kmap(&nvbo->bo, 0, nvbo->bo.mem.num_pages, &nvbo->kmap);
	ttm_bo_unreserve(&nvbo->bo);
	return ret;
}

void
nouveau_bo_unmap(struct nouveau_bo *nvbo)
{
	ttm_bo_kunmap(&nvbo->kmap);
}

u16
nouveau_bo_rd16(struct nouveau_bo *nvbo, unsigned index)
{
	bool is_iomem;
	u16 *mem = ttm_kmap_obj_virtual(&nvbo->kmap, &is_iomem);
	mem = &mem[index];
	if (is_iomem)
		return ioread16_native((void __force __iomem *)mem);
	else
		return *mem;
}

void
nouveau_bo_wr16(struct nouveau_bo *nvbo, unsigned index, u16 val)
{
	bool is_iomem;
	u16 *mem = ttm_kmap_obj_virtual(&nvbo->kmap, &is_iomem);
	mem = &mem[index];
	if (is_iomem)
		iowrite16_native(val, (void __force __iomem *)mem);
	else
		*mem = val;
}

u32
nouveau_bo_rd32(struct nouveau_bo *nvbo, unsigned index)
{
	bool is_iomem;
	u32 *mem = ttm_kmap_obj_virtual(&nvbo->kmap, &is_iomem);
	mem = &mem[index];
	if (is_iomem)
		return ioread32_native((void __force __iomem *)mem);
	else
		return *mem;
}

void
nouveau_bo_wr32(struct nouveau_bo *nvbo, unsigned index, u32 val)
{
	bool is_iomem;
	u32 *mem = ttm_kmap_obj_virtual(&nvbo->kmap, &is_iomem);
	mem = &mem[index];
	if (is_iomem)
		iowrite32_native(val, (void __force __iomem *)mem);
	else
		*mem = val;
}

static struct ttm_backend *
nouveau_bo_create_ttm_backend_entry(struct ttm_bo_device *bdev)
{
	struct drm_nouveau_private *dev_priv = nouveau_bdev(bdev);
	struct drm_device *dev = dev_priv->dev;

	switch (dev_priv->gart_info.type) {
#if __OS_HAS_AGP
	case NOUVEAU_GART_AGP:
		return ttm_agp_backend_init(bdev, dev->agp->bridge);
#endif
	case NOUVEAU_GART_SGDMA:
		return nouveau_sgdma_init_ttm(dev);
	default:
		NV_ERROR(dev, "Unknown GART type %d\n",
			 dev_priv->gart_info.type);
		break;
	}

	return NULL;
}

static int
nouveau_bo_invalidate_caches(struct ttm_bo_device *bdev, uint32_t flags)
{
	/* We'll do this from user space. */
	return 0;
}

static int
nouveau_bo_init_mem_type(struct ttm_bo_device *bdev, uint32_t type,
			 struct ttm_mem_type_manager *man)
{
	struct drm_nouveau_private *dev_priv = nouveau_bdev(bdev);
	struct drm_device *dev = dev_priv->dev;

	switch (type) {
	case TTM_PL_SYSTEM:
		man->flags = TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_MASK_CACHING;
		man->default_caching = TTM_PL_FLAG_CACHED;
		break;
	case TTM_PL_VRAM:
		man->flags = TTM_MEMTYPE_FLAG_FIXED |
			     TTM_MEMTYPE_FLAG_MAPPABLE |
			     TTM_MEMTYPE_FLAG_NEEDS_IOREMAP;
		man->available_caching = TTM_PL_FLAG_UNCACHED |
					 TTM_PL_FLAG_WC;
		man->default_caching = TTM_PL_FLAG_WC;

		man->io_addr = NULL;
		man->io_offset = drm_get_resource_start(dev, 1);
		man->io_size = drm_get_resource_len(dev, 1);
		if (man->io_size > nouveau_mem_fb_amount(dev))
			man->io_size = nouveau_mem_fb_amount(dev);

		man->gpu_offset = dev_priv->vm_vram_base;
		break;
	case TTM_PL_TT:
		switch (dev_priv->gart_info.type) {
		case NOUVEAU_GART_AGP:
			man->flags = TTM_MEMTYPE_FLAG_MAPPABLE |
				     TTM_MEMTYPE_FLAG_NEEDS_IOREMAP;
			man->available_caching = TTM_PL_FLAG_UNCACHED;
			man->default_caching = TTM_PL_FLAG_UNCACHED;
			break;
		case NOUVEAU_GART_SGDMA:
			man->flags = TTM_MEMTYPE_FLAG_MAPPABLE |
				     TTM_MEMTYPE_FLAG_CMA;
			man->available_caching = TTM_PL_MASK_CACHING;
			man->default_caching = TTM_PL_FLAG_CACHED;
			break;
		default:
			NV_ERROR(dev, "Unknown GART type: %d\n",
				 dev_priv->gart_info.type);
			return -EINVAL;
		}

		man->io_offset  = dev_priv->gart_info.aper_base;
		man->io_size    = dev_priv->gart_info.aper_size;
		man->io_addr   = NULL;
		man->gpu_offset = dev_priv->vm_gart_base;
		break;
	default:
		NV_ERROR(dev, "Unsupported memory type %u\n", (unsigned)type);
		return -EINVAL;
	}
	return 0;
}

static void
nouveau_bo_evict_flags(struct ttm_buffer_object *bo, struct ttm_placement *pl)
{
	struct nouveau_bo *nvbo = nouveau_bo(bo);

	switch (bo->mem.mem_type) {
	case TTM_PL_VRAM:
		nouveau_bo_placement_set(nvbo, TTM_PL_FLAG_TT |
					 TTM_PL_FLAG_SYSTEM);
		break;
	default:
		nouveau_bo_placement_set(nvbo, TTM_PL_FLAG_SYSTEM);
		break;
	}

	*pl = nvbo->placement;
}


/* GPU-assisted copy using NV_MEMORY_TO_MEMORY_FORMAT, can access
 * TTM_PL_{VRAM,TT} directly.
 */

static int
nouveau_bo_move_accel_cleanup(struct nouveau_channel *chan,
			      struct nouveau_bo *nvbo, bool evict, bool no_wait,
			      struct ttm_mem_reg *new_mem)
{
	struct nouveau_fence *fence = NULL;
	int ret;

	ret = nouveau_fence_new(chan, &fence, true);
	if (ret)
		return ret;

	ret = ttm_bo_move_accel_cleanup(&nvbo->bo, fence, NULL,
					evict, no_wait, new_mem);
	nouveau_fence_unref((void *)&fence);
	return ret;
}

static inline uint32_t
nouveau_bo_mem_ctxdma(struct nouveau_bo *nvbo, struct nouveau_channel *chan,
		      struct ttm_mem_reg *mem)
{
	if (chan == nouveau_bdev(nvbo->bo.bdev)->channel) {
		if (mem->mem_type == TTM_PL_TT)
			return NvDmaGART;
		return NvDmaVRAM;
	}

	if (mem->mem_type == TTM_PL_TT)
		return chan->gart_handle;
	return chan->vram_handle;
}

static int
nouveau_bo_move_m2mf(struct ttm_buffer_object *bo, int evict, bool intr,
		     int no_wait, struct ttm_mem_reg *new_mem)
{
	struct nouveau_bo *nvbo = nouveau_bo(bo);
	struct drm_nouveau_private *dev_priv = nouveau_bdev(bo->bdev);
	struct ttm_mem_reg *old_mem = &bo->mem;
	struct nouveau_channel *chan;
	uint64_t src_offset, dst_offset;
	uint32_t page_count;
	int ret;

	chan = nvbo->channel;
	if (!chan || nvbo->tile_flags || nvbo->no_vm)
		chan = dev_priv->channel;

	src_offset = old_mem->mm_node->start << PAGE_SHIFT;
	dst_offset = new_mem->mm_node->start << PAGE_SHIFT;
	if (chan != dev_priv->channel) {
		if (old_mem->mem_type == TTM_PL_TT)
			src_offset += dev_priv->vm_gart_base;
		else
			src_offset += dev_priv->vm_vram_base;

		if (new_mem->mem_type == TTM_PL_TT)
			dst_offset += dev_priv->vm_gart_base;
		else
			dst_offset += dev_priv->vm_vram_base;
	}

	ret = RING_SPACE(chan, 3);
	if (ret)
		return ret;
	BEGIN_RING(chan, NvSubM2MF, NV_MEMORY_TO_MEMORY_FORMAT_DMA_SOURCE, 2);
	OUT_RING(chan, nouveau_bo_mem_ctxdma(nvbo, chan, old_mem));
	OUT_RING(chan, nouveau_bo_mem_ctxdma(nvbo, chan, new_mem));

	if (dev_priv->card_type >= NV_50) {
		ret = RING_SPACE(chan, 4);
		if (ret)
			return ret;
		BEGIN_RING(chan, NvSubM2MF, 0x0200, 1);
		OUT_RING(chan, 1);
		BEGIN_RING(chan, NvSubM2MF, 0x021c, 1);
		OUT_RING(chan, 1);
	}

	page_count = new_mem->num_pages;
	while (page_count) {
		int line_count = (page_count > 2047) ? 2047 : page_count;

		if (dev_priv->card_type >= NV_50) {
			ret = RING_SPACE(chan, 3);
			if (ret)
				return ret;
			BEGIN_RING(chan, NvSubM2MF, 0x0238, 2);
			OUT_RING(chan, upper_32_bits(src_offset));
			OUT_RING(chan, upper_32_bits(dst_offset));
		}
		ret = RING_SPACE(chan, 11);
		if (ret)
			return ret;
		BEGIN_RING(chan, NvSubM2MF,
				 NV_MEMORY_TO_MEMORY_FORMAT_OFFSET_IN, 8);
		OUT_RING(chan, lower_32_bits(src_offset));
		OUT_RING(chan, lower_32_bits(dst_offset));
		OUT_RING(chan, PAGE_SIZE); /* src_pitch */
		OUT_RING(chan, PAGE_SIZE); /* dst_pitch */
		OUT_RING(chan, PAGE_SIZE); /* line_length */
		OUT_RING(chan, line_count);
		OUT_RING(chan, (1<<8)|(1<<0));
		OUT_RING(chan, 0);
		BEGIN_RING(chan, NvSubM2MF, NV_MEMORY_TO_MEMORY_FORMAT_NOP, 1);
		OUT_RING(chan, 0);

		page_count -= line_count;
		src_offset += (PAGE_SIZE * line_count);
		dst_offset += (PAGE_SIZE * line_count);
	}

	return nouveau_bo_move_accel_cleanup(chan, nvbo, evict, no_wait, new_mem);
}

static int
nouveau_bo_move_flipd(struct ttm_buffer_object *bo, bool evict, bool intr,
		      bool no_wait, struct ttm_mem_reg *new_mem)
{
	u32 placement_memtype = TTM_PL_FLAG_TT | TTM_PL_MASK_CACHING;
	struct ttm_placement placement;
	struct ttm_mem_reg tmp_mem;
	int ret;

	placement.fpfn = placement.lpfn = 0;
	placement.num_placement = placement.num_busy_placement = 1;
	placement.placement = placement.busy_placement = &placement_memtype;

	tmp_mem = *new_mem;
	tmp_mem.mm_node = NULL;
	ret = ttm_bo_mem_space(bo, &placement, &tmp_mem, intr, no_wait);
	if (ret)
		return ret;

	ret = ttm_tt_bind(bo->ttm, &tmp_mem);
	if (ret)
		goto out;

	ret = nouveau_bo_move_m2mf(bo, true, intr, no_wait, &tmp_mem);
	if (ret)
		goto out;

	ret = ttm_bo_move_ttm(bo, evict, no_wait, new_mem);
out:
	if (tmp_mem.mm_node) {
		spin_lock(&bo->bdev->glob->lru_lock);
		drm_mm_put_block(tmp_mem.mm_node);
		spin_unlock(&bo->bdev->glob->lru_lock);
	}

	return ret;
}

static int
nouveau_bo_move_flips(struct ttm_buffer_object *bo, bool evict, bool intr,
		      bool no_wait, struct ttm_mem_reg *new_mem)
{
	u32 placement_memtype = TTM_PL_FLAG_TT | TTM_PL_MASK_CACHING;
	struct ttm_placement placement;
	struct ttm_mem_reg tmp_mem;
	int ret;

	placement.fpfn = placement.lpfn = 0;
	placement.num_placement = placement.num_busy_placement = 1;
	placement.placement = placement.busy_placement = &placement_memtype;

	tmp_mem = *new_mem;
	tmp_mem.mm_node = NULL;
	ret = ttm_bo_mem_space(bo, &placement, &tmp_mem, intr, no_wait);
	if (ret)
		return ret;

	ret = ttm_bo_move_ttm(bo, evict, no_wait, &tmp_mem);
	if (ret)
		goto out;

	ret = nouveau_bo_move_m2mf(bo, evict, intr, no_wait, new_mem);
	if (ret)
		goto out;

out:
	if (tmp_mem.mm_node) {
		spin_lock(&bo->bdev->glob->lru_lock);
		drm_mm_put_block(tmp_mem.mm_node);
		spin_unlock(&bo->bdev->glob->lru_lock);
	}

	return ret;
}

static int
nouveau_bo_vm_bind(struct ttm_buffer_object *bo, struct ttm_mem_reg *new_mem,
		   struct nouveau_tile_reg **new_tile)
{
	struct drm_nouveau_private *dev_priv = nouveau_bdev(bo->bdev);
	struct drm_device *dev = dev_priv->dev;
	struct nouveau_bo *nvbo = nouveau_bo(bo);
	uint64_t offset;
	int ret;

	if (nvbo->no_vm || new_mem->mem_type != TTM_PL_VRAM) {
		/* Nothing to do. */
		*new_tile = NULL;
		return 0;
	}

	offset = new_mem->mm_node->start << PAGE_SHIFT;

	if (dev_priv->card_type == NV_50) {
		ret = nv50_mem_vm_bind_linear(dev,
					      offset + dev_priv->vm_vram_base,
					      new_mem->size, nvbo->tile_flags,
					      offset);
		if (ret)
			return ret;

	} else if (dev_priv->card_type >= NV_10) {
		*new_tile = nv10_mem_set_tiling(dev, offset, new_mem->size,
						nvbo->tile_mode);
	}

	return 0;
}

static void
nouveau_bo_vm_cleanup(struct ttm_buffer_object *bo,
		      struct nouveau_tile_reg *new_tile,
		      struct nouveau_tile_reg **old_tile)
{
	struct drm_nouveau_private *dev_priv = nouveau_bdev(bo->bdev);
	struct drm_device *dev = dev_priv->dev;

	if (dev_priv->card_type >= NV_10 &&
	    dev_priv->card_type < NV_50) {
		if (*old_tile)
			nv10_mem_expire_tiling(dev, *old_tile, bo->sync_obj);

		*old_tile = new_tile;
	}
}

static int
nouveau_bo_move(struct ttm_buffer_object *bo, bool evict, bool intr,
		bool no_wait, struct ttm_mem_reg *new_mem)
{
	struct drm_nouveau_private *dev_priv = nouveau_bdev(bo->bdev);
	struct nouveau_bo *nvbo = nouveau_bo(bo);
	struct ttm_mem_reg *old_mem = &bo->mem;
	struct nouveau_tile_reg *new_tile = NULL;
	int ret = 0;

	ret = nouveau_bo_vm_bind(bo, new_mem, &new_tile);
	if (ret)
		return ret;

	/* Software copy if the card isn't up and running yet. */
	if (dev_priv->init_state != NOUVEAU_CARD_INIT_DONE ||
	    !dev_priv->channel) {
		ret = ttm_bo_move_memcpy(bo, evict, no_wait, new_mem);
		goto out;
	}

	/* Fake bo copy. */
	if (old_mem->mem_type == TTM_PL_SYSTEM && !bo->ttm) {
		BUG_ON(bo->mem.mm_node != NULL);
		bo->mem = *new_mem;
		new_mem->mm_node = NULL;
		goto out;
	}

	/* Hardware assisted copy. */
	if (new_mem->mem_type == TTM_PL_SYSTEM)
		ret = nouveau_bo_move_flipd(bo, evict, intr, no_wait, new_mem);
	else if (old_mem->mem_type == TTM_PL_SYSTEM)
		ret = nouveau_bo_move_flips(bo, evict, intr, no_wait, new_mem);
	else
		ret = nouveau_bo_move_m2mf(bo, evict, intr, no_wait, new_mem);

	if (!ret)
		goto out;

	/* Fallback to software copy. */
	ret = ttm_bo_move_memcpy(bo, evict, no_wait, new_mem);

out:
	if (ret)
		nouveau_bo_vm_cleanup(bo, NULL, &new_tile);
	else
		nouveau_bo_vm_cleanup(bo, new_tile, &nvbo->tile);

	return ret;
}

static int
nouveau_bo_verify_access(struct ttm_buffer_object *bo, struct file *filp)
{
	return 0;
}

struct ttm_bo_driver nouveau_bo_driver = {
	.create_ttm_backend_entry = nouveau_bo_create_ttm_backend_entry,
	.invalidate_caches = nouveau_bo_invalidate_caches,
	.init_mem_type = nouveau_bo_init_mem_type,
	.evict_flags = nouveau_bo_evict_flags,
	.move = nouveau_bo_move,
	.verify_access = nouveau_bo_verify_access,
	.sync_obj_signaled = nouveau_fence_signalled,
	.sync_obj_wait = nouveau_fence_wait,
	.sync_obj_flush = nouveau_fence_flush,
	.sync_obj_unref = nouveau_fence_unref,
	.sync_obj_ref = nouveau_fence_ref,
};

