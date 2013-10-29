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

static void pmu_irq_mask(struct irq_data *d)
{
	int pin = irq_to_pmu(d->irq);
	u32 u;

	u = readl(PMU_INTERRUPT_MASK);
	u &= ~(1 << (pin & 31));
	writel(u, PMU_INTERRUPT_MASK);
}

static void pmu_irq_unmask(struct irq_data *d)
{
	int pin = irq_to_pmu(d->irq);
	u32 u;

	u = readl(PMU_INTERRUPT_MASK);
	u |= 1 << (pin & 31);
	writel(u, PMU_INTERRUPT_MASK);
}

static void pmu_irq_ack(struct irq_data *d)
{
	int pin = irq_to_pmu(d->irq);
	u32 u;

	/*
	 * The PMU mask register is not RW0C: it is RW.  This means that
	 * the bits take whatever value is written to them; if you write
	 * a '1', you will set the interrupt.
	 *
	 * Unfortunately this means there is NO race free way to clear
	 * these interrupts.
	 *
	 * So, let's structure the code so that the window is as small as
	 * possible.
	 */
	u = ~(1 << (pin & 31));
	u &= readl_relaxed(PMU_INTERRUPT_CAUSE);
	writel_relaxed(u, PMU_INTERRUPT_CAUSE);
}

static struct irq_chip pmu_irq_chip = {
	.name		= "pmu_irq",
	.irq_mask	= pmu_irq_mask,
	.irq_unmask	= pmu_irq_unmask,
	.irq_ack	= pmu_irq_ack,
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
		generic_handle_irq(irq);
	}
}

void __init dove_init_irq(void)
{
	int i;

	orion_irq_init(0, (void __iomem *)(IRQ_VIRT_BASE + IRQ_MASK_LOW_OFF));
	orion_irq_init(32, (void __iomem *)(IRQ_VIRT_BASE + IRQ_MASK_HIGH_OFF));

	/*
	 * Initialize gpiolib for GPIOs 0-71.
	 */
	orion_gpio_init(0, 32, DOVE_GPIO_LO_VIRT_BASE, 0,
			IRQ_DOVE_GPIO_START);
	irq_set_chained_handler(IRQ_DOVE_GPIO_0_7, gpio_irq_handler);
	irq_set_chained_handler(IRQ_DOVE_GPIO_8_15, gpio_irq_handler);
	irq_set_chained_handler(IRQ_DOVE_GPIO_16_23, gpio_irq_handler);
	irq_set_chained_handler(IRQ_DOVE_GPIO_24_31, gpio_irq_handler);

	orion_gpio_init(32, 32, DOVE_GPIO_HI_VIRT_BASE, 0,
			IRQ_DOVE_GPIO_START + 32);
	irq_set_chained_handler(IRQ_DOVE_HIGH_GPIO, gpio_irq_handler);

	orion_gpio_init(64, 8, DOVE_GPIO2_VIRT_BASE, 0,
			IRQ_DOVE_GPIO_START + 64);

	/*
	 * Mask and clear PMU interrupts
	 */
	writel(0, PMU_INTERRUPT_MASK);
	writel(0, PMU_INTERRUPT_CAUSE);

	for (i = IRQ_DOVE_PMU_START; i < NR_IRQS; i++) {
		irq_set_chip_and_handler(i, &pmu_irq_chip, handle_level_irq);
		irq_set_status_flags(i, IRQ_LEVEL);
		set_irq_flags(i, IRQF_VALID);
	}
	irq_set_chained_handler(IRQ_DOVE_PMU, pmu_irq_handler);
}
