/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * shmob_drm.h  --  SH Mobile DRM driver
 *
 * Copyright (C) 2012 Renesas Electronics Corporation
 *
 * Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#ifndef __SHMOB_DRM_DRV_H__
#define __SHMOB_DRM_DRV_H__

#include <linux/kernel.h>
#include <linux/platform_data/shmob_drm.h>
#include <linux/spinlock.h>

#include "shmob_drm_crtc.h"

struct clk;
struct device;
struct drm_device;

struct shmob_drm_config {
	enum shmob_drm_clk_source clk_source;
	unsigned int clk_div;
};

struct shmob_drm_device {
	struct device *dev;
	const struct shmob_drm_platform_data *pdata;
	struct shmob_drm_config config;

	void __iomem *mmio;
	struct clk *clock;
	u32 lddckr;

	unsigned int irq;
	spinlock_t irq_lock;		/* Protects hardware LDINTR register */

	struct drm_device ddev;

	struct shmob_drm_crtc crtc;
	struct drm_encoder encoder;
	struct drm_connector *connector;
};

static inline struct shmob_drm_device *to_shmob_device(struct drm_device *dev)
{
	return container_of(dev, struct shmob_drm_device, ddev);
}

#endif /* __SHMOB_DRM_DRV_H__ */
