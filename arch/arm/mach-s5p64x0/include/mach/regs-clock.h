/* linux/arch/arm/mach-s5p64x0/include/mach/regs-clock.h
 *
 * Copyright (c) 2009-2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * S5P64X0 - Clock register definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_REGS_CLOCK_H
#define __ASM_ARCH_REGS_CLOCK_H __FILE__

#include <mach/map.h>

#define S5P_CLKREG(x)			(S3C_VA_SYS + (x))

#define S5P64X0_APLL_CON		S5P_CLKREG(0x0C)
#define S5P64X0_MPLL_CON		S5P_CLKREG(0x10)
#define S5P64X0_EPLL_CON		S5P_CLKREG(0x14)
#define S5P64X0_EPLL_CON_K		S5P_CLKREG(0x18)

#define S5P64X0_CLK_SRC0		S5P_CLKREG(0x1C)

#define S5P64X0_CLK_DIV0		S5P_CLKREG(0x20)
#define S5P64X0_CLK_DIV1		S5P_CLKREG(0x24)
#define S5P64X0_CLK_DIV2		S5P_CLKREG(0x28)

#define S5P64X0_CLK_GATE_HCLK0		S5P_CLKREG(0x30)
#define S5P64X0_CLK_GATE_PCLK		S5P_CLKREG(0x34)
#define S5P64X0_CLK_GATE_SCLK0		S5P_CLKREG(0x38)
#define S5P64X0_CLK_GATE_MEM0		S5P_CLKREG(0x3C)

#define S5P64X0_CLK_DIV3		S5P_CLKREG(0x40)

#define S5P64X0_CLK_GATE_HCLK1		S5P_CLKREG(0x44)
#define S5P64X0_CLK_GATE_SCLK1		S5P_CLKREG(0x48)

#define S5P6450_DPLL_CON		S5P_CLKREG(0x50)
#define S5P6450_DPLL_CON_K		S5P_CLKREG(0x54)

#define S5P64X0_CLK_SRC1		S5P_CLKREG(0x10C)

#define S5P64X0_SYS_ID			S5P_CLKREG(0x118)
#define S5P64X0_SYS_OTHERS		S5P_CLKREG(0x11C)

#define S5P64X0_PWR_CFG			S5P_CLKREG(0x804)
#define S5P64X0_OTHERS			S5P_CLKREG(0x900)

#define S5P64X0_CLKDIV0_HCLK_SHIFT	(8)
#define S5P64X0_CLKDIV0_HCLK_MASK	(0xF << S5P64X0_CLKDIV0_HCLK_SHIFT)

#define S5P64X0_OTHERS_USB_SIG_MASK	(1 << 16)

/* Compatibility defines */

#define ARM_CLK_DIV			S5P64X0_CLK_DIV0
#define ARM_DIV_RATIO_SHIFT		0
#define ARM_DIV_MASK			(0xF << ARM_DIV_RATIO_SHIFT)

#endif /* __ASM_ARCH_REGS_CLOCK_H */
