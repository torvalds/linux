/*
 * linux/arch/arm/mach-iop3xx/iop321-irq.c
 *
 * Generic IOP321 IRQ handling functionality
 *
 * Author: Rory Bolt <rorybolt@pacbell.net>
 * Copyright (C) 2002 Rory Bolt
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Added IOP3XX chipset and IQ80321 board masking code.
 *
 */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/list.h>

#include <asm/mach/irq.h>
#include <asm/irq.h>
#include <asm/hardware.h>

#include <asm/mach-types.h>

static u32 iop321_mask /* = 0 */;

static inline void intctl_write(u32 val)
{
	asm volatile("mcr p6,0,%0,c0,c0,0"::"r" (val));
}

static inline void intstr_write(u32 val)
{
	asm volatile("mcr p6,0,%0,c4,c0,0"::"r" (val));
}

static void
iop321_irq_mask (unsigned int irq)
{

	iop321_mask &= ~(1 << (irq - IOP321_IRQ_OFS));

	intctl_write(iop321_mask);
}

static void
iop321_irq_unmask (unsigned int irq)
{
	iop321_mask |= (1 << (irq - IOP321_IRQ_OFS));

	intctl_write(iop321_mask);
}

struct irqchip ext_chip = {
	.ack    = iop321_irq_mask,
	.mask   = iop321_irq_mask,
	.unmask = iop321_irq_unmask,
};

void __init iop321_init_irq(void)
{
	unsigned int i, tmp;

	/* Enable access to coprocessor 6 for dealing with IRQs.
	 * From RMK:
	 * Basically, the Intel documentation here is poor.  It appears that
	 * you need to set the bit to be able to access the coprocessor from
	 * SVC mode.  Whether that allows access from user space or not is
	 * unclear.
	 */
	asm volatile (
		"mrc p15, 0, %0, c15, c1, 0\n\t"
		"orr %0, %0, %1\n\t"
		"mcr p15, 0, %0, c15, c1, 0\n\t"
		/* The action is delayed, so we have to do this: */
		"mrc p15, 0, %0, c15, c1, 0\n\t"
		"mov %0, %0\n\t"
		"sub pc, pc, #4"
		: "=r" (tmp) : "i" (1 << 6) );

	intctl_write(0);		// disable all interrupts
	intstr_write(0);		// treat all as IRQ
	if(machine_is_iq80321() ||
	   machine_is_iq31244()) 	// all interrupts are inputs to chip
		*IOP321_PCIIRSR = 0x0f;

	for(i = IOP321_IRQ_OFS; i < NR_IOP321_IRQS; i++)
	{
		set_irq_chip(i, &ext_chip);
		set_irq_handler(i, do_level_IRQ);
		set_irq_flags(i, IRQF_VALID | IRQF_PROBE);

	}
}

