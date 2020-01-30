// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018 The Linux Foundation. All rights reserved. */

#include <linux/dma-mapping.h>

#include "msm_drv.h"
#include "msm_mmu.h"
#include "adreno/adreno_gpu.h"
#include "adreno/a2xx.xml.h"

struct msm_gpummu {
	struct msm_mmu base;
	struct msm_gpu *gpu;
	dma_addr_t pt_base;
	uint32_t *table;
};
#define to_msm_gpummu(x) container_of(x, struct msm_gpummu, base)

#define GPUMMU_VA_START SZ_16M
#define GPUMMU_VA_RANGE (0xfff * SZ_64K)
#define GPUMMU_PAGE_SIZE SZ_4K
#define TABLE_SIZE (sizeof(uint32_t) * GPUMMU_VA_RANGE / GPUMMU_PAGE_SIZE)

static int msm_gpummu_attach(struct msm_mmu *mmu, const char * const *names,
		int cnt)
{
	return 0;
}

static void msm_gpummu_detach(struct msm_mmu *mmu, const char * const *names,
		int cnt)
{
}

static int msm_gpummu_map(struct msm_mmu *mmu, uint64_t iova,
		struct sg_table *sgt, unsigned len, int prot)
{
	struct msm_gpummu *gpummu = to_msm_gpummu(mmu);
	unsigned idx = (iova - GPUMMU_VA_START) / GPUMMU_PAGE_SIZE;
	struct scatterlist *sg;
	unsigned prot_bits = 0;
	unsigned i, j;

	if (prot & IOMMU_WRITE)
		prot_bits |= 1;
	if (prot & IOMMU_READ)
		prot_bits |= 2;

	for_each_sg(sgt->sgl, sg, sgt->nents, i) {
		dma_addr_t addr = sg->dma_address;
		for (j = 0; j < sg->length / GPUMMU_PAGE_SIZE; j++, idx++) {
			gpummu->table[idx] = addr | prot_bits;
			addr += GPUMMU_PAGE_SIZE;
		}
	}

	/* we can improve by deferring flush for multiple map() */
	gpu_write(gpummu->gpu, REG_A2XX_MH_MMU_INVALIDATE,
		A2XX_MH_MMU_INVALIDATE_INVALIDATE_ALL |
		A2XX_MH_MMU_INVALIDATE_INVALIDATE_TC);
	return 0;
}

static int msm_gpummu_unmap(struct msm_mmu *mmu, uint64_t iova, unsigned len)
{
	struct msm_gpummu *gpummu = to_msm_gpummu(mmu);
	unsigned idx = (iova - GPUMMU_VA_START) / GPUMMU_PAGE_SIZE;
	unsigned i;

	for (i = 0; i < len / GPUMMU_PAGE_SIZE; i++, idx++)
                gpummu->table[idx] = 0;

	gpu_write(gpummu->gpu, REG_A2XX_MH_MMU_INVALIDATE,
		A2XX_MH_MMU_INVALIDATE_INVALIDATE_ALL |
		A2XX_MH_MMU_INVALIDATE_INVALIDATE_TC);
	return 0;
}

static void msm_gpummu_destroy(struct msm_mmu *mmu)
{
	struct msm_gpummu *gpummu = to_msm_gpummu(mmu);

	dma_free_attrs(mmu->dev, TABLE_SIZE, gpummu->table, gpummu->pt_base,
		DMA_ATTR_FORCE_CONTIGUOUS);

	kfree(gpummu);
}

static const struct msm_mmu_funcs funcs = {
		.attach = msm_gpummu_attach,
		.detach = msm_gpummu_detach,
		.map = msm_gpummu_map,
		.unmap = msm_gpummu_unmap,
		.destroy = msm_gpummu_destroy,
};

struct msm_mmu *msm_gpummu_new(struct device *dev, struct msm_gpu *gpu)
{
	struct msm_gpummu *gpummu;

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
	msm_mmu_init(&gpummu->base, dev, &funcs);

	return &gpummu->base;
}

void msm_gpummu_params(struct msm_mmu *mmu, dma_addr_t *pt_base,
		dma_addr_t *tran_error)
{
	dma_addr_t base = to_msm_gpummu(mmu)->pt_base;

	*pt_base = base;
	*tran_error = base + TABLE_SIZE; /* 32-byte aligned */
}
