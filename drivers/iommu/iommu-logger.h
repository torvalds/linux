/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2020-2021, The Linux Foundation. All rights reserved. */

#ifndef __LINUX_QTI_IOMMU_LOGGER_H
#define __LINUX_QTI_IOMMU_LOGGER_H

#include <linux/io-pgtable.h>

enum iommu_logger_pgtable_fmt {
	IOMMU_LOGGER_ARM_32_LPAE_S1,
	IOMMU_LOGGER_ARM_64_LPAE_S1,
	IOMMU_LOGGER_MAX_PGTABLE_FMTS,
};

/*
 * Each group may have more than one domain; but each domain may
 * only have one group.
 */
struct iommu_debug_attachment {
	struct iommu_domain *domain;
	struct iommu_group *group;
	char *client_name;
	enum iommu_logger_pgtable_fmt fmt;
	unsigned int levels;
	/*
	 * Virtual addresses of the top-level page tables are stored here,
	 * as they are more useful for debug tools than physical addresses.
	 */
	void *ttbr0;
	void *ttbr1;
	struct list_head list;
};

#if IS_ENABLED(CONFIG_QTI_IOMMU_SUPPORT)

int iommu_logger_register(struct iommu_debug_attachment **a,
			  struct iommu_domain *domain, struct device *dev,
			  struct io_pgtable *iop);
void iommu_logger_unregister(struct iommu_debug_attachment *a);
#else
static inline int iommu_logger_register(struct iommu_debug_attachment **a,
					struct iommu_domain *domain,
					struct device *dev,
					struct io_pgtable *iop)
{
	return 0;
}

static inline void iommu_logger_unregister(struct iommu_debug_attachment *a) {}
#endif /* CONFIG_QTI_IOMMU_LOGGER */
#endif /* __LINUX_QTI_IOMMU_LOGGER_H */
