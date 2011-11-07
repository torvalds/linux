/* linux/arch/arm/mach-exynos4/irq-combiner.c
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Based on arch/arm/common/gic.c
 *
 * IRQ COMBINER support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/io.h>

#include <asm/mach/irq.h>

#define COMBINER_ENABLE_SET	0x0
#define COMBINER_ENABLE_CLEAR	0x4
#define COMBINER_INT_STATUS	0xC

static DEFINE_SPINLOCK(irq_controller_lock);

struct combiner_chip_data {
	unsigned int irq_offset;
	unsigned int irq_mask;
	void __iomem *base;
};

static struct combiner_chip_data combiner_data[MAX_COMBINER_NR];

static inline void __iomem *combiner_base(struct irq_data *data)
{
	struct combiner_chip_data *combiner_data =
		irq_data_get_irq_chip_data(data);

	return combiner_data->base;
}

static void combiner_mask_irq(struct irq_data *data)
{
	u32 mask = 1 << (data->irq % 32);

	__raw_writel(mask, combiner_base(data) + COMBINER_ENABLE_CLEAR);
}

static void combiner_unmask_irq(struct irq_data *data)
{
	u32 mask = 1 << (data->irq % 32);

	__raw_writel(mask, combiner_base(data) + COMBINER_ENABLE_SET);
}

static void combiner_handle_cascade_irq(unsigned int irq, struct irq_desc *desc)
{
	struct combiner_chip_data *chip_data = irq_get_handler_data(irq);
	struct irq_chip *chip = irq_get_chip(irq);
	unsigned int cascade_irq, combiner_irq;
	unsigned long status;

	chained_irq_enter(chip, desc);

	spin_lock(&irq_controller_lock);
	status = __raw_readl(chip_data->base + COMBINER_INT_STATUS);
	spin_unlock(&irq_controller_lock);
	status &= chip_data->irq_mask;

	if (status == 0)
		goto out;

	combiner_irq = __ffs(status);

	cascade_irq = combiner_irq + (chip_data->irq_offset & ~31);
	if (unlikely(cascade_irq >= NR_IRQS))
		do_bad_IRQ(cascade_irq, desc);
	else
		generic_handle_irq(cascade_irq);

 out:
	chained_irq_exit(chip, desc);
}

static struct irq_chip combiner_chip = {
	.name		= "COMBINER",
	.irq_mask	= combiner_mask_irq,
	.irq_unmask	= combiner_unmask_irq,
};

void __init combiner_cascade_irq(unsigned int combiner_nr, unsigned int irq)
{
	if (combiner_nr >= MAX_COMBINER_NR)
		BUG();
	if (irq_set_handler_data(irq, &combiner_data[combiner_nr]) != 0)
		BUG();
	irq_set_chained_handler(irq, combiner_handle_cascade_irq);
}

void __init combiner_init(unsigned int combiner_nr, void __iomem *base,
			  unsigned int irq_start)
{
	unsigned int i;

	if (combiner_nr >= MAX_COMBINER_NR)
		BUG();

	combiner_data[combiner_nr].base = base;
	combiner_data[combiner_nr].irq_offset = irq_start;
	combiner_data[combiner_nr].irq_mask = 0xff << ((combiner_nr % 4) << 3);

	/* Disable all interrupts */

	__raw_writel(combiner_data[combiner_nr].irq_mask,
		     base + COMBINER_ENABLE_CLEAR);

	/* Setup the Linux IRQ subsystem */

	for (i = irq_start; i < combiner_data[combiner_nr].irq_offset
				+ MAX_IRQ_IN_COMBINER; i++) {
		irq_set_chip_and_handler(i, &combiner_chip, handle_level_irq);
		irq_set_chip_data(i, &combiner_data[combiner_nr]);
		set_irq_flags(i, IRQF_VALID | IRQF_PROBE);
	}
}
