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
#include "nouveau_chan.h"
#include "nouveau_fence.h"

#include "nouveau_bo.h"
#include "nouveau_ttm.h"
#include "nouveau_gem.h"
#include "nouveau_mem.h"
#include "nouveau_vmm.h"

#include <nvif/class.h>
#include <nvif/if500b.h>
#include <nvif/if900b.h>

static int nouveau_ttm_tt_bind(struct ttm_bo_device *bdev, struct ttm_tt *ttm,
			       struct ttm_resource *reg);

/*
 * NV10-NV40 tiling helpers
 */

static void
nv10_bo_update_tile_region(struct drm_device *dev, struct nouveau_drm_tile *reg,
			   u32 addr, u32 size, u32 pitch, u32 flags)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	int i = reg - drm->tile.reg;
	struct nvkm_fb *fb = nvxx_fb(&drm->client.device);
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
			struct dma_fence *fence)
{
	struct nouveau_drm *drm = nouveau_drm(dev);

	if (tile) {
		spin_lock(&drm->tile.lock);
		tile->fence = (struct nouveau_fence *)dma_fence_get(fence);
		tile->used = false;
		spin_unlock(&drm->tile.lock);
	}
}

static struct nouveau_drm_tile *
nv10_bo_set_tiling(struct drm_device *dev, u32 addr,
		   u32 size, u32 pitch, u32 zeta)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nvkm_fb *fb = nvxx_fb(&drm->client.device);
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
		nv10_bo_update_tile_region(dev, found, addr, size, pitch, zeta);
	return found;
}

static void
nouveau_bo_del_ttm(struct ttm_buffer_object *bo)
{
	struct nouveau_drm *drm = nouveau_bdev(bo->bdev);
	struct drm_device *dev = drm->dev;
	struct nouveau_bo *nvbo = nouveau_bo(bo);

	WARN_ON(nvbo->pin_refcnt > 0);
	nouveau_bo_del_io_reserve_lru(bo);
	nv10_bo_put_tile_region(dev, nvbo->tile, NULL);

	/*
	 * If nouveau_bo_new() allocated this buffer, the GEM object was never
	 * initialized, so don't attempt to release it.
	 */
	if (bo->base.dev)
		drm_gem_object_release(&bo->base);

	kfree(nvbo);
}

static inline u64
roundup_64(u64 x, u32 y)
{
	x += y - 1;
	do_div(x, y);
	return x * y;
}

static void
nouveau_bo_fixup_align(struct nouveau_bo *nvbo, int *align, u64 *size)
{
	struct nouveau_drm *drm = nouveau_bdev(nvbo->bo.bdev);
	struct nvif_device *device = &drm->client.device;

	if (device->info.family < NV_DEVICE_INFO_V0_TESLA) {
		if (nvbo->mode) {
			if (device->info.chipset >= 0x40) {
				*align = 65536;
				*size = roundup_64(*size, 64 * nvbo->mode);

			} else if (device->info.chipset >= 0x30) {
				*align = 32768;
				*size = roundup_64(*size, 64 * nvbo->mode);

			} else if (device->info.chipset >= 0x20) {
				*align = 16384;
				*size = roundup_64(*size, 64 * nvbo->mode);

			} else if (device->info.chipset >= 0x10) {
				*align = 16384;
				*size = roundup_64(*size, 32 * nvbo->mode);
			}
		}
	} else {
		*size = roundup_64(*size, (1 << nvbo->page));
		*align = max((1 <<  nvbo->page), *align);
	}

	*size = roundup_64(*size, PAGE_SIZE);
}

