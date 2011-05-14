/*
 * Copyright (C) Telechips, Inc.
 * Copyright (C) 2009-2010 Hans J. Koch <hjk@linutronix.de>
 *
 * Licensed under the terms of the GNU GPL version 2.
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>

#include <asm/irq.h>
#include <asm/mach/irq.h>

#include <mach/tcc8k-regs.h>
#include <mach/irqs.h>

#include "common.h"

/* Disable IRQ */
static void tcc8000_mask_ack_irq0(struct irq_data *d)
{
	PIC0_IEN &= ~(1 << d->irq);
	PIC0_CREQ |=  (1 << d->irq);
}

static void tcc8000_mask_ack_irq1(struct irq_data *d)
{
	PIC1_IEN &= ~(1 << (d->irq - 32));
	PIC1_CREQ |= (1 << (d->irq - 32));
}

static void tcc8000_mask_irq0(struct irq_data *d)
{
	PIC0_IEN &= ~(1 << d->irq);
}

static void tcc8000_mask_irq1(struct irq_data *d)
{
	PIC1_IEN &= ~(1 << (d->irq - 32));
}

static void tcc8000_ack_irq0(struct irq_data *d)
{
	PIC0_CREQ |=  (1 << d->irq);
}

static void tcc8000_ack_irq1(struct irq_data *d)
{
	PIC1_CREQ |= (1 << (d->irq - 32));
}

/* Enable IRQ */
static void tcc8000_unmask_irq0(struct irq_data *d)
{
	PIC0_IEN |= (1 << d->irq);
	PIC0_INTOEN |= (1 << d->irq);
}

static void tcc8000_unmask_irq1(struct irq_data *d)
{
	PIC1_IEN |= (1 << (d->irq - 32));
	PIC1_INTOEN |= (1 << (d->irq - 32));
}

static struct irq_chip tcc8000_irq_chip0 = {
	.name		= "tcc_irq0",
	.irq_mask	= tcc8000_mask_irq0,
	.irq_ack	= tcc8000_ack_irq0,
	.irq_mask_ack	= tcc8000_mask_ack_irq0,
	.irq_unmask	= tcc8000_unmask_irq0,
};

static struct irq_chip tcc8000_irq_chip1 = {
	.name		= "tcc_irq1",
	.irq_mask	= tcc8000_mask_irq1,
	.irq_ack	= tcc8000_ack_irq1,
	.irq_mask_ack	= tcc8000_mask_ack_irq1,
	.irq_unmask	= tcc8000_unmask_irq1,
};

void __init tcc8k_init_irq(void)
{
	int irqno;

	/* Mask and clear all interrupts */
	PIC0_IEN = 0x00000000;
	PIC0_CREQ = 0xffffffff;
	PIC1_IEN = 0x00000000;
	PIC1_CREQ = 0xffffffff;

	PIC0_MEN0 = 0x00000003;
	PIC1_MEN1 = 0x00000003;
	PIC1_MEN = 0x00000003;

	/* let all IRQs be level triggered */
	PIC0_TMODE = 0xffffffff;
	PIC1_TMODE = 0xffffffff;
	/* all IRQs are IRQs (not FIQs) */
	PIC0_IRQSEL = 0xffffffff;
	PIC1_IRQSEL = 0xffffffff;

	for (irqno = 0; irqno < NR_IRQS; irqno++) {
		if (irqno < 32)
			irq_set_chip(irqno, &tcc8000_irq_chip0);
		else
			irq_set_chip(irqno, &tcc8000_irq_chip1);
		irq_set_handler(irqno, handle_level_irq);
		set_irq_flags(irqno, IRQF_VALID);
	}
}
