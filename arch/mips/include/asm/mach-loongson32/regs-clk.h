/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2011 Zhang, Keguang <keguang.zhang@gmail.com>
 *
 * Loongson 1 Clock Register Definitions.
 */

#ifndef __ASM_MACH_LOONGSON32_REGS_CLK_H
#define __ASM_MACH_LOONGSON32_REGS_CLK_H

#define LS1X_CLK_REG(x) \
		((void __iomem *)KSEG1ADDR(LS1X_CLK_BASE + (x)))

#define LS1X_CLK_PLL_FREQ		LS1X_CLK_REG(0x0)
#define LS1X_CLK_PLL_DIV		LS1X_CLK_REG(0x4)

#if defined(CONFIG_LOONGSON1_LS1B)
/* Clock PLL Divisor Register Bits */
#define DIV_DC_EN			BIT(31)
#define DIV_DC_RST			BIT(30)
#define DIV_CPU_EN			BIT(25)
#define DIV_CPU_RST			BIT(24)
#define DIV_DDR_EN			BIT(19)
#define DIV_DDR_RST			BIT(18)
#define RST_DC_EN			BIT(5)
#define RST_DC				BIT(4)
#define RST_DDR_EN			BIT(3)
#define RST_DDR				BIT(2)
#define RST_CPU_EN			BIT(1)
#define RST_CPU				BIT(0)

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

#elif defined(CONFIG_LOONGSON1_LS1C)
/* PLL/SDRAM Frequency configuration register Bits */
#define PLL_VALID			BIT(31)
#define FRAC_N				GENMASK(23, 16)
#define RST_TIME			GENMASK(3, 2)
#define SDRAM_DIV			GENMASK(1, 0)

/* CPU/CAMERA/DC Frequency configuration register Bits */
#define DIV_DC_EN			BIT(31)
#define DIV_DC				GENMASK(30, 24)
#define DIV_CAM_EN			BIT(23)
#define DIV_CAM				GENMASK(22, 16)
#define DIV_CPU_EN			BIT(15)
#define DIV_CPU				GENMASK(14, 8)
#define DIV_DC_SEL_EN			BIT(5)
#define DIV_DC_SEL			BIT(4)
#define DIV_CAM_SEL_EN			BIT(3)
#define DIV_CAM_SEL			BIT(2)
#define DIV_CPU_SEL_EN			BIT(1)
#define DIV_CPU_SEL			BIT(0)

#define DIV_DC_SHIFT			24
#define DIV_CAM_SHIFT			16
#define DIV_CPU_SHIFT			8
#define DIV_DDR_SHIFT			0

#define DIV_DC_WIDTH			7
#define DIV_CAM_WIDTH			7
#define DIV_CPU_WIDTH			7
#define DIV_DDR_WIDTH			2

#endif

#endif /* __ASM_MACH_LOONGSON32_REGS_CLK_H */
