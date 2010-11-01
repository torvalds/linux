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

#define S5P_CLKREG(x)			(S5P_VA_CMU + (x))

#define S5P_INFORM0			S5P_CLKREG(0x800)

#define S5P_EPLL_CON0			S5P_CLKREG(0x0C110)
#define S5P_EPLL_CON1			S5P_CLKREG(0x0C114)
#define S5P_VPLL_CON0			S5P_CLKREG(0x0C120)
#define S5P_VPLL_CON1			S5P_CLKREG(0x0C124)

#define S5P_CLKSRC_TOP0			S5P_CLKREG(0x0C210)
#define S5P_CLKSRC_TOP1			S5P_CLKREG(0x0C214)

#define S5P_CLKSRC_PERIL0		S5P_CLKREG(0x0C250)

#define S5P_CLKDIV_TOP			S5P_CLKREG(0x0C510)

#define S5P_CLKDIV_PERIL0		S5P_CLKREG(0x0C550)
#define S5P_CLKDIV_PERIL1		S5P_CLKREG(0x0C554)
#define S5P_CLKDIV_PERIL2		S5P_CLKREG(0x0C558)
#define S5P_CLKDIV_PERIL3		S5P_CLKREG(0x0C55C)
#define S5P_CLKDIV_PERIL4		S5P_CLKREG(0x0C560)
#define S5P_CLKDIV_PERIL5		S5P_CLKREG(0x0C564)

#define S5P_CLKSRC_MASK_PERIL0		S5P_CLKREG(0x0C350)

#define S5P_CLKGATE_IP_PERIL		S5P_CLKREG(0x0C950)

#define S5P_CLKSRC_CORE			S5P_CLKREG(0x10200)
#define S5P_CLKDIV_CORE0		S5P_CLKREG(0x10500)

#define S5P_APLL_LOCK			S5P_CLKREG(0x14000)
#define S5P_MPLL_LOCK			S5P_CLKREG(0x14004)
#define S5P_APLL_CON0			S5P_CLKREG(0x14100)
#define S5P_APLL_CON1			S5P_CLKREG(0x14104)
#define S5P_MPLL_CON0			S5P_CLKREG(0x14108)
#define S5P_MPLL_CON1			S5P_CLKREG(0x1410C)

#define S5P_CLKSRC_CPU			S5P_CLKREG(0x14200)
#define S5P_CLKMUX_STATCPU		S5P_CLKREG(0x14400)

#define S5P_CLKDIV_CPU			S5P_CLKREG(0x14500)
#define S5P_CLKDIV_STATCPU		S5P_CLKREG(0x14600)

#define S5P_CLKGATE_SCLKCPU		S5P_CLKREG(0x14800)

#endif /* __ASM_ARCH_REGS_CLOCK_H */
