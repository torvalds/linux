/*
 * arch/arm/mach-kirkwood/irq.c
 *
 * Kirkwood IRQ handling.
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
	IRQ_KIRKWOOD_GPIO_LOW_0_7,
	IRQ_KIRKWOOD_GPIO_LOW_8_15,
	IRQ_KIRKWOOD_GPIO_LOW_16_23,
	IRQ_KIRKWOOD_GPIO_LOW_24_31,
};

static int __initdata gpio1_irqs[4] = {
	IRQ_KIRKWOOD_GPIO_HIGH_0_7,
	IRQ_KIRKWOOD_GPIO_HIGH_8_15,
	IRQ_KIRKWOOD_GPIO_HIGH_16_23,
	0,
};

void __init kirkwood_init_irq(void)
{
	orion_irq_init(0, IRQ_VIRT_BASE + IRQ_MASK_LOW_OFF);
	orion_irq_init(32, IRQ_VIRT_BASE + IRQ_MASK_HIGH_OFF);

	/*
	 * Initialize gpiolib for GPIOs 0-49.
	 */
	orion_gpio_init(NULL, 0, 32, GPIO_LOW_VIRT_BASE, 0,
			IRQ_KIRKWOOD_GPIO_START, gpio0_irqs);
	orion_gpio_init(NULL, 32, 18, GPIO_HIGH_VIRT_BASE, 0,
			IRQ_KIRKWOOD_GPIO_START + 32, gpio1_irqs);
}
