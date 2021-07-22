// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2016-2018 Etnaviv Project
 */

#include <linux/bitops.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "etnaviv_cmdbuf.h"
#include "etnaviv_gpu.h"
#include "etnaviv_mmu.h"
#include "state.xml.h"
#include "state_hi.xml.h"

#define MMUv2_PTE_PRESENT		BIT(0)
#define MMUv2_PTE_EXCEPTION		BIT(1)
#define MMUv2_PTE_WRITEABLE		BIT(2)

#define MMUv2_MTLB_MASK			0xffc00000
#define MMUv2_MTLB_SHIFT		22
#define MMUv2_STLB_MASK			0x003ff000
#define MMUv2_STLB_SHIFT		12

#define MMUv2_MAX_STLB_ENTRIES		1024

struct etnaviv_iommuv2_context {
	struct etnaviv_iommu_context base;
	unsigned short id;
	/* M(aster) TLB aka first level pagetable */
	u32 *mtlb_cpu;
	dma_addr_t mtlb_dma;
	/* S(lave) TLB aka second level pagetable */
	u32 *stlb_cpu[MMUv2_MAX_STLB_ENTRIES];
	dma_addr_t stlb_dma[MMUv2_MAX_STLB_ENTRIES];
};

static struct etnaviv_iommuv2_context *
to_v2_context(struct etnaviv_iommu_context *context)
{
	return container_of(context, struct etnaviv_iommuv2_context, base);
}

static void etnaviv_iommuv2_free(struct etnaviv_iommu_context *context)
{
	struct etnaviv_iommuv2_context *v2_context = to_v2_context(context);
	int i;

	drm_mm_takedown(&context->mm);

	for (i = 0; i < MMUv2_MAX_STLB_ENTRIES; i++) {
		if (v2_context->stlb_cpu[i])
			dma_free_wc(context->global->dev, SZ_4K,
				    v2_context->stlb_cpu[i],
				    v2_context->stlb_dma[i]);
	}

	dma_free_wc(context->global->dev, SZ_4K, v2_context->mtlb_cpu,
		    v2_context->mtlb_dma);

	clear_bit(v2_context->id, context->global->v2.pta_alloc);

	vfree(v2_context);
}
static int
etnaviv_iommuv2_ensure_stlb(struct etnaviv_iommuv2_context *v2_context,
			    int stlb)
{
	if (v2_context->stlb_cpu[stlb])
		return 0;

	v2_context->stlb_cpu[stlb] =
			dma_alloc_wc(v2_context->base.global->dev, SZ_4K,
				     &v2_context->stlb_dma[stlb],
				     GFP_KERNEL);

	if (!v2_context->stlb_cpu[stlb])
		return -ENOMEM;

	memset32(v2_context->stlb_cpu[stlb], MMUv2_PTE_EXCEPTION,
		 SZ_4K / sizeof(u32));

	v2_context->mtlb_cpu[stlb] =
			v2_context->stlb_dma[stlb] | MMUv2_PTE_PRESENT;

	return 0;
}

static int etnaviv_iommuv2_map(struct etnaviv_iommu_context *context,
			       unsigned long iova, phys_addr_t paddr,
			       size_t size, int prot)
{
	struct etnaviv_iommuv2_context *v2_context = to_v2_context(context);
	int mtlb_entry, stlb_entry, ret;
	u32 entry = lower_32_bits(paddr) | MMUv2_PTE_PRESENT;

	if (size != SZ_4K)
		return -EINVAL;

	if (IS_ENABLED(CONFIG_PHYS_ADDR_T_64BIT))
		entry |= (upper_32_bits(paddr) & 0xff) << 4;

	if (prot & ETNAVIV_PROT_WRITE)
		entry |= MMUv2_PTE_WRITEABLE;

	mtlb_entry = (iova & MMUv2_MTLB_MASK) >> MMUv2_MTLB_SHIFT;
	stlb_entry = (iova & MMUv2_STLB_MASK) >> MMUv2_STLB_SHIFT;

	ret = etnaviv_iommuv2_ensure_stlb(v2_context, mtlb_entry);
	if (ret)
		return ret;

	v2_context->stlb_cpu[mtlb_entry][stlb_entry] = entry;

	return 0;
}

