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

#include <linux/dma-mapping.h>
#include <linux/swiotlb.h>

#include "nouveau_drv.h"
#include "nouveau_dma.h"
#include "nouveau_fence.h"

#include "nouveau_bo.h"
#include "nouveau_ttm.h"
#include "nouveau_gem.h"

/*
 * NV10-NV40 tiling helpers
 */

static void
nv10_bo_update_tile_region(struct drm_device *dev, struct nouveau_drm_tile *reg,
			   u32 addr, u32 size, u32 pitch, u32 flags)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	int i = reg - drm->tile.reg;
	struct nvkm_device *device = nvxx_device(&drm->device);
	struct nvkm_fb *fb = device->fb;
	struct nvkm_fb_tile *tile = &fb->tile.region[i];

	nouveau_fence_unref(&reg->fence);

	if (tile->pitch)
		nvkm_fb_tile_fini(fb, i, tile);

	if (pitch)
		nvkm_fb_tile_init(fb, i, addr, size, pitch, flags, tile);

	nvkm_fb_tile_prog(fb, i, tile);
}

static struct nouveau_drm_tile *
nv10_bo_get_tile_region(struct drm_device *dev, int i)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_drm_tile *tile = &drm->tile.reg[i];

	spin_lock(&drm->tile.lock);

	if (!tile->used &&
	    (!tile->fence || nouveau_fence_done(tile->fence)))
		tile->used = true;
	else
		tile = NULL;

	spin_unlock(&drm->tile.lock);
	return tile;
}

static void
nv10_bo_put_tile_region(struct drm_device *dev, struct nouveau_drm_tile *tile,
			struct fence *fence)
{
	struct nouveau_drm *drm = nouveau_drm(dev);

	if (tile) {
		spin_lock(&drm->tile.lock);
		tile->fence = (struct nouveau_fence *)fence_get(fence);
		tile->used = false;
		spin_unlock(&drm->tile.lock);
	}
}

static struct nouveau_drm_tile *
nv10_bo_set_tiling(struct drm_device *dev, u32 addr,
		   u32 size, u32 pitch, u32 flags)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nvkm_fb *fb = nvxx_fb(&drm->device);
	struct nouveau_drm_tile *tile, *found = NULL;
	int i;

	for (i = 0; i < fb->tile.regions; i++) {
		tile = nv10_bo_get_tile_region(dev, i);

		if (pitch && !found) {
			found = tile;
			continue;

		} else if (tile && fb->tile.region[i].pitch) {
			/* Kill an unused tile region. */
			nv10_bo_update_tile_region(dev, tile, 0, 0, 0, 0);
		}

		nv10_bo_put_tile_region(dev, tile, NULL);
	}

	if (found)
		nv10_bo_update_tile_region(dev, found, addr, size,
					    pitch, flags);
	return found;
}

static void
nouveau_bo_del_ttm(struct ttm_buffer_object *bo)
{
	struct nouveau_drm *drm = nouveau_bdev(bo->bdev);
	struct drm_device *dev = drm->dev;
	struct nouveau_bo *nvbo = nouveau_bo(bo);

	if (unlikely(nvbo->gem.filp))
		DRM_ERROR("bo %p still attached to GEM object\n", bo);
	WARN_ON(nvbo->pin_refcnt > 0);
	nv10_bo_put_tile_region(dev, nvbo->tile, NULL);
	kfree(nvbo);
}

static void
nouveau_bo_fixup_align(struct nouveau_bo *nvbo, u32 flags,
		       int *align, int *size)
{
	struct nouveau_drm *drm = nouveau_bdev(nvbo->bo.bdev);
	struct nvif_device *device = &drm->device;

	if (device->info.family < NV_DEVICE_INFO_V0_TESLA) {
		if (nvbo->tile_mode) {
			if (device->info.chipset >= 0x40) {
				*align = 65536;
				*size = roundup(*size, 64 * nvbo->tile_mode);

			} else if (device->info.chipset >= 0x30) {
				*align = 32768;
				*size = roundup(*size, 64 * nvbo->tile_mode);

			} else if (device->info.chipset >= 0x20) {
				*align = 16384;
				*size = roundup(*size, 64 * nvbo->tile_mode);

			} else if (device->info.chipset >= 0x10) {
				*align = 16384;
				*size = roundup(*size, 32 * nvbo->tile_mode);
			}
		}
	} else {
		*size = roundup(*size, (1 << nvbo->page_shift));
		*align = max((1 <<  nvbo->page_shift), *align);
	}

	*size = roundup(*size, PAGE_SIZE);
}

int
nouveau_bo_new(struct drm_device *dev, int size, int align,
	       uint32_t flags, uint32_t tile_mode, uint32_t tile_flags,
	       struct sg_table *sg, struct reservation_object *robj,
	       struct nouveau_bo **pnvbo)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_bo *nvbo;
	size_t acc_size;
	int ret;
	int type = ttm_bo_type_device;
	int lpg_shift = 12;
	int max_size;

	if (drm->client.vm)
		lpg_shift = drm->client.vm->mmu->lpg_shift;
	max_size = INT_MAX & ~((1 << lpg_shift) - 1);

	if (size <= 0 || size > max_size) {
		NV_WARN(drm, "skipped size %x\n", (u32)size);
		return -EINVAL;
	}

	if (sg)
		type = ttm_bo_type_sg;

	nvbo = kzalloc(sizeof(struct nouveau_bo), GFP_KERNEL);
	if (!nvbo)
		return -ENOMEM;
	INIT_LIST_HEAD(&nvbo->head);
	INIT_LIST_HEAD(&nvbo->entry);
	INIT_LIST_HEAD(&nvbo->vma_list);
	nvbo->tile_mode = tile_mode;
	nvbo->tile_flags = tile_flags;
	nvbo->bo.bdev = &drm->ttm.bdev;

	nvbo->force_coherent = flags & TTM_PL_FLAG_UNCACHED;

	nvbo->page_shift = 12;
	if (drm->client.vm) {
		if (!(flags & TTM_PL_FLAG_TT) && size > 256 * 1024)
			nvbo->page_shift = drm->client.vm->mmu->lpg_shift;
	}

	nouveau_bo_fixup_align(nvbo, flags, &align, &size);
	nvbo->bo.mem.num_pages = size >> PAGE_SHIFT;
	nouveau_bo_placement_set(nvbo, flags, 0);

	acc_size = ttm_bo_dma_acc_size(&drm->ttm.bdev, size,
				       sizeof(struct nouveau_bo));

	ret = ttm_bo_init(&drm->ttm.bdev, &nvbo->bo, size,
			  type, &nvbo->placement,
			  align >> PAGE_SHIFT, false, NULL, acc_size, sg,
			  robj, nouveau_bo_del_ttm);
	if (ret) {
		/* ttm will call nouveau_bo_del_ttm if it fails.. */
		return ret;
	}

	*pnvbo = nvbo;
	return 0;
}

