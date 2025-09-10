/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2024-2025 Tomeu Vizoso <tomeu@tomeuvizoso.net> */

#ifndef __ROCKET_DEVICE_H__
#define __ROCKET_DEVICE_H__

#include <drm/drm_device.h>
#include <linux/clk.h>
#include <linux/container_of.h>
#include <linux/iommu.h>
#include <linux/platform_device.h>

#include "rocket_core.h"

struct rocket_device {
	struct drm_device ddev;

	struct mutex sched_lock;

	struct rocket_core *cores;
	unsigned int num_cores;
};

struct rocket_device *rocket_device_init(struct platform_device *pdev,
					 const struct drm_driver *rocket_drm_driver);
void rocket_device_fini(struct rocket_device *rdev);
#define to_rocket_device(drm_dev) \
	((struct rocket_device *)(container_of((drm_dev), struct rocket_device, ddev)))

#endif /* __ROCKET_DEVICE_H__ */