static size_t etnaviv_iommuv2_unmap(struct etnaviv_iommu_context *context,
				    unsigned long iova, size_t size)
{
	struct etnaviv_iommuv2_context *etnaviv_domain = to_v2_context(context);
	int mtlb_entry, stlb_entry;

	if (size != SZ_4K)
		return -EINVAL;

	mtlb_entry = (iova & MMUv2_MTLB_MASK) >> MMUv2_MTLB_SHIFT;
	stlb_entry = (iova & MMUv2_STLB_MASK) >> MMUv2_STLB_SHIFT;

	etnaviv_domain->stlb_cpu[mtlb_entry][stlb_entry] = MMUv2_PTE_EXCEPTION;

	return SZ_4K;
}

static size_t etnaviv_iommuv2_dump_size(struct etnaviv_iommu_context *context)
{
	struct etnaviv_iommuv2_context *v2_context = to_v2_context(context);
	size_t dump_size = SZ_4K;
	int i;

	for (i = 0; i < MMUv2_MAX_STLB_ENTRIES; i++)
		if (v2_context->mtlb_cpu[i] & MMUv2_PTE_PRESENT)
			dump_size += SZ_4K;

	return dump_size;
}

static void etnaviv_iommuv2_dump(struct etnaviv_iommu_context *context, void *buf)
{
	struct etnaviv_iommuv2_context *v2_context = to_v2_context(context);
	int i;

	memcpy(buf, v2_context->mtlb_cpu, SZ_4K);
	buf += SZ_4K;
	for (i = 0; i < MMUv2_MAX_STLB_ENTRIES; i++)
		if (v2_context->mtlb_cpu[i] & MMUv2_PTE_PRESENT) {
			memcpy(buf, v2_context->stlb_cpu[i], SZ_4K);
			buf += SZ_4K;
		}
}

static void etnaviv_iommuv2_restore_nonsec(struct etnaviv_gpu *gpu,
	struct etnaviv_iommu_context *context)
{
	struct etnaviv_iommuv2_context *v2_context = to_v2_context(context);
	u16 prefetch;

	/* If the MMU is already enabled the state is still there. */
	if (gpu_read(gpu, VIVS_MMUv2_CONTROL) & VIVS_MMUv2_CONTROL_ENABLE)
		return;

	prefetch = etnaviv_buffer_config_mmuv2(gpu,
				(u32)v2_context->mtlb_dma,
				(u32)context->global->bad_page_dma);
	etnaviv_gpu_start_fe(gpu, (u32)etnaviv_cmdbuf_get_pa(&gpu->buffer),
			     prefetch);
	etnaviv_gpu_wait_idle(gpu, 100);

	gpu_write(gpu, VIVS_MMUv2_CONTROL, VIVS_MMUv2_CONTROL_ENABLE);
}

static void etnaviv_iommuv2_restore_sec(struct etnaviv_gpu *gpu,
	struct etnaviv_iommu_context *context)
{
	struct etnaviv_iommuv2_context *v2_context = to_v2_context(context);
	u16 prefetch;

	/* If the MMU is already enabled the state is still there. */
	if (gpu_read(gpu, VIVS_MMUv2_SEC_CONTROL) & VIVS_MMUv2_SEC_CONTROL_ENABLE)
		return;

	gpu_write(gpu, VIVS_MMUv2_PTA_ADDRESS_LOW,
		  lower_32_bits(context->global->v2.pta_dma));
	gpu_write(gpu, VIVS_MMUv2_PTA_ADDRESS_HIGH,
		  upper_32_bits(context->global->v2.pta_dma));
	gpu_write(gpu, VIVS_MMUv2_PTA_CONTROL, VIVS_MMUv2_PTA_CONTROL_ENABLE);

