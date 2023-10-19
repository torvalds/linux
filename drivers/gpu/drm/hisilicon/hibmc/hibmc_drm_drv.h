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

#include <drm/drm_edid.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_framebuffer.h>

struct hibmc_connector {
	struct drm_connector base;

	struct i2c_adapter adapter;
	struct i2c_algo_bit_data bit_data;
};

struct hibmc_drm_private {
	/* hw */
	void __iomem   *mmio;
	void __iomem   *fb_map;
	resource_size_t  fb_base;
	resource_size_t  fb_size;

	/* drm */
	struct drm_device dev;
	struct drm_plane primary_plane;
	struct drm_crtc crtc;
	struct drm_encoder encoder;
	struct hibmc_connector connector;
};

static inline struct hibmc_connector *to_hibmc_connector(struct drm_connector *connector)
{
	return container_of(connector, struct hibmc_connector, base);
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

int hibmc_mm_init(struct hibmc_drm_private *hibmc);
int hibmc_ddc_create(struct drm_device *drm_dev, struct hibmc_connector *connector);

#endif
