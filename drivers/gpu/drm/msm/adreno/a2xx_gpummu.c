// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018 The Linux Foundation. All rights reserved. */

#include <linux/dma-mapping.h>

#include "msm_drv.h"
#include "msm_mmu.h"

#include "adreno_gpu.h"
#include "a2xx_gpu.h"

#include "a2xx.xml.h"

struct a2xx_gpummu {
	struct msm_mmu base;
	struct msm_gpu *gpu;
	dma_addr_t pt_base;
	uint32_t *table;
};
#define to_a2xx_gpummu(x) container_of(x, struct a2xx_gpummu, base)

#define GPUMMU_VA_START SZ_16M
#define GPUMMU_VA_RANGE (0xfff * SZ_64K)
#define GPUMMU_PAGE_SIZE SZ_4K
#define TABLE_SIZE (sizeof(uint32_t) * GPUMMU_VA_RANGE / GPUMMU_PAGE_SIZE)

static void a2xx_gpummu_detach(struct msm_mmu *mmu)
{
}

static int a2xx_gpummu_map(struct msm_mmu *mmu, uint64_t iova,
		struct sg_table *sgt, size_t len, int prot)
{
	struct a2xx_gpummu *gpummu = to_a2xx_gpummu(mmu);
	unsigned idx = (iova - GPUMMU_VA_START) / GPUMMU_PAGE_SIZE;
	struct sg_dma_page_iter dma_iter;
	unsigned prot_bits = 0;

	if (prot & IOMMU_WRITE)
		prot_bits |= 1;
	if (prot & IOMMU_READ)
		prot_bits |= 2;

	for_each_sgtable_dma_page(sgt, &dma_iter, 0) {
		dma_addr_t addr = sg_page_iter_dma_address(&dma_iter);
		int i;

		for (i = 0; i < PAGE_SIZE; i += GPUMMU_PAGE_SIZE)
			gpummu->table[idx++] = (addr + i) | prot_bits;
	}

	/* we can improve by deferring flush for multiple map() */
	gpu_write(gpummu->gpu, REG_A2XX_MH_MMU_INVALIDATE,
		A2XX_MH_MMU_INVALIDATE_INVALIDATE_ALL |
		A2XX_MH_MMU_INVALIDATE_INVALIDATE_TC);
	return 0;
}

static int a2xx_gpummu_unmap(struct msm_mmu *mmu, uint64_t iova, size_t len)
{
	struct a2xx_gpummu *gpummu = to_a2xx_gpummu(mmu);
	unsigned idx = (iova - GPUMMU_VA_START) / GPUMMU_PAGE_SIZE;
	unsigned i;

	for (i = 0; i < len / GPUMMU_PAGE_SIZE; i++, idx++)
                gpummu->table[idx] = 0;

	gpu_write(gpummu->gpu, REG_A2XX_MH_MMU_INVALIDATE,
		A2XX_MH_MMU_INVALIDATE_INVALIDATE_ALL |
		A2XX_MH_MMU_INVALIDATE_INVALIDATE_TC);
	return 0;
}

static void a2xx_gpummu_resume_translation(struct msm_mmu *mmu)
{
}

static void a2xx_gpummu_destroy(struct msm_mmu *mmu)
{
	struct a2xx_gpummu *gpummu = to_a2xx_gpummu(mmu);

	dma_free_attrs(mmu->dev, TABLE_SIZE, gpummu->table, gpummu->pt_base,
		DMA_ATTR_FORCE_CONTIGUOUS);

	kfree(gpummu);
}

static const struct msm_mmu_funcs funcs = {
		.detach = a2xx_gpummu_detach,
		.map = a2xx_gpummu_map,
		.unmap = a2xx_gpummu_unmap,
		.destroy = a2xx_gpummu_destroy,
		.resume_translation = a2xx_gpummu_resume_translation,
};

struct msm_mmu *a2xx_gpummu_new(struct device *dev, struct msm_gpu *gpu)
{
	struct a2xx_gpummu *gpummu;

	gpummu = kzalloc(sizeof(*gpummu), GFP_KERNEL);
	if (!gpummu)
		return ERR_PTR(-ENOMEM);

	gpummu->table = dma_alloc_attrs(dev, TABLE_SIZE + 32, &gpummu->pt_base,
		GFP_KERNEL | __GFP_ZERO, DMA_ATTR_FORCE_CONTIGUOUS);
	if (!gpummu->table) {
		kfree(gpummu);
		return ERR_PTR(-ENOMEM);
	}

	gpummu->gpu = gpu;
	msm_mmu_init(&gpummu->base, dev, &funcs, MSM_MMU_GPUMMU);

	return &gpummu->base;
}

void a2xx_gpummu_params(struct msm_mmu *mmu, dma_addr_t *pt_base,
		dma_addr_t *tran_error)
{
	dma_addr_t base = to_a2xx_gpummu(mmu)->pt_base;

	*pt_base = base;
	*tran_error = base + TABLE_SIZE; /* 32-byte aligned */
}
