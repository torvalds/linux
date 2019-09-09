/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2014-2018 Etnaviv Project
 */

#ifndef __ETNAVIV_IOMMU_H__
#define __ETNAVIV_IOMMU_H__

struct etnaviv_gpu;
struct etnaviv_iommu_domain;

struct etnaviv_iommu_domain *
etnaviv_iommuv1_domain_alloc(struct etnaviv_gpu *gpu);
void etnaviv_iommuv1_restore(struct etnaviv_gpu *gpu);

struct etnaviv_iommu_domain *
etnaviv_iommuv2_domain_alloc(struct etnaviv_gpu *gpu);
void etnaviv_iommuv2_restore(struct etnaviv_gpu *gpu);

#endif /* __ETNAVIV_IOMMU_H__ */
