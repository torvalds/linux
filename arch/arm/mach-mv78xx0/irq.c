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
#include <linux/io.h>
#include <asm/exception.h>
#include <plat/orion-gpio.h>
#include <plat/irq.h>
#include "bridge-regs.h"
#include "common.h"

static int __initdata gpio0_irqs[4] = {
	IRQ_MV78XX0_GPIO_0_7,
	IRQ_MV78XX0_GPIO_8_15,
	IRQ_MV78XX0_GPIO_16_23,
	IRQ_MV78XX0_GPIO_24_31,
};

static void __iomem *mv78xx0_irq_base = IRQ_VIRT_BASE;

static asmlinkage void
__exception_irq_entry mv78xx0_legacy_handle_irq(struct pt_regs *regs)
{
	u32 stat;

	stat = readl_relaxed(mv78xx0_irq_base + IRQ_CAUSE_LOW_OFF);
	stat &= readl_relaxed(mv78xx0_irq_base + IRQ_MASK_LOW_OFF);
	if (stat) {
		unsigned int hwirq = __fls(stat);
		handle_IRQ(hwirq, regs);
		return;
	}
	stat = readl_relaxed(mv78xx0_irq_base + IRQ_CAUSE_HIGH_OFF);
	stat &= readl_relaxed(mv78xx0_irq_base + IRQ_MASK_HIGH_OFF);
	if (stat) {
		unsigned int hwirq = 32 + __fls(stat);
		handle_IRQ(hwirq, regs);
		return;
	}
	stat = readl_relaxed(mv78xx0_irq_base + IRQ_CAUSE_ERR_OFF);
	stat &= readl_relaxed(mv78xx0_irq_base + IRQ_MASK_ERR_OFF);
	if (stat) {
		unsigned int hwirq = 64 + __fls(stat);
		handle_IRQ(hwirq, regs);
		return;
	}
}

void __init mv78xx0_init_irq(void)
{
	orion_irq_init(0, IRQ_VIRT_BASE + IRQ_MASK_LOW_OFF);
	orion_irq_init(32, IRQ_VIRT_BASE + IRQ_MASK_HIGH_OFF);
	orion_irq_init(64, IRQ_VIRT_BASE + IRQ_MASK_ERR_OFF);

	set_handle_irq(mv78xx0_legacy_handle_irq);

	/*
	 * Initialize gpiolib for GPIOs 0-31.  (The GPIO interrupt mask
	 * registers for core #1 are at an offset of 0x18 from those of
	 * core #0.)
	 */
	orion_gpio_init(0, 32, GPIO_VIRT_BASE, mv78xx0_core_index() ? 0x18 : 0,
			IRQ_MV78XX0_GPIO_START, gpio0_irqs);
}
