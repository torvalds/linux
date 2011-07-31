/*
 * arch/arm/mach-nuc93x/include/mach/regs-timer.h
 *
 * Copyright (c) 2008 Nuvoton technology corporation
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

#ifndef __ASM_ARCH_REGS_TIMER_H
#define __ASM_ARCH_REGS_TIMER_H

/* Timer Registers */

#define TMR_BA			NUC93X_VA_TIMER
#define REG_TCSR0		(TMR_BA+0x00)
#define REG_TICR0		(TMR_BA+0x08)
#define REG_TDR0		(TMR_BA+0x10)
#define REG_TISR		(TMR_BA+0x18)
#define REG_WTCR		(TMR_BA+0x1C)

#endif /*  __ASM_ARCH_REGS_TIMER_H */