static void
set_placement_list(struct ttm_place *pl, unsigned *n, uint32_t type, uint32_t flags)
{
	*n = 0;

	if (type & TTM_PL_FLAG_VRAM)
		pl[(*n)++].flags = TTM_PL_FLAG_VRAM | flags;
	if (type & TTM_PL_FLAG_TT)
		pl[(*n)++].flags = TTM_PL_FLAG_TT | flags;
	if (type & TTM_PL_FLAG_SYSTEM)
		pl[(*n)++].flags = TTM_PL_FLAG_SYSTEM | flags;
}

static void
set_placement_range(struct nouveau_bo *nvbo, uint32_t type)
{
	struct nouveau_drm *drm = nouveau_bdev(nvbo->bo.bdev);
	u32 vram_pages = drm->device.info.ram_size >> PAGE_SHIFT;
	unsigned i, fpfn, lpfn;

	if (drm->device.info.family == NV_DEVICE_INFO_V0_CELSIUS &&
	    nvbo->tile_mode && (type & TTM_PL_FLAG_VRAM) &&
	    nvbo->bo.mem.num_pages < vram_pages / 4) {
		/*
		 * Make sure that the color and depth buffers are handled
		 * by independent memory controller units. Up to a 9x
		 * speed up when alpha-blending and depth-test are enabled
		 * at the same time.
		 */
		if (nvbo->tile_flags & NOUVEAU_GEM_TILE_ZETA) {
			fpfn = vram_pages / 2;
			lpfn = ~0;
		} else {
			fpfn = 0;
			lpfn = vram_pages / 2;
		}
		for (i = 0; i < nvbo->placement.num_placement; ++i) {
			nvbo->placements[i].fpfn = fpfn;
			nvbo->placements[i].lpfn = lpfn;
		}
		for (i = 0; i < nvbo->placement.num_busy_placement; ++i) {
			nvbo->busy_placements[i].fpfn = fpfn;
			nvbo->busy_placements[i].lpfn = lpfn;
		}
	}
}

void
nouveau_bo_placement_set(struct nouveau_bo *nvbo, uint32_t type, uint32_t busy)
{
	struct ttm_placement *pl = &nvbo->placement;
	uint32_t flags = (nvbo->force_coherent ? TTM_PL_FLAG_UNCACHED :
						 TTM_PL_MASK_CACHING) |
			 (nvbo->pin_refcnt ? TTM_PL_FLAG_NO_EVICT : 0);

	pl->placement = nvbo->placements;
	set_placement_list(nvbo->placements, &pl->num_placement,
			   type, flags);

	pl->busy_placement = nvbo->busy_placements;
	set_placement_list(nvbo->busy_placements, &pl->num_busy_placement,
			   type | busy, flags);

	set_placement_range(nvbo, type);
}

int
nouveau_bo_pin(struct nouveau_bo *nvbo, uint32_t memtype, bool contig)
{
	struct nouveau_drm *drm = nouveau_bdev(nvbo->bo.bdev);
	struct ttm_buffer_object *bo = &nvbo->bo;
	bool force = false, evict = false;
	int ret;

	ret = ttm_bo_reserve(bo, false, false, NULL);
	if (ret)
		return ret;

	if (drm->device.info.family >= NV_DEVICE_INFO_V0_TESLA &&
	    memtype == TTM_PL_FLAG_VRAM && contig) {
		if (nvbo->tile_flags & NOUVEAU_GEM_TILE_NONCONTIG) {
			if (bo->mem.mem_type == TTM_PL_VRAM) {
				struct nvkm_mem *mem = bo->mem.mm_node;
				if (!list_is_singular(&mem->regions))
					evict = true;
			}
			nvbo->tile_flags &= ~NOUVEAU_GEM_TILE_NONCONTIG;
			force = true;
		}
	}

	if (nvbo->pin_refcnt) {
		if (!(memtype & (1 << bo->mem.mem_type)) || evict) {
			NV_ERROR(drm, "bo %p pinned elsewhere: "
				      "0x%08x vs 0x%08x\n", bo,
				 1 << bo->mem.mem_type, memtype);
			ret = -EBUSY;
		}
		nvbo->pin_refcnt++;
		goto out;
	}

	if (evict) {
		nouveau_bo_placement_set(nvbo, TTM_PL_FLAG_TT, 0);
		ret = nouveau_bo_validate(nvbo, false, false);
		if (ret)
			goto out;
	}

	nvbo->pin_refcnt++;
	nouveau_bo_placement_set(nvbo, memtype, 0);

	/* drop pin_refcnt temporarily, so we don't trip the assertion
	 * in nouveau_bo_move() that makes sure we're not trying to
	 * move a pinned buffer
	 */
	nvbo->pin_refcnt--;
	ret = nouveau_bo_validate(nvbo, false, false);
	if (ret)
		goto out;
	nvbo->pin_refcnt++;

	switch (bo->mem.mem_type) {
	case TTM_PL_VRAM:
		drm->gem.vram_available -= bo->mem.size;
		break;
	case TTM_PL_TT:
		drm->gem.gart_available -= bo->mem.size;
		break;
	default:
		break;
	}

out:
	if (force && ret)
		nvbo->tile_flags |= NOUVEAU_GEM_TILE_NONCONTIG;
	ttm_bo_unreserve(bo);
	return ret;
}

int
nouveau_bo_unpin(struct nouveau_bo *nvbo)
{
	struct nouveau_drm *drm = nouveau_bdev(nvbo->bo.bdev);
	struct ttm_buffer_object *bo = &nvbo->bo;
	int ret, ref;

	ret = ttm_bo_reserve(bo, false, false, NULL);
	if (ret)
		return ret;

	ref = --nvbo->pin_refcnt;
	WARN_ON_ONCE(ref < 0);
	if (ref)
		goto out;

	nouveau_bo_placement_set(nvbo, bo->mem.placement, 0);

	ret = nouveau_bo_validate(nvbo, false, false);
	if (ret == 0) {
		switch (bo->mem.mem_type) {
		case TTM_PL_VRAM:
			drm->gem.vram_available += bo->mem.size;
			break;
		case TTM_PL_TT:
			drm->gem.gart_available += bo->mem.size;
			break;
		default:
			break;
		}
	}

out:
	ttm_bo_unreserve(bo);
	return ret;
}

int
nouveau_bo_map(struct nouveau_bo *nvbo)
{
	int ret;

	ret = ttm_bo_reserve(&nvbo->bo, false, false, NULL);
	if (ret)
		return ret;

	ret = ttm_bo_kmap(&nvbo->bo, 0, nvbo->bo.mem.num_pages, &nvbo->kmap);

	ttm_bo_unreserve(&nvbo->bo);
	return ret;
}

void
nouveau_bo_unmap(struct nouveau_bo *nvbo)
{
	if (!nvbo)
		return;

	ttm_bo_kunmap(&nvbo->kmap);
}

void
nouveau_bo_sync_for_device(struct nouveau_bo *nvbo)
{
	struct nouveau_drm *drm = nouveau_bdev(nvbo->bo.bdev);
	struct nvkm_device *device = nvxx_device(&drm->device);
	struct ttm_dma_tt *ttm_dma = (struct ttm_dma_tt *)nvbo->bo.ttm;
	int i;

	if (!ttm_dma)
		return;

	/* Don't waste time looping if the object is coherent */
	if (nvbo->force_coherent)
		return;

	for (i = 0; i < ttm_dma->ttm.num_pages; i++)
		dma_sync_single_for_device(device->dev, ttm_dma->dma_address[i],
					   PAGE_SIZE, DMA_TO_DEVICE);
}

