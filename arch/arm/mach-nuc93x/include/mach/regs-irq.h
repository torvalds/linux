/*
 * arch/arm/mach-nuc93x/include/mach/regs-irq.h
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

#ifndef ___ASM_ARCH_REGS_IRQ_H
#define ___ASM_ARCH_REGS_IRQ_H

/* Advance Interrupt Controller (AIC) Registers */

#define AIC_BA    		NUC93X_VA_IRQ

#define REG_AIC_IRQSC		(AIC_BA+0x80)
#define REG_AIC_GEN		(AIC_BA+0x84)
#define REG_AIC_GASR		(AIC_BA+0x88)
#define REG_AIC_GSCR		(AIC_BA+0x8C)
#define REG_AIC_IRSR		(AIC_BA+0x100)
#define REG_AIC_IASR		(AIC_BA+0x104)
#define REG_AIC_ISR		(AIC_BA+0x108)
#define REG_AIC_IPER		(AIC_BA+0x10C)
#define REG_AIC_ISNR		(AIC_BA+0x110)
#define REG_AIC_IMR		(AIC_BA+0x114)
#define REG_AIC_OISR		(AIC_BA+0x118)
#define REG_AIC_MECR		(AIC_BA+0x120)
#define REG_AIC_MDCR		(AIC_BA+0x124)
#define REG_AIC_SSCR		(AIC_BA+0x128)
#define REG_AIC_SCCR		(AIC_BA+0x12C)
#define REG_AIC_EOSCR		(AIC_BA+0x130)
#define AIC_IPER		(0x10C)
#define AIC_ISNR		(0x110)

#endif /* ___ASM_ARCH_REGS_IRQ_H */
