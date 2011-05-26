/*
 * arch/arm/mach-kirkwood/irq.c
 *
 * Kirkwood IRQ handling.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <mach/bridge-regs.h>
#include <plat/irq.h>
#include <asm/gpio.h>
#include "common.h"

static void gpio_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	BUG_ON(irq < IRQ_KIRKWOOD_GPIO_LOW_0_7);
	BUG_ON(irq > IRQ_KIRKWOOD_GPIO_HIGH_16_23);

	orion_gpio_irq_handler((irq - IRQ_KIRKWOOD_GPIO_LOW_0_7) << 3);
}

void __init kirkwood_init_irq(void)
{
	orion_irq_init(0, (void __iomem *)(IRQ_VIRT_BASE + IRQ_MASK_LOW_OFF));
	orion_irq_init(32, (void __iomem *)(IRQ_VIRT_BASE + IRQ_MASK_HIGH_OFF));

	/*
	 * Initialize gpiolib for GPIOs 0-49.
	 */
	orion_gpio_init(0, 32, GPIO_LOW_VIRT_BASE, 0,
			IRQ_KIRKWOOD_GPIO_START);
	irq_set_chained_handler(IRQ_KIRKWOOD_GPIO_LOW_0_7, gpio_irq_handler);
	irq_set_chained_handler(IRQ_KIRKWOOD_GPIO_LOW_8_15, gpio_irq_handler);
	irq_set_chained_handler(IRQ_KIRKWOOD_GPIO_LOW_16_23, gpio_irq_handler);
	irq_set_chained_handler(IRQ_KIRKWOOD_GPIO_LOW_24_31, gpio_irq_handler);

	orion_gpio_init(32, 18, GPIO_HIGH_VIRT_BASE, 0,
			IRQ_KIRKWOOD_GPIO_START + 32);
	irq_set_chained_handler(IRQ_KIRKWOOD_GPIO_HIGH_0_7, gpio_irq_handler);
	irq_set_chained_handler(IRQ_KIRKWOOD_GPIO_HIGH_8_15, gpio_irq_handler);
	irq_set_chained_handler(IRQ_KIRKWOOD_GPIO_HIGH_16_23,
				gpio_irq_handler);
}
