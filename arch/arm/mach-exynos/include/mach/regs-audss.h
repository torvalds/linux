/* arch/arm/mach-exynos/include/mach/regs-audss.h
 *
 * Copyright 2011 Samsung Electronics
 *
 * EXYNOS4 Audio SubSystem clock register definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __PLAT_REGS_AUDSS_H
#define __PLAT_REGS_AUDSS_H __FILE__

#define EXYNOS4_AUDSSREG(x)			(S5P_VA_AUDSS + (x))

#define S5P_CLKSRC_AUDSS		EXYNOS4_AUDSSREG(0x0)
#define S5P_CLKDIV_AUDSS		EXYNOS4_AUDSSREG(0x4)
#define S5P_CLKGATE_AUDSS		EXYNOS4_AUDSSREG(0x8)

/* CLKSRC0 */
#define S5P_AUDSS_CLKSRC_MAIN_MASK	(0x1<<0)
#define S5P_AUDSS_CLKSRC_MAIN_SHIFT	(0)
#define S5P_AUDSS_CLKSRC_I2SCLK_MASK	(0x3<<2)
#define S5P_AUDSS_CLKSRC_I2SCLK_SHIFT	(2)

/* CLKDIV0 */
#define S5P_AUDSS_CLKDIV_RP_MASK	(0xf<<0)
#define S5P_AUDSS_CLKDIV_RP_SHIFT	(0)
#define S5P_AUDSS_CLKDIV_BUSCLK_MASK	(0xf<<4)
#define S5P_AUDSS_CLKDIV_BUSCLK_SHIFT	(4)
#define S5P_AUDSS_CLKDIV_I2SCLK_MASK	(0xf<<8)
#define S5P_AUDSS_CLKDIV_I2SCLK_SHIFT	(8)

/* IP Clock Gate 0 Registers */
#define S5P_AUDSS_CLKGATE_RP		(1<<0)
#define S5P_AUDSS_CLKGATE_INTMEM	(1<<1)
#define S5P_AUDSS_CLKGATE_I2SBUS	(1<<2)
#define S5P_AUDSS_CLKGATE_I2SSPECIAL	(1<<3)
#define S5P_AUDSS_CLKGATE_PCMBUS	(1<<4)
#define S5P_AUDSS_CLKGATE_PCMSPECIAL	(1<<5)
#define S5P_AUDSS_CLKGATE_GPIO		(1<<6)
#define S5P_AUDSS_CLKGATE_UART		(1<<7)
#define S5P_AUDSS_CLKGATE_TIMER		(1<<8)

#endif /* _PLAT_REGS_AUDSS_H */
