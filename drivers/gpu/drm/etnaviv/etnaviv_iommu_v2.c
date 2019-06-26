// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2016-2018 Etnaviv Project
 */

#include <linux/platform_device.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/bitops.h>

#include "etnaviv_cmdbuf.h"
#include "etnaviv_gpu.h"
#include "etnaviv_mmu.h"
#include "etnaviv_iommu.h"
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

struct etnaviv_iommuv2_domain {
	struct etnaviv_iommu_domain base;
	/* P(age) T(able) A(rray) */
	u64 *pta_cpu;
	dma_addr_t pta_dma;
	/* M(aster) TLB aka first level pagetable */
	u32 *mtlb_cpu;
	dma_addr_t mtlb_dma;
	/* S(lave) TLB aka second level pagetable */
	u32 *stlb_cpu[MMUv2_MAX_STLB_ENTRIES];
	dma_addr_t stlb_dma[MMUv2_MAX_STLB_ENTRIES];
};

static struct etnaviv_iommuv2_domain *
to_etnaviv_domain(struct etnaviv_iommu_domain *domain)
{
	return container_of(domain, struct etnaviv_iommuv2_domain, base);
}

static int
etnaviv_iommuv2_ensure_stlb(struct etnaviv_iommuv2_domain *etnaviv_domain,
			    int stlb)
{
	if (etnaviv_domain->stlb_cpu[stlb])
		return 0;

	etnaviv_domain->stlb_cpu[stlb] =
			dma_alloc_wc(etnaviv_domain->base.dev, SZ_4K,
				     &etnaviv_domain->stlb_dma[stlb],
				     GFP_KERNEL);

	if (!etnaviv_domain->stlb_cpu[stlb])
		return -ENOMEM;

	memset32(etnaviv_domain->stlb_cpu[stlb], MMUv2_PTE_EXCEPTION,
		 SZ_4K / sizeof(u32));

	etnaviv_domain->mtlb_cpu[stlb] = etnaviv_domain->stlb_dma[stlb] |
						      MMUv2_PTE_PRESENT;
	return 0;
}

static int etnaviv_iommuv2_map(struct etnaviv_iommu_domain *domain,
			       unsigned long iova, phys_addr_t paddr,
			       size_t size, int prot)
{
	struct etnaviv_iommuv2_domain *etnaviv_domain =
			to_etnaviv_domain(domain);
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

	ret = etnaviv_iommuv2_ensure_stlb(etnaviv_domain, mtlb_entry);
	if (ret)
		return ret;

	etnaviv_domain->stlb_cpu[mtlb_entry][stlb_entry] = entry;

	return 0;
}

static size_t etnaviv_iommuv2_unmap(struct etnaviv_iommu_domain *domain,
				    unsigned long iova, size_t size)
{
	struct etnaviv_iommuv2_domain *etnaviv_domain =
			to_etnaviv_domain(domain);
	int mtlb_entry, stlb_entry;

	if (size != SZ_4K)
		return -EINVAL;

	mtlb_entry = (iova & MMUv2_MTLB_MASK) >> MMUv2_MTLB_SHIFT;
	stlb_entry = (iova & MMUv2_STLB_MASK) >> MMUv2_STLB_SHIFT;

	etnaviv_domain->stlb_cpu[mtlb_entry][stlb_entry] = MMUv2_PTE_EXCEPTION;

	return SZ_4K;
}

