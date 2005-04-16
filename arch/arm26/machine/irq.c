/*
 *  linux/arch/arm26/mach-arc/irq.c
 *
 *  Copyright (C) 1996 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Changelog:
 *   24-09-1996	RMK	Created
 *   10-10-1996	RMK	Brought up to date with arch-sa110eval
 *   22-10-1996	RMK	Changed interrupt numbers & uses new inb/outb macros
 *   11-01-1998	RMK	Added mask_and_ack_irq
 *   22-08-1998	RMK	Restructured IRQ routines
 *   08-09-2002 IM	Brought up to date for 2.5
 *   01-06-2003 JMA     Removed arc_fiq_chip
 */
#include <linux/config.h>
#include <linux/init.h>

#include <asm/irq.h>
#include <asm/irqchip.h>
#include <asm/ioc.h>
#include <asm/io.h>
#include <asm/system.h>

extern void init_FIQ(void);

#define a_clf()	clf()
#define a_stf()	stf()

static void arc_ack_irq_a(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << irq;
	a_clf();
	val = ioc_readb(IOC_IRQMASKA);
	ioc_writeb(val & ~mask, IOC_IRQMASKA);
	ioc_writeb(mask, IOC_IRQCLRA);
	a_stf();
}

static void arc_mask_irq_a(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << irq;
	a_clf();
	val = ioc_readb(IOC_IRQMASKA);
	ioc_writeb(val & ~mask, IOC_IRQMASKA);
	a_stf();
}

static void arc_unmask_irq_a(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << irq;
	a_clf();
	val = ioc_readb(IOC_IRQMASKA);
	ioc_writeb(val | mask, IOC_IRQMASKA);
	a_stf();
}

static struct irqchip arc_a_chip = {
        .ack    = arc_ack_irq_a,
        .mask   = arc_mask_irq_a,
        .unmask = arc_unmask_irq_a,
};

static void arc_mask_irq_b(unsigned int irq)
{
	unsigned int val, mask;
	mask = 1 << (irq & 7);
	val = ioc_readb(IOC_IRQMASKB);
	ioc_writeb(val & ~mask, IOC_IRQMASKB);
}

static void arc_unmask_irq_b(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << (irq & 7);
	val = ioc_readb(IOC_IRQMASKB);
	ioc_writeb(val | mask, IOC_IRQMASKB);
}

static struct irqchip arc_b_chip = {
        .ack    = arc_mask_irq_b,
        .mask   = arc_mask_irq_b,
        .unmask = arc_unmask_irq_b,
};

/* FIXME - JMA none of these functions are used in arm26 currently
static void arc_mask_irq_fiq(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << (irq & 7);
	val = ioc_readb(IOC_FIQMASK);
	ioc_writeb(val & ~mask, IOC_FIQMASK);
}

static void arc_unmask_irq_fiq(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << (irq & 7);
	val = ioc_readb(IOC_FIQMASK);
	ioc_writeb(val | mask, IOC_FIQMASK);
}

static struct irqchip arc_fiq_chip = {
        .ack    = arc_mask_irq_fiq,
        .mask   = arc_mask_irq_fiq,
        .unmask = arc_unmask_irq_fiq,
};
*/

void __init arc_init_irq(void)
{
	unsigned int irq, flags;

	/* Disable all IOC interrupt sources */
	ioc_writeb(0, IOC_IRQMASKA);
	ioc_writeb(0, IOC_IRQMASKB);
	ioc_writeb(0, IOC_FIQMASK);

	for (irq = 0; irq < NR_IRQS; irq++) {
		flags = IRQF_VALID;
		
		if (irq <= 6 || (irq >= 9 && irq <= 15))
                        flags |= IRQF_PROBE;
	
		if (irq == IRQ_KEYBOARDTX)
                        flags |= IRQF_NOAUTOEN;	
		
		switch (irq) {
		case 0 ... 7:
			set_irq_chip(irq, &arc_a_chip);
                        set_irq_handler(irq, do_level_IRQ);
                        set_irq_flags(irq, flags);
			break;

		case 8 ... 15:
			set_irq_chip(irq, &arc_b_chip);
                        set_irq_handler(irq, do_level_IRQ);
                        set_irq_flags(irq, flags);

/*		case 64 ... 72:
			set_irq_chip(irq, &arc_fiq_chip);
                        set_irq_flags(irq, flags);
			break;
*/

		}
	}

	irq_desc[IRQ_KEYBOARDTX].noautoenable = 1;

	init_FIQ();
}

