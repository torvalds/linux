/* linux/arch/arm/mach-exynos/include/mach/regs-usb-phy-4212.h
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS4 - 4212 USB PHY definition
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_REGS_USB_PHY_4212_H
#define __ASM_ARCH_REGS_USB_PHY_4212_H __FILE__

/* EXYNOS4212 HSIC PHYPWR */
#define EXYNOS4212_HSIC1_NORMAL_MASK		(0x7 << 12)
#define EXYNOS4212_HSIC1_SLEEP			(1 << 14)
#define EXYNOS4212_HSIC1_ANALOG_POWERDOWN	(1 << 13)
#define EXYNOS4212_HSIC1_FORCE_SUSPEND		(1 << 12)
#define EXYNOS4212_HSIC0_NORMAL_MASK		(0x7 << 9)
#define EXYNOS4212_HSIC0_SLEEP			(1 << 11)
#define EXYNOS4212_HSIC0_ANALOG_POWERDOWN	(1 << 10)
#define EXYNOS4212_HSIC0_FORCE_SUSPEND		(1 << 9)

/* EXYNOS4212 PHYCLK */
#define EXYNOS4212_PHY0_ID_PULLUP		(1 << 3)
#define EXYNOS4212_CLKSEL_MASK			(0x7 << 0)
#define EXYNOS4212_CLKSEL_SHIFT			(0)
#define EXYNOS4212_CLKSEL_9600K			(0x0 << 0)
#define EXYNOS4212_CLKSEL_10M			(0x1 << 0)
#define EXYNOS4212_CLKSEL_12M			(0x2 << 0)
#define EXYNOS4212_CLKSEL_19200K		(0x3 << 0)
#define EXYNOS4212_CLKSEL_20M			(0x4 << 0)
#define EXYNOS4212_CLKSEL_24M			(0x5 << 0)

/* EXYNOS4212 PHYCLK */
#define EXYNOS4212_HOST_LINK_PORT_SWRST_MASK	(0xf << 7)
#define EXYNOS4212_HOST_LINK_PORT2_SWRST	(1 << 10)
#define EXYNOS4212_HOST_LINK_PORT1_SWRST	(1 << 9)
#define EXYNOS4212_HOST_LINK_PORT0_SWRST	(1 << 8)
#define EXYNOS4212_HOST_LINK_ALL_SWRST		(1 << 7)
#define EXYNOS4212_PHY1_SWRST_MASK		(0xf << 3)
#define EXYNOS4212_PHY1_HSIC1_SWRST		(1 << 6)
#define EXYNOS4212_PHY1_HSIC0_SWRST		(1 << 5)
#define EXYNOS4212_PHY1_SWRST			(1 << 4)
#define EXYNOS4212_HOST_PHY_SWRST		(1 << 3)

#endif /* __ASM_ARCH_REGS_USB_PHY_4212_H */
