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
#include <linux/irq.h>
#include <linux/io.h>
#include <mach/bridge-regs.h>
#include <plat/orion-gpio.h>
#include <plat/irq.h>

static int __initdata gpio0_irqs[4] = {
	IRQ_ORION5X_GPIO_0_7,
	IRQ_ORION5X_GPIO_8_15,
	IRQ_ORION5X_GPIO_16_23,
	IRQ_ORION5X_GPIO_24_31,
};

void __init orion5x_init_irq(void)
{
	orion_irq_init(0, MAIN_IRQ_MASK);

	/*
	 * Initialize gpiolib for GPIOs 0-31.
	 */
	orion_gpio_init(NULL, 0, 32, GPIO_VIRT_BASE, 0,
			IRQ_ORION5X_GPIO_START, gpio0_irqs);
}
