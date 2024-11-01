// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include "hdmi.h"

static void hdmi_phy_8x74_powerup(struct hdmi_phy *phy,
		unsigned long int pixclock)
{
	hdmi_phy_write(phy, REG_HDMI_8x74_ANA_CFG0,   0x1b);
	hdmi_phy_write(phy, REG_HDMI_8x74_ANA_CFG1,   0xf2);
	hdmi_phy_write(phy, REG_HDMI_8x74_BIST_CFG0,  0x0);
	hdmi_phy_write(phy, REG_HDMI_8x74_BIST_PATN0, 0x0);
	hdmi_phy_write(phy, REG_HDMI_8x74_BIST_PATN1, 0x0);
	hdmi_phy_write(phy, REG_HDMI_8x74_BIST_PATN2, 0x0);
	hdmi_phy_write(phy, REG_HDMI_8x74_BIST_PATN3, 0x0);
	hdmi_phy_write(phy, REG_HDMI_8x74_PD_CTRL1,   0x20);
}

static void hdmi_phy_8x74_powerdown(struct hdmi_phy *phy)
{
	hdmi_phy_write(phy, REG_HDMI_8x74_PD_CTRL0, 0x7f);
}

static const char * const hdmi_phy_8x74_reg_names[] = {
	"core-vdda",
	"vddio",
};

static const char * const hdmi_phy_8x74_clk_names[] = {
	"iface", "alt_iface"
};

const struct hdmi_phy_cfg msm_hdmi_phy_8x74_cfg = {
	.type = MSM_HDMI_PHY_8x74,
	.powerup = hdmi_phy_8x74_powerup,
	.powerdown = hdmi_phy_8x74_powerdown,
	.reg_names = hdmi_phy_8x74_reg_names,
	.num_regs = ARRAY_SIZE(hdmi_phy_8x74_reg_names),
	.clk_names = hdmi_phy_8x74_clk_names,
	.num_clks = ARRAY_SIZE(hdmi_phy_8x74_clk_names),
};
