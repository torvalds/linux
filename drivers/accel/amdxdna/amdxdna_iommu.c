// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025, Advanced Micro Devices, Inc.
 */

#include <drm/amdxdna_accel.h>
#include <linux/iommu.h>
#include <linux/iova.h>

#include "amdxdna_gem.h"
#include "amdxdna_pci_drv.h"

static bool force_iova;
module_param(force_iova, bool, 0600);
MODULE_PARM_DESC(force_iova, "Force use IOVA (Default false)");

static struct iova *amdxdna_iommu_alloc_iova(struct amdxdna_dev *xdna,
					     size_t size,
					     dma_addr_t *dma_addr,
					     bool size_aligned)
{
	unsigned long shift, end;
	struct iova *iova;

	end = xdna->domain->geometry.aperture_end;
	shift = iova_shift(&xdna->iovad);
	size = iova_align(&xdna->iovad, size);

	iova = alloc_iova(&xdna->iovad, size >> shift, end >> shift, size_aligned);
	if (!iova)
		return ERR_PTR(-ENOMEM);

	*dma_addr = iova_dma_addr(&xdna->iovad, iova);

	return iova;
}

int amdxdna_iommu_map_bo(struct amdxdna_dev *xdna, struct amdxdna_gem_obj *abo)
{
	struct sg_table *sgt;
	dma_addr_t dma_addr;
	struct iova *iova;
	size_t size;

	if (abo->type != AMDXDNA_BO_DEV_HEAP && abo->type != AMDXDNA_BO_SHMEM)
		return 0;

	sgt = drm_gem_shmem_get_pages_sgt(&abo->base);
	if (IS_ERR(sgt)) {
		XDNA_ERR(xdna, "Get sgt failed, ret %ld", PTR_ERR(sgt));
		return PTR_ERR(sgt);
	}

	if (!sgt->orig_nents || !sg_page(sgt->sgl)) {
		XDNA_ERR(xdna, "sgl is zero length or not page backed");
		return -EOPNOTSUPP;
	}

	iova = amdxdna_iommu_alloc_iova(xdna, abo->mem.size, &dma_addr,
					(abo->type == AMDXDNA_BO_DEV_HEAP));
	if (IS_ERR(iova)) {
		XDNA_ERR(xdna, "Alloc iova failed, ret %ld", PTR_ERR(iova));
		return PTR_ERR(iova);
	}

	size = iommu_map_sgtable(xdna->domain, dma_addr, sgt,
				 IOMMU_READ | IOMMU_WRITE);
	if (size < abo->mem.size) {
		__free_iova(&xdna->iovad, iova);
		return -ENXIO;
	}

	abo->mem.dma_addr = dma_addr;

	return 0;
}

void amdxdna_iommu_unmap_bo(struct amdxdna_dev *xdna, struct amdxdna_gem_obj *abo)
{
	size_t size;

	if (abo->mem.dma_addr == AMDXDNA_INVALID_ADDR)
		return;

	size = iova_align(&xdna->iovad, abo->mem.size);
	iommu_unmap(xdna->domain, abo->mem.dma_addr, size);
	free_iova(&xdna->iovad, iova_pfn(&xdna->iovad, abo->mem.dma_addr));
	abo->mem.dma_addr = AMDXDNA_INVALID_ADDR;
}

void *amdxdna_iommu_alloc(struct amdxdna_dev *xdna, size_t size, dma_addr_t *dma_addr)
{
	struct iova *iova;
	void *cpu_addr;
	int ret;

	iova = amdxdna_iommu_alloc_iova(xdna, size, dma_addr, true);
	if (IS_ERR(iova)) {
		XDNA_ERR(xdna, "Alloc iova failed, ret %ld", PTR_ERR(iova));
		return iova;
	}

	cpu_addr = (void *)__get_free_pages(GFP_KERNEL, get_order(size));
	if (!cpu_addr) {
		ret = -ENOMEM;
		goto free_iova;
	}

	ret = iommu_map(xdna->domain, *dma_addr, virt_to_phys(cpu_addr),
			iova_align(&xdna->iovad, size),
			IOMMU_READ | IOMMU_WRITE, GFP_KERNEL);
	if (ret)
		goto free_iova;

	return cpu_addr;

free_iova:
	__free_iova(&xdna->iovad, iova);
	return ERR_PTR(ret);
}

void amdxdna_iommu_free(struct amdxdna_dev *xdna, size_t size,
			void *cpu_addr, dma_addr_t dma_addr)
{
	iommu_unmap(xdna->domain, dma_addr, iova_align(&xdna->iovad, size));
	free_iova(&xdna->iovad, iova_pfn(&xdna->iovad, dma_addr));
	free_pages((unsigned long)cpu_addr, get_order(size));
}

int amdxdna_iommu_init(struct amdxdna_dev *xdna)
{
	unsigned long order;
	int ret;

	xdna->group = iommu_group_get(xdna->ddev.dev);
	if (!xdna->group || !force_iova)
		return 0;

	XDNA_WARN(xdna, "Enabled force_iova mode.");
	xdna->domain = iommu_paging_domain_alloc_flags(xdna->ddev.dev,
						       IOMMU_HWPT_ALLOC_PASID);
	if (IS_ERR(xdna->domain)) {
		XDNA_ERR(xdna, "Failed to alloc iommu domain");
		ret = PTR_ERR(xdna->domain);
		goto put_group;
	}

	ret = iova_cache_get();
	if (ret)
		goto free_domain;

	order = __ffs(xdna->domain->pgsize_bitmap);
	init_iova_domain(&xdna->iovad, 1UL << order, 0);

	ret = iommu_attach_group(xdna->domain, xdna->group);
	if (ret)
		goto put_iova;

	return 0;

put_iova:
	put_iova_domain(&xdna->iovad);
	iova_cache_put();
free_domain:
	iommu_domain_free(xdna->domain);
put_group:
	iommu_group_put(xdna->group);
	xdna->domain = NULL;

	return ret;
}

void amdxdna_iommu_fini(struct amdxdna_dev *xdna)
{
	if (xdna->domain) {
		iommu_detach_group(xdna->domain, xdna->group);
		put_iova_domain(&xdna->iovad);
		iova_cache_put();
		iommu_domain_free(xdna->domain);
	}

	if (xdna->group)
		iommu_group_put(xdna->group);
}
