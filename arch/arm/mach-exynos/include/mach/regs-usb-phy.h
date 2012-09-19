/*
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __PLAT_S5P_REGS_USB_PHY_H
#define __PLAT_S5P_REGS_USB_PHY_H

#define EXYNOS4_HSOTG_PHYREG(x)		((x) + S3C_VA_USB_HSPHY)

#define EXYNOS4_PHYPWR			EXYNOS4_HSOTG_PHYREG(0x00)
#define PHY1_HSIC_NORMAL_MASK		(0xf << 9)
#define PHY1_HSIC1_SLEEP		(1 << 12)
#define PHY1_HSIC1_FORCE_SUSPEND	(1 << 11)
#define PHY1_HSIC0_SLEEP		(1 << 10)
#define PHY1_HSIC0_FORCE_SUSPEND	(1 << 9)

#define PHY1_STD_NORMAL_MASK		(0x7 << 6)
#define PHY1_STD_SLEEP			(1 << 8)
#define PHY1_STD_ANALOG_POWERDOWN	(1 << 7)
#define PHY1_STD_FORCE_SUSPEND		(1 << 6)

#define PHY0_NORMAL_MASK		(0x39 << 0)
#define PHY0_SLEEP			(1 << 5)
#define PHY0_OTG_DISABLE		(1 << 4)
#define PHY0_ANALOG_POWERDOWN		(1 << 3)
#define PHY0_FORCE_SUSPEND		(1 << 0)

#define EXYNOS4_PHYCLK			EXYNOS4_HSOTG_PHYREG(0x04)
#define PHY1_COMMON_ON_N		(1 << 7)
#define PHY0_COMMON_ON_N		(1 << 4)
#define PHY0_ID_PULLUP			(1 << 2)

#define EXYNOS4_CLKSEL_SHIFT		(0)

#define EXYNOS4210_CLKSEL_MASK		(0x3 << 0)
#define EXYNOS4210_CLKSEL_48M		(0x0 << 0)
#define EXYNOS4210_CLKSEL_12M		(0x2 << 0)
#define EXYNOS4210_CLKSEL_24M		(0x3 << 0)

#define EXYNOS4X12_CLKSEL_MASK		(0x7 << 0)
#define EXYNOS4X12_CLKSEL_9600K		(0x0 << 0)
#define EXYNOS4X12_CLKSEL_10M		(0x1 << 0)
#define EXYNOS4X12_CLKSEL_12M		(0x2 << 0)
#define EXYNOS4X12_CLKSEL_19200K	(0x3 << 0)
#define EXYNOS4X12_CLKSEL_20M		(0x4 << 0)
#define EXYNOS4X12_CLKSEL_24M		(0x5 << 0)

#define EXYNOS4_RSTCON			EXYNOS4_HSOTG_PHYREG(0x08)
#define HOST_LINK_PORT_SWRST_MASK	(0xf << 6)
#define HOST_LINK_PORT2_SWRST		(1 << 9)
#define HOST_LINK_PORT1_SWRST		(1 << 8)
#define HOST_LINK_PORT0_SWRST		(1 << 7)
#define HOST_LINK_ALL_SWRST		(1 << 6)

#define PHY1_SWRST_MASK			(0x7 << 3)
#define PHY1_HSIC_SWRST			(1 << 5)
#define PHY1_STD_SWRST			(1 << 4)
#define PHY1_ALL_SWRST			(1 << 3)

#define PHY0_SWRST_MASK			(0x7 << 0)
#define PHY0_PHYLINK_SWRST		(1 << 2)
#define PHY0_HLINK_SWRST		(1 << 1)
#define PHY0_SWRST			(1 << 0)

#define EXYNOS4_PHY1CON			EXYNOS4_HSOTG_PHYREG(0x34)
#define FPENABLEN			(1 << 0)

#endif /* __PLAT_S5P_REGS_USB_PHY_H */
