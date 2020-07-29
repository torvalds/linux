/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2018 Linus Walleij <linus.walleij@linaro.org>
 * Parts of this file were based on the MCDE driver by Marcus Lorentzon
 * (C) ST-Ericsson SA 2013
 */
#include <drm/drm_simple_kms_helper.h>

#ifndef _MCDE_DRM_H_
#define _MCDE_DRM_H_

enum mcde_flow_mode {
	/* One-shot mode: flow stops after one frame */
	MCDE_COMMAND_ONESHOT_FLOW,
	/* Command mode with tearing effect (TE) IRQ sync */
	MCDE_COMMAND_TE_FLOW,
	/*
	 * Command mode with bus turn-around (BTA) and tearing effect
	 * (TE) IRQ sync.
	 */
	MCDE_COMMAND_BTA_TE_FLOW,
	/* Video mode with tearing effect (TE) sync IRQ */
	MCDE_VIDEO_TE_FLOW,
	/* Video mode with the formatter itself as sync source */
	MCDE_VIDEO_FORMATTER_FLOW,
};

struct mcde {
	struct drm_device drm;
	struct device *dev;
	struct drm_panel *panel;
	struct drm_bridge *bridge;
	struct drm_connector *connector;
	struct drm_simple_display_pipe pipe;
	struct mipi_dsi_device *mdsi;
	s16 stride;
	enum mcde_flow_mode flow_mode;
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

static inline bool mcde_flow_is_video(struct mcde *mcde)
{
	return (mcde->flow_mode == MCDE_VIDEO_TE_FLOW ||
		mcde->flow_mode == MCDE_VIDEO_FORMATTER_FLOW);
}

bool mcde_dsi_irq(struct mipi_dsi_device *mdsi);
void mcde_dsi_te_request(struct mipi_dsi_device *mdsi);
extern struct platform_driver mcde_dsi_driver;

void mcde_display_irq(struct mcde *mcde);
void mcde_display_disable_irqs(struct mcde *mcde);
int mcde_display_init(struct drm_device *drm);

#endif /* _MCDE_DRM_H_ */
