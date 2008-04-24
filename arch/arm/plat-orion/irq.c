/*
 * arch/arm/plat-orion/irq.c
 *
 * Marvell Orion SoC IRQ handling.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <asm/plat-orion/irq.h>

static void orion_irq_mask(u32 irq)
{
	void __iomem *maskaddr = get_irq_chip_data(irq);
	u32 mask;

	mask = readl(maskaddr);
	mask &= ~(1 << (irq & 31));
	writel(mask, maskaddr);
}

static void orion_irq_unmask(u32 irq)
{
	void __iomem *maskaddr = get_irq_chip_data(irq);
	u32 mask;

	mask = readl(maskaddr);
	mask |= 1 << (irq & 31);
	writel(mask, maskaddr);
}

static struct irq_chip orion_irq_chip = {
	.name		= "orion_irq",
	.ack		= orion_irq_mask,
	.mask		= orion_irq_mask,
	.unmask		= orion_irq_unmask,
};

void __init orion_irq_init(unsigned int irq_start, void __iomem *maskaddr)
{
	unsigned int i;

	/*
	 * Mask all interrupts initially.
	 */
	writel(0, maskaddr);

	/*
	 * Register IRQ sources.
	 */
	for (i = 0; i < 32; i++) {
		unsigned int irq = irq_start + i;

		set_irq_chip(irq, &orion_irq_chip);
		set_irq_chip_data(irq, maskaddr);
		set_irq_handler(irq, handle_level_irq);
		set_irq_flags(irq, IRQF_VALID);
	}
}
