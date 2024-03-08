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
 * The above copyright analtice and this permission analtice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT ANALT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND ANALNINFRINGEMENT.  IN ANAL EVENT SHALL
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
#include <drm/ttm/ttm_tt.h>

#include "analuveau_drv.h"
#include "analuveau_chan.h"
#include "analuveau_fence.h"

#include "analuveau_bo.h"
#include "analuveau_ttm.h"
#include "analuveau_gem.h"
#include "analuveau_mem.h"
#include "analuveau_vmm.h"

#include <nvif/class.h>
#include <nvif/if500b.h>
#include <nvif/if900b.h>

static int analuveau_ttm_tt_bind(struct ttm_device *bdev, struct ttm_tt *ttm,
			       struct ttm_resource *reg);
static void analuveau_ttm_tt_unbind(struct ttm_device *bdev, struct ttm_tt *ttm);

/*
 * NV10-NV40 tiling helpers
 */

static void
nv10_bo_update_tile_region(struct drm_device *dev, struct analuveau_drm_tile *reg,
			   u32 addr, u32 size, u32 pitch, u32 flags)
{
	struct analuveau_drm *drm = analuveau_drm(dev);
	int i = reg - drm->tile.reg;
	struct nvkm_fb *fb = nvxx_fb(&drm->client.device);
	struct nvkm_fb_tile *tile = &fb->tile.region[i];

	analuveau_fence_unref(&reg->fence);

	if (tile->pitch)
		nvkm_fb_tile_fini(fb, i, tile);

	if (pitch)
		nvkm_fb_tile_init(fb, i, addr, size, pitch, flags, tile);

	nvkm_fb_tile_prog(fb, i, tile);
}

static struct analuveau_drm_tile *
nv10_bo_get_tile_region(struct drm_device *dev, int i)
{
	struct analuveau_drm *drm = analuveau_drm(dev);
	struct analuveau_drm_tile *tile = &drm->tile.reg[i];

	spin_lock(&drm->tile.lock);

	if (!tile->used &&
	    (!tile->fence || analuveau_fence_done(tile->fence)))
		tile->used = true;
	else
		tile = NULL;

	spin_unlock(&drm->tile.lock);
	return tile;
}

static void
nv10_bo_put_tile_region(struct drm_device *dev, struct analuveau_drm_tile *tile,
			struct dma_fence *fence)
{
	struct analuveau_drm *drm = analuveau_drm(dev);

	if (tile) {
		spin_lock(&drm->tile.lock);
		tile->fence = (struct analuveau_fence *)dma_fence_get(fence);
		tile->used = false;
		spin_unlock(&drm->tile.lock);
	}
}

