/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Copyright (C) 2013 Freescale Semiconductor, Inc.
 */

#ifndef __FSL_PAMU_DOMAIN_H
#define __FSL_PAMU_DOMAIN_H

#include "fsl_pamu.h"

struct fsl_dma_domain {
	/* list of devices associated with the domain */
	struct list_head		devices;
	u32				stash_id;
	struct iommu_domain		iommu_domain;
	spinlock_t			domain_lock;
};

/* domain-device relationship */
struct device_domain_info {
	struct list_head link;	/* link to domain siblings */
	struct device *dev;
	u32 liodn;
	struct fsl_dma_domain *domain; /* pointer to domain */
};
#endif  /* __FSL_PAMU_DOMAIN_H */