struct nouveau_bo *
nouveau_bo_alloc(struct nouveau_cli *cli, u64 *size, int *align, u32 domain,
		 u32 tile_mode, u32 tile_flags)
{
	struct nouveau_drm *drm = cli->drm;
	struct nouveau_bo *nvbo;
	struct nvif_mmu *mmu = &cli->mmu;
	struct nvif_vmm *vmm = cli->svm.cli ? &cli->svm.vmm : &cli->vmm.vmm;
	int i, pi = -1;

	if (!*size) {
		NV_WARN(drm, "skipped size %016llx\n", *size);
		return ERR_PTR(-EINVAL);
	}

	nvbo = kzalloc(sizeof(struct nouveau_bo), GFP_KERNEL);
	if (!nvbo)
		return ERR_PTR(-ENOMEM);
	INIT_LIST_HEAD(&nvbo->head);
	INIT_LIST_HEAD(&nvbo->entry);
	INIT_LIST_HEAD(&nvbo->vma_list);
	nvbo->bo.bdev = &drm->ttm.bdev;

	/* This is confusing, and doesn't actually mean we want an uncached
	 * mapping, but is what NOUVEAU_GEM_DOMAIN_COHERENT gets translated
	 * into in nouveau_gem_new().
	 */
	if (domain & NOUVEAU_GEM_DOMAIN_COHERENT) {
		/* Determine if we can get a cache-coherent map, forcing
		 * uncached mapping if we can't.
		 */
		if (!nouveau_drm_use_coherent_gpu_mapping(drm))
			nvbo->force_coherent = true;
	}

	if (cli->device.info.family >= NV_DEVICE_INFO_V0_FERMI) {
		nvbo->kind = (tile_flags & 0x0000ff00) >> 8;
		if (!nvif_mmu_kind_valid(mmu, nvbo->kind)) {
			kfree(nvbo);
			return ERR_PTR(-EINVAL);
		}

		nvbo->comp = mmu->kind[nvbo->kind] != nvbo->kind;
	} else
	if (cli->device.info.family >= NV_DEVICE_INFO_V0_TESLA) {
		nvbo->kind = (tile_flags & 0x00007f00) >> 8;
		nvbo->comp = (tile_flags & 0x00030000) >> 16;
		if (!nvif_mmu_kind_valid(mmu, nvbo->kind)) {
			kfree(nvbo);
			return ERR_PTR(-EINVAL);
		}
	} else {
		nvbo->zeta = (tile_flags & 0x00000007);
	}
	nvbo->mode = tile_mode;
	nvbo->contig = !(tile_flags & NOUVEAU_GEM_TILE_NONCONTIG);

	/* Determine the desirable target GPU page size for the buffer. */
	for (i = 0; i < vmm->page_nr; i++) {
		/* Because we cannot currently allow VMM maps to fail
		 * during buffer migration, we need to determine page
		 * size for the buffer up-front, and pre-allocate its
		 * page tables.
		 *
		 * Skip page sizes that can't support needed domains.
		 */
		if (cli->device.info.family > NV_DEVICE_INFO_V0_CURIE &&
		    (domain & NOUVEAU_GEM_DOMAIN_VRAM) && !vmm->page[i].vram)
			continue;
		if ((domain & NOUVEAU_GEM_DOMAIN_GART) &&
		    (!vmm->page[i].host || vmm->page[i].shift > PAGE_SHIFT))
			continue;

		/* Select this page size if it's the first that supports
		 * the potential memory domains, or when it's compatible
		 * with the requested compression settings.
		 */
		if (pi < 0 || !nvbo->comp || vmm->page[i].comp)
			pi = i;

		/* Stop once the buffer is larger than the current page size. */
		if (*size >= 1ULL << vmm->page[i].shift)
			break;
	}

	if (WARN_ON(pi < 0))
		return ERR_PTR(-EINVAL);

	/* Disable compression if suitable settings couldn't be found. */
	if (nvbo->comp && !vmm->page[pi].comp) {
		if (mmu->object.oclass >= NVIF_CLASS_MMU_GF100)
			nvbo->kind = mmu->kind[nvbo->kind];
		nvbo->comp = 0;
	}
	nvbo->page = vmm->page[pi].shift;

	nouveau_bo_fixup_align(nvbo, align, size);

	return nvbo;
}

int
nouveau_bo_init(struct nouveau_bo *nvbo, u64 size, int align, u32 domain,
		struct sg_table *sg, struct dma_resv *robj)
{
	int type = sg ? ttm_bo_type_sg : ttm_bo_type_device;
	size_t acc_size;
	int ret;

	acc_size = ttm_bo_dma_acc_size(nvbo->bo.bdev, size, sizeof(*nvbo));

	nvbo->bo.mem.num_pages = size >> PAGE_SHIFT;
	nouveau_bo_placement_set(nvbo, domain, 0);
	INIT_LIST_HEAD(&nvbo->io_reserve_lru);

	ret = ttm_bo_init(nvbo->bo.bdev, &nvbo->bo, size, type,
			  &nvbo->placement, align >> PAGE_SHIFT, false,
			  acc_size, sg, robj, nouveau_bo_del_ttm);
	if (ret) {
		/* ttm will call nouveau_bo_del_ttm if it fails.. */
		return ret;
	}

	return 0;
}

int
nouveau_bo_new(struct nouveau_cli *cli, u64 size, int align,
	       uint32_t domain, uint32_t tile_mode, uint32_t tile_flags,
	       struct sg_table *sg, struct dma_resv *robj,
	       struct nouveau_bo **pnvbo)
{
	struct nouveau_bo *nvbo;
	int ret;

	nvbo = nouveau_bo_alloc(cli, &size, &align, domain, tile_mode,
				tile_flags);
	if (IS_ERR(nvbo))
		return PTR_ERR(nvbo);

	ret = nouveau_bo_init(nvbo, size, align, domain, sg, robj);
	if (ret)
		return ret;

	*pnvbo = nvbo;
	return 0;
}