void
nouveau_bo_sync_for_cpu(struct nouveau_bo *nvbo)
{
	struct nouveau_drm *drm = nouveau_bdev(nvbo->bo.bdev);
	struct nvkm_device *device = nvxx_device(&drm->device);
	struct ttm_dma_tt *ttm_dma = (struct ttm_dma_tt *)nvbo->bo.ttm;
	int i;

	if (!ttm_dma)
		return;

	/* Don't waste time looping if the object is coherent */
	if (nvbo->force_coherent)
		return;

	for (i = 0; i < ttm_dma->ttm.num_pages; i++)
		dma_sync_single_for_cpu(device->dev, ttm_dma->dma_address[i],
					PAGE_SIZE, DMA_FROM_DEVICE);
}

int
nouveau_bo_validate(struct nouveau_bo *nvbo, bool interruptible,
		    bool no_wait_gpu)
{
	int ret;

	ret = ttm_bo_validate(&nvbo->bo, &nvbo->placement,
			      interruptible, no_wait_gpu);
	if (ret)
		return ret;

	nouveau_bo_sync_for_device(nvbo);

	return 0;
}

void
nouveau_bo_wr16(struct nouveau_bo *nvbo, unsigned index, u16 val)
{
	bool is_iomem;
	u16 *mem = ttm_kmap_obj_virtual(&nvbo->kmap, &is_iomem);

	mem += index;

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

	mem += index;

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

	mem += index;

	if (is_iomem)
		iowrite32_native(val, (void __force __iomem *)mem);
	else
		*mem = val;
}

static struct ttm_tt *
nouveau_ttm_tt_create(struct ttm_bo_device *bdev, unsigned long size,
		      uint32_t page_flags, struct page *dummy_read)
{
#if IS_ENABLED(CONFIG_AGP)
	struct nouveau_drm *drm = nouveau_bdev(bdev);

	if (drm->agp.bridge) {
		return ttm_agp_tt_create(bdev, drm->agp.bridge, size,
					 page_flags, dummy_read);
	}
#endif

	return nouveau_sgdma_create_ttm(bdev, size, page_flags, dummy_read);
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
	struct nouveau_drm *drm = nouveau_bdev(bdev);

	switch (type) {
	case TTM_PL_SYSTEM:
		man->flags = TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_MASK_CACHING;
		man->default_caching = TTM_PL_FLAG_CACHED;
		break;
	case TTM_PL_VRAM:
		man->flags = TTM_MEMTYPE_FLAG_FIXED |
			     TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_FLAG_UNCACHED |
					 TTM_PL_FLAG_WC;
		man->default_caching = TTM_PL_FLAG_WC;

		if (drm->device.info.family >= NV_DEVICE_INFO_V0_TESLA) {
			/* Some BARs do not support being ioremapped WC */
			if (nvxx_bar(&drm->device)->iomap_uncached) {
				man->available_caching = TTM_PL_FLAG_UNCACHED;
				man->default_caching = TTM_PL_FLAG_UNCACHED;
			}

			man->func = &nouveau_vram_manager;
			man->io_reserve_fastpath = false;
			man->use_io_reserve_lru = true;
		} else {
			man->func = &ttm_bo_manager_func;
		}
		break;
	case TTM_PL_TT:
		if (drm->device.info.family >= NV_DEVICE_INFO_V0_TESLA)
			man->func = &nouveau_gart_manager;
		else
		if (!drm->agp.bridge)
			man->func = &nv04_gart_manager;
		else
			man->func = &ttm_bo_manager_func;

		if (drm->agp.bridge) {
			man->flags = TTM_MEMTYPE_FLAG_MAPPABLE;
			man->available_caching = TTM_PL_FLAG_UNCACHED |
				TTM_PL_FLAG_WC;
			man->default_caching = TTM_PL_FLAG_WC;
		} else {
			man->flags = TTM_MEMTYPE_FLAG_MAPPABLE |
				     TTM_MEMTYPE_FLAG_CMA;
			man->available_caching = TTM_PL_MASK_CACHING;
			man->default_caching = TTM_PL_FLAG_CACHED;
		}

		break;
	default:
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
		nouveau_bo_placement_set(nvbo, TTM_PL_FLAG_TT,
					 TTM_PL_FLAG_SYSTEM);
		break;
	default:
		nouveau_bo_placement_set(nvbo, TTM_PL_FLAG_SYSTEM, 0);
		break;
	}

	*pl = nvbo->placement;
}


static int
nve0_bo_move_init(struct nouveau_channel *chan, u32 handle)
{
	int ret = RING_SPACE(chan, 2);
	if (ret == 0) {
		BEGIN_NVC0(chan, NvSubCopy, 0x0000, 1);
		OUT_RING  (chan, handle & 0x0000ffff);
		FIRE_RING (chan);
	}
	return ret;
}

static int
nve0_bo_move_copy(struct nouveau_channel *chan, struct ttm_buffer_object *bo,
		  struct ttm_mem_reg *old_mem, struct ttm_mem_reg *new_mem)
{
	struct nvkm_mem *node = old_mem->mm_node;
	int ret = RING_SPACE(chan, 10);
	if (ret == 0) {
		BEGIN_NVC0(chan, NvSubCopy, 0x0400, 8);
		OUT_RING  (chan, upper_32_bits(node->vma[0].offset));
		OUT_RING  (chan, lower_32_bits(node->vma[0].offset));
		OUT_RING  (chan, upper_32_bits(node->vma[1].offset));
		OUT_RING  (chan, lower_32_bits(node->vma[1].offset));
		OUT_RING  (chan, PAGE_SIZE);
		OUT_RING  (chan, PAGE_SIZE);
		OUT_RING  (chan, PAGE_SIZE);
		OUT_RING  (chan, new_mem->num_pages);
		BEGIN_IMC0(chan, NvSubCopy, 0x0300, 0x0386);
	}
	return ret;
}

static int
nvc0_bo_move_init(struct nouveau_channel *chan, u32 handle)
{
	int ret = RING_SPACE(chan, 2);
	if (ret == 0) {
		BEGIN_NVC0(chan, NvSubCopy, 0x0000, 1);
		OUT_RING  (chan, handle);
	}
	return ret;
}

static int
nvc0_bo_move_copy(struct nouveau_channel *chan, struct ttm_buffer_object *bo,
		  struct ttm_mem_reg *old_mem, struct ttm_mem_reg *new_mem)
{
	struct nvkm_mem *node = old_mem->mm_node;
	u64 src_offset = node->vma[0].offset;
	u64 dst_offset = node->vma[1].offset;
	u32 page_count = new_mem->num_pages;
	int ret;