static int etnaviv_iommuv2_init(struct etnaviv_iommuv2_domain *etnaviv_domain)
{
	int ret;

	/* allocate scratch page */
	etnaviv_domain->base.bad_page_cpu =
			dma_alloc_wc(etnaviv_domain->base.dev, SZ_4K,
				     &etnaviv_domain->base.bad_page_dma,
				     GFP_KERNEL);
	if (!etnaviv_domain->base.bad_page_cpu) {
		ret = -ENOMEM;
		goto fail_mem;
	}

	memset32(etnaviv_domain->base.bad_page_cpu, 0xdead55aa,
		 SZ_4K / sizeof(u32));

	etnaviv_domain->pta_cpu = dma_alloc_wc(etnaviv_domain->base.dev,
					       SZ_4K, &etnaviv_domain->pta_dma,
					       GFP_KERNEL);
	if (!etnaviv_domain->pta_cpu) {
		ret = -ENOMEM;
		goto fail_mem;
	}

	etnaviv_domain->mtlb_cpu = dma_alloc_wc(etnaviv_domain->base.dev,
						SZ_4K, &etnaviv_domain->mtlb_dma,
						GFP_KERNEL);
	if (!etnaviv_domain->mtlb_cpu) {
		ret = -ENOMEM;
		goto fail_mem;
	}

	memset32(etnaviv_domain->mtlb_cpu, MMUv2_PTE_EXCEPTION,
		 MMUv2_MAX_STLB_ENTRIES);

	return 0;

fail_mem:
	if (etnaviv_domain->base.bad_page_cpu)
		dma_free_wc(etnaviv_domain->base.dev, SZ_4K,
			    etnaviv_domain->base.bad_page_cpu,
			    etnaviv_domain->base.bad_page_dma);

	if (etnaviv_domain->pta_cpu)
		dma_free_wc(etnaviv_domain->base.dev, SZ_4K,
			    etnaviv_domain->pta_cpu, etnaviv_domain->pta_dma);

	if (etnaviv_domain->mtlb_cpu)
		dma_free_wc(etnaviv_domain->base.dev, SZ_4K,
			    etnaviv_domain->mtlb_cpu, etnaviv_domain->mtlb_dma);

	return ret;
}

static void etnaviv_iommuv2_domain_free(struct etnaviv_iommu_domain *domain)
{
	struct etnaviv_iommuv2_domain *etnaviv_domain =
			to_etnaviv_domain(domain);
	int i;

	dma_free_wc(etnaviv_domain->base.dev, SZ_4K,
		    etnaviv_domain->base.bad_page_cpu,
		    etnaviv_domain->base.bad_page_dma);

	dma_free_wc(etnaviv_domain->base.dev, SZ_4K,
		    etnaviv_domain->pta_cpu, etnaviv_domain->pta_dma);

	dma_free_wc(etnaviv_domain->base.dev, SZ_4K,
		    etnaviv_domain->mtlb_cpu, etnaviv_domain->mtlb_dma);

	for (i = 0; i < MMUv2_MAX_STLB_ENTRIES; i++) {
		if (etnaviv_domain->stlb_cpu[i])
			dma_free_wc(etnaviv_domain->base.dev, SZ_4K,
				    etnaviv_domain->stlb_cpu[i],
				    etnaviv_domain->stlb_dma[i]);
	}

	vfree(etnaviv_domain);
}

static size_t etnaviv_iommuv2_dump_size(struct etnaviv_iommu_domain *domain)
{
	struct etnaviv_iommuv2_domain *etnaviv_domain =
			to_etnaviv_domain(domain);
	size_t dump_size = SZ_4K;
	int i;

	for (i = 0; i < MMUv2_MAX_STLB_ENTRIES; i++)
		if (etnaviv_domain->mtlb_cpu[i] & MMUv2_PTE_PRESENT)
			dump_size += SZ_4K;

	return dump_size;
}

static void etnaviv_iommuv2_dump(struct etnaviv_iommu_domain *domain, void *buf)
{
	struct etnaviv_iommuv2_domain *etnaviv_domain =
			to_etnaviv_domain(domain);
	int i;

	memcpy(buf, etnaviv_domain->mtlb_cpu, SZ_4K);
	buf += SZ_4K;
	for (i = 0; i < MMUv2_MAX_STLB_ENTRIES; i++, buf += SZ_4K)
		if (etnaviv_domain->mtlb_cpu[i] & MMUv2_PTE_PRESENT)
			memcpy(buf, etnaviv_domain->stlb_cpu[i], SZ_4K);
}

static void etnaviv_iommuv2_restore_nonsec(struct etnaviv_gpu *gpu)
{
	struct etnaviv_iommuv2_domain *etnaviv_domain =
			to_etnaviv_domain(gpu->mmu->domain);
	u16 prefetch;

	/* If the MMU is already enabled the state is still there. */
	if (gpu_read(gpu, VIVS_MMUv2_CONTROL) & VIVS_MMUv2_CONTROL_ENABLE)
		return;

	prefetch = etnaviv_buffer_config_mmuv2(gpu,
				(u32)etnaviv_domain->mtlb_dma,
				(u32)etnaviv_domain->base.bad_page_dma);
	etnaviv_gpu_start_fe(gpu, (u32)etnaviv_cmdbuf_get_pa(&gpu->buffer),
			     prefetch);
	etnaviv_gpu_wait_idle(gpu, 100);

	gpu_write(gpu, VIVS_MMUv2_CONTROL, VIVS_MMUv2_CONTROL_ENABLE);
}

