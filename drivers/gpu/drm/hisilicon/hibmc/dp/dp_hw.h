/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2024 Hisilicon Limited. */

#ifndef DP_KAPI_H
#define DP_KAPI_H

#include <linux/types.h>
#include <linux/delay.h>
#include <drm/drm_device.h>
#include <drm/drm_encoder.h>
#include <drm/drm_connector.h>
#include <drm/drm_print.h>

struct hibmc_dp_dev;

struct hibmc_dp {
	struct hibmc_dp_dev *dp_dev;
	struct drm_device *drm_dev;
	struct drm_encoder encoder;
	struct drm_connector connector;
	void __iomem *mmio;
};

int hibmc_dp_hw_init(struct hibmc_dp *dp);
int hibmc_dp_mode_set(struct hibmc_dp *dp, struct drm_display_mode *mode);
void hibmc_dp_display_en(struct hibmc_dp *dp, bool enable);

#endif
