/*
 * arch/arm/mach-mv78xx0/irq.c
 *
 * MV78xx0 IRQ handling.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/irq.h>
#include <asm/gpio.h>
#include <mach/bridge-regs.h>
#include <plat/irq.h>
#include "common.h"

static void gpio_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	BUG_ON(irq < IRQ_MV78XX0_GPIO_0_7 || irq > IRQ_MV78XX0_GPIO_24_31);

	orion_gpio_irq_handler((irq - IRQ_MV78XX0_GPIO_0_7) << 3);
}

void __init mv78xx0_init_irq(void)
{
	orion_irq_init(0, (void __iomem *)(IRQ_VIRT_BASE + IRQ_MASK_LOW_OFF));
	orion_irq_init(32, (void __iomem *)(IRQ_VIRT_BASE + IRQ_MASK_HIGH_OFF));
	orion_irq_init(64, (void __iomem *)(IRQ_VIRT_BASE + IRQ_MASK_ERR_OFF));

	/*
	 * Initialize gpiolib for GPIOs 0-31.  (The GPIO interrupt mask
	 * registers for core #1 are at an offset of 0x18 from those of
	 * core #0.)
	 */
	orion_gpio_init(0, 32, GPIO_VIRT_BASE,
			mv78xx0_core_index() ? 0x18 : 0,
			IRQ_MV78XX0_GPIO_START);
	irq_set_chained_handler(IRQ_MV78XX0_GPIO_0_7, gpio_irq_handler);
	irq_set_chained_handler(IRQ_MV78XX0_GPIO_8_15, gpio_irq_handler);
	irq_set_chained_handler(IRQ_MV78XX0_GPIO_16_23, gpio_irq_handler);
	irq_set_chained_handler(IRQ_MV78XX0_GPIO_24_31, gpio_irq_handler);
}
