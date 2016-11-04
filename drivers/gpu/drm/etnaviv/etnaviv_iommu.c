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

#include <linux/iommu.h>
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

struct etnaviv_iommu_domain_pgtable {
	u32 *pgtable;
	dma_addr_t paddr;
};

struct etnaviv_iommu_domain {
	struct iommu_domain domain;
	struct device *dev;
	void *bad_page_cpu;
	dma_addr_t bad_page_dma;
	struct etnaviv_iommu_domain_pgtable pgtable;
	spinlock_t map_lock;
};

static struct etnaviv_iommu_domain *to_etnaviv_domain(struct iommu_domain *domain)
{
	return container_of(domain, struct etnaviv_iommu_domain, domain);
}

static int pgtable_alloc(struct etnaviv_iommu_domain_pgtable *pgtable,
			 size_t size)
{
	pgtable->pgtable = dma_alloc_coherent(NULL, size, &pgtable->paddr, GFP_KERNEL);
	if (!pgtable->pgtable)
		return -ENOMEM;

	return 0;
}

static void pgtable_free(struct etnaviv_iommu_domain_pgtable *pgtable,
			 size_t size)
{
	dma_free_coherent(NULL, size, pgtable->pgtable, pgtable->paddr);
}

static u32 pgtable_read(struct etnaviv_iommu_domain_pgtable *pgtable,
			   unsigned long iova)
{
	/* calcuate index into page table */
	unsigned int index = (iova - GPU_MEM_START) / SZ_4K;
	phys_addr_t paddr;

	paddr = pgtable->pgtable[index];

	return paddr;
}

static void pgtable_write(struct etnaviv_iommu_domain_pgtable *pgtable,
			  unsigned long iova, phys_addr_t paddr)
{
	/* calcuate index into page table */
	unsigned int index = (iova - GPU_MEM_START) / SZ_4K;

	pgtable->pgtable[index] = paddr;
}

static int __etnaviv_iommu_init(struct etnaviv_iommu_domain *etnaviv_domain)
{
	u32 *p;
	int ret, i;

	etnaviv_domain->bad_page_cpu = dma_alloc_coherent(etnaviv_domain->dev,
						  SZ_4K,
						  &etnaviv_domain->bad_page_dma,
						  GFP_KERNEL);
	if (!etnaviv_domain->bad_page_cpu)
		return -ENOMEM;

	p = etnaviv_domain->bad_page_cpu;
	for (i = 0; i < SZ_4K / 4; i++)
		*p++ = 0xdead55aa;

	ret = pgtable_alloc(&etnaviv_domain->pgtable, PT_SIZE);
	if (ret < 0) {
		dma_free_coherent(etnaviv_domain->dev, SZ_4K,
				  etnaviv_domain->bad_page_cpu,
				  etnaviv_domain->bad_page_dma);
		return ret;
	}

	for (i = 0; i < PT_ENTRIES; i++)
		etnaviv_domain->pgtable.pgtable[i] =
			etnaviv_domain->bad_page_dma;

	spin_lock_init(&etnaviv_domain->map_lock);

	return 0;
}

static void etnaviv_domain_free(struct iommu_domain *domain)
{
	struct etnaviv_iommu_domain *etnaviv_domain = to_etnaviv_domain(domain);

	pgtable_free(&etnaviv_domain->pgtable, PT_SIZE);

	dma_free_coherent(etnaviv_domain->dev, SZ_4K,
			  etnaviv_domain->bad_page_cpu,
			  etnaviv_domain->bad_page_dma);

	kfree(etnaviv_domain);
}

static int etnaviv_iommuv1_map(struct iommu_domain *domain, unsigned long iova,
	   phys_addr_t paddr, size_t size, int prot)
{
	struct etnaviv_iommu_domain *etnaviv_domain = to_etnaviv_domain(domain);

	if (size != SZ_4K)
		return -EINVAL;

	spin_lock(&etnaviv_domain->map_lock);
	pgtable_write(&etnaviv_domain->pgtable, iova, paddr);
	spin_unlock(&etnaviv_domain->map_lock);

	return 0;
}

