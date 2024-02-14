/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2022 Marek Vasut <marex@denx.de>
 *
 * i.MX8MP/i.MXRT LCDIFv3 LCD controller driver.
 */

#ifndef __LCDIF_DRV_H__
#define __LCDIF_DRV_H__

#include <drm/drm_bridge.h>
#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_encoder.h>
#include <drm/drm_plane.h>

struct clk;

struct lcdif_drm_private {
	void __iomem			*base;	/* registers */
	struct clk			*clk;
	struct clk			*clk_axi;
	struct clk			*clk_disp_axi;

	unsigned int			irq;

	struct drm_device		*drm;
	struct {
		struct drm_plane	primary;
		/* i.MXRT does support overlay planes, add them here. */
	} planes;
	struct drm_crtc			crtc;
	struct drm_encoder		encoder;
	struct drm_bridge		*bridge;
};

static inline struct lcdif_drm_private *
to_lcdif_drm_private(struct drm_device *drm)
{
	return drm->dev_private;
}

int lcdif_kms_init(struct lcdif_drm_private *lcdif);

#endif /* __LCDIF_DRV_H__ */
