/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2016 Marek Vasut <marex@denx.de>
 *
 * i.MX23/i.MX28/i.MX6SX MXSFB LCD controller driver.
 */

#ifndef __MXSFB_DRV_H__
#define __MXSFB_DRV_H__

#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_encoder.h>
#include <drm/drm_plane.h>

struct clk;

struct mxsfb_devdata {
	unsigned int	transfer_count;
	unsigned int	cur_buf;
	unsigned int	next_buf;
	unsigned int	hs_wdth_mask;
	unsigned int	hs_wdth_shift;
	bool		has_overlay;
	bool		has_ctrl2;
};

struct mxsfb_drm_private {
	const struct mxsfb_devdata	*devdata;

	void __iomem			*base;	/* registers */
	struct clk			*clk;
	struct clk			*clk_axi;
	struct clk			*clk_disp_axi;

	struct drm_device		*drm;
	struct {
		struct drm_plane	primary;
		struct drm_plane	overlay;
	} planes;
	struct drm_crtc			crtc;
	struct drm_encoder		encoder;
	struct drm_connector		*connector;
	struct drm_bridge		*bridge;
};

static inline struct mxsfb_drm_private *
to_mxsfb_drm_private(struct drm_device *drm)
{
	return drm->dev_private;
}

void mxsfb_enable_axi_clk(struct mxsfb_drm_private *mxsfb);
void mxsfb_disable_axi_clk(struct mxsfb_drm_private *mxsfb);

int mxsfb_kms_init(struct mxsfb_drm_private *mxsfb);

#endif /* __MXSFB_DRV_H__ */
