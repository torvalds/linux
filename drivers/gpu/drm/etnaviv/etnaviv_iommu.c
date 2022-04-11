// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2014-2018 Etnaviv Project
 */

#include <linux/bitops.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>
#include <linux/slab.h>

#include "etnaviv_gpu.h"
#include "etnaviv_mmu.h"
#include "state_hi.xml.h"

#define PT_SIZE		SZ_2M
#define PT_ENTRIES	(PT_SIZE / sizeof(u32))

#define GPU_MEM_START	0x80000000

struct etnaviv_iommuv1_context {
	struct etnaviv_iommu_context base;
	u32 *pgtable_cpu;
	dma_addr_t pgtable_dma;
};

static struct etnaviv_iommuv1_context *
to_v1_context(struct etnaviv_iommu_context *context)
{
	return container_of(context, struct etnaviv_iommuv1_context, base);
}

static void etnaviv_iommuv1_free(struct etnaviv_iommu_context *context)
{
	struct etnaviv_iommuv1_context *v1_context = to_v1_context(context);

	drm_mm_takedown(&context->mm);

	dma_free_wc(context->global->dev, PT_SIZE, v1_context->pgtable_cpu,
		    v1_context->pgtable_dma);

	context->global->v1.shared_context = NULL;

	kfree(v1_context);
}

static int etnaviv_iommuv1_map(struct etnaviv_iommu_context *context,
			       unsigned long iova, phys_addr_t paddr,
			       size_t size, int prot)
{
	struct etnaviv_iommuv1_context *v1_context = to_v1_context(context);
	unsigned int index = (iova - GPU_MEM_START) / SZ_4K;

	if (size != SZ_4K)
		return -EINVAL;

	v1_context->pgtable_cpu[index] = paddr;

	return 0;
}

static size_t etnaviv_iommuv1_unmap(struct etnaviv_iommu_context *context,
	unsigned long iova, size_t size)
{
	struct etnaviv_iommuv1_context *v1_context = to_v1_context(context);
	unsigned int index = (iova - GPU_MEM_START) / SZ_4K;

	if (size != SZ_4K)
		return -EINVAL;

	v1_context->pgtable_cpu[index] = context->global->bad_page_dma;

	return SZ_4K;
}

static size_t etnaviv_iommuv1_dump_size(struct etnaviv_iommu_context *context)
{
	return PT_SIZE;
}

static void etnaviv_iommuv1_dump(struct etnaviv_iommu_context *context,
				 void *buf)
{
	struct etnaviv_iommuv1_context *v1_context = to_v1_context(context);

	memcpy(buf, v1_context->pgtable_cpu, PT_SIZE);
}

static void etnaviv_iommuv1_restore(struct etnaviv_gpu *gpu,
			     struct etnaviv_iommu_context *context)
{
	struct etnaviv_iommuv1_context *v1_context = to_v1_context(context);
	u32 pgtable;

	/* set base addresses */
	gpu_write(gpu, VIVS_MC_MEMORY_BASE_ADDR_RA, context->global->memory_base);
	gpu_write(gpu, VIVS_MC_MEMORY_BASE_ADDR_FE, context->global->memory_base);
	gpu_write(gpu, VIVS_MC_MEMORY_BASE_ADDR_TX, context->global->memory_base);
	gpu_write(gpu, VIVS_MC_MEMORY_BASE_ADDR_PEZ, context->global->memory_base);
	gpu_write(gpu, VIVS_MC_MEMORY_BASE_ADDR_PE, context->global->memory_base);

	/* set page table address in MC */
	pgtable = (u32)v1_context->pgtable_dma;

	gpu_write(gpu, VIVS_MC_MMU_FE_PAGE_TABLE, pgtable);
	gpu_write(gpu, VIVS_MC_MMU_TX_PAGE_TABLE, pgtable);
	gpu_write(gpu, VIVS_MC_MMU_PE_PAGE_TABLE, pgtable);
	gpu_write(gpu, VIVS_MC_MMU_PEZ_PAGE_TABLE, pgtable);
	gpu_write(gpu, VIVS_MC_MMU_RA_PAGE_TABLE, pgtable);
}


const struct etnaviv_iommu_ops etnaviv_iommuv1_ops = {
	.free = etnaviv_iommuv1_free,
	.map = etnaviv_iommuv1_map,
	.unmap = etnaviv_iommuv1_unmap,
	.dump_size = etnaviv_iommuv1_dump_size,
	.dump = etnaviv_iommuv1_dump,
	.restore = etnaviv_iommuv1_restore,
};

struct etnaviv_iommu_context *
etnaviv_iommuv1_context_alloc(struct etnaviv_iommu_global *global)
{
	struct etnaviv_iommuv1_context *v1_context;
	struct etnaviv_iommu_context *context;

	mutex_lock(&global->lock);

	/*
	 * MMUv1 does not support switching between different contexts without
	 * a stop the world operation, so we only support a single shared
	 * context with this version.
	 */
	if (global->v1.shared_context) {
		context = global->v1.shared_context;
		etnaviv_iommu_context_get(context);
		mutex_unlock(&global->lock);
		return context;
	}

	v1_context = kzalloc(sizeof(*v1_context), GFP_KERNEL);
	if (!v1_context) {
		mutex_unlock(&global->lock);
		return NULL;
	}

	v1_context->pgtable_cpu = dma_alloc_wc(global->dev, PT_SIZE,
					       &v1_context->pgtable_dma,
					       GFP_KERNEL);
	if (!v1_context->pgtable_cpu)
		goto out_free;

	memset32(v1_context->pgtable_cpu, global->bad_page_dma, PT_ENTRIES);

	context = &v1_context->base;
	context->global = global;
	kref_init(&context->refcount);
	mutex_init(&context->lock);
	INIT_LIST_HEAD(&context->mappings);
	drm_mm_init(&context->mm, GPU_MEM_START, PT_ENTRIES * SZ_4K);
	context->global->v1.shared_context = context;

	mutex_unlock(&global->lock);

	return context;

out_free:
	mutex_unlock(&global->lock);
	kfree(v1_context);
	return NULL;
}
