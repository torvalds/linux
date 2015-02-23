/*
 * Copyright (c) 2014-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __EDP_CONNECTOR_H__
#define __EDP_CONNECTOR_H__

#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>

#include "drm_crtc.h"
#include "drm_dp_helper.h"
#include "msm_drv.h"

#define edp_read(offset) msm_readl((offset))
#define edp_write(offset, data) msm_writel((data), (offset))

struct edp_ctrl;
struct edp_aux;
struct edp_phy;

struct msm_edp {
	struct drm_device *dev;
	struct platform_device *pdev;

	struct drm_connector *connector;
	struct drm_bridge *bridge;

	/* the encoder we are hooked to (outside of eDP block) */
	struct drm_encoder *encoder;

	struct edp_ctrl *ctrl;

	int irq;
};

/* eDP bridge */
struct drm_bridge *msm_edp_bridge_init(struct msm_edp *edp);
void edp_bridge_destroy(struct drm_bridge *bridge);

/* eDP connector */
struct drm_connector *msm_edp_connector_init(struct msm_edp *edp);

/* AUX */
void *msm_edp_aux_init(struct device *dev, void __iomem *regbase,
			struct drm_dp_aux **drm_aux);
void msm_edp_aux_destroy(struct device *dev, struct edp_aux *aux);
irqreturn_t msm_edp_aux_irq(struct edp_aux *aux, u32 isr);
void msm_edp_aux_ctrl(struct edp_aux *aux, int enable);

/* Phy */
bool msm_edp_phy_ready(struct edp_phy *phy);
void msm_edp_phy_ctrl(struct edp_phy *phy, int enable);
void msm_edp_phy_vm_pe_init(struct edp_phy *phy);
void msm_edp_phy_vm_pe_cfg(struct edp_phy *phy, u32 v0, u32 v1);
void msm_edp_phy_lane_power_ctrl(struct edp_phy *phy, bool up, u32 max_lane);
void *msm_edp_phy_init(struct device *dev, void __iomem *regbase);

/* Ctrl */
irqreturn_t msm_edp_ctrl_irq(struct edp_ctrl *ctrl);
void msm_edp_ctrl_power(struct edp_ctrl *ctrl, bool on);
int msm_edp_ctrl_init(struct msm_edp *edp);
void msm_edp_ctrl_destroy(struct edp_ctrl *ctrl);
bool msm_edp_ctrl_panel_connected(struct edp_ctrl *ctrl);
int msm_edp_ctrl_get_panel_info(struct edp_ctrl *ctrl,
	struct drm_connector *connector, struct edid **edid);
int msm_edp_ctrl_timing_cfg(struct edp_ctrl *ctrl,
				const struct drm_display_mode *mode,
				const struct drm_display_info *info);
/* @pixel_rate is in kHz */
bool msm_edp_ctrl_pixel_clock_valid(struct edp_ctrl *ctrl,
	u32 pixel_rate, u32 *pm, u32 *pn);

#endif /* __EDP_CONNECTOR_H__ */
