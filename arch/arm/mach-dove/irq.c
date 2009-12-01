/*
 * arch/arm/mach-dove/irq.c
 *
 * Dove IRQ handling.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <asm/mach/arch.h>
#include <plat/irq.h>
#include <asm/mach/irq.h>
#include <mach/pm.h>
#include <mach/bridge-regs.h>
#include "common.h"

static void gpio_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	int irqoff;
	BUG_ON(irq < IRQ_DOVE_GPIO_0_7 || irq > IRQ_DOVE_HIGH_GPIO);

	irqoff = irq <= IRQ_DOVE_GPIO_16_23 ? irq - IRQ_DOVE_GPIO_0_7 :
		3 + irq - IRQ_DOVE_GPIO_24_31;

	orion_gpio_irq_handler(irqoff << 3);
	if (irq == IRQ_DOVE_HIGH_GPIO) {
		orion_gpio_irq_handler(40);
		orion_gpio_irq_handler(48);
		orion_gpio_irq_handler(56);
	}
}

static void pmu_irq_mask(unsigned int irq)
{
	int pin = irq_to_pmu(irq);
	u32 u;

	u = readl(PMU_INTERRUPT_MASK);
	u &= ~(1 << (pin & 31));
	writel(u, PMU_INTERRUPT_MASK);
}

static void pmu_irq_unmask(unsigned int irq)
{
	int pin = irq_to_pmu(irq);
	u32 u;

	u = readl(PMU_INTERRUPT_MASK);
	u |= 1 << (pin & 31);
	writel(u, PMU_INTERRUPT_MASK);
}

static void pmu_irq_ack(unsigned int irq)
{
	int pin = irq_to_pmu(irq);
	u32 u;

	u = ~(1 << (pin & 31));
	writel(u, PMU_INTERRUPT_CAUSE);
}

static struct irq_chip pmu_irq_chip = {
	.name		= "pmu_irq",
	.mask		= pmu_irq_mask,
	.unmask		= pmu_irq_unmask,
	.ack		= pmu_irq_ack,
};

static void pmu_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	unsigned long cause = readl(PMU_INTERRUPT_CAUSE);

	cause &= readl(PMU_INTERRUPT_MASK);
	if (cause == 0) {
		do_bad_IRQ(irq, desc);
		return;
	}

	for (irq = 0; irq < NR_PMU_IRQS; irq++) {
		if (!(cause & (1 << irq)))
			continue;
		irq = pmu_to_irq(irq);
		desc = irq_desc + irq;
		desc_handle_irq(irq, desc);
	}
}

void __init dove_init_irq(void)
{
	int i;

	orion_irq_init(0, (void __iomem *)(IRQ_VIRT_BASE + IRQ_MASK_LOW_OFF));
	orion_irq_init(32, (void __iomem *)(IRQ_VIRT_BASE + IRQ_MASK_HIGH_OFF));

	/*
	 * Mask and clear GPIO IRQ interrupts.
	 */
	writel(0, GPIO_LEVEL_MASK(0));
	writel(0, GPIO_EDGE_MASK(0));
	writel(0, GPIO_EDGE_CAUSE(0));

	/*
	 * Mask and clear PMU interrupts
	 */
	writel(0, PMU_INTERRUPT_MASK);
	writel(0, PMU_INTERRUPT_CAUSE);

	for (i = IRQ_DOVE_GPIO_START; i < IRQ_DOVE_PMU_START; i++) {
		set_irq_chip(i, &orion_gpio_irq_chip);
		set_irq_handler(i, handle_level_irq);
		irq_desc[i].status |= IRQ_LEVEL;
		set_irq_flags(i, IRQF_VALID);
	}
	set_irq_chained_handler(IRQ_DOVE_GPIO_0_7, gpio_irq_handler);
	set_irq_chained_handler(IRQ_DOVE_GPIO_8_15, gpio_irq_handler);
	set_irq_chained_handler(IRQ_DOVE_GPIO_16_23, gpio_irq_handler);
	set_irq_chained_handler(IRQ_DOVE_GPIO_24_31, gpio_irq_handler);
	set_irq_chained_handler(IRQ_DOVE_HIGH_GPIO, gpio_irq_handler);

	for (i = IRQ_DOVE_PMU_START; i < NR_IRQS; i++) {
		set_irq_chip(i, &pmu_irq_chip);
		set_irq_handler(i, handle_level_irq);
		irq_desc[i].status |= IRQ_LEVEL;
		set_irq_flags(i, IRQF_VALID);
	}
	set_irq_chained_handler(IRQ_DOVE_PMU, pmu_irq_handler);
}
