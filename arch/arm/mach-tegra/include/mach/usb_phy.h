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

struct tegra_usb_phy {
	int instance;
	void __iomem *regs;
	struct clk *pll_u;
};

struct tegra_usb_phy *tegra_usb_phy_open(int instance, void __iomem *regs);

int tegra_usb_phy_power_on(struct tegra_usb_phy *phy);

int tegra_usb_phy_power_off(struct tegra_usb_phy *phy);

int tegra_usb_phy_close(struct tegra_usb_phy *phy);

#endif //__MACH_USB_PHY_H
