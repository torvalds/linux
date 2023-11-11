// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/arm/mach-orion5x/irq.c
 *
 * Core IRQ functions for Marvell Orion System On Chip
 *
 * Maintainer: Tzachi Perelstein <tzachi@marvell.com>
 */
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <plat/orion-gpio.h>
#include <plat/irq.h>
#include <asm/exception.h>
#include "bridge-regs.h"
#include "common.h"

static int __initdata gpio0_irqs[4] = {
	IRQ_ORION5X_GPIO_0_7,
	IRQ_ORION5X_GPIO_8_15,
	IRQ_ORION5X_GPIO_16_23,
	IRQ_ORION5X_GPIO_24_31,
};

static asmlinkage void
__exception_irq_entry orion5x_legacy_handle_irq(struct pt_regs *regs)
{
	u32 stat;

	stat = readl_relaxed(MAIN_IRQ_CAUSE);
	stat &= readl_relaxed(MAIN_IRQ_MASK);
	if (stat) {
		unsigned int hwirq = 1 + __fls(stat);
		handle_IRQ(hwirq, regs);
		return;
	}
}

void __init orion5x_init_irq(void)
{
	orion_irq_init(1, MAIN_IRQ_MASK);

	set_handle_irq(orion5x_legacy_handle_irq);

	/*
	 * Initialize gpiolib for GPIOs 0-31.
	 */
	orion_gpio_init(0, 32, GPIO_VIRT_BASE, 0,
			IRQ_ORION5X_GPIO_START, gpio0_irqs);
}