static size_t etnaviv_iommuv1_unmap(struct iommu_domain *domain,
	unsigned long iova, size_t size)
{
	struct etnaviv_iommu_domain *etnaviv_domain = to_etnaviv_domain(domain);

	if (size != SZ_4K)
		return -EINVAL;

	spin_lock(&etnaviv_domain->map_lock);
	pgtable_write(&etnaviv_domain->pgtable, iova,
		      etnaviv_domain->bad_page_dma);
	spin_unlock(&etnaviv_domain->map_lock);

	return SZ_4K;
}

static phys_addr_t etnaviv_iommu_iova_to_phys(struct iommu_domain *domain,
	dma_addr_t iova)
{
	struct etnaviv_iommu_domain *etnaviv_domain = to_etnaviv_domain(domain);

	return pgtable_read(&etnaviv_domain->pgtable, iova);
}

static size_t etnaviv_iommuv1_dump_size(struct iommu_domain *domain)
{
	return PT_SIZE;
}

static void etnaviv_iommuv1_dump(struct iommu_domain *domain, void *buf)
{
	struct etnaviv_iommu_domain *etnaviv_domain = to_etnaviv_domain(domain);

	memcpy(buf, etnaviv_domain->pgtable.pgtable, PT_SIZE);
}

static struct etnaviv_iommu_ops etnaviv_iommu_ops = {
	.ops = {
		.domain_free = etnaviv_domain_free,
		.map = etnaviv_iommuv1_map,
		.unmap = etnaviv_iommuv1_unmap,
		.iova_to_phys = etnaviv_iommu_iova_to_phys,
		.pgsize_bitmap = SZ_4K,
	},
	.dump_size = etnaviv_iommuv1_dump_size,
	.dump = etnaviv_iommuv1_dump,
};

void etnaviv_iommuv1_restore(struct etnaviv_gpu *gpu)
{
	struct etnaviv_iommu_domain *etnaviv_domain =
			to_etnaviv_domain(gpu->mmu->domain);
	u32 pgtable;

	/* set base addresses */
	gpu_write(gpu, VIVS_MC_MEMORY_BASE_ADDR_RA, gpu->memory_base);
	gpu_write(gpu, VIVS_MC_MEMORY_BASE_ADDR_FE, gpu->memory_base);
	gpu_write(gpu, VIVS_MC_MEMORY_BASE_ADDR_TX, gpu->memory_base);
	gpu_write(gpu, VIVS_MC_MEMORY_BASE_ADDR_PEZ, gpu->memory_base);
	gpu_write(gpu, VIVS_MC_MEMORY_BASE_ADDR_PE, gpu->memory_base);

	/* set page table address in MC */
	pgtable = (u32)etnaviv_domain->pgtable.paddr;

	gpu_write(gpu, VIVS_MC_MMU_FE_PAGE_TABLE, pgtable);
	gpu_write(gpu, VIVS_MC_MMU_TX_PAGE_TABLE, pgtable);
	gpu_write(gpu, VIVS_MC_MMU_PE_PAGE_TABLE, pgtable);
	gpu_write(gpu, VIVS_MC_MMU_PEZ_PAGE_TABLE, pgtable);
	gpu_write(gpu, VIVS_MC_MMU_RA_PAGE_TABLE, pgtable);
}

struct iommu_domain *etnaviv_iommuv1_domain_alloc(struct etnaviv_gpu *gpu)
{
	struct etnaviv_iommu_domain *etnaviv_domain;
	int ret;

	etnaviv_domain = kzalloc(sizeof(*etnaviv_domain), GFP_KERNEL);
	if (!etnaviv_domain)
		return NULL;

	etnaviv_domain->dev = gpu->dev;

	etnaviv_domain->domain.type = __IOMMU_DOMAIN_PAGING;
	etnaviv_domain->domain.ops = &etnaviv_iommu_ops.ops;
	etnaviv_domain->domain.pgsize_bitmap = SZ_4K;
	etnaviv_domain->domain.geometry.aperture_start = GPU_MEM_START;
	etnaviv_domain->domain.geometry.aperture_end = GPU_MEM_START + PT_ENTRIES * SZ_4K - 1;

	ret = __etnaviv_iommu_init(etnaviv_domain);
	if (ret)
		goto out_free;

	return &etnaviv_domain->domain;

out_free:
	kfree(etnaviv_domain);
	return NULL;
}