static void etnaviv_iommuv2_restore_sec(struct etnaviv_gpu *gpu)
{
	struct etnaviv_iommuv2_domain *etnaviv_domain =
				to_etnaviv_domain(gpu->mmu->domain);
	u16 prefetch;

	/* If the MMU is already enabled the state is still there. */
	if (gpu_read(gpu, VIVS_MMUv2_SEC_CONTROL) & VIVS_MMUv2_SEC_CONTROL_ENABLE)
		return;

	gpu_write(gpu, VIVS_MMUv2_PTA_ADDRESS_LOW,
		  lower_32_bits(etnaviv_domain->pta_dma));
	gpu_write(gpu, VIVS_MMUv2_PTA_ADDRESS_HIGH,
		  upper_32_bits(etnaviv_domain->pta_dma));
	gpu_write(gpu, VIVS_MMUv2_PTA_CONTROL, VIVS_MMUv2_PTA_CONTROL_ENABLE);

	gpu_write(gpu, VIVS_MMUv2_NONSEC_SAFE_ADDR_LOW,
		  lower_32_bits(etnaviv_domain->base.bad_page_dma));
	gpu_write(gpu, VIVS_MMUv2_SEC_SAFE_ADDR_LOW,
		  lower_32_bits(etnaviv_domain->base.bad_page_dma));
	gpu_write(gpu, VIVS_MMUv2_SAFE_ADDRESS_CONFIG,
		  VIVS_MMUv2_SAFE_ADDRESS_CONFIG_NON_SEC_SAFE_ADDR_HIGH(
		  upper_32_bits(etnaviv_domain->base.bad_page_dma)) |
		  VIVS_MMUv2_SAFE_ADDRESS_CONFIG_SEC_SAFE_ADDR_HIGH(
		  upper_32_bits(etnaviv_domain->base.bad_page_dma)));

	etnaviv_domain->pta_cpu[0] = etnaviv_domain->mtlb_dma |
				     VIVS_MMUv2_CONFIGURATION_MODE_MODE4_K;

	/* trigger a PTA load through the FE */
	prefetch = etnaviv_buffer_config_pta(gpu);
	etnaviv_gpu_start_fe(gpu, (u32)etnaviv_cmdbuf_get_pa(&gpu->buffer),
			     prefetch);
	etnaviv_gpu_wait_idle(gpu, 100);

	gpu_write(gpu, VIVS_MMUv2_SEC_CONTROL, VIVS_MMUv2_SEC_CONTROL_ENABLE);
}

void etnaviv_iommuv2_restore(struct etnaviv_gpu *gpu)
{
	switch (gpu->sec_mode) {
	case ETNA_SEC_NONE:
		etnaviv_iommuv2_restore_nonsec(gpu);
		break;
	case ETNA_SEC_KERNEL:
		etnaviv_iommuv2_restore_sec(gpu);
		break;
	default:
		WARN(1, "unhandled GPU security mode\n");
		break;
	}
}

static const struct etnaviv_iommu_domain_ops etnaviv_iommuv2_ops = {
	.free = etnaviv_iommuv2_domain_free,
	.map = etnaviv_iommuv2_map,
	.unmap = etnaviv_iommuv2_unmap,
	.dump_size = etnaviv_iommuv2_dump_size,
	.dump = etnaviv_iommuv2_dump,
};

struct etnaviv_iommu_domain *
etnaviv_iommuv2_domain_alloc(struct etnaviv_gpu *gpu)
{
	struct etnaviv_iommuv2_domain *etnaviv_domain;
	struct etnaviv_iommu_domain *domain;
	int ret;

	etnaviv_domain = vzalloc(sizeof(*etnaviv_domain));
	if (!etnaviv_domain)
		return NULL;

	domain = &etnaviv_domain->base;

	domain->dev = gpu->dev;
	domain->base = SZ_4K;
	domain->size = (u64)SZ_1G * 4 - SZ_4K;
	domain->ops = &etnaviv_iommuv2_ops;

	ret = etnaviv_iommuv2_init(etnaviv_domain);
	if (ret)
		goto out_free;

	return &etnaviv_domain->base;

out_free:
	vfree(etnaviv_domain);
	return NULL;
}
