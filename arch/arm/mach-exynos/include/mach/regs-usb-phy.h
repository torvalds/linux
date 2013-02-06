/*
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 * Author: Yulgon Kim <yulgon.kim@samsung.com>
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __ASM_ARCH_REGS_USB_PHY_H
#define __ASM_ARCH_REGS_USB_PHY_H

#define EXYNOS4_HSOTG_PHYREG(x)			((x) + S3C_VA_USB_HSPHY)

#include "regs-usb-phy-4210.h"
#include "regs-usb-phy-4212.h"

/* For Exynos4 */
#define EXYNOS4_PHYPWR				EXYNOS4_HSOTG_PHYREG(0x00)

#define PHY1_STD_SLEEP				(1 << 8)
#define PHY1_STD_ANALOG_POWERDOWN		(1 << 7)
#define PHY1_STD_FORCE_SUSPEND			(1 << 6)
#define PHY1_STD_NORMAL_MASK			(0x7 << 6)

#define PHY0_SLEEP				(1 << 5)
#define PHY0_OTG_DISABLE			(1 << 4)
#define PHY0_ANALOG_POWERDOWN			(1 << 3)
#define PHY0_FORCE_SUSPEND			(1 << 0)
#define PHY0_NORMAL_MASK			(0x29 << 0)
#define PHY0_PHYTUNE				EXYNOS4_HSOTG_PHYREG(0x24)
#define PHY1_PHYTUNE				EXYNOS4_HSOTG_PHYREG(0x20)

#define EXYNOS4_PHYCLK				EXYNOS4_HSOTG_PHYREG(0x04)
#define PHY1_COMMON_ON_N			(1 << 7)
#define PHY0_COMMON_ON_N			(1 << 4)

#define CLKSEL_SHIFT				(0)

#define EXYNOS4_RSTCON				EXYNOS4_HSOTG_PHYREG(0x08)

#define PHY0_SWRST_MASK				(0x7 << 0)
#define PHY0_PHYLINK_SWRST			(1 << 2)
#define PHY0_HLINK_SWRST			(1 << 1)
#define PHY0_SWRST				(1 << 0)

#define EXYNOS4_PHY1CON				EXYNOS4_HSOTG_PHYREG(0x34)
#define FPENABLEN				(1 << 0)

/* For Exynos5 */
#define EXYNOS5_PHY_HOST_CTRL0			EXYNOS4_HSOTG_PHYREG(0x00)
#define HOST_CTRL0_PHYSWRSTALL			(0x1 << 31)
#define HOST_CTRL0_REFCLKSEL(val)		(val << 19)
#define EXYNOS5_CLKSEL_50M			(0x7)
#define EXYNOS5_CLKSEL_24M			(0x5)
#define EXYNOS5_CLKSEL_20M			(0x4)
#define EXYNOS5_CLKSEL_19200K			(0x3)
#define EXYNOS5_CLKSEL_12M			(0x2)
#define EXYNOS5_CLKSEL_10M			(0x1)
#define EXYNOS5_CLKSEL_9600K			(0x0)
#define HOST_CTRL0_CLKSEL_SHIFT			(16)
#define HOST_CTRL0_FSEL_MASK			(0x7 << 16)

#define HOST_CTRL0_COMMONON_N			(0x1 << 9)
#define HOST_CTRL0_SIDDQ			(0x1 << 6)
#define HOST_CTRL0_FORCESLEEP			(0x1 << 5)
#define HOST_CTRL0_FORCESUSPEND			(0x1 << 4)
#define HOST_CTRL0_WORDINTERFACE		(0x1 << 3)
#define HOST_CTRL0_UTMISWRST			(0x1 << 2)
#define HOST_CTRL0_LINKSWRST			(0x1 << 1)
#define HOST_CTRL0_PHYSWRST			(0x1 << 0)

#define EXYNOS5_PHY_HOST_TUNE0			EXYNOS4_HSOTG_PHYREG(0x04)
#define EXYNOS5_PHY_HOST_TEST0			EXYNOS4_HSOTG_PHYREG(0x08)

#define EXYNOS5_PHY_HSIC_CTRL1			EXYNOS4_HSOTG_PHYREG(0x10)
#define EXYNOS5_PHY_HSIC_CTRL2			EXYNOS4_HSOTG_PHYREG(0x20)
#define HSIC_CTRL_REFCLKSEL(val)		((val&0x3) << 23)
#define HSIC_CTRL_REFCLKDIV(val)		((val&0x7f) << 16)
#define HSIC_CTRL_SIDDQ				(0x1 << 6)
#define HSIC_CTRL_FORCESLEEP			(0x1 << 5)
#define HSIC_CTRL_FORCESUSPEND			(0x1 << 4)
#define HSIC_CTRL_WORDINTERFACE			(0x1 << 3)
#define HSIC_CTRL_UTMISWRST			(0x1 << 2)
#define HSIC_CTRL_PHYSWRST			(0x1 << 0)

#define EXYNOS5_PHY_HOST_EHCICTRL		EXYNOS4_HSOTG_PHYREG(0x30)
#define EHCICTRL_ENAINCRXALIGN			(0x1 << 29)
#define EHCICTRL_ENAINCR4			(0x1 << 28)
#define EHCICTRL_ENAINCR8			(0x1 << 27)
#define EHCICTRL_ENAINCR16			(0x1 << 26)

#define EXYNOS5_PHY_HOST_OHCICTRL		EXYNOS4_HSOTG_PHYREG(0x34)
#define OHCICTRL_SUSPLGCY			(0x1 << 3)
#define OHCICTRL_APPSTARTCLK			(0x1 << 2)
#define OHCICTRL_CNTSEL				(0x1 << 1)
#define OHCICTRL_CLKCKTRST			(0x1 << 0)

#define EXYNOS5_PHY_OTG_SYS			EXYNOS4_HSOTG_PHYREG(0x38)
#define OTG_SYS_PHYLINK_SW_RESET		(0x1 << 14)
#define OTG_SYS_LINK_SW_RST_UOTG		(0x1 << 13)
#define OTG_SYS_PHY0_SW_RST			(0x1 << 12)
#define OTG_SYS_REF_CLK_SEL(val)		((val&0x3) << 9)
#define OTG_SYS_REF_CLK_SEL_MASK		(0x3 << 9)
#define OTG_SYS_IP_PULLUP_UOTG			(0x1 << 8)
#define OTG_SYS_COMMON_ON			(0x1 << 7)
#define OTG_SYS_CLKSEL_SHIFT			(4)
#define OTG_SYS_CTRL0_FSEL_MASK			(0x7 << 4)
#define OTG_SYS_FORCE_SLEEP			(0x1 <<3)
#define OTG_SYS_OTGDISABLE			(0x1 <<2)
#define OTG_SYS_SIDDQ_UOTG			(0x1 <<1)
#define OTG_SYS_FORCE_SUSPEND			(0x1 <<0)

extern int exynos_check_usb_op(void);

#endif /* __ASM_ARCH_REGS_USB_PHY_H */
