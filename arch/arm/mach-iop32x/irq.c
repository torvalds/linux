/*
 * arch/arm/mach-iop32x/irq.c
 *
 * Generic IOP32X IRQ handling functionality
 *
 * Author: Rory Bolt <rorybolt@pacbell.net>
 * Copyright (C) 2002 Rory Bolt
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <asm/mach/irq.h>
#include <asm/irq.h>
#include <asm/hardware.h>
#include <asm/mach-types.h>

static u32 iop32x_mask;

static void intctl_write(u32 val)
{
	asm volatile("mcr p6, 0, %0, c0, c0, 0" : : "r" (val));
}

static void intstr_write(u32 val)
{
	asm volatile("mcr p6, 0, %0, c4, c0, 0" : : "r" (val));
}

static void
iop32x_irq_mask(unsigned int irq)
{
	iop32x_mask &= ~(1 << irq);
	intctl_write(iop32x_mask);
}

static void
iop32x_irq_unmask(unsigned int irq)
{
	iop32x_mask |= 1 << irq;
	intctl_write(iop32x_mask);
}

struct irq_chip ext_chip = {
	.name	= "IOP32x",
	.ack	= iop32x_irq_mask,
	.mask	= iop32x_irq_mask,
	.unmask	= iop32x_irq_unmask,
};

void __init iop32x_init_irq(void)
{
	int i;

	iop_init_cp6_handler();

	intctl_write(0);
	intstr_write(0);
	if (machine_is_glantank() ||
	    machine_is_iq80321() ||
	    machine_is_iq31244() ||
	    machine_is_n2100())
		*IOP3XX_PCIIRSR = 0x0f;

	for (i = 0; i < NR_IRQS; i++) {
		set_irq_chip(i, &ext_chip);
		set_irq_handler(i, handle_level_irq);
		set_irq_flags(i, IRQF_VALID | IRQF_PROBE);
	}
}
