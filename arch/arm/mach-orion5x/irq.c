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
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/io.h>
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
	orion_irq_init(0, (void __iomem *)MAIN_IRQ_MASK);

	/*
	 * Initialize gpiolib for GPIOs 0-31.
	 */
	orion_gpio_init(0, 32, GPIO_VIRT_BASE, 0, IRQ_ORION5X_GPIO_START);
	irq_set_chained_handler(IRQ_ORION5X_GPIO_0_7, gpio_irq_handler);
	irq_set_chained_handler(IRQ_ORION5X_GPIO_8_15, gpio_irq_handler);
	irq_set_chained_handler(IRQ_ORION5X_GPIO_16_23, gpio_irq_handler);
	irq_set_chained_handler(IRQ_ORION5X_GPIO_24_31, gpio_irq_handler);
}
