/*
 * arch/arm/mach-mv78xx0/irq.c
 *
 * MV78xx0 IRQ handling.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/irq.h>
#include <mach/bridge-regs.h>
#include <plat/irq.h>
#include "common.h"

static int __initdata gpio0_irqs[4] = {
	IRQ_MV78XX0_GPIO_0_7,
	IRQ_MV78XX0_GPIO_8_15,
	IRQ_MV78XX0_GPIO_16_23,
	IRQ_MV78XX0_GPIO_24_31,
};

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
	orion_gpio_init(NULL, 0, 32, (void __iomem *)GPIO_VIRT_BASE,
			mv78xx0_core_index() ? 0x18 : 0,
			IRQ_MV78XX0_GPIO_START, gpio0_irqs);
}