static void
set_placement_list(struct nouveau_drm *drm, struct ttm_place *pl, unsigned *n,
		   uint32_t domain, uint32_t flags)
{
	*n = 0;

	if (domain & NOUVEAU_GEM_DOMAIN_VRAM) {
		struct nvif_mmu *mmu = &drm->client.mmu;

		pl[*n].mem_type = TTM_PL_VRAM;
		pl[*n].flags = flags & ~TTM_PL_FLAG_CACHED;

		/* Some BARs do not support being ioremapped WC */
		if (drm->client.device.info.family >= NV_DEVICE_INFO_V0_TESLA &&
		    mmu->type[drm->ttm.type_vram].type & NVIF_MEM_UNCACHED)
			pl[*n].flags &= ~TTM_PL_FLAG_WC;

		(*n)++;
	}
	if (domain & NOUVEAU_GEM_DOMAIN_GART) {
		pl[*n].mem_type = TTM_PL_TT;
		pl[*n].flags = flags;

		if (drm->agp.bridge)
			pl[*n].flags &= ~TTM_PL_FLAG_CACHED;

		(*n)++;
	}
	if (domain & NOUVEAU_GEM_DOMAIN_CPU) {
		pl[*n].mem_type = TTM_PL_SYSTEM;
		pl[(*n)++].flags = flags;
	}
}

