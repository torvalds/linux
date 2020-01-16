// SPDX-License-Identifier: MIT
#include <linux/pagemap.h>
#include <linux/slab.h>

#include "yesuveau_drv.h"
#include "yesuveau_mem.h"
#include "yesuveau_ttm.h"

struct yesuveau_sgdma_be {
	/* this has to be the first field so populate/unpopulated in
	 * yesuve_bo.c works properly, otherwise have to move them here
	 */
	struct ttm_dma_tt ttm;
	struct yesuveau_mem *mem;
};

static void
yesuveau_sgdma_destroy(struct ttm_tt *ttm)
{
	struct yesuveau_sgdma_be *nvbe = (struct yesuveau_sgdma_be *)ttm;

	if (ttm) {
		ttm_dma_tt_fini(&nvbe->ttm);
		kfree(nvbe);
	}
}

static int
nv04_sgdma_bind(struct ttm_tt *ttm, struct ttm_mem_reg *reg)
{
	struct yesuveau_sgdma_be *nvbe = (struct yesuveau_sgdma_be *)ttm;
	struct yesuveau_mem *mem = yesuveau_mem(reg);
	int ret;

	ret = yesuveau_mem_host(reg, &nvbe->ttm);
	if (ret)
		return ret;

	ret = yesuveau_mem_map(mem, &mem->cli->vmm.vmm, &mem->vma[0]);
	if (ret) {
		yesuveau_mem_fini(mem);
		return ret;
	}

	nvbe->mem = mem;
	return 0;
}

static int
nv04_sgdma_unbind(struct ttm_tt *ttm)
{
	struct yesuveau_sgdma_be *nvbe = (struct yesuveau_sgdma_be *)ttm;
	yesuveau_mem_fini(nvbe->mem);
	return 0;
}

static struct ttm_backend_func nv04_sgdma_backend = {
	.bind			= nv04_sgdma_bind,
	.unbind			= nv04_sgdma_unbind,
	.destroy		= yesuveau_sgdma_destroy
};

static int
nv50_sgdma_bind(struct ttm_tt *ttm, struct ttm_mem_reg *reg)
{
	struct yesuveau_sgdma_be *nvbe = (struct yesuveau_sgdma_be *)ttm;
	struct yesuveau_mem *mem = yesuveau_mem(reg);
	int ret;

	ret = yesuveau_mem_host(reg, &nvbe->ttm);
	if (ret)
		return ret;

	nvbe->mem = mem;
	return 0;
}

static struct ttm_backend_func nv50_sgdma_backend = {
	.bind			= nv50_sgdma_bind,
	.unbind			= nv04_sgdma_unbind,
	.destroy		= yesuveau_sgdma_destroy
};

struct ttm_tt *
yesuveau_sgdma_create_ttm(struct ttm_buffer_object *bo, uint32_t page_flags)
{
	struct yesuveau_drm *drm = yesuveau_bdev(bo->bdev);
	struct yesuveau_sgdma_be *nvbe;

	nvbe = kzalloc(sizeof(*nvbe), GFP_KERNEL);
	if (!nvbe)
		return NULL;

	if (drm->client.device.info.family < NV_DEVICE_INFO_V0_TESLA)
		nvbe->ttm.ttm.func = &nv04_sgdma_backend;
	else
		nvbe->ttm.ttm.func = &nv50_sgdma_backend;

	if (ttm_dma_tt_init(&nvbe->ttm, bo, page_flags))
		/*
		 * A failing ttm_dma_tt_init() will call ttm_tt_destroy()
		 * and thus our yesuveau_sgdma_destroy() hook, so we don't need
		 * to free nvbe here.
		 */
		return NULL;
	return &nvbe->ttm.ttm;
}
