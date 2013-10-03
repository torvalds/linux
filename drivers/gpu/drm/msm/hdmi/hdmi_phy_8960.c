/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "hdmi.h"

struct hdmi_phy_8960 {
	struct hdmi_phy base;
	struct hdmi *hdmi;
};
#define to_hdmi_phy_8960(x) container_of(x, struct hdmi_phy_8960, base)

static void hdmi_phy_8960_destroy(struct hdmi_phy *phy)
{
	struct hdmi_phy_8960 *phy_8960 = to_hdmi_phy_8960(phy);
	kfree(phy_8960);
}

static void hdmi_phy_8960_reset(struct hdmi_phy *phy)
{
	struct hdmi_phy_8960 *phy_8960 = to_hdmi_phy_8960(phy);
	struct hdmi *hdmi = phy_8960->hdmi;
	unsigned int val;

	val = hdmi_read(hdmi, REG_HDMI_PHY_CTRL);

	if (val & HDMI_PHY_CTRL_SW_RESET_LOW) {
		/* pull low */
		hdmi_write(hdmi, REG_HDMI_PHY_CTRL,
				val & ~HDMI_PHY_CTRL_SW_RESET);
	} else {
		/* pull high */
		hdmi_write(hdmi, REG_HDMI_PHY_CTRL,
				val | HDMI_PHY_CTRL_SW_RESET);
	}

	if (val & HDMI_PHY_CTRL_SW_RESET_PLL_LOW) {
		/* pull low */
		hdmi_write(hdmi, REG_HDMI_PHY_CTRL,
				val & ~HDMI_PHY_CTRL_SW_RESET_PLL);
	} else {
		/* pull high */
		hdmi_write(hdmi, REG_HDMI_PHY_CTRL,
				val | HDMI_PHY_CTRL_SW_RESET_PLL);
	}

	msleep(100);

	if (val & HDMI_PHY_CTRL_SW_RESET_LOW) {
		/* pull high */
		hdmi_write(hdmi, REG_HDMI_PHY_CTRL,
				val | HDMI_PHY_CTRL_SW_RESET);
	} else {
		/* pull low */
		hdmi_write(hdmi, REG_HDMI_PHY_CTRL,
				val & ~HDMI_PHY_CTRL_SW_RESET);
	}

	if (val & HDMI_PHY_CTRL_SW_RESET_PLL_LOW) {
		/* pull high */
		hdmi_write(hdmi, REG_HDMI_PHY_CTRL,
				val | HDMI_PHY_CTRL_SW_RESET_PLL);
	} else {
		/* pull low */
		hdmi_write(hdmi, REG_HDMI_PHY_CTRL,
				val & ~HDMI_PHY_CTRL_SW_RESET_PLL);
	}
}

static void hdmi_phy_8960_powerup(struct hdmi_phy *phy,
		unsigned long int pixclock)
{
	struct hdmi_phy_8960 *phy_8960 = to_hdmi_phy_8960(phy);
	struct hdmi *hdmi = phy_8960->hdmi;

	hdmi_write(hdmi, REG_HDMI_8960_PHY_REG0, 0x1b);
	hdmi_write(hdmi, REG_HDMI_8960_PHY_REG1, 0xf2);
	hdmi_write(hdmi, REG_HDMI_8960_PHY_REG4, 0x00);
	hdmi_write(hdmi, REG_HDMI_8960_PHY_REG5, 0x00);
	hdmi_write(hdmi, REG_HDMI_8960_PHY_REG6, 0x00);
	hdmi_write(hdmi, REG_HDMI_8960_PHY_REG7, 0x00);
	hdmi_write(hdmi, REG_HDMI_8960_PHY_REG8, 0x00);
	hdmi_write(hdmi, REG_HDMI_8960_PHY_REG9, 0x00);
	hdmi_write(hdmi, REG_HDMI_8960_PHY_REG10, 0x00);
	hdmi_write(hdmi, REG_HDMI_8960_PHY_REG11, 0x00);
	hdmi_write(hdmi, REG_HDMI_8960_PHY_REG3, 0x20);
}

static void hdmi_phy_8960_powerdown(struct hdmi_phy *phy)
{
	struct hdmi_phy_8960 *phy_8960 = to_hdmi_phy_8960(phy);
	struct hdmi *hdmi = phy_8960->hdmi;

	hdmi_write(hdmi, REG_HDMI_8960_PHY_REG2, 0x7f);
}

static const struct hdmi_phy_funcs hdmi_phy_8960_funcs = {
		.destroy = hdmi_phy_8960_destroy,
		.reset = hdmi_phy_8960_reset,
		.powerup = hdmi_phy_8960_powerup,
		.powerdown = hdmi_phy_8960_powerdown,
};

struct hdmi_phy *hdmi_phy_8960_init(struct hdmi *hdmi)
{
	struct hdmi_phy_8960 *phy_8960;
	struct hdmi_phy *phy = NULL;
	int ret;

	phy_8960 = kzalloc(sizeof(*phy_8960), GFP_KERNEL);
	if (!phy_8960) {
		ret = -ENOMEM;
		goto fail;
	}

	phy = &phy_8960->base;

	phy->funcs = &hdmi_phy_8960_funcs;

	phy_8960->hdmi = hdmi;

	return phy;

fail:
	if (phy)
		hdmi_phy_8960_destroy(phy);
	return ERR_PTR(ret);
}