	page_count = new_mem->num_pages;
	while (page_count) {
		int line_count = (page_count > 8191) ? 8191 : page_count;

		ret = RING_SPACE(chan, 11);
		if (ret)
			return ret;

		BEGIN_NVC0(chan, NvSubCopy, 0x030c, 8);
		OUT_RING  (chan, upper_32_bits(src_offset));
		OUT_RING  (chan, lower_32_bits(src_offset));
		OUT_RING  (chan, upper_32_bits(dst_offset));
		OUT_RING  (chan, lower_32_bits(dst_offset));
		OUT_RING  (chan, PAGE_SIZE);
		OUT_RING  (chan, PAGE_SIZE);
		OUT_RING  (chan, PAGE_SIZE);
		OUT_RING  (chan, line_count);
		BEGIN_NVC0(chan, NvSubCopy, 0x0300, 1);
		OUT_RING  (chan, 0x00000110);

		page_count -= line_count;
		src_offset += (PAGE_SIZE * line_count);
		dst_offset += (PAGE_SIZE * line_count);
	}

	return 0;
}

static int
nvc0_bo_move_m2mf(struct nouveau_channel *chan, struct ttm_buffer_object *bo,
		  struct ttm_mem_reg *old_mem, struct ttm_mem_reg *new_mem)
{
	struct nvkm_mem *node = old_mem->mm_node;
	u64 src_offset = node->vma[0].offset;
	u64 dst_offset = node->vma[1].offset;
	u32 page_count = new_mem->num_pages;
	int ret;

	page_count = new_mem->num_pages;
	while (page_count) {
		int line_count = (page_count > 2047) ? 2047 : page_count;

		ret = RING_SPACE(chan, 12);
		if (ret)
			return ret;

		BEGIN_NVC0(chan, NvSubCopy, 0x0238, 2);
		OUT_RING  (chan, upper_32_bits(dst_offset));
		OUT_RING  (chan, lower_32_bits(dst_offset));
		BEGIN_NVC0(chan, NvSubCopy, 0x030c, 6);
		OUT_RING  (chan, upper_32_bits(src_offset));
		OUT_RING  (chan, lower_32_bits(src_offset));
		OUT_RING  (chan, PAGE_SIZE); /* src_pitch */
		OUT_RING  (chan, PAGE_SIZE); /* dst_pitch */
		OUT_RING  (chan, PAGE_SIZE); /* line_length */
		OUT_RING  (chan, line_count);
		BEGIN_NVC0(chan, NvSubCopy, 0x0300, 1);
		OUT_RING  (chan, 0x00100110);

		page_count -= line_count;
		src_offset += (PAGE_SIZE * line_count);
		dst_offset += (PAGE_SIZE * line_count);
	}

	return 0;
}

static int
nva3_bo_move_copy(struct nouveau_channel *chan, struct ttm_buffer_object *bo,
		  struct ttm_mem_reg *old_mem, struct ttm_mem_reg *new_mem)
{
	struct nvkm_mem *node = old_mem->mm_node;
	u64 src_offset = node->vma[0].offset;
	u64 dst_offset = node->vma[1].offset;
	u32 page_count = new_mem->num_pages;
	int ret;

	page_count = new_mem->num_pages;
	while (page_count) {
		int line_count = (page_count > 8191) ? 8191 : page_count;

		ret = RING_SPACE(chan, 11);
		if (ret)
			return ret;

		BEGIN_NV04(chan, NvSubCopy, 0x030c, 8);
		OUT_RING  (chan, upper_32_bits(src_offset));
		OUT_RING  (chan, lower_32_bits(src_offset));
		OUT_RING  (chan, upper_32_bits(dst_offset));
		OUT_RING  (chan, lower_32_bits(dst_offset));
		OUT_RING  (chan, PAGE_SIZE);
		OUT_RING  (chan, PAGE_SIZE);
		OUT_RING  (chan, PAGE_SIZE);
		OUT_RING  (chan, line_count);
		BEGIN_NV04(chan, NvSubCopy, 0x0300, 1);
		OUT_RING  (chan, 0x00000110);

		page_count -= line_count;
		src_offset += (PAGE_SIZE * line_count);
		dst_offset += (PAGE_SIZE * line_count);
	}

	return 0;
}

static int
nv98_bo_move_exec(struct nouveau_channel *chan, struct ttm_buffer_object *bo,
		  struct ttm_mem_reg *old_mem, struct ttm_mem_reg *new_mem)
{
	struct nvkm_mem *node = old_mem->mm_node;
	int ret = RING_SPACE(chan, 7);
	if (ret == 0) {
		BEGIN_NV04(chan, NvSubCopy, 0x0320, 6);
		OUT_RING  (chan, upper_32_bits(node->vma[0].offset));
		OUT_RING  (chan, lower_32_bits(node->vma[0].offset));
		OUT_RING  (chan, upper_32_bits(node->vma[1].offset));
		OUT_RING  (chan, lower_32_bits(node->vma[1].offset));
		OUT_RING  (chan, 0x00000000 /* COPY */);
		OUT_RING  (chan, new_mem->num_pages << PAGE_SHIFT);
	}
	return ret;
}

static int
nv84_bo_move_exec(struct nouveau_channel *chan, struct ttm_buffer_object *bo,
		  struct ttm_mem_reg *old_mem, struct ttm_mem_reg *new_mem)
{
	struct nvkm_mem *node = old_mem->mm_node;
	int ret = RING_SPACE(chan, 7);
	if (ret == 0) {
		BEGIN_NV04(chan, NvSubCopy, 0x0304, 6);
		OUT_RING  (chan, new_mem->num_pages << PAGE_SHIFT);
		OUT_RING  (chan, upper_32_bits(node->vma[0].offset));
		OUT_RING  (chan, lower_32_bits(node->vma[0].offset));
		OUT_RING  (chan, upper_32_bits(node->vma[1].offset));
		OUT_RING  (chan, lower_32_bits(node->vma[1].offset));
		OUT_RING  (chan, 0x00000000 /* MODE_COPY, QUERY_NONE */);
	}
	return ret;
}

static int
nv50_bo_move_init(struct nouveau_channel *chan, u32 handle)
{
	int ret = RING_SPACE(chan, 6);
	if (ret == 0) {
		BEGIN_NV04(chan, NvSubCopy, 0x0000, 1);
		OUT_RING  (chan, handle);
		BEGIN_NV04(chan, NvSubCopy, 0x0180, 3);
		OUT_RING  (chan, chan->drm->ntfy.handle);
		OUT_RING  (chan, chan->vram.handle);
		OUT_RING  (chan, chan->vram.handle);
	}

	return ret;
}

static int
nv50_bo_move_m2mf(struct nouveau_channel *chan, struct ttm_buffer_object *bo,
		  struct ttm_mem_reg *old_mem, struct ttm_mem_reg *new_mem)
{
	struct nvkm_mem *node = old_mem->mm_node;
	u64 length = (new_mem->num_pages << PAGE_SHIFT);
	u64 src_offset = node->vma[0].offset;
	u64 dst_offset = node->vma[1].offset;
	int src_tiled = !!node->memtype;
	int dst_tiled = !!((struct nvkm_mem *)new_mem->mm_node)->memtype;
	int ret;

