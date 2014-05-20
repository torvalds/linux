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
#include <plat/orion-gpio.h>
#include "common.h"

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

#ifdef CONFIG_MULTI_IRQ_HANDLER
/*
 * Compiling with both non-DT and DT support enabled, will
 * break asm irq handler used by non-DT boards. Therefore,
 * we provide a C-style irq handler even for non-DT boards,
 * if MULTI_IRQ_HANDLER is set.
 */

static void __iomem *dove_irq_base = IRQ_VIRT_BASE;

static asmlinkage void
__exception_irq_entry dove_legacy_handle_irq(struct pt_regs *regs)
{
	u32 stat;

	stat = readl_relaxed(dove_irq_base + IRQ_CAUSE_LOW_OFF);
	stat &= readl_relaxed(dove_irq_base + IRQ_MASK_LOW_OFF);
	if (stat) {
		unsigned int hwirq = __fls(stat);
		handle_IRQ(hwirq, regs);
		return;
	}
	stat = readl_relaxed(dove_irq_base + IRQ_CAUSE_HIGH_OFF);
	stat &= readl_relaxed(dove_irq_base + IRQ_MASK_HIGH_OFF);
	if (stat) {
		unsigned int hwirq = 32 + __fls(stat);
		handle_IRQ(hwirq, regs);
		return;
	}
}
#endif

void __init dove_init_irq(void)
{
	int i;

	orion_irq_init(0, IRQ_VIRT_BASE + IRQ_MASK_LOW_OFF);
	orion_irq_init(32, IRQ_VIRT_BASE + IRQ_MASK_HIGH_OFF);

#ifdef CONFIG_MULTI_IRQ_HANDLER
	set_handle_irq(dove_legacy_handle_irq);
#endif

	/*
	 * Initialize gpiolib for GPIOs 0-71.
	 */
	orion_gpio_init(NULL, 0, 32, DOVE_GPIO_LO_VIRT_BASE, 0,
			IRQ_DOVE_GPIO_START, gpio0_irqs);

	orion_gpio_init(NULL, 32, 32, DOVE_GPIO_HI_VIRT_BASE, 0,
			IRQ_DOVE_GPIO_START + 32, gpio1_irqs);

	orion_gpio_init(NULL, 64, 8, DOVE_GPIO2_VIRT_BASE, 0,
			IRQ_DOVE_GPIO_START + 64, gpio2_irqs);

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
