// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Rockchip Electronics Co.Ltd
 * Author: Felix Zeng <felix.zeng@rock-chips.com>
 */

#include "rknpu_iommu.h"

dma_addr_t rknpu_iommu_dma_alloc_iova(struct iommu_domain *domain, size_t size,
				      u64 dma_limit, struct device *dev)
{
	struct rknpu_iommu_dma_cookie *cookie = (void *)domain->iova_cookie;
	struct iova_domain *iovad = &cookie->iovad;
	unsigned long shift, iova_len, iova = 0;
#if (KERNEL_VERSION(5, 4, 0) > LINUX_VERSION_CODE)
	dma_addr_t limit;
#endif

	shift = iova_shift(iovad);
	iova_len = size >> shift;

#if KERNEL_VERSION(6, 1, 0) > LINUX_VERSION_CODE
	/*
	 * Freeing non-power-of-two-sized allocations back into the IOVA caches
	 * will come back to bite us badly, so we have to waste a bit of space
	 * rounding up anything cacheable to make sure that can't happen. The
	 * order of the unadjusted size will still match upon freeing.
	 */
	if (iova_len < (1 << (IOVA_RANGE_CACHE_MAX_SIZE - 1)))
		iova_len = roundup_pow_of_two(iova_len);
#endif

#if (KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE)
	dma_limit = min_not_zero(dma_limit, dev->bus_dma_limit);
#else
	if (dev->bus_dma_mask)
		dma_limit &= dev->bus_dma_mask;
#endif

	if (domain->geometry.force_aperture)
		dma_limit =
			min_t(u64, dma_limit, domain->geometry.aperture_end);

#if (KERNEL_VERSION(5, 4, 0) <= LINUX_VERSION_CODE)
	iova = alloc_iova_fast(iovad, iova_len, dma_limit >> shift, true);
#else
	limit = min_t(dma_addr_t, dma_limit >> shift, iovad->end_pfn);

	iova = alloc_iova_fast(iovad, iova_len, limit, true);
#endif

	return (dma_addr_t)iova << shift;
}

void rknpu_iommu_dma_free_iova(struct rknpu_iommu_dma_cookie *cookie,
			       dma_addr_t iova, size_t size)
{
	struct iova_domain *iovad = &cookie->iovad;

	free_iova_fast(iovad, iova_pfn(iovad, iova), size >> iova_shift(iovad));
}