	while (length) {
		u32 amount, stride, height;

		ret = RING_SPACE(chan, 18 + 6 * (src_tiled + dst_tiled));
		if (ret)
			return ret;

		amount  = min(length, (u64)(4 * 1024 * 1024));
		stride  = 16 * 4;
		height  = amount / stride;

		if (src_tiled) {
			BEGIN_NV04(chan, NvSubCopy, 0x0200, 7);
			OUT_RING  (chan, 0);
			OUT_RING  (chan, 0);
			OUT_RING  (chan, stride);
			OUT_RING  (chan, height);
			OUT_RING  (chan, 1);
			OUT_RING  (chan, 0);
			OUT_RING  (chan, 0);
		} else {
			BEGIN_NV04(chan, NvSubCopy, 0x0200, 1);
			OUT_RING  (chan, 1);
		}
		if (dst_tiled) {
			BEGIN_NV04(chan, NvSubCopy, 0x021c, 7);
			OUT_RING  (chan, 0);
			OUT_RING  (chan, 0);
			OUT_RING  (chan, stride);
			OUT_RING  (chan, height);
			OUT_RING  (chan, 1);
			OUT_RING  (chan, 0);
			OUT_RING  (chan, 0);
		} else {
			BEGIN_NV04(chan, NvSubCopy, 0x021c, 1);
			OUT_RING  (chan, 1);
		}

		BEGIN_NV04(chan, NvSubCopy, 0x0238, 2);
		OUT_RING  (chan, upper_32_bits(src_offset));
		OUT_RING  (chan, upper_32_bits(dst_offset));
		BEGIN_NV04(chan, NvSubCopy, 0x030c, 8);
		OUT_RING  (chan, lower_32_bits(src_offset));
		OUT_RING  (chan, lower_32_bits(dst_offset));
		OUT_RING  (chan, stride);
		OUT_RING  (chan, stride);
		OUT_RING  (chan, stride);
		OUT_RING  (chan, height);
		OUT_RING  (chan, 0x00000101);
		OUT_RING  (chan, 0x00000000);
		BEGIN_NV04(chan, NvSubCopy, NV_MEMORY_TO_MEMORY_FORMAT_NOP, 1);
		OUT_RING  (chan, 0);

		length -= amount;
		src_offset += amount;
		dst_offset += amount;
	}

	return 0;
}

static int
nv04_bo_move_init(struct nouveau_channel *chan, u32 handle)
{
	int ret = RING_SPACE(chan, 4);
	if (ret == 0) {
		BEGIN_NV04(chan, NvSubCopy, 0x0000, 1);
		OUT_RING  (chan, handle);
		BEGIN_NV04(chan, NvSubCopy, 0x0180, 1);
		OUT_RING  (chan, chan->drm->ntfy.handle);
	}

	return ret;
}

static inline uint32_t
nouveau_bo_mem_ctxdma(struct ttm_buffer_object *bo,
		      struct nouveau_channel *chan, struct ttm_mem_reg *mem)
{
	if (mem->mem_type == TTM_PL_TT)
		return NvDmaTT;
	return chan->vram.handle;
}

static int
nv04_bo_move_m2mf(struct nouveau_channel *chan, struct ttm_buffer_object *bo,
		  struct ttm_mem_reg *old_mem, struct ttm_mem_reg *new_mem)
{
	u32 src_offset = old_mem->start << PAGE_SHIFT;
	u32 dst_offset = new_mem->start << PAGE_SHIFT;
	u32 page_count = new_mem->num_pages;
	int ret;

	ret = RING_SPACE(chan, 3);
	if (ret)
		return ret;

	BEGIN_NV04(chan, NvSubCopy, NV_MEMORY_TO_MEMORY_FORMAT_DMA_SOURCE, 2);
	OUT_RING  (chan, nouveau_bo_mem_ctxdma(bo, chan, old_mem));
	OUT_RING  (chan, nouveau_bo_mem_ctxdma(bo, chan, new_mem));

	page_count = new_mem->num_pages;
	while (page_count) {
		int line_count = (page_count > 2047) ? 2047 : page_count;

		ret = RING_SPACE(chan, 11);
		if (ret)
			return ret;

		BEGIN_NV04(chan, NvSubCopy,
				 NV_MEMORY_TO_MEMORY_FORMAT_OFFSET_IN, 8);
		OUT_RING  (chan, src_offset);
		OUT_RING  (chan, dst_offset);
		OUT_RING  (chan, PAGE_SIZE); /* src_pitch */
		OUT_RING  (chan, PAGE_SIZE); /* dst_pitch */
		OUT_RING  (chan, PAGE_SIZE); /* line_length */
		OUT_RING  (chan, line_count);
		OUT_RING  (chan, 0x00000101);
		OUT_RING  (chan, 0x00000000);
		BEGIN_NV04(chan, NvSubCopy, NV_MEMORY_TO_MEMORY_FORMAT_NOP, 1);
		OUT_RING  (chan, 0);

		page_count -= line_count;
		src_offset += (PAGE_SIZE * line_count);
		dst_offset += (PAGE_SIZE * line_count);
	}

	return 0;
}

static int
nouveau_bo_move_prep(struct nouveau_drm *drm, struct ttm_buffer_object *bo,
		     struct ttm_mem_reg *mem)
{
	struct nvkm_mem *old_node = bo->mem.mm_node;
	struct nvkm_mem *new_node = mem->mm_node;
	u64 size = (u64)mem->num_pages << PAGE_SHIFT;
	int ret;

	ret = nvkm_vm_get(drm->client.vm, size, old_node->page_shift,
			  NV_MEM_ACCESS_RW, &old_node->vma[0]);
	if (ret)
		return ret;

	ret = nvkm_vm_get(drm->client.vm, size, new_node->page_shift,
			  NV_MEM_ACCESS_RW, &old_node->vma[1]);
	if (ret) {
		nvkm_vm_put(&old_node->vma[0]);
		return ret;
	}

	nvkm_vm_map(&old_node->vma[0], old_node);
	nvkm_vm_map(&old_node->vma[1], new_node);
	return 0;
}

static int
nouveau_bo_move_m2mf(struct ttm_buffer_object *bo, int evict, bool intr,
		     bool no_wait_gpu, struct ttm_mem_reg *new_mem)
{
	struct nouveau_drm *drm = nouveau_bdev(bo->bdev);
	struct nouveau_channel *chan = drm->ttm.chan;
	struct nouveau_cli *cli = (void *)chan->user.client;
	struct nouveau_fence *fence;
	int ret;

	/* create temporary vmas for the transfer and attach them to the
	 * old nvkm_mem node, these will get cleaned up after ttm has
	 * destroyed the ttm_mem_reg
	 */
	if (drm->device.info.family >= NV_DEVICE_INFO_V0_TESLA) {
		ret = nouveau_bo_move_prep(drm, bo, new_mem);
		if (ret)
			return ret;
	}

	mutex_lock_nested(&cli->mutex, SINGLE_DEPTH_NESTING);
	ret = nouveau_fence_sync(nouveau_bo(bo), chan, true, intr);
	if (ret == 0) {
		ret = drm->ttm.move(chan, bo, &bo->mem, new_mem);
		if (ret == 0) {
			ret = nouveau_fence_new(chan, false, &fence);
			if (ret == 0) {
				ret = ttm_bo_move_accel_cleanup(bo,
								&fence->base,
								evict,
								new_mem);
				nouveau_fence_unref(&fence);
			}
		}
	}
	mutex_unlock(&cli->mutex);
	return ret;
}

