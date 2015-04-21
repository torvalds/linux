/*
 * Copyright (c) 2011 Zhang, Keguang <keguang.zhang@gmail.com>
 *
 * Loongson 1 Clock Register Definitions.
 *
 * This program is free software; you can redistribute	it and/or modify it
 * under  the terms of	the GNU General	 Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __ASM_MACH_LOONGSON32_REGS_CLK_H
#define __ASM_MACH_LOONGSON32_REGS_CLK_H

#define LS1X_CLK_REG(x) \
		((void __iomem *)KSEG1ADDR(LS1X_CLK_BASE + (x)))

#define LS1X_CLK_PLL_FREQ		LS1X_CLK_REG(0x0)
#define LS1X_CLK_PLL_DIV		LS1X_CLK_REG(0x4)

/* Clock PLL Divisor Register Bits */
#define DIV_DC_EN			(0x1 << 31)
#define DIV_DC_RST			(0x1 << 30)
#define DIV_CPU_EN			(0x1 << 25)
#define DIV_CPU_RST			(0x1 << 24)
#define DIV_DDR_EN			(0x1 << 19)
#define DIV_DDR_RST			(0x1 << 18)
#define RST_DC_EN			(0x1 << 5)
#define RST_DC				(0x1 << 4)
#define RST_DDR_EN			(0x1 << 3)
#define RST_DDR				(0x1 << 2)
#define RST_CPU_EN			(0x1 << 1)
#define RST_CPU				0x1

#define DIV_DC_SHIFT			26
#define DIV_CPU_SHIFT			20
#define DIV_DDR_SHIFT			14

#define DIV_DC_WIDTH			4
#define DIV_CPU_WIDTH			4
#define DIV_DDR_WIDTH			4

#define BYPASS_DC_SHIFT			12
#define BYPASS_DDR_SHIFT		10
#define BYPASS_CPU_SHIFT		8

#define BYPASS_DC_WIDTH			1
#define BYPASS_DDR_WIDTH		1
#define BYPASS_CPU_WIDTH		1

#endif /* __ASM_MACH_LOONGSON32_REGS_CLK_H */
