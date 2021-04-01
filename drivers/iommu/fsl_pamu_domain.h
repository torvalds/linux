/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Copyright (C) 2013 Freescale Semiconductor, Inc.
 */

#ifndef __FSL_PAMU_DOMAIN_H
#define __FSL_PAMU_DOMAIN_H

#include "fsl_pamu.h"

struct dma_window {
	phys_addr_t paddr;
	u64 size;
	int valid;
	int prot;
};

struct fsl_dma_domain {
	/*
	 * Number of windows assocaited with this domain.
	 * During domain initialization, it is set to the
	 * the maximum number of subwindows allowed for a LIODN.
	 * Minimum value for this is 1 indicating a single PAMU
	 * window, without any sub windows. Value can be set/
	 * queried by set_attr/get_attr API for DOMAIN_ATTR_WINDOWS.
	 * Value can only be set once the geometry has been configured.
	 */
	u32				win_cnt;
	/*
	 * win_arr contains information of the configured
	 * windows for a domain. This is allocated only
	 * when the number of windows for the domain are
	 * set.
	 */
	struct dma_window		*win_arr;
	/* list of devices associated with the domain */
	struct list_head		devices;
	/* dma_domain states:
	 * mapped - A particular mapping has been created
	 * within the configured geometry.
	 * enabled - DMA has been enabled for the given
	 * domain. This translates to setting of the
	 * valid bit for the primary PAACE in the PAMU
	 * PAACT table. Domain geometry should be set and
	 * it must have a valid mapping before DMA can be
	 * enabled for it.
	 *
	 */
	int				mapped;
	int				enabled;
	/* stash_id obtained from the stash attribute details */
	u32				stash_id;
	struct pamu_stash_attribute	dma_stash;
	u32				snoop_id;
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
