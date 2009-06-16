/*
 * arch/arm/mach-orion5x/irq.c
 *
 * Core IRQ functions for Marvell Orion System On Chip
 *
 * Maintainer: Tzachi Perelstein <tzachi@marvell.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <asm/gpio.h>
#include <mach/bridge-regs.h>
#include <plat/irq.h>
#include "common.h"

static void gpio_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	BUG_ON(irq < IRQ_ORION5X_GPIO_0_7 || irq > IRQ_ORION5X_GPIO_24_31);

	orion_gpio_irq_handler((irq - IRQ_ORION5X_GPIO_0_7) << 3);
}

void __init orion5x_init_irq(void)
{
	int i;

	orion_irq_init(0, (void __iomem *)MAIN_IRQ_MASK);

	/*
	 * Mask and clear GPIO IRQ interrupts
	 */
	writel(0x0, GPIO_LEVEL_MASK(0));
	writel(0x0, GPIO_EDGE_MASK(0));
	writel(0x0, GPIO_EDGE_CAUSE(0));

	/*
	 * Register chained level handlers for GPIO IRQs by default.
	 * User can use set_type() if he wants to use edge types handlers.
	 */
	for (i = IRQ_ORION5X_GPIO_START; i < NR_IRQS; i++) {
		set_irq_chip(i, &orion_gpio_irq_chip);
		set_irq_handler(i, handle_level_irq);
		irq_desc[i].status |= IRQ_LEVEL;
		set_irq_flags(i, IRQF_VALID);
	}
	set_irq_chained_handler(IRQ_ORION5X_GPIO_0_7, gpio_irq_handler);
	set_irq_chained_handler(IRQ_ORION5X_GPIO_8_15, gpio_irq_handler);
	set_irq_chained_handler(IRQ_ORION5X_GPIO_16_23, gpio_irq_handler);
	set_irq_chained_handler(IRQ_ORION5X_GPIO_24_31, gpio_irq_handler);
}
