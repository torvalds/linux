/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2018 Linus Walleij <linus.walleij@linaro.org>
 * Parts of this file were based on the MCDE driver by Marcus Lorentzon
 * (C) ST-Ericsson SA 2013
 */
#include <drm/drm_simple_kms_helper.h>

#ifndef _MCDE_DRM_H_
#define _MCDE_DRM_H_

/* Shared basic registers */
#define MCDE_CR 0x00000000
#define MCDE_CR_IFIFOEMPTYLINECOUNT_V422_SHIFT 0
#define MCDE_CR_IFIFOEMPTYLINECOUNT_V422_MASK 0x0000003F
#define MCDE_CR_IFIFOCTRLEN BIT(15)
#define MCDE_CR_UFRECOVERY_MODE_V422 BIT(16)
#define MCDE_CR_WRAP_MODE_V422_SHIFT BIT(17)
#define MCDE_CR_AUTOCLKG_EN BIT(30)
#define MCDE_CR_MCDEEN BIT(31)

#define MCDE_CONF0 0x00000004
#define MCDE_CONF0_SYNCMUX0 BIT(0)
#define MCDE_CONF0_SYNCMUX1 BIT(1)
#define MCDE_CONF0_SYNCMUX2 BIT(2)
#define MCDE_CONF0_SYNCMUX3 BIT(3)
#define MCDE_CONF0_SYNCMUX4 BIT(4)
#define MCDE_CONF0_SYNCMUX5 BIT(5)
#define MCDE_CONF0_SYNCMUX6 BIT(6)
#define MCDE_CONF0_SYNCMUX7 BIT(7)
#define MCDE_CONF0_IFIFOCTRLWTRMRKLVL_SHIFT 12
#define MCDE_CONF0_IFIFOCTRLWTRMRKLVL_MASK 0x00007000
#define MCDE_CONF0_OUTMUX0_SHIFT 16
#define MCDE_CONF0_OUTMUX0_MASK 0x00070000
#define MCDE_CONF0_OUTMUX1_SHIFT 19
#define MCDE_CONF0_OUTMUX1_MASK 0x00380000
#define MCDE_CONF0_OUTMUX2_SHIFT 22
#define MCDE_CONF0_OUTMUX2_MASK 0x01C00000
#define MCDE_CONF0_OUTMUX3_SHIFT 25
#define MCDE_CONF0_OUTMUX3_MASK 0x0E000000
#define MCDE_CONF0_OUTMUX4_SHIFT 28
#define MCDE_CONF0_OUTMUX4_MASK 0x70000000

#define MCDE_SSP 0x00000008
#define MCDE_AIS 0x00000100
#define MCDE_IMSCERR 0x00000110
#define MCDE_RISERR 0x00000120
#define MCDE_MISERR 0x00000130
#define MCDE_SISERR 0x00000140

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
void mcde_dsi_enable(struct drm_bridge *bridge);
void mcde_dsi_disable(struct drm_bridge *bridge);
extern struct platform_driver mcde_dsi_driver;

void mcde_display_irq(struct mcde *mcde);
void mcde_display_disable_irqs(struct mcde *mcde);
int mcde_display_init(struct drm_device *drm);

#endif /* _MCDE_DRM_H_ */