void
nouveau_bo_move_init(struct nouveau_drm *drm)
{
	static const struct {
		const char *name;
		int engine;
		s32 oclass;
		int (*exec)(struct nouveau_channel *,
			    struct ttm_buffer_object *,
			    struct ttm_mem_reg *, struct ttm_mem_reg *);
		int (*init)(struct nouveau_channel *, u32 handle);
	} _methods[] = {
		{  "COPY", 4, 0xc1b5, nve0_bo_move_copy, nve0_bo_move_init },
		{  "GRCE", 0, 0xc1b5, nve0_bo_move_copy, nvc0_bo_move_init },
		{  "COPY", 4, 0xc0b5, nve0_bo_move_copy, nve0_bo_move_init },
		{  "GRCE", 0, 0xc0b5, nve0_bo_move_copy, nvc0_bo_move_init },
		{  "COPY", 4, 0xb0b5, nve0_bo_move_copy, nve0_bo_move_init },
		{  "GRCE", 0, 0xb0b5, nve0_bo_move_copy, nvc0_bo_move_init },
		{  "COPY", 4, 0xa0b5, nve0_bo_move_copy, nve0_bo_move_init },
		{  "GRCE", 0, 0xa0b5, nve0_bo_move_copy, nvc0_bo_move_init },
		{ "COPY1", 5, 0x90b8, nvc0_bo_move_copy, nvc0_bo_move_init },
		{ "COPY0", 4, 0x90b5, nvc0_bo_move_copy, nvc0_bo_move_init },
		{  "COPY", 0, 0x85b5, nva3_bo_move_copy, nv50_bo_move_init },
		{ "CRYPT", 0, 0x74c1, nv84_bo_move_exec, nv50_bo_move_init },
		{  "M2MF", 0, 0x9039, nvc0_bo_move_m2mf, nvc0_bo_move_init },
		{  "M2MF", 0, 0x5039, nv50_bo_move_m2mf, nv50_bo_move_init },
		{  "M2MF", 0, 0x0039, nv04_bo_move_m2mf, nv04_bo_move_init },
		{},
		{ "CRYPT", 0, 0x88b4, nv98_bo_move_exec, nv50_bo_move_init },
	}, *mthd = _methods;
	const char *name = "CPU";
	int ret;

	do {
		struct nouveau_channel *chan;

		if (mthd->engine)
			chan = drm->cechan;
		else
			chan = drm->channel;
		if (chan == NULL)
			continue;

		ret = nvif_object_init(&chan->user,
				       mthd->oclass | (mthd->engine << 16),
				       mthd->oclass, NULL, 0,
				       &drm->ttm.copy);
		if (ret == 0) {
			ret = mthd->init(chan, drm->ttm.copy.handle);
			if (ret) {
				nvif_object_fini(&drm->ttm.copy);
				continue;
			}

			drm->ttm.move = mthd->exec;
			drm->ttm.chan = chan;
			name = mthd->name;
			break;
		}
	} while ((++mthd)->exec);

	NV_INFO(drm, "MM: using %s for buffer copies\n", name);
}

static int
nouveau_bo_move_flipd(struct ttm_buffer_object *bo, bool evict, bool intr,
		      bool no_wait_gpu, struct ttm_mem_reg *new_mem)
{
	struct ttm_place placement_memtype = {
		.fpfn = 0,
		.lpfn = 0,
		.flags = TTM_PL_FLAG_TT | TTM_PL_MASK_CACHING
	};
	struct ttm_placement placement;
	struct ttm_mem_reg tmp_mem;
	int ret;

	placement.num_placement = placement.num_busy_placement = 1;
	placement.placement = placement.busy_placement = &placement_memtype;

	tmp_mem = *new_mem;
	tmp_mem.mm_node = NULL;
	ret = ttm_bo_mem_space(bo, &placement, &tmp_mem, intr, no_wait_gpu);
	if (ret)
		return ret;

	ret = ttm_tt_bind(bo->ttm, &tmp_mem);
	if (ret)
		goto out;

	ret = nouveau_bo_move_m2mf(bo, true, intr, no_wait_gpu, &tmp_mem);
	if (ret)
		goto out;

	ret = ttm_bo_move_ttm(bo, true, intr, no_wait_gpu, new_mem);
out:
	ttm_bo_mem_put(bo, &tmp_mem);
	return ret;
}

static int
nouveau_bo_move_flips(struct ttm_buffer_object *bo, bool evict, bool intr,
		      bool no_wait_gpu, struct ttm_mem_reg *new_mem)
{
	struct ttm_place placement_memtype = {
		.fpfn = 0,
		.lpfn = 0,
		.flags = TTM_PL_FLAG_TT | TTM_PL_MASK_CACHING
	};
	struct ttm_placement placement;
	struct ttm_mem_reg tmp_mem;
	int ret;

	placement.num_placement = placement.num_busy_placement = 1;
	placement.placement = placement.busy_placement = &placement_memtype;

	tmp_mem = *new_mem;
	tmp_mem.mm_node = NULL;
	ret = ttm_bo_mem_space(bo, &placement, &tmp_mem, intr, no_wait_gpu);
	if (ret)
		return ret;

	ret = ttm_bo_move_ttm(bo, true, intr, no_wait_gpu, &tmp_mem);
	if (ret)
		goto out;

	ret = nouveau_bo_move_m2mf(bo, true, intr, no_wait_gpu, new_mem);
	if (ret)
		goto out;

out:
	ttm_bo_mem_put(bo, &tmp_mem);
	return ret;
}

static void
nouveau_bo_move_ntfy(struct ttm_buffer_object *bo, struct ttm_mem_reg *new_mem)
{
	struct nouveau_bo *nvbo = nouveau_bo(bo);
	struct nvkm_vma *vma;

	/* ttm can now (stupidly) pass the driver bos it didn't create... */
	if (bo->destroy != nouveau_bo_del_ttm)
		return;

	list_for_each_entry(vma, &nvbo->vma_list, head) {
		if (new_mem && new_mem->mem_type != TTM_PL_SYSTEM &&
			      (new_mem->mem_type == TTM_PL_VRAM ||
			       nvbo->page_shift != vma->vm->mmu->lpg_shift)) {
			nvkm_vm_map(vma, new_mem->mm_node);
		} else {
			nvkm_vm_unmap(vma);
		}
	}
}

static int
nouveau_bo_vm_bind(struct ttm_buffer_object *bo, struct ttm_mem_reg *new_mem,
		   struct nouveau_drm_tile **new_tile)
{
	struct nouveau_drm *drm = nouveau_bdev(bo->bdev);
	struct drm_device *dev = drm->dev;
	struct nouveau_bo *nvbo = nouveau_bo(bo);
	u64 offset = new_mem->start << PAGE_SHIFT;

