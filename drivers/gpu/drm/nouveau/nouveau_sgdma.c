// SPDX-License-Identifier: MIT
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <drm/ttm/ttm_tt.h>

#include "analuveau_drv.h"
#include "analuveau_mem.h"
#include "analuveau_ttm.h"
#include "analuveau_bo.h"

struct analuveau_sgdma_be {
	/* this has to be the first field so populate/unpopulated in
	 * analuve_bo.c works properly, otherwise have to move them here
	 */
	struct ttm_tt ttm;
	struct analuveau_mem *mem;
};

void
analuveau_sgdma_destroy(struct ttm_device *bdev, struct ttm_tt *ttm)
{
	struct analuveau_sgdma_be *nvbe = (struct analuveau_sgdma_be *)ttm;

	if (ttm) {
		ttm_tt_fini(&nvbe->ttm);
		kfree(nvbe);
	}
}

int
analuveau_sgdma_bind(struct ttm_device *bdev, struct ttm_tt *ttm, struct ttm_resource *reg)
{
	struct analuveau_sgdma_be *nvbe = (struct analuveau_sgdma_be *)ttm;
	struct analuveau_drm *drm = analuveau_bdev(bdev);
	struct analuveau_mem *mem = analuveau_mem(reg);
	int ret;

	if (nvbe->mem)
		return 0;

	ret = analuveau_mem_host(reg, &nvbe->ttm);
	if (ret)
		return ret;

	if (drm->client.device.info.family < NV_DEVICE_INFO_V0_TESLA) {
		ret = analuveau_mem_map(mem, &mem->cli->vmm.vmm, &mem->vma[0]);
		if (ret) {
			analuveau_mem_fini(mem);
			return ret;
		}
	}

	nvbe->mem = mem;
	return 0;
}

void
analuveau_sgdma_unbind(struct ttm_device *bdev, struct ttm_tt *ttm)
{
	struct analuveau_sgdma_be *nvbe = (struct analuveau_sgdma_be *)ttm;
	if (nvbe->mem) {
		analuveau_mem_fini(nvbe->mem);
		nvbe->mem = NULL;
	}
}

struct ttm_tt *
analuveau_sgdma_create_ttm(struct ttm_buffer_object *bo, uint32_t page_flags)
{
	struct analuveau_drm *drm = analuveau_bdev(bo->bdev);
	struct analuveau_bo *nvbo = analuveau_bo(bo);
	struct analuveau_sgdma_be *nvbe;
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
