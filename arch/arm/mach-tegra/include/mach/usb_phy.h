/*
 * arch/arm/mach-tegra/include/mach/usb_phy.h
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __MACH_USB_PHY_H
#define __MACH_USB_PHY_H

#include <linux/platform_device.h>
#include <linux/clk.h>

struct tegra_utmip_config {
	u8 hssync_start_delay;
	u8 elastic_limit;
	u8 idle_wait_delay;
	u8 term_range_adj;
	u8 xcvr_setup;
	u8 xcvr_lsfslew;
	u8 xcvr_lsrslew;
};

struct tegra_usb_phy {
	int instance;
	void __iomem *regs;
	void __iomem *pad_regs;
	struct clk *pll_u;
	struct clk *pad_clk;
	struct tegra_utmip_config *config;
};

enum tegra_usb_phy_mode {
	TEGRA_USB_PHY_MODE_DEVICE,
	TEGRA_USB_PHY_MODE_HOST,
};

struct tegra_usb_phy *tegra_usb_phy_open(int instance, void __iomem *regs,
					 struct tegra_utmip_config *config);

int tegra_usb_phy_power_on(struct tegra_usb_phy *phy,
			   enum tegra_usb_phy_mode phy_mode);

int tegra_usb_phy_power_off(struct tegra_usb_phy *phy);

int tegra_usb_phy_close(struct tegra_usb_phy *phy);

#endif //__MACH_USB_PHY_H