	gpu_write(gpu, VIVS_MMUv2_NONSEC_SAFE_ADDR_LOW,
		  lower_32_bits(context->global->bad_page_dma));
	gpu_write(gpu, VIVS_MMUv2_SEC_SAFE_ADDR_LOW,
		  lower_32_bits(context->global->bad_page_dma));
	gpu_write(gpu, VIVS_MMUv2_SAFE_ADDRESS_CONFIG,
		  VIVS_MMUv2_SAFE_ADDRESS_CONFIG_NON_SEC_SAFE_ADDR_HIGH(
		  upper_32_bits(context->global->bad_page_dma)) |
		  VIVS_MMUv2_SAFE_ADDRESS_CONFIG_SEC_SAFE_ADDR_HIGH(
		  upper_32_bits(context->global->bad_page_dma)));

	context->global->v2.pta_cpu[v2_context->id] = v2_context->mtlb_dma |
				 	 VIVS_MMUv2_CONFIGURATION_MODE_MODE4_K;

	/* trigger a PTA load through the FE */
	prefetch = etnaviv_buffer_config_pta(gpu, v2_context->id);
	etnaviv_gpu_start_fe(gpu, (u32)etnaviv_cmdbuf_get_pa(&gpu->buffer),
			     prefetch);
	etnaviv_gpu_wait_idle(gpu, 100);

	gpu_write(gpu, VIVS_MMUv2_SEC_CONTROL, VIVS_MMUv2_SEC_CONTROL_ENABLE);
}

u32 etnaviv_iommuv2_get_mtlb_addr(struct etnaviv_iommu_context *context)
{
	struct etnaviv_iommuv2_context *v2_context = to_v2_context(context);

	return v2_context->mtlb_dma;
}

unsigned short etnaviv_iommuv2_get_pta_id(struct etnaviv_iommu_context *context)
{
	struct etnaviv_iommuv2_context *v2_context = to_v2_context(context);

	return v2_context->id;
}
static void etnaviv_iommuv2_restore(struct etnaviv_gpu *gpu,
				    struct etnaviv_iommu_context *context)
{
	switch (gpu->sec_mode) {
	case ETNA_SEC_NONE:
		etnaviv_iommuv2_restore_nonsec(gpu, context);
		break;
	case ETNA_SEC_KERNEL:
		etnaviv_iommuv2_restore_sec(gpu, context);
		break;
	default:
		WARN(1, "unhandled GPU security mode\n");
		break;
	}
}

const struct etnaviv_iommu_ops etnaviv_iommuv2_ops = {
	.free = etnaviv_iommuv2_free,
	.map = etnaviv_iommuv2_map,
	.unmap = etnaviv_iommuv2_unmap,
	.dump_size = etnaviv_iommuv2_dump_size,
	.dump = etnaviv_iommuv2_dump,
	.restore = etnaviv_iommuv2_restore,
};

struct etnaviv_iommu_context *
etnaviv_iommuv2_context_alloc(struct etnaviv_iommu_global *global)
{
	struct etnaviv_iommuv2_context *v2_context;
	struct etnaviv_iommu_context *context;

	v2_context = vzalloc(sizeof(*v2_context));
	if (!v2_context)
		return NULL;

	mutex_lock(&global->lock);
	v2_context->id = find_first_zero_bit(global->v2.pta_alloc,
					     ETNAVIV_PTA_ENTRIES);
	if (v2_context->id < ETNAVIV_PTA_ENTRIES) {
		set_bit(v2_context->id, global->v2.pta_alloc);
	} else {
		mutex_unlock(&global->lock);
		goto out_free;
	}
	mutex_unlock(&global->lock);

	v2_context->mtlb_cpu = dma_alloc_wc(global->dev, SZ_4K,
					    &v2_context->mtlb_dma, GFP_KERNEL);
	if (!v2_context->mtlb_cpu)
		goto out_free_id;

	memset32(v2_context->mtlb_cpu, MMUv2_PTE_EXCEPTION,
		 MMUv2_MAX_STLB_ENTRIES);

	global->v2.pta_cpu[v2_context->id] = v2_context->mtlb_dma;

	context = &v2_context->base;
	context->global = global;
	kref_init(&context->refcount);
	mutex_init(&context->lock);
	INIT_LIST_HEAD(&context->mappings);
	drm_mm_init(&context->mm, SZ_4K, (u64)SZ_1G * 4 - SZ_4K);

	return context;

out_free_id:
	clear_bit(v2_context->id, global->v2.pta_alloc);
out_free:
	vfree(v2_context);
	return NULL;
}
