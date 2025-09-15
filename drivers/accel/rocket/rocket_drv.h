/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2024-2025 Tomeu Vizoso <tomeu@tomeuvizoso.net> */

#ifndef __ROCKET_DRV_H__
#define __ROCKET_DRV_H__

#include <drm/drm_mm.h>
#include <drm/gpu_scheduler.h>

#include "rocket_device.h"

extern const struct dev_pm_ops rocket_pm_ops;

struct rocket_iommu_domain {
	struct iommu_domain *domain;
	struct kref kref;
};

struct rocket_file_priv {
	struct rocket_device *rdev;

	struct rocket_iommu_domain *domain;
	struct drm_mm mm;
	struct mutex mm_lock;

	struct drm_sched_entity sched_entity;
};

struct rocket_iommu_domain *rocket_iommu_domain_get(struct rocket_file_priv *rocket_priv);
void rocket_iommu_domain_put(struct rocket_iommu_domain *domain);

#endif
