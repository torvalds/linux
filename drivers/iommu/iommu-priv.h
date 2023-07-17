/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __LINUX_IOMMU_PRIV_H
#define __LINUX_IOMMU_PRIV_H

#include <linux/iommu.h>

int iommu_group_replace_domain(struct iommu_group *group,
			       struct iommu_domain *new_domain);

#endif /* __LINUX_IOMMU_PRIV_H */
