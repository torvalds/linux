/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright 2025 Arm, Ltd. */
#ifndef __ETHOSU_DRV_H__
#define __ETHOSU_DRV_H__

#include <drm/gpu_scheduler.h>

struct ethosu_device;

struct ethosu_file_priv {
	struct ethosu_device *edev;
	struct drm_sched_entity sched_entity;
};

#endif
