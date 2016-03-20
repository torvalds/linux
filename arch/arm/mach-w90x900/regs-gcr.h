/*
 * arch/arm/mach-w90x900/include/mach/regs-gcr.h
 *
 * Copyright (c) 2010 Nuvoton technology corporation
 * All rights reserved.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef __ASM_ARCH_REGS_GCR_H
#define __ASM_ARCH_REGS_GCR_H

/* Global control registers */

#define GCR_BA		W90X900_VA_GCR
#define REG_PDID	(GCR_BA+0x000)
#define REG_PWRON	(GCR_BA+0x004)
#define REG_ARBCON	(GCR_BA+0x008)
#define REG_MFSEL	(GCR_BA+0x00C)
#define REG_EBIDPE	(GCR_BA+0x010)
#define REG_LCDDPE	(GCR_BA+0x014)
#define REG_GPIOCPE	(GCR_BA+0x018)
#define REG_GPIODPE	(GCR_BA+0x01C)
#define REG_GPIOEPE	(GCR_BA+0x020)
#define REG_GPIOFPE	(GCR_BA+0x024)
#define REG_GPIOGPE	(GCR_BA+0x028)
#define REG_GPIOHPE	(GCR_BA+0x02C)
#define REG_GPIOIPE	(GCR_BA+0x030)
#define REG_GTMP1	(GCR_BA+0x034)
#define REG_GTMP2	(GCR_BA+0x038)
#define REG_GTMP3	(GCR_BA+0x03C)

#endif /*  __ASM_ARCH_REGS_GCR_H */
