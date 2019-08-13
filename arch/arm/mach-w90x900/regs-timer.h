/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * arch/arm/mach-w90x900/include/mach/regs-timer.h
 *
 * Copyright (c) 2008 Nuvoton technology corporation
 * All rights reserved.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * Based on arch/arm/mach-s3c2410/include/mach/regs-timer.h
 */

#ifndef __ASM_ARCH_REGS_TIMER_H
#define __ASM_ARCH_REGS_TIMER_H

/* Timer Registers */

#define TMR_BA			W90X900_VA_TIMER
#define REG_TCSR0		(TMR_BA+0x00)
#define REG_TCSR1		(TMR_BA+0x04)
#define REG_TICR0		(TMR_BA+0x08)
#define REG_TICR1		(TMR_BA+0x0C)
#define REG_TDR0		(TMR_BA+0x10)
#define REG_TDR1		(TMR_BA+0x14)
#define REG_TISR		(TMR_BA+0x18)
#define REG_WTCR		(TMR_BA+0x1C)
#define REG_TCSR2		(TMR_BA+0x20)
#define REG_TCSR3		(TMR_BA+0x24)
#define REG_TICR2		(TMR_BA+0x28)
#define REG_TICR3		(TMR_BA+0x2C)
#define REG_TDR2		(TMR_BA+0x30)
#define REG_TDR3		(TMR_BA+0x34)
#define REG_TCSR4		(TMR_BA+0x40)
#define REG_TICR4		(TMR_BA+0x48)
#define REG_TDR4		(TMR_BA+0x50)

#endif /*  __ASM_ARCH_REGS_TIMER_H */
