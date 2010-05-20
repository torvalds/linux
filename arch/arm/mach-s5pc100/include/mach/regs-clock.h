/* linux/arch/arm/mach-s5pc100/include/mach/regs-clock.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * S5PC100 - Clock register definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_REGS_CLOCK_H
#define __ASM_ARCH_REGS_CLOCK_H __FILE__

#include <mach/map.h>

#define S5P_CLKREG(x)		(S3C_VA_SYS + (x))

#define S5PC100_REG_OTHERS(x)	(S5PC100_VA_OTHERS + (x))

#define S5P_APLL_LOCK		S5P_CLKREG(0x00)
#define S5P_MPLL_LOCK		S5P_CLKREG(0x04)
#define S5P_EPLL_LOCK		S5P_CLKREG(0x08)
#define S5P_HPLL_LOCK		S5P_CLKREG(0x0C)

#define S5P_APLL_CON		S5P_CLKREG(0x100)
#define S5P_MPLL_CON		S5P_CLKREG(0x104)
#define S5P_EPLL_CON		S5P_CLKREG(0x108)
#define S5P_HPLL_CON		S5P_CLKREG(0x10C)

#define S5P_CLK_SRC0		S5P_CLKREG(0x200)
#define S5P_CLK_SRC1		S5P_CLKREG(0x204)
#define S5P_CLK_SRC2		S5P_CLKREG(0x208)
#define S5P_CLK_SRC3		S5P_CLKREG(0x20C)

#define S5P_CLK_DIV0		S5P_CLKREG(0x300)
#define S5P_CLK_DIV1		S5P_CLKREG(0x304)
#define S5P_CLK_DIV2		S5P_CLKREG(0x308)
#define S5P_CLK_DIV3		S5P_CLKREG(0x30C)
#define S5P_CLK_DIV4		S5P_CLKREG(0x310)

#define S5P_CLK_OUT		S5P_CLKREG(0x400)

#define S5P_CLKGATE_D00		S5P_CLKREG(0x500)
#define S5P_CLKGATE_D01		S5P_CLKREG(0x504)
#define S5P_CLKGATE_D02		S5P_CLKREG(0x508)

#define S5P_CLKGATE_D10		S5P_CLKREG(0x520)
#define S5P_CLKGATE_D11		S5P_CLKREG(0x524)
#define S5P_CLKGATE_D12		S5P_CLKREG(0x528)
#define S5P_CLKGATE_D13		S5P_CLKREG(0x52C)
#define S5P_CLKGATE_D14		S5P_CLKREG(0x530)
#define S5P_CLKGATE_D15		S5P_CLKREG(0x534)

#define S5P_CLKGATE_D20		S5P_CLKREG(0x540)

#define S5P_CLKGATE_SCLK0	S5P_CLKREG(0x560)
#define S5P_CLKGATE_SCLK1	S5P_CLKREG(0x564)

/* CLKDIV0 */
#define S5P_CLKDIV0_D0_MASK		(0x7<<8)
#define S5P_CLKDIV0_D0_SHIFT		(8)
#define S5P_CLKDIV0_PCLKD0_MASK		(0x7<<12)
#define S5P_CLKDIV0_PCLKD0_SHIFT	(12)

/* CLKDIV1 */
#define S5P_CLKDIV1_D1_MASK		(0x7<<12)
#define S5P_CLKDIV1_D1_SHIFT		(12)
#define S5P_CLKDIV1_PCLKD1_MASK		(0x7<<16)
#define S5P_CLKDIV1_PCLKD1_SHIFT	(16)

#define S5PC100_SWRESET		S5PC100_REG_OTHERS(0x000)

#define S5PC100_SWRESET_RESETVAL	0xc100

#endif /* __ASM_ARCH_REGS_CLOCK_H */
