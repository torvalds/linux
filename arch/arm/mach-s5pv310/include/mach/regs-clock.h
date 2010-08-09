/* linux/arch/arm/mach-s5pv310/include/mach/regs-clock.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * S5PV310 - Clock register definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_REGS_CLOCK_H
#define __ASM_ARCH_REGS_CLOCK_H __FILE__

#include <mach/map.h>

#define S5P_CLKREG(x)			(S3C_VA_SYS + (x))

#define S5P_INFORM0			S5P_CLKREG(0x800)

#define S5P_EPLL_CON0			S5P_CLKREG(0x1C110)
#define S5P_EPLL_CON1			S5P_CLKREG(0x1C114)
#define S5P_VPLL_CON0			S5P_CLKREG(0x1C120)
#define S5P_VPLL_CON1			S5P_CLKREG(0x1C124)

#define S5P_CLKSRC_TOP0			S5P_CLKREG(0x1C210)
#define S5P_CLKSRC_TOP1			S5P_CLKREG(0x1C214)

#define S5P_CLKSRC_PERIL0		S5P_CLKREG(0x1C250)

#define S5P_CLKDIV_TOP			S5P_CLKREG(0x1C510)

#define S5P_CLKDIV_PERIL0		S5P_CLKREG(0x1C550)
#define S5P_CLKDIV_PERIL1		S5P_CLKREG(0x1C554)
#define S5P_CLKDIV_PERIL2		S5P_CLKREG(0x1C558)
#define S5P_CLKDIV_PERIL3		S5P_CLKREG(0x1C55C)
#define S5P_CLKDIV_PERIL4		S5P_CLKREG(0x1C560)
#define S5P_CLKDIV_PERIL5		S5P_CLKREG(0x1C564)

#define S5P_CLKGATE_IP_PERIL		S5P_CLKREG(0x1C950)

#define S5P_CLKSRC_CORE			S5P_CLKREG(0x20200)

#define S5P_CLKDIV_CORE0		S5P_CLKREG(0x20500)

#define S5P_APLL_LOCK			S5P_CLKREG(0x24000)
#define S5P_MPLL_LOCK			S5P_CLKREG(0x24004)
#define S5P_APLL_CON0			S5P_CLKREG(0x24100)
#define S5P_APLL_CON1			S5P_CLKREG(0x24104)
#define S5P_MPLL_CON0			S5P_CLKREG(0x24108)
#define S5P_MPLL_CON1			S5P_CLKREG(0x2410C)

#define S5P_CLKSRC_CPU			S5P_CLKREG(0x24200)
#define S5P_CLKMUX_STATCPU		S5P_CLKREG(0x24400)

#define S5P_CLKDIV_CPU			S5P_CLKREG(0x24500)
#define S5P_CLKDIV_STATCPU		S5P_CLKREG(0x24600)

#define S5P_CLKGATE_SCLKCPU		S5P_CLKREG(0x24800)

#endif /* __ASM_ARCH_REGS_CLOCK_H */
