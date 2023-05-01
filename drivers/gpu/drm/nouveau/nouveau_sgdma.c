// SPDX-License-Identifier: MIT
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <drm/ttm/ttm_tt.h>

#include "nouveau_drv.h"
#include "nouveau_mem.h"
#include "nouveau_ttm.h"
#include "nouveau_bo.h"

struct nouveau_sgdma_be {
	/* this has to be the first field so populate/unpopulated in
	 * nouve_bo.c works properly, otherwise have to move them here
	 */
	struct ttm_tt ttm;
	struct nouveau_mem *mem;
};

void
nouveau_sgdma_destroy(struct ttm_device *bdev, struct ttm_tt *ttm)
{
	struct nouveau_sgdma_be *nvbe = (struct nouveau_sgdma_be *)ttm;

	if (ttm) {
		ttm_tt_fini(&nvbe->ttm);
		kfree(nvbe);
	}
}

int
nouveau_sgdma_bind(struct ttm_device *bdev, struct ttm_tt *ttm, struct ttm_resource *reg)
{
	struct nouveau_sgdma_be *nvbe = (struct nouveau_sgdma_be *)ttm;
	struct nouveau_drm *drm = nouveau_bdev(bdev);
	struct nouveau_mem *mem = nouveau_mem(reg);
	int ret;

	if (nvbe->mem)
		return 0;

	ret = nouveau_mem_host(reg, &nvbe->ttm);
	if (ret)
		return ret;

	if (drm->client.device.info.family < NV_DEVICE_INFO_V0_TESLA) {
		ret = nouveau_mem_map(mem, &mem->cli->vmm.vmm, &mem->vma[0]);
		if (ret) {
			nouveau_mem_fini(mem);
			return ret;
		}
	}

	nvbe->mem = mem;
	return 0;
}

void
nouveau_sgdma_unbind(struct ttm_device *bdev, struct ttm_tt *ttm)
{
	struct nouveau_sgdma_be *nvbe = (struct nouveau_sgdma_be *)ttm;
	if (nvbe->mem) {
		nouveau_mem_fini(nvbe->mem);
		nvbe->mem = NULL;
	}
}

struct ttm_tt *
nouveau_sgdma_create_ttm(struct ttm_buffer_object *bo, uint32_t page_flags)
{
	struct nouveau_drm *drm = nouveau_bdev(bo->bdev);
	struct nouveau_bo *nvbo = nouveau_bo(bo);
	struct nouveau_sgdma_be *nvbe;
	enum ttm_caching caching;

	if (nvbo->force_coherent)
		caching = ttm_uncached;
	else if (drm->agp.bridge)
		caching = ttm_write_combined;
	else
		caching = ttm_cached;

	nvbe = kzalloc(sizeof(*nvbe), GFP_KERNEL);
	if (!nvbe)
		return NULL;

	if (ttm_sg_tt_init(&nvbe->ttm, bo, page_flags, caching)) {
		kfree(nvbe);
		return NULL;
	}
	return &nvbe->ttm;
}
