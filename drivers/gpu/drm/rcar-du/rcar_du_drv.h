/*
 * rcar_du_drv.h  --  R-Car Display Unit DRM driver
 *
 * Copyright (C) 2013 Renesas Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __RCAR_DU_DRV_H__
#define __RCAR_DU_DRV_H__

#include <linux/kernel.h>
#include <linux/platform_data/rcar-du.h>

#include "rcar_du_crtc.h"
#include "rcar_du_group.h"

struct clk;
struct device;
struct drm_device;
struct rcar_du_device;

#define RCAR_DU_FEATURE_CRTC_IRQ_CLOCK	(1 << 0)	/* Per-CRTC IRQ and clock */
#define RCAR_DU_FEATURE_ALIGN_128B	(1 << 1)	/* Align pitches to 128 bytes */
#define RCAR_DU_FEATURE_DEFR8		(1 << 2)	/* Has DEFR8 register */

/*
 * struct rcar_du_device_info - DU model-specific information
 * @features: device features (RCAR_DU_FEATURE_*)
 * @num_crtcs: total number of CRTCs
 */
struct rcar_du_device_info {
	unsigned int features;
	unsigned int num_crtcs;
};

struct rcar_du_device {
	struct device *dev;
	const struct rcar_du_platform_data *pdata;
	const struct rcar_du_device_info *info;

	void __iomem *mmio;

	struct drm_device *ddev;

	struct rcar_du_crtc crtcs[3];
	unsigned int num_crtcs;

	struct rcar_du_group groups[2];
};

static inline bool rcar_du_has(struct rcar_du_device *rcdu,
			       unsigned int feature)
{
	return rcdu->info->features & feature;
}

static inline u32 rcar_du_read(struct rcar_du_device *rcdu, u32 reg)
{
	return ioread32(rcdu->mmio + reg);
}

static inline void rcar_du_write(struct rcar_du_device *rcdu, u32 reg, u32 data)
{
	iowrite32(data, rcdu->mmio + reg);
}

#endif /* __RCAR_DU_DRV_H__ */
