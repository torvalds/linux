/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef ASMARM_DMA_IOMMU_H
#define ASMARM_DMA_IOMMU_H

#ifdef __KERNEL__

#include <linux/err.h>
#include <linux/mm_types.h>
#include <linux/scatterlist.h>
#include <linux/dma-debug.h>
#include <linux/kref.h>
#include <linux/dma-mapping-fast.h>

struct dma_iommu_mapping {
	/* iommu specific data */
	struct iommu_domain	*domain;
	bool			init;
	struct kref		kref;
	const struct dma_map_ops *ops;

	/* Protects bitmap */
	spinlock_t		lock;
	void			*bitmap;
	size_t			bits;
	dma_addr_t		base;

	struct dma_fast_smmu_mapping *fast;
};

#ifdef CONFIG_ARM64_DMA_USE_IOMMU
void arm_iommu_put_dma_cookie(struct iommu_domain *domain);
#else  /* !CONFIG_ARM64_DMA_USE_IOMMU */
static inline void arm_iommu_put_dma_cookie(struct iommu_domain *domain) {}
#endif	/* CONFIG_ARM64_DMA_USE_IOMMU */

#endif /* __KERNEL__ */
#endif
