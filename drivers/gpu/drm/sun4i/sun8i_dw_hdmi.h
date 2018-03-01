/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2018 Jernej Skrabec <jernej.skrabec@siol.net>
 */

#ifndef _SUN8I_DW_HDMI_H_
#define _SUN8I_DW_HDMI_H_

#include <drm/bridge/dw_hdmi.h>
#include <drm/drm_encoder.h>
#include <linux/clk.h>
#include <linux/regmap.h>
#include <linux/reset.h>

struct sun8i_hdmi_phy {
	struct clk		*clk_bus;
	struct clk		*clk_mod;
	struct regmap		*regs;
	struct reset_control	*rst_phy;
};

struct sun8i_dw_hdmi {
	struct clk			*clk_tmds;
	struct device			*dev;
	struct dw_hdmi			*hdmi;
	struct drm_encoder		encoder;
	struct sun8i_hdmi_phy		*phy;
	struct dw_hdmi_plat_data	plat_data;
	struct reset_control		*rst_ctrl;
};

static inline struct sun8i_dw_hdmi *
encoder_to_sun8i_dw_hdmi(struct drm_encoder *encoder)
{
	return container_of(encoder, struct sun8i_dw_hdmi, encoder);
}

int sun8i_hdmi_phy_probe(struct sun8i_dw_hdmi *hdmi, struct device_node *node);
void sun8i_hdmi_phy_remove(struct sun8i_dw_hdmi *hdmi);

void sun8i_hdmi_phy_init(struct sun8i_hdmi_phy *phy);
const struct dw_hdmi_phy_ops *sun8i_hdmi_phy_get_ops(void);

#endif /* _SUN8I_DW_HDMI_H_ */