	*new_tile = NULL;
	if (new_mem->mem_type != TTM_PL_VRAM)
		return 0;

	if (drm->device.info.family >= NV_DEVICE_INFO_V0_CELSIUS) {
		*new_tile = nv10_bo_set_tiling(dev, offset, new_mem->size,
						nvbo->tile_mode,
						nvbo->tile_flags);
	}

	return 0;
}

static void
nouveau_bo_vm_cleanup(struct ttm_buffer_object *bo,
		      struct nouveau_drm_tile *new_tile,
		      struct nouveau_drm_tile **old_tile)
{
	struct nouveau_drm *drm = nouveau_bdev(bo->bdev);
	struct drm_device *dev = drm->dev;
	struct fence *fence = reservation_object_get_excl(bo->resv);

	nv10_bo_put_tile_region(dev, *old_tile, fence);
	*old_tile = new_tile;
}

static int
nouveau_bo_move(struct ttm_buffer_object *bo, bool evict, bool intr,
		bool no_wait_gpu, struct ttm_mem_reg *new_mem)
{
	struct nouveau_drm *drm = nouveau_bdev(bo->bdev);
	struct nouveau_bo *nvbo = nouveau_bo(bo);
	struct ttm_mem_reg *old_mem = &bo->mem;
	struct nouveau_drm_tile *new_tile = NULL;
	int ret = 0;

	ret = ttm_bo_wait(bo, intr, no_wait_gpu);
	if (ret)
		return ret;

	if (nvbo->pin_refcnt)
		NV_WARN(drm, "Moving pinned object %p!\n", nvbo);

	if (drm->device.info.family < NV_DEVICE_INFO_V0_TESLA) {
		ret = nouveau_bo_vm_bind(bo, new_mem, &new_tile);
		if (ret)
			return ret;
	}

	/* Fake bo copy. */
	if (old_mem->mem_type == TTM_PL_SYSTEM && !bo->ttm) {
		BUG_ON(bo->mem.mm_node != NULL);
		bo->mem = *new_mem;
		new_mem->mm_node = NULL;
		goto out;
	}

	/* Hardware assisted copy. */
	if (drm->ttm.move) {
		if (new_mem->mem_type == TTM_PL_SYSTEM)
			ret = nouveau_bo_move_flipd(bo, evict, intr,
						    no_wait_gpu, new_mem);
		else if (old_mem->mem_type == TTM_PL_SYSTEM)
			ret = nouveau_bo_move_flips(bo, evict, intr,
						    no_wait_gpu, new_mem);
		else
			ret = nouveau_bo_move_m2mf(bo, evict, intr,
						   no_wait_gpu, new_mem);
		if (!ret)
			goto out;
	}

	/* Fallback to software copy. */
	ret = ttm_bo_wait(bo, intr, no_wait_gpu);
	if (ret == 0)
		ret = ttm_bo_move_memcpy(bo, evict, intr, no_wait_gpu, new_mem);

out:
	if (drm->device.info.family < NV_DEVICE_INFO_V0_TESLA) {
		if (ret)
			nouveau_bo_vm_cleanup(bo, NULL, &new_tile);
		else
			nouveau_bo_vm_cleanup(bo, new_tile, &nvbo->tile);
	}

	return ret;
}

static int
nouveau_bo_verify_access(struct ttm_buffer_object *bo, struct file *filp)
{
	struct nouveau_bo *nvbo = nouveau_bo(bo);

	return drm_vma_node_verify_access(&nvbo->gem.vma_node, filp);
}

