/* linux/arch/arm/mach-exynos/include/mach/regs-usb-phy-4210.h
 *
 * Copyright (c) 2010-2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS4 - 4210 USB PHY definition
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_REGS_USB_PHY_4210_H
#define __ASM_ARCH_REGS_USB_PHY_4210_H __FILE__

/* EXYNOS4210 HSIC PHYPWR */
#define EXYNOS4210_HSIC1_NORMAL_MASK		(0x3 << 11)
#define EXYNOS4210_HSIC1_SLEEP			(1 << 12)
#define EXYNOS4210_HSIC1_FORCE_SUSPEND		(1 << 11)
#define EXYNOS4210_HSIC0_NORMAL_MASK		(0x3 << 9)
#define EXYNOS4210_HSIC0_SLEEP			(1 << 10)
#define EXYNOS4210_HSIC0_FORCE_SUSPEND		(1 << 9)

/* EXYNOS4210 PHYCLK */
#define EXYNOS4210_PHY0_ID_PULLUP		(1 << 2)
#define EXYNOS4210_CLKSEL_MASK			(0x3 << 0)
#define EXYNOS4210_CLKSEL_48M			(0x0 << 0)
#define EXYNOS4210_CLKSEL_12M			(0x2 << 0)
#define EXYNOS4210_CLKSEL_24M			(0x3 << 0)

/* EXYNOS4210 PHYCLK */
#define EXYNOS4210_HOST_LINK_PORT_SWRST_MASK	(0xf << 6)
#define EXYNOS4210_HOST_LINK_PORT2_SWRST	(1 << 9)
#define EXYNOS4210_HOST_LINK_PORT1_SWRST	(1 << 8)
#define EXYNOS4210_HOST_LINK_PORT0_SWRST	(1 << 7)
#define EXYNOS4210_HOST_LINK_ALL_SWRST		(1 << 6)
#define EXYNOS4210_PHY1_SWRST_MASK		(0x7 << 3)
#define EXYNOS4210_PHY1_HSIC_SWRST		(1 << 5)
#define EXYNOS4210_PHY1_STD_SWRST		(1 << 4)
#define EXYNOS4210_PHY1_ALL_SWRST		(1 << 3)

#endif /* __ASM_ARCH_REGS_USB_PHY_4210_H */
