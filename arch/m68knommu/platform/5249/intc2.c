/*
 * intc2.c  -- support for the 2nd INTC controller of the 5249
 *
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

static void intc2_irq_gpio_mask(unsigned int irq)
{
	u32 imr;
	imr = readl(MCF_MBAR2 + MCFSIM2_GPIOINTENABLE);
	imr &= ~(0x1 << (irq - MCFINTC2_GPIOIRQ0));
	writel(imr, MCF_MBAR2 + MCFSIM2_GPIOINTENABLE);
}

static void intc2_irq_gpio_unmask(unsigned int irq)
{
	u32 imr;
	imr = readl(MCF_MBAR2 + MCFSIM2_GPIOINTENABLE);
	imr |= (0x1 << (irq - MCFINTC2_GPIOIRQ0));
	writel(imr, MCF_MBAR2 + MCFSIM2_GPIOINTENABLE);
}

static void intc2_irq_gpio_ack(unsigned int irq)
{
	writel(0x1 << (irq - MCFINTC2_GPIOIRQ0), MCF_MBAR2 + MCFSIM2_GPIOINTCLEAR);
}

static struct irq_chip intc2_irq_gpio_chip = {
	.name		= "CF-INTC2",
	.mask		= intc2_irq_gpio_mask,
	.unmask		= intc2_irq_gpio_unmask,
	.ack		= intc2_irq_gpio_ack,
};

static int __init mcf_intc2_init(void)
{
	int irq;

	/* GPIO interrupt sources */
	for (irq = MCFINTC2_GPIOIRQ0; (irq <= MCFINTC2_GPIOIRQ7); irq++) {
		irq_desc[irq].chip = &intc2_irq_gpio_chip;
		set_irq_handler(irq, handle_edge_irq);
	}

	return 0;
}

arch_initcall(mcf_intc2_init);
