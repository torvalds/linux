/*
 * Copyright (C) 2014 Christian Gmeiner <christian.gmeiner@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/platform_device.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/bitops.h>

#include "etnaviv_gpu.h"
#include "etnaviv_mmu.h"
#include "etnaviv_iommu.h"
#include "state_hi.xml.h"

#define PT_SIZE		SZ_2M
#define PT_ENTRIES	(PT_SIZE / sizeof(u32))

#define GPU_MEM_START	0x80000000

struct etnaviv_iommuv1_domain {
	struct etnaviv_iommu_domain base;
	u32 *pgtable_cpu;
	dma_addr_t pgtable_dma;
};

static struct etnaviv_iommuv1_domain *
to_etnaviv_domain(struct etnaviv_iommu_domain *domain)
{
	return container_of(domain, struct etnaviv_iommuv1_domain, base);
}

static int __etnaviv_iommu_init(struct etnaviv_iommuv1_domain *etnaviv_domain)
{
	u32 *p;
	int i;

	etnaviv_domain->base.bad_page_cpu = dma_alloc_coherent(
						etnaviv_domain->base.dev,
						SZ_4K,
						&etnaviv_domain->base.bad_page_dma,
						GFP_KERNEL);
	if (!etnaviv_domain->base.bad_page_cpu)
		return -ENOMEM;

	p = etnaviv_domain->base.bad_page_cpu;
	for (i = 0; i < SZ_4K / 4; i++)
		*p++ = 0xdead55aa;

	etnaviv_domain->pgtable_cpu =
			dma_alloc_coherent(etnaviv_domain->base.dev, PT_SIZE,
					   &etnaviv_domain->pgtable_dma,
					   GFP_KERNEL);
	if (!etnaviv_domain->pgtable_cpu) {
		dma_free_coherent(etnaviv_domain->base.dev, SZ_4K,
				  etnaviv_domain->base.bad_page_cpu,
				  etnaviv_domain->base.bad_page_dma);
		return -ENOMEM;
	}

	memset32(etnaviv_domain->pgtable_cpu, etnaviv_domain->base.bad_page_dma,
		 PT_ENTRIES);

	return 0;
}

static void etnaviv_iommuv1_domain_free(struct etnaviv_iommu_domain *domain)
{
	struct etnaviv_iommuv1_domain *etnaviv_domain =
			to_etnaviv_domain(domain);

	dma_free_coherent(etnaviv_domain->base.dev, PT_SIZE,
			  etnaviv_domain->pgtable_cpu,
			  etnaviv_domain->pgtable_dma);

	dma_free_coherent(etnaviv_domain->base.dev, SZ_4K,
			  etnaviv_domain->base.bad_page_cpu,
			  etnaviv_domain->base.bad_page_dma);

	kfree(etnaviv_domain);
}

static int etnaviv_iommuv1_map(struct etnaviv_iommu_domain *domain,
			       unsigned long iova, phys_addr_t paddr,
			       size_t size, int prot)
{
	struct etnaviv_iommuv1_domain *etnaviv_domain = to_etnaviv_domain(domain);
	unsigned int index = (iova - GPU_MEM_START) / SZ_4K;

	if (size != SZ_4K)
		return -EINVAL;

	etnaviv_domain->pgtable_cpu[index] = paddr;

	return 0;
}

static size_t etnaviv_iommuv1_unmap(struct etnaviv_iommu_domain *domain,
	unsigned long iova, size_t size)
{
	struct etnaviv_iommuv1_domain *etnaviv_domain =
			to_etnaviv_domain(domain);
	unsigned int index = (iova - GPU_MEM_START) / SZ_4K;

	if (size != SZ_4K)
		return -EINVAL;

	etnaviv_domain->pgtable_cpu[index] = etnaviv_domain->base.bad_page_dma;

	return SZ_4K;
}

static size_t etnaviv_iommuv1_dump_size(struct etnaviv_iommu_domain *domain)
{
	return PT_SIZE;
}

static void etnaviv_iommuv1_dump(struct etnaviv_iommu_domain *domain, void *buf)
{
	struct etnaviv_iommuv1_domain *etnaviv_domain =
			to_etnaviv_domain(domain);

	memcpy(buf, etnaviv_domain->pgtable_cpu, PT_SIZE);
}

void etnaviv_iommuv1_restore(struct etnaviv_gpu *gpu)
{
	struct etnaviv_iommuv1_domain *etnaviv_domain =
			to_etnaviv_domain(gpu->mmu->domain);
	u32 pgtable;

	/* set base addresses */
	gpu_write(gpu, VIVS_MC_MEMORY_BASE_ADDR_RA, gpu->memory_base);
	gpu_write(gpu, VIVS_MC_MEMORY_BASE_ADDR_FE, gpu->memory_base);
	gpu_write(gpu, VIVS_MC_MEMORY_BASE_ADDR_TX, gpu->memory_base);
	gpu_write(gpu, VIVS_MC_MEMORY_BASE_ADDR_PEZ, gpu->memory_base);
	gpu_write(gpu, VIVS_MC_MEMORY_BASE_ADDR_PE, gpu->memory_base);

	/* set page table address in MC */
	pgtable = (u32)etnaviv_domain->pgtable_dma;

	gpu_write(gpu, VIVS_MC_MMU_FE_PAGE_TABLE, pgtable);
	gpu_write(gpu, VIVS_MC_MMU_TX_PAGE_TABLE, pgtable);
	gpu_write(gpu, VIVS_MC_MMU_PE_PAGE_TABLE, pgtable);
	gpu_write(gpu, VIVS_MC_MMU_PEZ_PAGE_TABLE, pgtable);
	gpu_write(gpu, VIVS_MC_MMU_RA_PAGE_TABLE, pgtable);
}

const struct etnaviv_iommu_domain_ops etnaviv_iommuv1_ops = {
	.free = etnaviv_iommuv1_domain_free,
	.map = etnaviv_iommuv1_map,
	.unmap = etnaviv_iommuv1_unmap,
	.dump_size = etnaviv_iommuv1_dump_size,
	.dump = etnaviv_iommuv1_dump,
};

struct etnaviv_iommu_domain *
etnaviv_iommuv1_domain_alloc(struct etnaviv_gpu *gpu)
{
	struct etnaviv_iommuv1_domain *etnaviv_domain;
	struct etnaviv_iommu_domain *domain;
	int ret;

	etnaviv_domain = kzalloc(sizeof(*etnaviv_domain), GFP_KERNEL);
	if (!etnaviv_domain)
		return NULL;

	domain = &etnaviv_domain->base;

	domain->dev = gpu->dev;
	domain->base = GPU_MEM_START;
	domain->size = PT_ENTRIES * SZ_4K;
	domain->ops = &etnaviv_iommuv1_ops;

	ret = __etnaviv_iommu_init(etnaviv_domain);
	if (ret)
		goto out_free;

	return &etnaviv_domain->base;

out_free:
	kfree(etnaviv_domain);
	return NULL;
}
