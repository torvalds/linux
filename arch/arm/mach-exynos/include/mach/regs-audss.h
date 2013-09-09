/* arch/arm/mach-exynos4/include/mach/regs-audss.h
 *
 * Copyright (c) 2011 Samsung Electronics
 *		http://www.samsung.com
 *
 * Exynos4 Audio SubSystem clock register definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __PLAT_REGS_AUDSS_H
#define __PLAT_REGS_AUDSS_H __FILE__

#define EXYNOS4_AUDSS_INT_MEM	(0x03000000)

#define EXYNOS_AUDSSREG(x)		(S5P_VA_AUDSS + (x))

#define EXYNOS_CLKSRC_AUDSS_OFFSET	0x0
#define EXYNOS_CLKDIV_AUDSS_OFFSET	0x4
#define EXYNOS_CLKGATE_AUDSS_OFFSET	0x8

#define EXYNOS_CLKSRC_AUDSS		EXYNOS_AUDSSREG(EXYNOS_CLKSRC_AUDSS_OFFSET)
#define EXYNOS_CLKDIV_AUDSS		EXYNOS_AUDSSREG(EXYNOS_CLKDIV_AUDSS_OFFSET)
#define EXYNOS_CLKGATE_AUDSS		EXYNOS_AUDSSREG(EXYNOS_CLKGATE_AUDSS_OFFSET)

/* IP Clock Gate 0 Registers */
#define EXYNOS_AUDSS_CLKGATE_RP		(1<<0)
#define EXYNOS_AUDSS_CLKGATE_I2SBUS	(1<<2)
#define EXYNOS_AUDSS_CLKGATE_I2SSPECIAL	(1<<3)
#define EXYNOS_AUDSS_CLKGATE_PCMBUS	(1<<4)
#define EXYNOS_AUDSS_CLKGATE_PCMSPECIAL	(1<<5)
#define EXYNOS_AUDSS_CLKGATE_GPIO	(1<<6)
#define EXYNOS_AUDSS_CLKGATE_UART	(1<<7)
#define EXYNOS_AUDSS_CLKGATE_TIMER	(1<<8)

#endif /* _PLAT_REGS_AUDSS_H */
