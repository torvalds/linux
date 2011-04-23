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
#include <plat/irq.h>

static void orion_irq_mask(struct irq_data *d)
{
	void __iomem *maskaddr = irq_data_get_irq_chip_data(d);
	u32 mask;

	mask = readl(maskaddr);
	mask &= ~(1 << (d->irq & 31));
	writel(mask, maskaddr);
}

static void orion_irq_unmask(struct irq_data *d)
{
	void __iomem *maskaddr = irq_data_get_irq_chip_data(d);
	u32 mask;

	mask = readl(maskaddr);
	mask |= 1 << (d->irq & 31);
	writel(mask, maskaddr);
}

static struct irq_chip orion_irq_chip = {
	.name		= "orion_irq",
	.irq_mask	= orion_irq_mask,
	.irq_mask_ack	= orion_irq_mask,
	.irq_unmask	= orion_irq_unmask,
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

		irq_set_chip_and_handler(irq, &orion_irq_chip,
					 handle_level_irq);
		irq_set_chip_data(irq, maskaddr);
		irq_set_status_flags(irq, IRQ_LEVEL);
		set_irq_flags(irq, IRQF_VALID);
	}
}