static int
nouveau_ttm_io_mem_reserve(struct ttm_bo_device *bdev, struct ttm_mem_reg *mem)
{
	struct ttm_mem_type_manager *man = &bdev->man[mem->mem_type];
	struct nouveau_drm *drm = nouveau_bdev(bdev);
	struct nvkm_device *device = nvxx_device(&drm->device);
	struct nvkm_mem *node = mem->mm_node;
	int ret;

	mem->bus.addr = NULL;
	mem->bus.offset = 0;
	mem->bus.size = mem->num_pages << PAGE_SHIFT;
	mem->bus.base = 0;
	mem->bus.is_iomem = false;
	if (!(man->flags & TTM_MEMTYPE_FLAG_MAPPABLE))
		return -EINVAL;
	switch (mem->mem_type) {
	case TTM_PL_SYSTEM:
		/* System memory */
		return 0;
	case TTM_PL_TT:
#if IS_ENABLED(CONFIG_AGP)
		if (drm->agp.bridge) {
			mem->bus.offset = mem->start << PAGE_SHIFT;
			mem->bus.base = drm->agp.base;
			mem->bus.is_iomem = !drm->agp.cma;
		}
#endif
		if (drm->device.info.family < NV_DEVICE_INFO_V0_TESLA || !node->memtype)
			/* untiled */
			break;
		/* fallthrough, tiled memory */
	case TTM_PL_VRAM:
		mem->bus.offset = mem->start << PAGE_SHIFT;
		mem->bus.base = device->func->resource_addr(device, 1);
		mem->bus.is_iomem = true;
		if (drm->device.info.family >= NV_DEVICE_INFO_V0_TESLA) {
			struct nvkm_bar *bar = nvxx_bar(&drm->device);
			int page_shift = 12;
			if (drm->device.info.family >= NV_DEVICE_INFO_V0_FERMI)
				page_shift = node->page_shift;

			ret = nvkm_bar_umap(bar, node->size << 12, page_shift,
					    &node->bar_vma);
			if (ret)
				return ret;

			nvkm_vm_map(&node->bar_vma, node);
			mem->bus.offset = node->bar_vma.offset;
		}
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static void
nouveau_ttm_io_mem_free(struct ttm_bo_device *bdev, struct ttm_mem_reg *mem)
{
	struct nvkm_mem *node = mem->mm_node;

	if (!node->bar_vma.node)
		return;

	nvkm_vm_unmap(&node->bar_vma);
	nvkm_vm_put(&node->bar_vma);
}

static int
nouveau_ttm_fault_reserve_notify(struct ttm_buffer_object *bo)
{
	struct nouveau_drm *drm = nouveau_bdev(bo->bdev);
	struct nouveau_bo *nvbo = nouveau_bo(bo);
	struct nvkm_device *device = nvxx_device(&drm->device);
	u32 mappable = device->func->resource_size(device, 1) >> PAGE_SHIFT;
	int i, ret;

	/* as long as the bo isn't in vram, and isn't tiled, we've got
	 * nothing to do here.
	 */
	if (bo->mem.mem_type != TTM_PL_VRAM) {
		if (drm->device.info.family < NV_DEVICE_INFO_V0_TESLA ||
		    !nouveau_bo_tile_layout(nvbo))
			return 0;

		if (bo->mem.mem_type == TTM_PL_SYSTEM) {
			nouveau_bo_placement_set(nvbo, TTM_PL_TT, 0);

			ret = nouveau_bo_validate(nvbo, false, false);
			if (ret)
				return ret;
		}
		return 0;
	}

	/* make sure bo is in mappable vram */
	if (drm->device.info.family >= NV_DEVICE_INFO_V0_TESLA ||
	    bo->mem.start + bo->mem.num_pages < mappable)
		return 0;

	for (i = 0; i < nvbo->placement.num_placement; ++i) {
		nvbo->placements[i].fpfn = 0;
		nvbo->placements[i].lpfn = mappable;
	}

	for (i = 0; i < nvbo->placement.num_busy_placement; ++i) {
		nvbo->busy_placements[i].fpfn = 0;
		nvbo->busy_placements[i].lpfn = mappable;
	}

	nouveau_bo_placement_set(nvbo, TTM_PL_FLAG_VRAM, 0);
	return nouveau_bo_validate(nvbo, false, false);
}

static int
nouveau_ttm_tt_populate(struct ttm_tt *ttm)
{
	struct ttm_dma_tt *ttm_dma = (void *)ttm;
	struct nouveau_drm *drm;
	struct nvkm_device *device;
	struct drm_device *dev;
	struct device *pdev;
	unsigned i;
	int r;
	bool slave = !!(ttm->page_flags & TTM_PAGE_FLAG_SG);

	if (ttm->state != tt_unpopulated)
		return 0;

	if (slave && ttm->sg) {
		/* make userspace faulting work */
		drm_prime_sg_to_page_addr_arrays(ttm->sg, ttm->pages,
						 ttm_dma->dma_address, ttm->num_pages);
		ttm->state = tt_unbound;
		return 0;
	}

	drm = nouveau_bdev(ttm->bdev);
	device = nvxx_device(&drm->device);
	dev = drm->dev;
	pdev = device->dev;

#if IS_ENABLED(CONFIG_AGP)
	if (drm->agp.bridge) {
		return ttm_agp_tt_populate(ttm);
	}
#endif

#if IS_ENABLED(CONFIG_SWIOTLB) && IS_ENABLED(CONFIG_X86)
	if (swiotlb_nr_tbl()) {
		return ttm_dma_populate((void *)ttm, dev->dev);
	}
#endif

	r = ttm_pool_populate(ttm);
	if (r) {
		return r;
	}

	for (i = 0; i < ttm->num_pages; i++) {
		dma_addr_t addr;

		addr = dma_map_page(pdev, ttm->pages[i], 0, PAGE_SIZE,
				    DMA_BIDIRECTIONAL);

		if (dma_mapping_error(pdev, addr)) {
			while (i--) {
				dma_unmap_page(pdev, ttm_dma->dma_address[i],
					       PAGE_SIZE, DMA_BIDIRECTIONAL);
				ttm_dma->dma_address[i] = 0;
			}
			ttm_pool_unpopulate(ttm);
			return -EFAULT;
		}

		ttm_dma->dma_address[i] = addr;
	}
	return 0;
}

static void
nouveau_ttm_tt_unpopulate(struct ttm_tt *ttm)
{
	struct ttm_dma_tt *ttm_dma = (void *)ttm;
	struct nouveau_drm *drm;
	struct nvkm_device *device;
	struct drm_device *dev;
	struct device *pdev;
	unsigned i;
	bool slave = !!(ttm->page_flags & TTM_PAGE_FLAG_SG);

	if (slave)
		return;

	drm = nouveau_bdev(ttm->bdev);
	device = nvxx_device(&drm->device);
	dev = drm->dev;
	pdev = device->dev;

#if IS_ENABLED(CONFIG_AGP)
	if (drm->agp.bridge) {
		ttm_agp_tt_unpopulate(ttm);
		return;
	}
#endif

#if IS_ENABLED(CONFIG_SWIOTLB) && IS_ENABLED(CONFIG_X86)
	if (swiotlb_nr_tbl()) {
		ttm_dma_unpopulate((void *)ttm, dev->dev);
		return;
	}
#endif

	for (i = 0; i < ttm->num_pages; i++) {
		if (ttm_dma->dma_address[i]) {
			dma_unmap_page(pdev, ttm_dma->dma_address[i], PAGE_SIZE,
				       DMA_BIDIRECTIONAL);
		}
	}

	ttm_pool_unpopulate(ttm);
}

void
nouveau_bo_fence(struct nouveau_bo *nvbo, struct nouveau_fence *fence, bool exclusive)
{
	struct reservation_object *resv = nvbo->bo.resv;

	if (exclusive)
		reservation_object_add_excl_fence(resv, &fence->base);
	else if (fence)
		reservation_object_add_shared_fence(resv, &fence->base);
}

struct ttm_bo_driver nouveau_bo_driver = {
	.ttm_tt_create = &nouveau_ttm_tt_create,
	.ttm_tt_populate = &nouveau_ttm_tt_populate,
	.ttm_tt_unpopulate = &nouveau_ttm_tt_unpopulate,
	.invalidate_caches = nouveau_bo_invalidate_caches,
	.init_mem_type = nouveau_bo_init_mem_type,
	.evict_flags = nouveau_bo_evict_flags,
	.move_notify = nouveau_bo_move_ntfy,
	.move = nouveau_bo_move,
	.verify_access = nouveau_bo_verify_access,
	.fault_reserve_notify = &nouveau_ttm_fault_reserve_notify,
	.io_mem_reserve = &nouveau_ttm_io_mem_reserve,
	.io_mem_free = &nouveau_ttm_io_mem_free,
	.lru_tail = &ttm_bo_default_lru_tail,
	.swap_lru_tail = &ttm_bo_default_swap_lru_tail,
};

struct nvkm_vma *
nouveau_bo_vma_find(struct nouveau_bo *nvbo, struct nvkm_vm *vm)
{
	struct nvkm_vma *vma;
	list_for_each_entry(vma, &nvbo->vma_list, head) {
		if (vma->vm == vm)
			return vma;
	}

	return NULL;
}

int
nouveau_bo_vma_add(struct nouveau_bo *nvbo, struct nvkm_vm *vm,
		   struct nvkm_vma *vma)
{
	const u32 size = nvbo->bo.mem.num_pages << PAGE_SHIFT;
	int ret;

	ret = nvkm_vm_get(vm, size, nvbo->page_shift,
			     NV_MEM_ACCESS_RW, vma);
	if (ret)
		return ret;

	if ( nvbo->bo.mem.mem_type != TTM_PL_SYSTEM &&
	    (nvbo->bo.mem.mem_type == TTM_PL_VRAM ||
	     nvbo->page_shift != vma->vm->mmu->lpg_shift))
		nvkm_vm_map(vma, nvbo->bo.mem.mm_node);

	list_add_tail(&vma->head, &nvbo->vma_list);
	vma->refcount = 1;
	return 0;
}

void
nouveau_bo_vma_del(struct nouveau_bo *nvbo, struct nvkm_vma *vma)
{
	if (vma->node) {
		if (nvbo->bo.mem.mem_type != TTM_PL_SYSTEM)
			nvkm_vm_unmap(vma);
		nvkm_vm_put(vma);
		list_del(&vma->head);
	}
}
