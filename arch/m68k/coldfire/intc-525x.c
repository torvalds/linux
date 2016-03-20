/*
 * intc2.c  -- support for the 2nd INTC controller of the 525x
 *
 * (C) Copyright 2012, Steven King <sfking@fdwdc.com>
 * (C) Copyright 2009, Greg Ungerer <gerg@snapgear.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>

static void intc2_irq_gpio_mask(struct irq_data *d)
{
	u32 imr = readl(MCFSIM2_GPIOINTENABLE);
	u32 type = irqd_get_trigger_type(d);
	int irq = d->irq - MCF_IRQ_GPIO0;

	if (type & IRQ_TYPE_EDGE_RISING)
		imr &= ~(0x001 << irq);
	if (type & IRQ_TYPE_EDGE_FALLING)
		imr &= ~(0x100 << irq);
	writel(imr, MCFSIM2_GPIOINTENABLE);
}

static void intc2_irq_gpio_unmask(struct irq_data *d)
{
	u32 imr = readl(MCFSIM2_GPIOINTENABLE);
	u32 type = irqd_get_trigger_type(d);
	int irq = d->irq - MCF_IRQ_GPIO0;

	if (type & IRQ_TYPE_EDGE_RISING)
		imr |= (0x001 << irq);
	if (type & IRQ_TYPE_EDGE_FALLING)
		imr |= (0x100 << irq);
	writel(imr, MCFSIM2_GPIOINTENABLE);
}

static void intc2_irq_gpio_ack(struct irq_data *d)
{
	u32 imr = 0;
	u32 type = irqd_get_trigger_type(d);
	int irq = d->irq - MCF_IRQ_GPIO0;

	if (type & IRQ_TYPE_EDGE_RISING)
		imr |= (0x001 << irq);
	if (type & IRQ_TYPE_EDGE_FALLING)
		imr |= (0x100 << irq);
	writel(imr, MCFSIM2_GPIOINTCLEAR);
}

static int intc2_irq_gpio_set_type(struct irq_data *d, unsigned int f)
{
	if (f & ~IRQ_TYPE_EDGE_BOTH)
		return -EINVAL;
	return 0;
}

static struct irq_chip intc2_irq_gpio_chip = {
	.name		= "CF-INTC2",
	.irq_mask	= intc2_irq_gpio_mask,
	.irq_unmask	= intc2_irq_gpio_unmask,
	.irq_ack	= intc2_irq_gpio_ack,
	.irq_set_type	= intc2_irq_gpio_set_type,
};

static int __init mcf_intc2_init(void)
{
	int irq;

	/* set the interrupt base for the second interrupt controller */
	writel(MCFINTC2_VECBASE, MCFINTC2_INTBASE);

	/* GPIO interrupt sources */
	for (irq = MCF_IRQ_GPIO0; (irq <= MCF_IRQ_GPIO6); irq++) {
		irq_set_chip(irq, &intc2_irq_gpio_chip);
		irq_set_handler(irq, handle_edge_irq);
	}

	return 0;
}

arch_initcall(mcf_intc2_init);
