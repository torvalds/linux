/*
 * arch/arm/mach-dove/irq.c
 *
 * Dove IRQ handling.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <asm/exception.h>

#include <plat/irq.h>
#include <plat/orion-gpio.h>

#include "pm.h"
#include "bridge-regs.h"
#include "common.h"

static int __initdata gpio0_irqs[4] = {
	IRQ_DOVE_GPIO_0_7,
	IRQ_DOVE_GPIO_8_15,
	IRQ_DOVE_GPIO_16_23,
	IRQ_DOVE_GPIO_24_31,
};

static int __initdata gpio1_irqs[4] = {
	IRQ_DOVE_HIGH_GPIO,
	0,
	0,
	0,
};

static int __initdata gpio2_irqs[4] = {
	0,
	0,
	0,
	0,
};

static void __iomem *dove_irq_base = IRQ_VIRT_BASE;

static asmlinkage void
__exception_irq_entry dove_legacy_handle_irq(struct pt_regs *regs)
{
	u32 stat;

	stat = readl_relaxed(dove_irq_base + IRQ_CAUSE_LOW_OFF);
	stat &= readl_relaxed(dove_irq_base + IRQ_MASK_LOW_OFF);
	if (stat) {
		unsigned int hwirq = 1 + __fls(stat);
		handle_IRQ(hwirq, regs);
		return;
	}
	stat = readl_relaxed(dove_irq_base + IRQ_CAUSE_HIGH_OFF);
	stat &= readl_relaxed(dove_irq_base + IRQ_MASK_HIGH_OFF);
	if (stat) {
		unsigned int hwirq = 33 + __fls(stat);
		handle_IRQ(hwirq, regs);
		return;
	}
}

void __init dove_init_irq(void)
{
	orion_irq_init(1, IRQ_VIRT_BASE + IRQ_MASK_LOW_OFF);
	orion_irq_init(33, IRQ_VIRT_BASE + IRQ_MASK_HIGH_OFF);

	set_handle_irq(dove_legacy_handle_irq);

	/*
	 * Initialize gpiolib for GPIOs 0-71.
	 */
	orion_gpio_init(NULL, 0, 32, DOVE_GPIO_LO_VIRT_BASE, 0,
			IRQ_DOVE_GPIO_START, gpio0_irqs);

	orion_gpio_init(NULL, 32, 32, DOVE_GPIO_HI_VIRT_BASE, 0,
			IRQ_DOVE_GPIO_START + 32, gpio1_irqs);

	orion_gpio_init(NULL, 64, 8, DOVE_GPIO2_VIRT_BASE, 0,
			IRQ_DOVE_GPIO_START + 64, gpio2_irqs);
}