static void
set_placement_range(struct nouveau_bo *nvbo, uint32_t domain)
{
	struct nouveau_drm *drm = nouveau_bdev(nvbo->bo.bdev);
	u32 vram_pages = drm->client.device.info.ram_size >> PAGE_SHIFT;
	unsigned i, fpfn, lpfn;

	if (drm->client.device.info.family == NV_DEVICE_INFO_V0_CELSIUS &&
	    nvbo->mode && (domain & NOUVEAU_GEM_DOMAIN_VRAM) &&
	    nvbo->bo.mem.num_pages < vram_pages / 4) {
		/*
		 * Make sure that the color and depth buffers are handled
		 * by independent memory controller units. Up to a 9x
		 * speed up when alpha-blending and depth-test are enabled
		 * at the same time.
		 */
		if (nvbo->zeta) {
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
nouveau_bo_placement_set(struct nouveau_bo *nvbo, uint32_t domain,
			 uint32_t busy)
{
	struct nouveau_drm *drm = nouveau_bdev(nvbo->bo.bdev);
	struct ttm_placement *pl = &nvbo->placement;
	uint32_t flags = (nvbo->force_coherent ? TTM_PL_FLAG_UNCACHED :
						 TTM_PL_MASK_CACHING) |
			 (nvbo->pin_refcnt ? TTM_PL_FLAG_NO_EVICT : 0);

	pl->placement = nvbo->placements;
	set_placement_list(drm, nvbo->placements, &pl->num_placement,
			   domain, flags);

	pl->busy_placement = nvbo->busy_placements;
	set_placement_list(drm, nvbo->busy_placements, &pl->num_busy_placement,
			   domain | busy, flags);

	set_placement_range(nvbo, domain);
}

int
nouveau_bo_pin(struct nouveau_bo *nvbo, uint32_t domain, bool contig)
{
	struct nouveau_drm *drm = nouveau_bdev(nvbo->bo.bdev);
	struct ttm_buffer_object *bo = &nvbo->bo;
	bool force = false, evict = false;
	int ret;

	ret = ttm_bo_reserve(bo, false, false, NULL);
	if (ret)
		return ret;

	if (drm->client.device.info.family >= NV_DEVICE_INFO_V0_TESLA &&
	    domain == NOUVEAU_GEM_DOMAIN_VRAM && contig) {
		if (!nvbo->contig) {
			nvbo->contig = true;
			force = true;
			evict = true;
		}
	}

	if (nvbo->pin_refcnt) {
		bool error = evict;

		switch (bo->mem.mem_type) {
		case TTM_PL_VRAM:
			error |= !(domain & NOUVEAU_GEM_DOMAIN_VRAM);
			break;
		case TTM_PL_TT:
			error |= !(domain & NOUVEAU_GEM_DOMAIN_GART);
		default:
			break;
		}

		if (error) {
			NV_ERROR(drm, "bo %p pinned elsewhere: "
				      "0x%08x vs 0x%08x\n", bo,
				 bo->mem.mem_type, domain);
			ret = -EBUSY;
		}
		nvbo->pin_refcnt++;
		goto out;
	}

	if (evict) {
		nouveau_bo_placement_set(nvbo, NOUVEAU_GEM_DOMAIN_GART, 0);
		ret = nouveau_bo_validate(nvbo, false, false);
		if (ret)
			goto out;
	}

	nvbo->pin_refcnt++;
	nouveau_bo_placement_set(nvbo, domain, 0);

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
		nvbo->contig = false;
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

	switch (bo->mem.mem_type) {
	case TTM_PL_VRAM:
		nouveau_bo_placement_set(nvbo, NOUVEAU_GEM_DOMAIN_VRAM, 0);
		break;
	case TTM_PL_TT:
		nouveau_bo_placement_set(nvbo, NOUVEAU_GEM_DOMAIN_GART, 0);
		break;
	default:
		break;
	}

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
	struct ttm_dma_tt *ttm_dma = (struct ttm_dma_tt *)nvbo->bo.ttm;
	int i;

	if (!ttm_dma)
		return;

	/* Don't waste time looping if the object is coherent */
	if (nvbo->force_coherent)
		return;

	for (i = 0; i < ttm_dma->ttm.num_pages; i++)
		dma_sync_single_for_device(drm->dev->dev,
					   ttm_dma->dma_address[i],
					   PAGE_SIZE, DMA_TO_DEVICE);
}

void
nouveau_bo_sync_for_cpu(struct nouveau_bo *nvbo)
{
	struct nouveau_drm *drm = nouveau_bdev(nvbo->bo.bdev);
	struct ttm_dma_tt *ttm_dma = (struct ttm_dma_tt *)nvbo->bo.ttm;
	int i;

	if (!ttm_dma)
		return;

	/* Don't waste time looping if the object is coherent */
	if (nvbo->force_coherent)
		return;

	for (i = 0; i < ttm_dma->ttm.num_pages; i++)
		dma_sync_single_for_cpu(drm->dev->dev, ttm_dma->dma_address[i],
					PAGE_SIZE, DMA_FROM_DEVICE);
}

void nouveau_bo_add_io_reserve_lru(struct ttm_buffer_object *bo)
{
	struct nouveau_drm *drm = nouveau_bdev(bo->bdev);
	struct nouveau_bo *nvbo = nouveau_bo(bo);

	mutex_lock(&drm->ttm.io_reserve_mutex);
	list_move_tail(&nvbo->io_reserve_lru, &drm->ttm.io_reserve_lru);
	mutex_unlock(&drm->ttm.io_reserve_mutex);
}

void nouveau_bo_del_io_reserve_lru(struct ttm_buffer_object *bo)
{
	struct nouveau_drm *drm = nouveau_bdev(bo->bdev);
	struct nouveau_bo *nvbo = nouveau_bo(bo);

	mutex_lock(&drm->ttm.io_reserve_mutex);
	list_del_init(&nvbo->io_reserve_lru);
	mutex_unlock(&drm->ttm.io_reserve_mutex);
}

int
nouveau_bo_validate(struct nouveau_bo *nvbo, bool interruptible,
		    bool no_wait_gpu)
{
	struct ttm_operation_ctx ctx = { interruptible, no_wait_gpu };
	int ret;

	ret = ttm_bo_validate(&nvbo->bo, &nvbo->placement, &ctx);
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
nouveau_ttm_tt_create(struct ttm_buffer_object *bo, uint32_t page_flags)
{
#if IS_ENABLED(CONFIG_AGP)
	struct nouveau_drm *drm = nouveau_bdev(bo->bdev);

	if (drm->agp.bridge) {
		return ttm_agp_tt_create(bo, drm->agp.bridge, page_flags);
	}
#endif

	return nouveau_sgdma_create_ttm(bo, page_flags);
}

static int
nouveau_ttm_tt_bind(struct ttm_bo_device *bdev, struct ttm_tt *ttm,
		    struct ttm_resource *reg)
{
#if IS_ENABLED(CONFIG_AGP)
	struct nouveau_drm *drm = nouveau_bdev(bdev);
#endif
	if (!reg)
		return -EINVAL;
#if IS_ENABLED(CONFIG_AGP)
	if (drm->agp.bridge)
		return ttm_agp_bind(ttm, reg);
#endif
	return nouveau_sgdma_bind(bdev, ttm, reg);
}

static void
nouveau_ttm_tt_unbind(struct ttm_bo_device *bdev, struct ttm_tt *ttm)
{
#if IS_ENABLED(CONFIG_AGP)
	struct nouveau_drm *drm = nouveau_bdev(bdev);

	if (drm->agp.bridge) {
		ttm_agp_unbind(ttm);
		return;
	}
#endif
	nouveau_sgdma_unbind(bdev, ttm);
}

static void
nouveau_bo_evict_flags(struct ttm_buffer_object *bo, struct ttm_placement *pl)
{
	struct nouveau_bo *nvbo = nouveau_bo(bo);

	switch (bo->mem.mem_type) {
	case TTM_PL_VRAM:
		nouveau_bo_placement_set(nvbo, NOUVEAU_GEM_DOMAIN_GART,
					 NOUVEAU_GEM_DOMAIN_CPU);
		break;
	default:
		nouveau_bo_placement_set(nvbo, NOUVEAU_GEM_DOMAIN_CPU, 0);
		break;
	}

	*pl = nvbo->placement;
}

static int
nouveau_bo_move_prep(struct nouveau_drm *drm, struct ttm_buffer_object *bo,
		     struct ttm_resource *reg)
{
	struct nouveau_mem *old_mem = nouveau_mem(&bo->mem);
	struct nouveau_mem *new_mem = nouveau_mem(reg);
	struct nvif_vmm *vmm = &drm->client.vmm.vmm;
	int ret;

	ret = nvif_vmm_get(vmm, LAZY, false, old_mem->mem.page, 0,
			   old_mem->mem.size, &old_mem->vma[0]);
	if (ret)
		return ret;

	ret = nvif_vmm_get(vmm, LAZY, false, new_mem->mem.page, 0,
			   new_mem->mem.size, &old_mem->vma[1]);
	if (ret)
		goto done;

	ret = nouveau_mem_map(old_mem, vmm, &old_mem->vma[0]);
	if (ret)
		goto done;

	ret = nouveau_mem_map(new_mem, vmm, &old_mem->vma[1]);
done:
	if (ret) {
		nvif_vmm_put(vmm, &old_mem->vma[1]);
		nvif_vmm_put(vmm, &old_mem->vma[0]);
	}
	return 0;
}

static int
nouveau_bo_move_m2mf(struct ttm_buffer_object *bo, int evict, bool intr,
		     bool no_wait_gpu, struct ttm_resource *new_reg)
{
	struct nouveau_drm *drm = nouveau_bdev(bo->bdev);
	struct nouveau_channel *chan = drm->ttm.chan;
	struct nouveau_cli *cli = (void *)chan->user.client;
	struct nouveau_fence *fence;
	int ret;

	/* create temporary vmas for the transfer and attach them to the
	 * old nvkm_mem node, these will get cleaned up after ttm has
	 * destroyed the ttm_resource
	 */
	if (drm->client.device.info.family >= NV_DEVICE_INFO_V0_TESLA) {
		ret = nouveau_bo_move_prep(drm, bo, new_reg);
		if (ret)
			return ret;
	}

	mutex_lock_nested(&cli->mutex, SINGLE_DEPTH_NESTING);
	ret = nouveau_fence_sync(nouveau_bo(bo), chan, true, intr);
	if (ret == 0) {
		ret = drm->ttm.move(chan, bo, &bo->mem, new_reg);
		if (ret == 0) {
			ret = nouveau_fence_new(chan, false, &fence);
			if (ret == 0) {
				ret = ttm_bo_move_accel_cleanup(bo,
								&fence->base,
								evict, false,
								new_reg);
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
	static const struct _method_table {
		const char *name;
		int engine;
		s32 oclass;
		int (*exec)(struct nouveau_channel *,
			    struct ttm_buffer_object *,
			    struct ttm_resource *, struct ttm_resource *);
		int (*init)(struct nouveau_channel *, u32 handle);
	} _methods[] = {
		{  "COPY", 4, 0xc5b5, nve0_bo_move_copy, nve0_bo_move_init },
		{  "GRCE", 0, 0xc5b5, nve0_bo_move_copy, nvc0_bo_move_init },
		{  "COPY", 4, 0xc3b5, nve0_bo_move_copy, nve0_bo_move_init },
		{  "GRCE", 0, 0xc3b5, nve0_bo_move_copy, nvc0_bo_move_init },
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
	};
	const struct _method_table *mthd = _methods;
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

		ret = nvif_object_ctor(&chan->user, "ttmBoMove",
				       mthd->oclass | (mthd->engine << 16),
				       mthd->oclass, NULL, 0,
				       &drm->ttm.copy);
		if (ret == 0) {
			ret = mthd->init(chan, drm->ttm.copy.handle);
			if (ret) {
				nvif_object_dtor(&drm->ttm.copy);
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
		      bool no_wait_gpu, struct ttm_resource *new_reg)
{
	struct ttm_operation_ctx ctx = { intr, no_wait_gpu };
	struct ttm_place placement_memtype = {
		.fpfn = 0,
		.lpfn = 0,
		.mem_type = TTM_PL_TT,
		.flags = TTM_PL_MASK_CACHING
	};
	struct ttm_placement placement;
	struct ttm_resource tmp_reg;
	int ret;

	placement.num_placement = placement.num_busy_placement = 1;
	placement.placement = placement.busy_placement = &placement_memtype;

	tmp_reg = *new_reg;
	tmp_reg.mm_node = NULL;
	ret = ttm_bo_mem_space(bo, &placement, &tmp_reg, &ctx);
	if (ret)
		return ret;

	ret = ttm_tt_populate(bo->bdev, bo->ttm, &ctx);
	if (ret)
		goto out;

	ret = nouveau_ttm_tt_bind(bo->bdev, bo->ttm, &tmp_reg);
	if (ret)
		goto out;

	ret = nouveau_bo_move_m2mf(bo, true, intr, no_wait_gpu, &tmp_reg);
	if (ret)
		goto out;

	ret = ttm_bo_move_ttm(bo, &ctx, new_reg);
out:
	ttm_resource_free(bo, &tmp_reg);
	return ret;
}

static int
nouveau_bo_move_flips(struct ttm_buffer_object *bo, bool evict, bool intr,
		      bool no_wait_gpu, struct ttm_resource *new_reg)
{
	struct ttm_operation_ctx ctx = { intr, no_wait_gpu };
	struct ttm_place placement_memtype = {
		.fpfn = 0,
		.lpfn = 0,
		.mem_type = TTM_PL_TT,
		.flags = TTM_PL_MASK_CACHING
	};
	struct ttm_placement placement;
	struct ttm_resource tmp_reg;
	int ret;

	placement.num_placement = placement.num_busy_placement = 1;
	placement.placement = placement.busy_placement = &placement_memtype;

	tmp_reg = *new_reg;
	tmp_reg.mm_node = NULL;
	ret = ttm_bo_mem_space(bo, &placement, &tmp_reg, &ctx);
	if (ret)
		return ret;

	ret = ttm_bo_move_ttm(bo, &ctx, &tmp_reg);
	if (ret)
		goto out;

	ret = nouveau_bo_move_m2mf(bo, true, intr, no_wait_gpu, new_reg);
	if (ret)
		goto out;

out:
	ttm_resource_free(bo, &tmp_reg);
	return ret;
}

static void
nouveau_bo_move_ntfy(struct ttm_buffer_object *bo, bool evict,
		     struct ttm_resource *new_reg)
{
	struct nouveau_mem *mem = new_reg ? nouveau_mem(new_reg) : NULL;
	struct nouveau_bo *nvbo = nouveau_bo(bo);
	struct nouveau_vma *vma;

	/* ttm can now (stupidly) pass the driver bos it didn't create... */
	if (bo->destroy != nouveau_bo_del_ttm)
		return;

	nouveau_bo_del_io_reserve_lru(bo);

	if (mem && new_reg->mem_type != TTM_PL_SYSTEM &&
	    mem->mem.page == nvbo->page) {
		list_for_each_entry(vma, &nvbo->vma_list, head) {
			nouveau_vma_map(vma, mem);
		}
	} else {
		list_for_each_entry(vma, &nvbo->vma_list, head) {
			WARN_ON(ttm_bo_wait(bo, false, false));
			nouveau_vma_unmap(vma);
		}
	}

	if (new_reg) {
		if (new_reg->mm_node)
			nvbo->offset = (new_reg->start << PAGE_SHIFT);
		else
			nvbo->offset = 0;
	}

}

static int
nouveau_bo_vm_bind(struct ttm_buffer_object *bo, struct ttm_resource *new_reg,
		   struct nouveau_drm_tile **new_tile)
{
	struct nouveau_drm *drm = nouveau_bdev(bo->bdev);
	struct drm_device *dev = drm->dev;
	struct nouveau_bo *nvbo = nouveau_bo(bo);
	u64 offset = new_reg->start << PAGE_SHIFT;

	*new_tile = NULL;
	if (new_reg->mem_type != TTM_PL_VRAM)
		return 0;

	if (drm->client.device.info.family >= NV_DEVICE_INFO_V0_CELSIUS) {
		*new_tile = nv10_bo_set_tiling(dev, offset, new_reg->size,
					       nvbo->mode, nvbo->zeta);
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
	struct dma_fence *fence = dma_resv_get_excl(bo->base.resv);

	nv10_bo_put_tile_region(dev, *old_tile, fence);
	*old_tile = new_tile;
}

static int
nouveau_bo_move(struct ttm_buffer_object *bo, bool evict,
		struct ttm_operation_ctx *ctx,
		struct ttm_resource *new_reg)
{
	struct nouveau_drm *drm = nouveau_bdev(bo->bdev);
	struct nouveau_bo *nvbo = nouveau_bo(bo);
	struct ttm_resource *old_reg = &bo->mem;
	struct nouveau_drm_tile *new_tile = NULL;
	int ret = 0;

	ret = ttm_bo_wait(bo, ctx->interruptible, ctx->no_wait_gpu);
	if (ret)
		return ret;

	if (nvbo->pin_refcnt)
		NV_WARN(drm, "Moving pinned object %p!\n", nvbo);

	if (drm->client.device.info.family < NV_DEVICE_INFO_V0_TESLA) {
		ret = nouveau_bo_vm_bind(bo, new_reg, &new_tile);
		if (ret)
			return ret;
	}

	/* Fake bo copy. */
	if (old_reg->mem_type == TTM_PL_SYSTEM && !bo->ttm) {
		ttm_bo_move_null(bo, new_reg);
		goto out;
	}

	/* Hardware assisted copy. */
	if (drm->ttm.move) {
		if (new_reg->mem_type == TTM_PL_SYSTEM)
			ret = nouveau_bo_move_flipd(bo, evict,
						    ctx->interruptible,
						    ctx->no_wait_gpu, new_reg);
		else if (old_reg->mem_type == TTM_PL_SYSTEM)
			ret = nouveau_bo_move_flips(bo, evict,
						    ctx->interruptible,
						    ctx->no_wait_gpu, new_reg);
		else
			ret = nouveau_bo_move_m2mf(bo, evict,
						   ctx->interruptible,
						   ctx->no_wait_gpu, new_reg);
		if (!ret)
			goto out;
	}

	/* Fallback to software copy. */
	ret = ttm_bo_wait(bo, ctx->interruptible, ctx->no_wait_gpu);
	if (ret == 0)
		ret = ttm_bo_move_memcpy(bo, ctx, new_reg);

out:
	if (drm->client.device.info.family < NV_DEVICE_INFO_V0_TESLA) {
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

	return drm_vma_node_verify_access(&nvbo->bo.base.vma_node,
					  filp->private_data);
}

static void
nouveau_ttm_io_mem_free_locked(struct nouveau_drm *drm,
			       struct ttm_resource *reg)
{
	struct nouveau_mem *mem = nouveau_mem(reg);

	if (drm->client.mem->oclass >= NVIF_CLASS_MEM_NV50) {
		switch (reg->mem_type) {
		case TTM_PL_TT:
			if (mem->kind)
				nvif_object_unmap_handle(&mem->mem.object);
			break;
		case TTM_PL_VRAM:
			nvif_object_unmap_handle(&mem->mem.object);
			break;
		default:
			break;
		}
	}
}

static int
nouveau_ttm_io_mem_reserve(struct ttm_bo_device *bdev, struct ttm_resource *reg)
{
	struct nouveau_drm *drm = nouveau_bdev(bdev);
	struct nvkm_device *device = nvxx_device(&drm->client.device);
	struct nouveau_mem *mem = nouveau_mem(reg);
	int ret;

	mutex_lock(&drm->ttm.io_reserve_mutex);
retry:
	switch (reg->mem_type) {
	case TTM_PL_SYSTEM:
		/* System memory */
		ret = 0;
		goto out;
	case TTM_PL_TT:
#if IS_ENABLED(CONFIG_AGP)
		if (drm->agp.bridge) {
			reg->bus.offset = (reg->start << PAGE_SHIFT) +
				drm->agp.base;
			reg->bus.is_iomem = !drm->agp.cma;
		}
#endif
		if (drm->client.mem->oclass < NVIF_CLASS_MEM_NV50 ||
		    !mem->kind) {
			/* untiled */
			ret = 0;
			break;
		}
		fallthrough;	/* tiled memory */
	case TTM_PL_VRAM:
		reg->bus.offset = (reg->start << PAGE_SHIFT) +
			device->func->resource_addr(device, 1);
		reg->bus.is_iomem = true;
		if (drm->client.mem->oclass >= NVIF_CLASS_MEM_NV50) {
			union {
				struct nv50_mem_map_v0 nv50;
				struct gf100_mem_map_v0 gf100;
			} args;
			u64 handle, length;
			u32 argc = 0;

			switch (mem->mem.object.oclass) {
			case NVIF_CLASS_MEM_NV50:
				args.nv50.version = 0;
				args.nv50.ro = 0;
				args.nv50.kind = mem->kind;
				args.nv50.comp = mem->comp;
				argc = sizeof(args.nv50);
				break;
			case NVIF_CLASS_MEM_GF100:
				args.gf100.version = 0;
				args.gf100.ro = 0;
				args.gf100.kind = mem->kind;
				argc = sizeof(args.gf100);
				break;
			default:
				WARN_ON(1);
				break;
			}

			ret = nvif_object_map_handle(&mem->mem.object,
						     &args, argc,
						     &handle, &length);
			if (ret != 1) {
				if (WARN_ON(ret == 0))
					ret = -EINVAL;
				goto out;
			}

			reg->bus.offset = handle;
		}
		ret = 0;
		break;
	default:
		ret = -EINVAL;
	}

out:
	if (ret == -ENOSPC) {
		struct nouveau_bo *nvbo;

		nvbo = list_first_entry_or_null(&drm->ttm.io_reserve_lru,
						typeof(*nvbo),
						io_reserve_lru);
		if (nvbo) {
			list_del_init(&nvbo->io_reserve_lru);
			drm_vma_node_unmap(&nvbo->bo.base.vma_node,
					   bdev->dev_mapping);
			nouveau_ttm_io_mem_free_locked(drm, &nvbo->bo.mem);
			goto retry;
		}

	}
	mutex_unlock(&drm->ttm.io_reserve_mutex);
	return ret;
}

static void
nouveau_ttm_io_mem_free(struct ttm_bo_device *bdev, struct ttm_resource *reg)
{
	struct nouveau_drm *drm = nouveau_bdev(bdev);

	mutex_lock(&drm->ttm.io_reserve_mutex);
	nouveau_ttm_io_mem_free_locked(drm, reg);
	mutex_unlock(&drm->ttm.io_reserve_mutex);
}

static int
nouveau_ttm_fault_reserve_notify(struct ttm_buffer_object *bo)
{
	struct nouveau_drm *drm = nouveau_bdev(bo->bdev);
	struct nouveau_bo *nvbo = nouveau_bo(bo);
	struct nvkm_device *device = nvxx_device(&drm->client.device);
	u32 mappable = device->func->resource_size(device, 1) >> PAGE_SHIFT;
	int i, ret;

	/* as long as the bo isn't in vram, and isn't tiled, we've got
	 * nothing to do here.
	 */
	if (bo->mem.mem_type != TTM_PL_VRAM) {
		if (drm->client.device.info.family < NV_DEVICE_INFO_V0_TESLA ||
		    !nvbo->kind)
			return 0;

		if (bo->mem.mem_type == TTM_PL_SYSTEM) {
			nouveau_bo_placement_set(nvbo, NOUVEAU_GEM_DOMAIN_GART,
						 0);

			ret = nouveau_bo_validate(nvbo, false, false);
			if (ret)
				return ret;
		}
		return 0;
	}

	/* make sure bo is in mappable vram */
	if (drm->client.device.info.family >= NV_DEVICE_INFO_V0_TESLA ||
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

	nouveau_bo_placement_set(nvbo, NOUVEAU_GEM_DOMAIN_VRAM, 0);
	return nouveau_bo_validate(nvbo, false, false);
}

static int
nouveau_ttm_tt_populate(struct ttm_bo_device *bdev,
			struct ttm_tt *ttm, struct ttm_operation_ctx *ctx)
{
	struct ttm_dma_tt *ttm_dma = (void *)ttm;
	struct nouveau_drm *drm;
	struct device *dev;
	bool slave = !!(ttm->page_flags & TTM_PAGE_FLAG_SG);

	if (ttm_tt_is_populated(ttm))
		return 0;

	if (slave && ttm->sg) {
		/* make userspace faulting work */
		drm_prime_sg_to_page_addr_arrays(ttm->sg, ttm->pages,
						 ttm_dma->dma_address, ttm->num_pages);
		ttm_tt_set_populated(ttm);
		return 0;
	}

	drm = nouveau_bdev(bdev);
	dev = drm->dev->dev;

#if IS_ENABLED(CONFIG_AGP)
	if (drm->agp.bridge) {
		return ttm_pool_populate(ttm, ctx);
	}
#endif

#if IS_ENABLED(CONFIG_SWIOTLB) && IS_ENABLED(CONFIG_X86)
	if (swiotlb_nr_tbl()) {
		return ttm_dma_populate((void *)ttm, dev, ctx);
	}
#endif
	return ttm_populate_and_map_pages(dev, ttm_dma, ctx);
}

static void
nouveau_ttm_tt_unpopulate(struct ttm_bo_device *bdev,
			  struct ttm_tt *ttm)
{
	struct ttm_dma_tt *ttm_dma = (void *)ttm;
	struct nouveau_drm *drm;
	struct device *dev;
	bool slave = !!(ttm->page_flags & TTM_PAGE_FLAG_SG);

	if (slave)
		return;

	drm = nouveau_bdev(bdev);
	dev = drm->dev->dev;

#if IS_ENABLED(CONFIG_AGP)
	if (drm->agp.bridge) {
		ttm_pool_unpopulate(ttm);
		return;
	}
#endif

#if IS_ENABLED(CONFIG_SWIOTLB) && IS_ENABLED(CONFIG_X86)
	if (swiotlb_nr_tbl()) {
		ttm_dma_unpopulate((void *)ttm, dev);
		return;
	}
#endif

	ttm_unmap_and_unpopulate_pages(dev, ttm_dma);
}

static void
nouveau_ttm_tt_destroy(struct ttm_bo_device *bdev,
		       struct ttm_tt *ttm)
{
#if IS_ENABLED(CONFIG_AGP)
	struct nouveau_drm *drm = nouveau_bdev(bdev);
	if (drm->agp.bridge) {
		ttm_agp_unbind(ttm);
		ttm_tt_destroy_common(bdev, ttm);
		ttm_agp_destroy(ttm);
		return;
	}
#endif
	nouveau_sgdma_destroy(bdev, ttm);
}

void
nouveau_bo_fence(struct nouveau_bo *nvbo, struct nouveau_fence *fence, bool exclusive)
{
	struct dma_resv *resv = nvbo->bo.base.resv;

	if (exclusive)
		dma_resv_add_excl_fence(resv, &fence->base);
	else if (fence)
		dma_resv_add_shared_fence(resv, &fence->base);
}

struct ttm_bo_driver nouveau_bo_driver = {
	.ttm_tt_create = &nouveau_ttm_tt_create,
	.ttm_tt_populate = &nouveau_ttm_tt_populate,
	.ttm_tt_unpopulate = &nouveau_ttm_tt_unpopulate,
	.ttm_tt_bind = &nouveau_ttm_tt_bind,
	.ttm_tt_unbind = &nouveau_ttm_tt_unbind,
	.ttm_tt_destroy = &nouveau_ttm_tt_destroy,
	.eviction_valuable = ttm_bo_eviction_valuable,
	.evict_flags = nouveau_bo_evict_flags,
	.move_notify = nouveau_bo_move_ntfy,
	.move = nouveau_bo_move,
	.verify_access = nouveau_bo_verify_access,
	.fault_reserve_notify = &nouveau_ttm_fault_reserve_notify,
	.io_mem_reserve = &nouveau_ttm_io_mem_reserve,
	.io_mem_free = &nouveau_ttm_io_mem_free,
};
