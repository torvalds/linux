/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Hisilicon Hibmc SoC drm driver
 *
 * Based on the bochs drm driver.
 *
 * Copyright (c) 2016 Huawei Limited.
 *
 * Author:
 *	Rongrong Zou <zourongrong@huawei.com>
 *	Rongrong Zou <zourongrong@gmail.com>
 *	Jianhua Li <lijianhua@huawei.com>
 */

#ifndef HIBMC_DRM_DRV_H
#define HIBMC_DRM_DRV_H

#include <linux/gpio/consumer.h>
#include <linux/i2c-algo-bit.h>
#include <linux/i2c.h>

#include <drm/drm_framebuffer.h>

#include "dp/dp_hw.h"

#define HIBMC_MIN_VECTORS	1
#define HIBMC_MAX_VECTORS	2

struct hibmc_vdac {
	struct drm_device *dev;
	struct drm_encoder encoder;
	struct drm_connector connector;
	struct i2c_adapter adapter;
	struct i2c_algo_bit_data bit_data;
};

struct hibmc_drm_private {
	/* hw */
	void __iomem   *mmio;

	/* drm */
	struct drm_device dev;
	struct drm_plane primary_plane;
	struct drm_crtc crtc;
	struct hibmc_vdac vdac;
	struct hibmc_dp dp;
};

static inline struct hibmc_vdac *to_hibmc_vdac(struct drm_connector *connector)
{
	return container_of(connector, struct hibmc_vdac, connector);
}

static inline struct hibmc_dp *to_hibmc_dp(struct drm_connector *connector)
{
	return container_of(connector, struct hibmc_dp, connector);
}

static inline struct hibmc_drm_private *to_hibmc_drm_private(struct drm_device *dev)
{
	return container_of(dev, struct hibmc_drm_private, dev);
}

void hibmc_set_power_mode(struct hibmc_drm_private *priv,
			  u32 power_mode);
void hibmc_set_current_gate(struct hibmc_drm_private *priv,
			    u32 gate);

int hibmc_de_init(struct hibmc_drm_private *priv);
int hibmc_vdac_init(struct hibmc_drm_private *priv);

int hibmc_ddc_create(struct drm_device *drm_dev, struct hibmc_vdac *connector);
void hibmc_ddc_del(struct hibmc_vdac *vdac);

int hibmc_dp_init(struct hibmc_drm_private *priv);

void hibmc_debugfs_init(struct drm_connector *connector, struct dentry *root);

irqreturn_t hibmc_dp_hpd_isr(int irq, void *arg);

#endif