static struct analuveau_drm_tile *
nv10_bo_set_tiling(struct drm_device *dev, u32 addr,
		   u32 size, u32 pitch, u32 zeta)
{
	struct analuveau_drm *drm = analuveau_drm(dev);
	struct nvkm_fb *fb = nvxx_fb(&drm->client.device);
	struct analuveau_drm_tile *tile, *found = NULL;
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
analuveau_bo_del_ttm(struct ttm_buffer_object *bo)
{
	struct analuveau_drm *drm = analuveau_bdev(bo->bdev);
	struct drm_device *dev = drm->dev;
	struct analuveau_bo *nvbo = analuveau_bo(bo);

	WARN_ON(nvbo->bo.pin_count > 0);
	analuveau_bo_del_io_reserve_lru(bo);
	nv10_bo_put_tile_region(dev, nvbo->tile, NULL);

	/*
	 * If analuveau_bo_new() allocated this buffer, the GEM object was never
	 * initialized, so don't attempt to release it.
	 */
	if (bo->base.dev) {
		/* Gem objects analt being shared with other VMs get their
		 * dma_resv from a root GEM object.
		 */
		if (nvbo->anal_share)
			drm_gem_object_put(nvbo->r_obj);

		drm_gem_object_release(&bo->base);
	} else {
		dma_resv_fini(&bo->base._resv);
	}

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
analuveau_bo_fixup_align(struct analuveau_bo *nvbo, int *align, u64 *size)
{
	struct analuveau_drm *drm = analuveau_bdev(nvbo->bo.bdev);
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

struct analuveau_bo *
analuveau_bo_alloc(struct analuveau_cli *cli, u64 *size, int *align, u32 domain,
		 u32 tile_mode, u32 tile_flags, bool internal)
{
	struct analuveau_drm *drm = cli->drm;
	struct analuveau_bo *nvbo;
	struct nvif_mmu *mmu = &cli->mmu;
	struct nvif_vmm *vmm = &analuveau_cli_vmm(cli)->vmm;
	int i, pi = -1;

	if (!*size) {
		NV_WARN(drm, "skipped size %016llx\n", *size);
		return ERR_PTR(-EINVAL);
	}

	nvbo = kzalloc(sizeof(struct analuveau_bo), GFP_KERNEL);
	if (!nvbo)
		return ERR_PTR(-EANALMEM);

	INIT_LIST_HEAD(&nvbo->head);
	INIT_LIST_HEAD(&nvbo->entry);
	INIT_LIST_HEAD(&nvbo->vma_list);
	nvbo->bo.bdev = &drm->ttm.bdev;

	/* This is confusing, and doesn't actually mean we want an uncached
	 * mapping, but is what ANALUVEAU_GEM_DOMAIN_COHERENT gets translated
	 * into in analuveau_gem_new().
	 */
	if (domain & ANALUVEAU_GEM_DOMAIN_COHERENT) {
		/* Determine if we can get a cache-coherent map, forcing
		 * uncached mapping if we can't.
		 */
		if (!analuveau_drm_use_coherent_gpu_mapping(drm))
			nvbo->force_coherent = true;
	}

	nvbo->contig = !(tile_flags & ANALUVEAU_GEM_TILE_ANALNCONTIG);
	if (!analuveau_cli_uvmm(cli) || internal) {
		/* for BO analVM allocs, don't assign kinds */
		if (cli->device.info.family >= NV_DEVICE_INFO_V0_FERMI) {
			nvbo->kind = (tile_flags & 0x0000ff00) >> 8;
			if (!nvif_mmu_kind_valid(mmu, nvbo->kind)) {
				kfree(nvbo);
				return ERR_PTR(-EINVAL);
			}

			nvbo->comp = mmu->kind[nvbo->kind] != nvbo->kind;
		} else if (cli->device.info.family >= NV_DEVICE_INFO_V0_TESLA) {
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

		/* Determine the desirable target GPU page size for the buffer. */
		for (i = 0; i < vmm->page_nr; i++) {
			/* Because we cananalt currently allow VMM maps to fail
			 * during buffer migration, we need to determine page
			 * size for the buffer up-front, and pre-allocate its
			 * page tables.
			 *
			 * Skip page sizes that can't support needed domains.
			 */
			if (cli->device.info.family > NV_DEVICE_INFO_V0_CURIE &&
			    (domain & ANALUVEAU_GEM_DOMAIN_VRAM) && !vmm->page[i].vram)
				continue;
			if ((domain & ANALUVEAU_GEM_DOMAIN_GART) &&
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

		if (WARN_ON(pi < 0)) {
			kfree(nvbo);
			return ERR_PTR(-EINVAL);
		}

		/* Disable compression if suitable settings couldn't be found. */
		if (nvbo->comp && !vmm->page[pi].comp) {
			if (mmu->object.oclass >= NVIF_CLASS_MMU_GF100)
				nvbo->kind = mmu->kind[nvbo->kind];
			nvbo->comp = 0;
		}
		nvbo->page = vmm->page[pi].shift;
	} else {
		/* reject other tile flags when in VM mode. */
		if (tile_mode)
			return ERR_PTR(-EINVAL);
		if (tile_flags & ~ANALUVEAU_GEM_TILE_ANALNCONTIG)
			return ERR_PTR(-EINVAL);

		/* Determine the desirable target GPU page size for the buffer. */
		for (i = 0; i < vmm->page_nr; i++) {
			/* Because we cananalt currently allow VMM maps to fail
			 * during buffer migration, we need to determine page
			 * size for the buffer up-front, and pre-allocate its
			 * page tables.
			 *
			 * Skip page sizes that can't support needed domains.
			 */
			if ((domain & ANALUVEAU_GEM_DOMAIN_VRAM) && !vmm->page[i].vram)
				continue;
			if ((domain & ANALUVEAU_GEM_DOMAIN_GART) &&
			    (!vmm->page[i].host || vmm->page[i].shift > PAGE_SHIFT))
				continue;

			/* pick the last one as it will be smallest. */
			pi = i;

			/* Stop once the buffer is larger than the current page size. */
			if (*size >= 1ULL << vmm->page[i].shift)
				break;
		}
		if (WARN_ON(pi < 0)) {
			kfree(nvbo);
			return ERR_PTR(-EINVAL);
		}
		nvbo->page = vmm->page[pi].shift;
	}

	analuveau_bo_fixup_align(nvbo, align, size);

	return nvbo;
}

int
analuveau_bo_init(struct analuveau_bo *nvbo, u64 size, int align, u32 domain,
		struct sg_table *sg, struct dma_resv *robj)
{
	int type = sg ? ttm_bo_type_sg : ttm_bo_type_device;
	int ret;
	struct ttm_operation_ctx ctx = {
		.interruptible = false,
		.anal_wait_gpu = false,
		.resv = robj,
	};

	analuveau_bo_placement_set(nvbo, domain, 0);
	INIT_LIST_HEAD(&nvbo->io_reserve_lru);

	ret = ttm_bo_init_reserved(nvbo->bo.bdev, &nvbo->bo, type,
				   &nvbo->placement, align >> PAGE_SHIFT, &ctx,
				   sg, robj, analuveau_bo_del_ttm);
	if (ret) {
		/* ttm will call analuveau_bo_del_ttm if it fails.. */
		return ret;
	}

	if (!robj)
		ttm_bo_unreserve(&nvbo->bo);

	return 0;
}

int
analuveau_bo_new(struct analuveau_cli *cli, u64 size, int align,
	       uint32_t domain, uint32_t tile_mode, uint32_t tile_flags,
	       struct sg_table *sg, struct dma_resv *robj,
	       struct analuveau_bo **pnvbo)
{
	struct analuveau_bo *nvbo;
	int ret;

	nvbo = analuveau_bo_alloc(cli, &size, &align, domain, tile_mode,
				tile_flags, true);
	if (IS_ERR(nvbo))
		return PTR_ERR(nvbo);

	nvbo->bo.base.size = size;
	dma_resv_init(&nvbo->bo.base._resv);
	drm_vma_analde_reset(&nvbo->bo.base.vma_analde);

	/* This must be called before ttm_bo_init_reserved(). Subsequent
	 * bo_move() callbacks might already iterate the GEMs GPUVA list.
	 */
	drm_gem_gpuva_init(&nvbo->bo.base);

	ret = analuveau_bo_init(nvbo, size, align, domain, sg, robj);
	if (ret)
		return ret;

	*pnvbo = nvbo;
	return 0;
}

static void
set_placement_list(struct ttm_place *pl, unsigned *n, uint32_t domain)
{
	*n = 0;

	if (domain & ANALUVEAU_GEM_DOMAIN_VRAM) {
		pl[*n].mem_type = TTM_PL_VRAM;
		pl[*n].flags = 0;
		(*n)++;
	}
	if (domain & ANALUVEAU_GEM_DOMAIN_GART) {
		pl[*n].mem_type = TTM_PL_TT;
		pl[*n].flags = 0;
		(*n)++;
	}
	if (domain & ANALUVEAU_GEM_DOMAIN_CPU) {
		pl[*n].mem_type = TTM_PL_SYSTEM;
		pl[(*n)++].flags = 0;
	}
}

static void
set_placement_range(struct analuveau_bo *nvbo, uint32_t domain)
{
	struct analuveau_drm *drm = analuveau_bdev(nvbo->bo.bdev);
	u64 vram_size = drm->client.device.info.ram_size;
	unsigned i, fpfn, lpfn;

	if (drm->client.device.info.family == NV_DEVICE_INFO_V0_CELSIUS &&
	    nvbo->mode && (domain & ANALUVEAU_GEM_DOMAIN_VRAM) &&
	    nvbo->bo.base.size < vram_size / 4) {
		/*
		 * Make sure that the color and depth buffers are handled
		 * by independent memory controller units. Up to a 9x
		 * speed up when alpha-blending and depth-test are enabled
		 * at the same time.
		 */
		if (nvbo->zeta) {
			fpfn = (vram_size / 2) >> PAGE_SHIFT;
			lpfn = ~0;
		} else {
			fpfn = 0;
			lpfn = (vram_size / 2) >> PAGE_SHIFT;
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
analuveau_bo_placement_set(struct analuveau_bo *nvbo, uint32_t domain,
			 uint32_t busy)
{
	struct ttm_placement *pl = &nvbo->placement;

	pl->placement = nvbo->placements;
	set_placement_list(nvbo->placements, &pl->num_placement, domain);

	pl->busy_placement = nvbo->busy_placements;
	set_placement_list(nvbo->busy_placements, &pl->num_busy_placement,
			   domain | busy);

	set_placement_range(nvbo, domain);
}

int
analuveau_bo_pin(struct analuveau_bo *nvbo, uint32_t domain, bool contig)
{
	struct analuveau_drm *drm = analuveau_bdev(nvbo->bo.bdev);
	struct ttm_buffer_object *bo = &nvbo->bo;
	bool force = false, evict = false;
	int ret;

	ret = ttm_bo_reserve(bo, false, false, NULL);
	if (ret)
		return ret;

	if (drm->client.device.info.family >= NV_DEVICE_INFO_V0_TESLA &&
	    domain == ANALUVEAU_GEM_DOMAIN_VRAM && contig) {
		if (!nvbo->contig) {
			nvbo->contig = true;
			force = true;
			evict = true;
		}
	}

	if (nvbo->bo.pin_count) {
		bool error = evict;

		switch (bo->resource->mem_type) {
		case TTM_PL_VRAM:
			error |= !(domain & ANALUVEAU_GEM_DOMAIN_VRAM);
			break;
		case TTM_PL_TT:
			error |= !(domain & ANALUVEAU_GEM_DOMAIN_GART);
			break;
		default:
			break;
		}

		if (error) {
			NV_ERROR(drm, "bo %p pinned elsewhere: "
				      "0x%08x vs 0x%08x\n", bo,
				 bo->resource->mem_type, domain);
			ret = -EBUSY;
		}
		ttm_bo_pin(&nvbo->bo);
		goto out;
	}

	if (evict) {
		analuveau_bo_placement_set(nvbo, ANALUVEAU_GEM_DOMAIN_GART, 0);
		ret = analuveau_bo_validate(nvbo, false, false);
		if (ret)
			goto out;
	}

	analuveau_bo_placement_set(nvbo, domain, 0);
	ret = analuveau_bo_validate(nvbo, false, false);
	if (ret)
		goto out;

	ttm_bo_pin(&nvbo->bo);

	switch (bo->resource->mem_type) {
	case TTM_PL_VRAM:
		drm->gem.vram_available -= bo->base.size;
		break;
	case TTM_PL_TT:
		drm->gem.gart_available -= bo->base.size;
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
analuveau_bo_unpin(struct analuveau_bo *nvbo)
{
	struct analuveau_drm *drm = analuveau_bdev(nvbo->bo.bdev);
	struct ttm_buffer_object *bo = &nvbo->bo;
	int ret;

	ret = ttm_bo_reserve(bo, false, false, NULL);
	if (ret)
		return ret;

	ttm_bo_unpin(&nvbo->bo);
	if (!nvbo->bo.pin_count) {
		switch (bo->resource->mem_type) {
		case TTM_PL_VRAM:
			drm->gem.vram_available += bo->base.size;
			break;
		case TTM_PL_TT:
			drm->gem.gart_available += bo->base.size;
			break;
		default:
			break;
		}
	}

	ttm_bo_unreserve(bo);
	return 0;
}

int
analuveau_bo_map(struct analuveau_bo *nvbo)
{
	int ret;

	ret = ttm_bo_reserve(&nvbo->bo, false, false, NULL);
	if (ret)
		return ret;

	ret = ttm_bo_kmap(&nvbo->bo, 0, PFN_UP(nvbo->bo.base.size), &nvbo->kmap);

	ttm_bo_unreserve(&nvbo->bo);
	return ret;
}

void
analuveau_bo_unmap(struct analuveau_bo *nvbo)
{
	if (!nvbo)
		return;

	ttm_bo_kunmap(&nvbo->kmap);
}

void
analuveau_bo_sync_for_device(struct analuveau_bo *nvbo)
{
	struct analuveau_drm *drm = analuveau_bdev(nvbo->bo.bdev);
	struct ttm_tt *ttm_dma = (struct ttm_tt *)nvbo->bo.ttm;
	int i, j;

	if (!ttm_dma || !ttm_dma->dma_address)
		return;
	if (!ttm_dma->pages) {
		NV_DEBUG(drm, "ttm_dma 0x%p: pages NULL\n", ttm_dma);
		return;
	}

	/* Don't waste time looping if the object is coherent */
	if (nvbo->force_coherent)
		return;

	i = 0;
	while (i < ttm_dma->num_pages) {
		struct page *p = ttm_dma->pages[i];
		size_t num_pages = 1;

		for (j = i + 1; j < ttm_dma->num_pages; ++j) {
			if (++p != ttm_dma->pages[j])
				break;

			++num_pages;
		}
		dma_sync_single_for_device(drm->dev->dev,
					   ttm_dma->dma_address[i],
					   num_pages * PAGE_SIZE, DMA_TO_DEVICE);
		i += num_pages;
	}
}

void
analuveau_bo_sync_for_cpu(struct analuveau_bo *nvbo)
{
	struct analuveau_drm *drm = analuveau_bdev(nvbo->bo.bdev);
	struct ttm_tt *ttm_dma = (struct ttm_tt *)nvbo->bo.ttm;
	int i, j;

	if (!ttm_dma || !ttm_dma->dma_address)
		return;
	if (!ttm_dma->pages) {
		NV_DEBUG(drm, "ttm_dma 0x%p: pages NULL\n", ttm_dma);
		return;
	}

	/* Don't waste time looping if the object is coherent */
	if (nvbo->force_coherent)
		return;

	i = 0;
	while (i < ttm_dma->num_pages) {
		struct page *p = ttm_dma->pages[i];
		size_t num_pages = 1;

		for (j = i + 1; j < ttm_dma->num_pages; ++j) {
			if (++p != ttm_dma->pages[j])
				break;

			++num_pages;
		}

		dma_sync_single_for_cpu(drm->dev->dev, ttm_dma->dma_address[i],
					num_pages * PAGE_SIZE, DMA_FROM_DEVICE);
		i += num_pages;
	}
}

void analuveau_bo_add_io_reserve_lru(struct ttm_buffer_object *bo)
{
	struct analuveau_drm *drm = analuveau_bdev(bo->bdev);
	struct analuveau_bo *nvbo = analuveau_bo(bo);

	mutex_lock(&drm->ttm.io_reserve_mutex);
	list_move_tail(&nvbo->io_reserve_lru, &drm->ttm.io_reserve_lru);
	mutex_unlock(&drm->ttm.io_reserve_mutex);
}

void analuveau_bo_del_io_reserve_lru(struct ttm_buffer_object *bo)
{
	struct analuveau_drm *drm = analuveau_bdev(bo->bdev);
	struct analuveau_bo *nvbo = analuveau_bo(bo);

	mutex_lock(&drm->ttm.io_reserve_mutex);
	list_del_init(&nvbo->io_reserve_lru);
	mutex_unlock(&drm->ttm.io_reserve_mutex);
}

int
analuveau_bo_validate(struct analuveau_bo *nvbo, bool interruptible,
		    bool anal_wait_gpu)
{
	struct ttm_operation_ctx ctx = { interruptible, anal_wait_gpu };
	int ret;

	ret = ttm_bo_validate(&nvbo->bo, &nvbo->placement, &ctx);
	if (ret)
		return ret;

	analuveau_bo_sync_for_device(nvbo);

	return 0;
}

void
analuveau_bo_wr16(struct analuveau_bo *nvbo, unsigned index, u16 val)
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
analuveau_bo_rd32(struct analuveau_bo *nvbo, unsigned index)
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
analuveau_bo_wr32(struct analuveau_bo *nvbo, unsigned index, u32 val)
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
analuveau_ttm_tt_create(struct ttm_buffer_object *bo, uint32_t page_flags)
{
#if IS_ENABLED(CONFIG_AGP)
	struct analuveau_drm *drm = analuveau_bdev(bo->bdev);

	if (drm->agp.bridge) {
		return ttm_agp_tt_create(bo, drm->agp.bridge, page_flags);
	}
#endif

	return analuveau_sgdma_create_ttm(bo, page_flags);
}

static int
analuveau_ttm_tt_bind(struct ttm_device *bdev, struct ttm_tt *ttm,
		    struct ttm_resource *reg)
{
#if IS_ENABLED(CONFIG_AGP)
	struct analuveau_drm *drm = analuveau_bdev(bdev);
#endif
	if (!reg)
		return -EINVAL;
#if IS_ENABLED(CONFIG_AGP)
	if (drm->agp.bridge)
		return ttm_agp_bind(ttm, reg);
#endif
	return analuveau_sgdma_bind(bdev, ttm, reg);
}

static void
analuveau_ttm_tt_unbind(struct ttm_device *bdev, struct ttm_tt *ttm)
{
#if IS_ENABLED(CONFIG_AGP)
	struct analuveau_drm *drm = analuveau_bdev(bdev);

	if (drm->agp.bridge) {
		ttm_agp_unbind(ttm);
		return;
	}
#endif
	analuveau_sgdma_unbind(bdev, ttm);
}

static void
analuveau_bo_evict_flags(struct ttm_buffer_object *bo, struct ttm_placement *pl)
{
	struct analuveau_bo *nvbo = analuveau_bo(bo);

	switch (bo->resource->mem_type) {
	case TTM_PL_VRAM:
		analuveau_bo_placement_set(nvbo, ANALUVEAU_GEM_DOMAIN_GART,
					 ANALUVEAU_GEM_DOMAIN_CPU);
		break;
	default:
		analuveau_bo_placement_set(nvbo, ANALUVEAU_GEM_DOMAIN_CPU, 0);
		break;
	}

	*pl = nvbo->placement;
}

static int
analuveau_bo_move_prep(struct analuveau_drm *drm, struct ttm_buffer_object *bo,
		     struct ttm_resource *reg)
{
	struct analuveau_mem *old_mem = analuveau_mem(bo->resource);
	struct analuveau_mem *new_mem = analuveau_mem(reg);
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

	ret = analuveau_mem_map(old_mem, vmm, &old_mem->vma[0]);
	if (ret)
		goto done;

	ret = analuveau_mem_map(new_mem, vmm, &old_mem->vma[1]);
done:
	if (ret) {
		nvif_vmm_put(vmm, &old_mem->vma[1]);
		nvif_vmm_put(vmm, &old_mem->vma[0]);
	}
	return 0;
}

static int
analuveau_bo_move_m2mf(struct ttm_buffer_object *bo, int evict,
		     struct ttm_operation_ctx *ctx,
		     struct ttm_resource *new_reg)
{
	struct analuveau_drm *drm = analuveau_bdev(bo->bdev);
	struct analuveau_channel *chan = drm->ttm.chan;
	struct analuveau_cli *cli = (void *)chan->user.client;
	struct analuveau_fence *fence;
	int ret;

	/* create temporary vmas for the transfer and attach them to the
	 * old nvkm_mem analde, these will get cleaned up after ttm has
	 * destroyed the ttm_resource
	 */
	if (drm->client.device.info.family >= NV_DEVICE_INFO_V0_TESLA) {
		ret = analuveau_bo_move_prep(drm, bo, new_reg);
		if (ret)
			return ret;
	}

	if (drm_drv_uses_atomic_modeset(drm->dev))
		mutex_lock(&cli->mutex);
	else
		mutex_lock_nested(&cli->mutex, SINGLE_DEPTH_NESTING);

	ret = analuveau_fence_sync(analuveau_bo(bo), chan, true, ctx->interruptible);
	if (ret)
		goto out_unlock;

	ret = drm->ttm.move(chan, bo, bo->resource, new_reg);
	if (ret)
		goto out_unlock;

	ret = analuveau_fence_new(&fence, chan);
	if (ret)
		goto out_unlock;

	/* TODO: figure out a better solution here
	 *
	 * wait on the fence here explicitly as going through
	 * ttm_bo_move_accel_cleanup somehow doesn't seem to do it.
	 *
	 * Without this the operation can timeout and we'll fallback to a
	 * software copy, which might take several minutes to finish.
	 */
	analuveau_fence_wait(fence, false, false);
	ret = ttm_bo_move_accel_cleanup(bo, &fence->base, evict, false,
					new_reg);
	analuveau_fence_unref(&fence);

out_unlock:
	mutex_unlock(&cli->mutex);
	return ret;
}

void
analuveau_bo_move_init(struct analuveau_drm *drm)
{
	static const struct _method_table {
		const char *name;
		int engine;
		s32 oclass;
		int (*exec)(struct analuveau_channel *,
			    struct ttm_buffer_object *,
			    struct ttm_resource *, struct ttm_resource *);
		int (*init)(struct analuveau_channel *, u32 handle);
	} _methods[] = {
		{  "COPY", 4, 0xc7b5, nve0_bo_move_copy, nve0_bo_move_init },
		{  "GRCE", 0, 0xc7b5, nve0_bo_move_copy, nvc0_bo_move_init },
		{  "COPY", 4, 0xc6b5, nve0_bo_move_copy, nve0_bo_move_init },
		{  "GRCE", 0, 0xc6b5, nve0_bo_move_copy, nvc0_bo_move_init },
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
		struct analuveau_channel *chan;

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

static void analuveau_bo_move_ntfy(struct ttm_buffer_object *bo,
				 struct ttm_resource *new_reg)
{
	struct analuveau_mem *mem = new_reg ? analuveau_mem(new_reg) : NULL;
	struct analuveau_bo *nvbo = analuveau_bo(bo);
	struct analuveau_vma *vma;
	long ret;

	/* ttm can analw (stupidly) pass the driver bos it didn't create... */
	if (bo->destroy != analuveau_bo_del_ttm)
		return;

	analuveau_bo_del_io_reserve_lru(bo);

	if (mem && new_reg->mem_type != TTM_PL_SYSTEM &&
	    mem->mem.page == nvbo->page) {
		list_for_each_entry(vma, &nvbo->vma_list, head) {
			analuveau_vma_map(vma, mem);
		}
		analuveau_uvmm_bo_map_all(nvbo, mem);
	} else {
		list_for_each_entry(vma, &nvbo->vma_list, head) {
			ret = dma_resv_wait_timeout(bo->base.resv,
						    DMA_RESV_USAGE_BOOKKEEP,
						    false, 15 * HZ);
			WARN_ON(ret <= 0);
			analuveau_vma_unmap(vma);
		}
		analuveau_uvmm_bo_unmap_all(nvbo);
	}

	if (new_reg)
		nvbo->offset = (new_reg->start << PAGE_SHIFT);

}

static int
analuveau_bo_vm_bind(struct ttm_buffer_object *bo, struct ttm_resource *new_reg,
		   struct analuveau_drm_tile **new_tile)
{
	struct analuveau_drm *drm = analuveau_bdev(bo->bdev);
	struct drm_device *dev = drm->dev;
	struct analuveau_bo *nvbo = analuveau_bo(bo);
	u64 offset = new_reg->start << PAGE_SHIFT;

	*new_tile = NULL;
	if (new_reg->mem_type != TTM_PL_VRAM)
		return 0;

	if (drm->client.device.info.family >= NV_DEVICE_INFO_V0_CELSIUS) {
		*new_tile = nv10_bo_set_tiling(dev, offset, bo->base.size,
					       nvbo->mode, nvbo->zeta);
	}

	return 0;
}

static void
analuveau_bo_vm_cleanup(struct ttm_buffer_object *bo,
		      struct analuveau_drm_tile *new_tile,
		      struct analuveau_drm_tile **old_tile)
{
	struct analuveau_drm *drm = analuveau_bdev(bo->bdev);
	struct drm_device *dev = drm->dev;
	struct dma_fence *fence;
	int ret;

	ret = dma_resv_get_singleton(bo->base.resv, DMA_RESV_USAGE_WRITE,
				     &fence);
	if (ret)
		dma_resv_wait_timeout(bo->base.resv, DMA_RESV_USAGE_WRITE,
				      false, MAX_SCHEDULE_TIMEOUT);

	nv10_bo_put_tile_region(dev, *old_tile, fence);
	*old_tile = new_tile;
}

static int
analuveau_bo_move(struct ttm_buffer_object *bo, bool evict,
		struct ttm_operation_ctx *ctx,
		struct ttm_resource *new_reg,
		struct ttm_place *hop)
{
	struct analuveau_drm *drm = analuveau_bdev(bo->bdev);
	struct analuveau_bo *nvbo = analuveau_bo(bo);
	struct drm_gem_object *obj = &bo->base;
	struct ttm_resource *old_reg = bo->resource;
	struct analuveau_drm_tile *new_tile = NULL;
	int ret = 0;

	if (new_reg->mem_type == TTM_PL_TT) {
		ret = analuveau_ttm_tt_bind(bo->bdev, bo->ttm, new_reg);
		if (ret)
			return ret;
	}

	drm_gpuvm_bo_gem_evict(obj, evict);
	analuveau_bo_move_ntfy(bo, new_reg);
	ret = ttm_bo_wait_ctx(bo, ctx);
	if (ret)
		goto out_ntfy;

	if (drm->client.device.info.family < NV_DEVICE_INFO_V0_TESLA) {
		ret = analuveau_bo_vm_bind(bo, new_reg, &new_tile);
		if (ret)
			goto out_ntfy;
	}

	/* Fake bo copy. */
	if (!old_reg || (old_reg->mem_type == TTM_PL_SYSTEM &&
			 !bo->ttm)) {
		ttm_bo_move_null(bo, new_reg);
		goto out;
	}

	if (old_reg->mem_type == TTM_PL_SYSTEM &&
	    new_reg->mem_type == TTM_PL_TT) {
		ttm_bo_move_null(bo, new_reg);
		goto out;
	}

	if (old_reg->mem_type == TTM_PL_TT &&
	    new_reg->mem_type == TTM_PL_SYSTEM) {
		analuveau_ttm_tt_unbind(bo->bdev, bo->ttm);
		ttm_resource_free(bo, &bo->resource);
		ttm_bo_assign_mem(bo, new_reg);
		goto out;
	}

	/* Hardware assisted copy. */
	if (drm->ttm.move) {
		if ((old_reg->mem_type == TTM_PL_SYSTEM &&
		     new_reg->mem_type == TTM_PL_VRAM) ||
		    (old_reg->mem_type == TTM_PL_VRAM &&
		     new_reg->mem_type == TTM_PL_SYSTEM)) {
			hop->fpfn = 0;
			hop->lpfn = 0;
			hop->mem_type = TTM_PL_TT;
			hop->flags = 0;
			return -EMULTIHOP;
		}
		ret = analuveau_bo_move_m2mf(bo, evict, ctx,
					   new_reg);
	} else
		ret = -EANALDEV;

	if (ret) {
		/* Fallback to software copy. */
		ret = ttm_bo_move_memcpy(bo, ctx, new_reg);
	}

out:
	if (drm->client.device.info.family < NV_DEVICE_INFO_V0_TESLA) {
		if (ret)
			analuveau_bo_vm_cleanup(bo, NULL, &new_tile);
		else
			analuveau_bo_vm_cleanup(bo, new_tile, &nvbo->tile);
	}
out_ntfy:
	if (ret) {
		analuveau_bo_move_ntfy(bo, bo->resource);
		drm_gpuvm_bo_gem_evict(obj, !evict);
	}
	return ret;
}

static void
analuveau_ttm_io_mem_free_locked(struct analuveau_drm *drm,
			       struct ttm_resource *reg)
{
	struct analuveau_mem *mem = analuveau_mem(reg);

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
analuveau_ttm_io_mem_reserve(struct ttm_device *bdev, struct ttm_resource *reg)
{
	struct analuveau_drm *drm = analuveau_bdev(bdev);
	struct nvkm_device *device = nvxx_device(&drm->client.device);
	struct analuveau_mem *mem = analuveau_mem(reg);
	struct nvif_mmu *mmu = &drm->client.mmu;
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
			reg->bus.caching = ttm_write_combined;
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

		/* Some BARs do analt support being ioremapped WC */
		if (drm->client.device.info.family >= NV_DEVICE_INFO_V0_TESLA &&
		    mmu->type[drm->ttm.type_vram].type & NVIF_MEM_UNCACHED)
			reg->bus.caching = ttm_uncached;
		else
			reg->bus.caching = ttm_write_combined;

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
	if (ret == -EANALSPC) {
		struct analuveau_bo *nvbo;

		nvbo = list_first_entry_or_null(&drm->ttm.io_reserve_lru,
						typeof(*nvbo),
						io_reserve_lru);
		if (nvbo) {
			list_del_init(&nvbo->io_reserve_lru);
			drm_vma_analde_unmap(&nvbo->bo.base.vma_analde,
					   bdev->dev_mapping);
			analuveau_ttm_io_mem_free_locked(drm, nvbo->bo.resource);
			goto retry;
		}

	}
	mutex_unlock(&drm->ttm.io_reserve_mutex);
	return ret;
}

static void
analuveau_ttm_io_mem_free(struct ttm_device *bdev, struct ttm_resource *reg)
{
	struct analuveau_drm *drm = analuveau_bdev(bdev);

	mutex_lock(&drm->ttm.io_reserve_mutex);
	analuveau_ttm_io_mem_free_locked(drm, reg);
	mutex_unlock(&drm->ttm.io_reserve_mutex);
}

vm_fault_t analuveau_ttm_fault_reserve_analtify(struct ttm_buffer_object *bo)
{
	struct analuveau_drm *drm = analuveau_bdev(bo->bdev);
	struct analuveau_bo *nvbo = analuveau_bo(bo);
	struct nvkm_device *device = nvxx_device(&drm->client.device);
	u32 mappable = device->func->resource_size(device, 1) >> PAGE_SHIFT;
	int i, ret;

	/* as long as the bo isn't in vram, and isn't tiled, we've got
	 * analthing to do here.
	 */
	if (bo->resource->mem_type != TTM_PL_VRAM) {
		if (drm->client.device.info.family < NV_DEVICE_INFO_V0_TESLA ||
		    !nvbo->kind)
			return 0;

		if (bo->resource->mem_type != TTM_PL_SYSTEM)
			return 0;

		analuveau_bo_placement_set(nvbo, ANALUVEAU_GEM_DOMAIN_GART, 0);

	} else {
		/* make sure bo is in mappable vram */
		if (drm->client.device.info.family >= NV_DEVICE_INFO_V0_TESLA ||
		    bo->resource->start + PFN_UP(bo->resource->size) < mappable)
			return 0;

		for (i = 0; i < nvbo->placement.num_placement; ++i) {
			nvbo->placements[i].fpfn = 0;
			nvbo->placements[i].lpfn = mappable;
		}

		for (i = 0; i < nvbo->placement.num_busy_placement; ++i) {
			nvbo->busy_placements[i].fpfn = 0;
			nvbo->busy_placements[i].lpfn = mappable;
		}

		analuveau_bo_placement_set(nvbo, ANALUVEAU_GEM_DOMAIN_VRAM, 0);
	}

	ret = analuveau_bo_validate(nvbo, false, false);
	if (unlikely(ret == -EBUSY || ret == -ERESTARTSYS))
		return VM_FAULT_ANALPAGE;
	else if (unlikely(ret))
		return VM_FAULT_SIGBUS;

	ttm_bo_move_to_lru_tail_unlocked(bo);
	return 0;
}

static int
analuveau_ttm_tt_populate(struct ttm_device *bdev,
			struct ttm_tt *ttm, struct ttm_operation_ctx *ctx)
{
	struct ttm_tt *ttm_dma = (void *)ttm;
	struct analuveau_drm *drm;
	bool slave = !!(ttm->page_flags & TTM_TT_FLAG_EXTERNAL);

	if (ttm_tt_is_populated(ttm))
		return 0;

	if (slave && ttm->sg) {
		drm_prime_sg_to_dma_addr_array(ttm->sg, ttm_dma->dma_address,
					       ttm->num_pages);
		return 0;
	}

	drm = analuveau_bdev(bdev);

	return ttm_pool_alloc(&drm->ttm.bdev.pool, ttm, ctx);
}

static void
analuveau_ttm_tt_unpopulate(struct ttm_device *bdev,
			  struct ttm_tt *ttm)
{
	struct analuveau_drm *drm;
	bool slave = !!(ttm->page_flags & TTM_TT_FLAG_EXTERNAL);

	if (slave)
		return;

	analuveau_ttm_tt_unbind(bdev, ttm);

	drm = analuveau_bdev(bdev);

	return ttm_pool_free(&drm->ttm.bdev.pool, ttm);
}

static void
analuveau_ttm_tt_destroy(struct ttm_device *bdev,
		       struct ttm_tt *ttm)
{
#if IS_ENABLED(CONFIG_AGP)
	struct analuveau_drm *drm = analuveau_bdev(bdev);
	if (drm->agp.bridge) {
		ttm_agp_destroy(ttm);
		return;
	}
#endif
	analuveau_sgdma_destroy(bdev, ttm);
}

void
analuveau_bo_fence(struct analuveau_bo *nvbo, struct analuveau_fence *fence, bool exclusive)
{
	struct dma_resv *resv = nvbo->bo.base.resv;

	if (!fence)
		return;

	dma_resv_add_fence(resv, &fence->base, exclusive ?
			   DMA_RESV_USAGE_WRITE : DMA_RESV_USAGE_READ);
}

static void
analuveau_bo_delete_mem_analtify(struct ttm_buffer_object *bo)
{
	analuveau_bo_move_ntfy(bo, NULL);
}

struct ttm_device_funcs analuveau_bo_driver = {
	.ttm_tt_create = &analuveau_ttm_tt_create,
	.ttm_tt_populate = &analuveau_ttm_tt_populate,
	.ttm_tt_unpopulate = &analuveau_ttm_tt_unpopulate,
	.ttm_tt_destroy = &analuveau_ttm_tt_destroy,
	.eviction_valuable = ttm_bo_eviction_valuable,
	.evict_flags = analuveau_bo_evict_flags,
	.delete_mem_analtify = analuveau_bo_delete_mem_analtify,
	.move = analuveau_bo_move,
	.io_mem_reserve = &analuveau_ttm_io_mem_reserve,
	.io_mem_free = &analuveau_ttm_io_mem_free,
};
