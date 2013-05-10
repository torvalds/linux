/*
 * arch/arm/mach-pnx4008/include/mach/irq.h
 *
 * PNX4008 IRQ controller driver - header file
 * this one is used in entry-arnv.S as well so it cannot contain C code
 *
 * Copyright (c) 2005 Philips Semiconductors
 * Copyright (c) 2005 MontaVista Software, Inc.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */
#ifndef __PNX4008_IRQ_H__
#define __PNX4008_IRQ_H__

#define MIC_VA_BASE             IO_ADDRESS(PNX4008_INTCTRLMIC_BASE)
#define SIC1_VA_BASE            IO_ADDRESS(PNX4008_INTCTRLSIC1_BASE)
#define SIC2_VA_BASE            IO_ADDRESS(PNX4008_INTCTRLSIC2_BASE)

/* Manual: Chapter 20, page 195 */

#define INTC_BIT(irq) (1<< ((irq) & 0x1F))

#define INTC_ER(irq)    IO_ADDRESS((PNX4008_INTCTRLMIC_BASE + 0x0 + (((irq)&(0x3<<5))<<9)))
#define INTC_RSR(irq)   IO_ADDRESS((PNX4008_INTCTRLMIC_BASE + 0x4 + (((irq)&(0x3<<5))<<9)))
#define INTC_SR(irq)    IO_ADDRESS((PNX4008_INTCTRLMIC_BASE + 0x8 + (((irq)&(0x3<<5))<<9)))
#define INTC_APR(irq)   IO_ADDRESS((PNX4008_INTCTRLMIC_BASE + 0xC + (((irq)&(0x3<<5))<<9)))
#define INTC_ATR(irq)   IO_ADDRESS((PNX4008_INTCTRLMIC_BASE + 0x10 + (((irq)&(0x3<<5))<<9)))
#define INTC_ITR(irq)   IO_ADDRESS((PNX4008_INTCTRLMIC_BASE + 0x14 + (((irq)&(0x3<<5))<<9)))

#define START_INT_REG_BIT(irq) (1<<((irq)&0x1F))

#define START_INT_ER_REG(irq)     IO_ADDRESS((PNX4008_PWRMAN_BASE + 0x20 + (((irq)&(0x1<<5))>>1)))
#define START_INT_RSR_REG(irq)    IO_ADDRESS((PNX4008_PWRMAN_BASE + 0x24 + (((irq)&(0x1<<5))>>1)))
#define START_INT_SR_REG(irq)     IO_ADDRESS((PNX4008_PWRMAN_BASE + 0x28 + (((irq)&(0x1<<5))>>1)))
#define START_INT_APR_REG(irq)    IO_ADDRESS((PNX4008_PWRMAN_BASE + 0x2C + (((irq)&(0x1<<5))>>1)))

extern void __init pnx4008_init_irq(void);

#endif /* __PNX4008_IRQ_H__ */
