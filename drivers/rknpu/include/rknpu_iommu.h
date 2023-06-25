/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Rockchip Electronics Co.Ltd
 * Author: Felix Zeng <felix.zeng@rock-chips.com>
 */

#ifndef __LINUX_RKNPU_IOMMU_H
#define __LINUX_RKNPU_IOMMU_H

#include <linux/mutex.h>
#include <linux/seq_file.h>
#include <linux/iommu.h>
#include <linux/iova.h>
#include <linux/version.h>

#if KERNEL_VERSION(6, 1, 0) > LINUX_VERSION_CODE
#include <linux/dma-iommu.h>
#endif

#include "rknpu_drv.h"

enum iommu_dma_cookie_type {
	IOMMU_DMA_IOVA_COOKIE,
	IOMMU_DMA_MSI_COOKIE,
};

struct rknpu_iommu_dma_cookie {
	enum iommu_dma_cookie_type type;

	/* Full allocator for IOMMU_DMA_IOVA_COOKIE */
	struct iova_domain iovad;
};

dma_addr_t rknpu_iommu_dma_alloc_iova(struct iommu_domain *domain, size_t size,
				      u64 dma_limit, struct device *dev);

void rknpu_iommu_dma_free_iova(struct rknpu_iommu_dma_cookie *cookie,
			       dma_addr_t iova, size_t size);

#endif
