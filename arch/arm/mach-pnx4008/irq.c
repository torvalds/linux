/*
 * arch/arm/mach-pnx4008/irq.c
 *
 * PNX4008 IRQ controller driver
 *
 * Author: Dmitry Chigirev <source@mvista.com>
 *
 * Based on reference code received from Philips:
 * Copyright (C) 2003 Philips Semiconductors
 *
 * 2005 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <mach/hardware.h>
#include <asm/setup.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/system.h>
#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <asm/mach/map.h>
#include <mach/irq.h>

static u8 pnx4008_irq_type[NR_IRQS] = PNX4008_IRQ_TYPES;

static void pnx4008_mask_irq(struct irq_data *d)
{
	__raw_writel(__raw_readl(INTC_ER(d->irq)) & ~INTC_BIT(d->irq), INTC_ER(d->irq));	/* mask interrupt */
}

static void pnx4008_unmask_irq(struct irq_data *d)
{
	__raw_writel(__raw_readl(INTC_ER(d->irq)) | INTC_BIT(d->irq), INTC_ER(d->irq));	/* unmask interrupt */
}

static void pnx4008_mask_ack_irq(struct irq_data *d)
{
	__raw_writel(__raw_readl(INTC_ER(d->irq)) & ~INTC_BIT(d->irq), INTC_ER(d->irq));	/* mask interrupt */
	__raw_writel(INTC_BIT(d->irq), INTC_SR(d->irq));	/* clear interrupt status */
}

static int pnx4008_set_irq_type(struct irq_data *d, unsigned int type)
{
	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		__raw_writel(__raw_readl(INTC_ATR(d->irq)) | INTC_BIT(d->irq), INTC_ATR(d->irq));	/*edge sensitive */
		__raw_writel(__raw_readl(INTC_APR(d->irq)) | INTC_BIT(d->irq), INTC_APR(d->irq));	/*rising edge */
		irq_set_handler(d->irq, handle_edge_irq);
		break;
	case IRQ_TYPE_EDGE_FALLING:
		__raw_writel(__raw_readl(INTC_ATR(d->irq)) | INTC_BIT(d->irq), INTC_ATR(d->irq));	/*edge sensitive */
		__raw_writel(__raw_readl(INTC_APR(d->irq)) & ~INTC_BIT(d->irq), INTC_APR(d->irq));	/*falling edge */
		irq_set_handler(d->irq, handle_edge_irq);
		break;
	case IRQ_TYPE_LEVEL_LOW:
		__raw_writel(__raw_readl(INTC_ATR(d->irq)) & ~INTC_BIT(d->irq), INTC_ATR(d->irq));	/*level sensitive */
		__raw_writel(__raw_readl(INTC_APR(d->irq)) & ~INTC_BIT(d->irq), INTC_APR(d->irq));	/*low level */
		irq_set_handler(d->irq, handle_level_irq);
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		__raw_writel(__raw_readl(INTC_ATR(d->irq)) & ~INTC_BIT(d->irq), INTC_ATR(d->irq));	/*level sensitive */
		__raw_writel(__raw_readl(INTC_APR(d->irq)) | INTC_BIT(d->irq), INTC_APR(d->irq));	/* high level */
		irq_set_handler(d->irq, handle_level_irq);
		break;

	/* IRQ_TYPE_EDGE_BOTH is not supported */
	default:
		printk(KERN_ERR "PNX4008 IRQ: Unsupported irq type %d\n", type);
		return -1;
	}
	return 0;
}

static struct irq_chip pnx4008_irq_chip = {
	.irq_ack = pnx4008_mask_ack_irq,
	.irq_mask = pnx4008_mask_irq,
	.irq_unmask = pnx4008_unmask_irq,
	.irq_set_type = pnx4008_set_irq_type,
};

void __init pnx4008_init_irq(void)
{
	unsigned int i;

	/* configure IRQ's */
	for (i = 0; i < NR_IRQS; i++) {
		set_irq_flags(i, IRQF_VALID);
		irq_set_chip(i, &pnx4008_irq_chip);
		pnx4008_set_irq_type(irq_get_irq_data(i), pnx4008_irq_type[i]);
	}

	/* configure and enable IRQ 0,1,30,31 (cascade interrupts) */
	pnx4008_set_irq_type(irq_get_irq_data(SUB1_IRQ_N),
			     pnx4008_irq_type[SUB1_IRQ_N]);
	pnx4008_set_irq_type(irq_get_irq_data(SUB2_IRQ_N),
			     pnx4008_irq_type[SUB2_IRQ_N]);
	pnx4008_set_irq_type(irq_get_irq_data(SUB1_FIQ_N),
			     pnx4008_irq_type[SUB1_FIQ_N]);
	pnx4008_set_irq_type(irq_get_irq_data(SUB2_FIQ_N),
			     pnx4008_irq_type[SUB2_FIQ_N]);

	/* mask all others */
	__raw_writel((1 << SUB2_FIQ_N) | (1 << SUB1_FIQ_N) |
			(1 << SUB2_IRQ_N) | (1 << SUB1_IRQ_N),
		INTC_ER(MAIN_BASE_INT));
	__raw_writel(0, INTC_ER(SIC1_BASE_INT));
	__raw_writel(0, INTC_ER(SIC2_BASE_INT));
}

