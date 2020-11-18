/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2018 Linus Walleij <linus.walleij@linaro.org>
 * Parts of this file were based on the MCDE driver by Marcus Lorentzon
 * (C) ST-Ericsson SA 2013
 */
#include <drm/drm_simple_kms_helper.h>

#ifndef _MCDE_DRM_H_
#define _MCDE_DRM_H_

struct mcde {
	struct drm_device drm;
	struct device *dev;
	struct drm_panel *panel;
	struct drm_bridge *bridge;
	struct drm_connector *connector;
	struct drm_simple_display_pipe pipe;
	struct mipi_dsi_device *mdsi;
	s16 stride;
	bool te_sync;
	bool video_mode;
	bool oneshot_mode;
	unsigned int flow_active;
	spinlock_t flow_lock; /* Locks the channel flow control */

	void __iomem *regs;

	struct clk *mcde_clk;
	struct clk *lcd_clk;
	struct clk *hdmi_clk;

	struct regulator *epod;
	struct regulator *vana;
};

#define to_mcde(dev) container_of(dev, struct mcde, drm)

bool mcde_dsi_irq(struct mipi_dsi_device *mdsi);
void mcde_dsi_te_request(struct mipi_dsi_device *mdsi);
extern struct platform_driver mcde_dsi_driver;

void mcde_display_irq(struct mcde *mcde);
void mcde_display_disable_irqs(struct mcde *mcde);
int mcde_display_init(struct drm_device *drm);

#endif /* _MCDE_DRM_H_ */
