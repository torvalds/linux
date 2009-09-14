/*
 * arch/arm/mach-w90x900/include/mach/regs-clock.h
 *
 * Copyright (c) 2008 Nuvoton technology corporation.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation;version 2 of the License.
 *
 */

#ifndef __ASM_ARCH_REGS_CLOCK_H
#define __ASM_ARCH_REGS_CLOCK_H

/* Clock Control Registers  */
#define CLK_BA		W90X900_VA_CLKPWR
#define REG_CLKEN	(CLK_BA + 0x00)
#define REG_CLKSEL	(CLK_BA + 0x04)
#define REG_CLKDIV	(CLK_BA + 0x08)
#define REG_PLLCON0	(CLK_BA + 0x0C)
#define REG_PLLCON1	(CLK_BA + 0x10)
#define REG_PMCON	(CLK_BA + 0x14)
#define REG_IRQWAKECON	(CLK_BA + 0x18)
#define REG_IRQWAKEFLAG	(CLK_BA + 0x1C)
#define REG_IPSRST	(CLK_BA + 0x20)
#define REG_CLKEN1	(CLK_BA + 0x24)
#define REG_CLKDIV1	(CLK_BA + 0x28)

#endif /*  __ASM_ARCH_REGS_CLOCK_H */
