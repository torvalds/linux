/*
 * linux/arch/arm/mach-iop33x/irq.c
 *
 * Generic IOP331 IRQ handling functionality
 *
 * Author: Dave Jiang <dave.jiang@intel.com>
 * Copyright (C) 2003 Intel Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/list.h>

#include <asm/mach/irq.h>
#include <asm/irq.h>
#include <asm/hardware.h>

#include <asm/mach-types.h>

static u32 iop331_mask0 = 0;
static u32 iop331_mask1 = 0;

static inline void intctl_write0(u32 val)
{
    // INTCTL0
	iop3xx_cp6_enable();
	asm volatile("mcr p6,0,%0,c0,c0,0"::"r" (val));
	iop3xx_cp6_disable();
}

static inline void intctl_write1(u32 val)
{
    // INTCTL1
	iop3xx_cp6_enable();
    asm volatile("mcr p6,0,%0,c1,c0,0"::"r" (val));
	iop3xx_cp6_disable();
}

static inline void intstr_write0(u32 val)
{
    // INTSTR0
	iop3xx_cp6_enable();
	asm volatile("mcr p6,0,%0,c2,c0,0"::"r" (val));
	iop3xx_cp6_disable();
}

static inline void intstr_write1(u32 val)
{
    // INTSTR1
	iop3xx_cp6_enable();
	asm volatile("mcr p6,0,%0,c3,c0,0"::"r" (val));
	iop3xx_cp6_disable();
}

static void
iop331_irq_mask1 (unsigned int irq)
{
        iop331_mask0 &= ~(1 << irq);
        intctl_write0(iop331_mask0);
}

static void
iop331_irq_mask2 (unsigned int irq)
{
        iop331_mask1 &= ~(1 << (irq - 32));
        intctl_write1(iop331_mask1);
}

static void
iop331_irq_unmask1(unsigned int irq)
{
        iop331_mask0 |= (1 << irq);
        intctl_write0(iop331_mask0);
}

static void
iop331_irq_unmask2(unsigned int irq)
{
        iop331_mask1 |= (1 << (irq - 32));
        intctl_write1(iop331_mask1);
}

struct irq_chip iop331_irqchip1 = {
	.name	= "IOP-1",
	.ack    = iop331_irq_mask1,
	.mask   = iop331_irq_mask1,
	.unmask = iop331_irq_unmask1,
};

struct irq_chip iop331_irqchip2 = {
	.name	= "IOP-2",
	.ack    = iop331_irq_mask2,
	.mask   = iop331_irq_mask2,
	.unmask = iop331_irq_unmask2,
};

void __init iop331_init_irq(void)
{
	unsigned int i;

	intctl_write0(0);		// disable all interrupts
    	intctl_write1(0);
	intstr_write0(0);		// treat all as IRQ
    	intstr_write1(0);
	if(machine_is_iq80331()) 	// all interrupts are inputs to chip
		*IOP3XX_PCIIRSR = 0x0f;

	for(i = 0; i < NR_IRQS; i++)
	{
		set_irq_chip(i, (i < 32) ? &iop331_irqchip1 : &iop331_irqchip2);
		set_irq_handler(i, do_level_IRQ);
		set_irq_flags(i, IRQF_VALID | IRQF_PROBE);
	}
}

