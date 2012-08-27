/*
 * Copyright (c) 2011 Zhang, Keguang <keguang.zhang@gmail.com>
 *
 * Loongson 1 Clock Register Definitions.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __ASM_MACH_LOONGSON1_REGS_CLK_H
#define __ASM_MACH_LOONGSON1_REGS_CLK_H

#define LS1X_CLK_REG(x) \
		((void __iomem *)KSEG1ADDR(LS1X_CLK_BASE + (x)))

#define LS1X_CLK_PLL_FREQ		LS1X_CLK_REG(0x0)
#define LS1X_CLK_PLL_DIV		LS1X_CLK_REG(0x4)

/* Clock PLL Divisor Register Bits */
#define DIV_DC_EN			(0x1 << 31)
#define DIV_DC				(0x1f << 26)
#define DIV_CPU_EN			(0x1 << 25)
#define DIV_CPU				(0x1f << 20)
#define DIV_DDR_EN			(0x1 << 19)
#define DIV_DDR				(0x1f << 14)

#define DIV_DC_SHIFT			26
#define DIV_CPU_SHIFT			20
#define DIV_DDR_SHIFT			14

#endif /* __ASM_MACH_LOONGSON1_REGS_CLK_H */
