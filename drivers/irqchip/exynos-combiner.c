/*
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Combiner irqchip for EXYNOS
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/err.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irqdomain.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <asm/mach/irq.h>

#include <plat/cpu.h>

#include "irqchip.h"

#define COMBINER_ENABLE_SET	0x0
#define COMBINER_ENABLE_CLEAR	0x4
#define COMBINER_INT_STATUS	0xC

static DEFINE_SPINLOCK(irq_controller_lock);

struct combiner_chip_data {
	unsigned int irq_offset;
	unsigned int irq_mask;
	void __iomem *base;
};

static struct irq_domain *combiner_irq_domain;
static struct combiner_chip_data combiner_data[MAX_COMBINER_NR];

static inline void __iomem *combiner_base(struct irq_data *data)
{
	struct combiner_chip_data *combiner_data =
		irq_data_get_irq_chip_data(data);

	return combiner_data->base;
}

static void combiner_mask_irq(struct irq_data *data)
{
	u32 mask = 1 << (data->hwirq % 32);

	__raw_writel(mask, combiner_base(data) + COMBINER_ENABLE_CLEAR);
}

static void combiner_unmask_irq(struct irq_data *data)
{
	u32 mask = 1 << (data->hwirq % 32);

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

static void __init combiner_cascade_irq(unsigned int combiner_nr, unsigned int irq)
{
	unsigned int max_nr;

	if (soc_is_exynos5250())
		max_nr = EXYNOS5_MAX_COMBINER_NR;
	else
		max_nr = EXYNOS4_MAX_COMBINER_NR;

	if (combiner_nr >= max_nr)
		BUG();
	if (irq_set_handler_data(irq, &combiner_data[combiner_nr]) != 0)
		BUG();
	irq_set_chained_handler(irq, combiner_handle_cascade_irq);
}

static void __init combiner_init_one(unsigned int combiner_nr,
				     void __iomem *base)
{
	combiner_data[combiner_nr].base = base;
	combiner_data[combiner_nr].irq_offset = irq_find_mapping(
		combiner_irq_domain, combiner_nr * MAX_IRQ_IN_COMBINER);
	combiner_data[combiner_nr].irq_mask = 0xff << ((combiner_nr % 4) << 3);

	/* Disable all interrupts */
	__raw_writel(combiner_data[combiner_nr].irq_mask,
		     base + COMBINER_ENABLE_CLEAR);
}

#ifdef CONFIG_OF
static int combiner_irq_domain_xlate(struct irq_domain *d,
				     struct device_node *controller,
				     const u32 *intspec, unsigned int intsize,
				     unsigned long *out_hwirq,
				     unsigned int *out_type)
{
	if (d->of_node != controller)
		return -EINVAL;

	if (intsize < 2)
		return -EINVAL;

	*out_hwirq = intspec[0] * MAX_IRQ_IN_COMBINER + intspec[1];
	*out_type = 0;

	return 0;
}
#else
static int combiner_irq_domain_xlate(struct irq_domain *d,
				     struct device_node *controller,
				     const u32 *intspec, unsigned int intsize,
				     unsigned long *out_hwirq,
				     unsigned int *out_type)
{
	return -EINVAL;
}
#endif

static int combiner_irq_domain_map(struct irq_domain *d, unsigned int irq,
				   irq_hw_number_t hw)
{
	irq_set_chip_and_handler(irq, &combiner_chip, handle_level_irq);
	irq_set_chip_data(irq, &combiner_data[hw >> 3]);
	set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);

	return 0;
}

static struct irq_domain_ops combiner_irq_domain_ops = {
	.xlate	= combiner_irq_domain_xlate,
	.map	= combiner_irq_domain_map,
};

void __init combiner_init(void __iomem *combiner_base,
			  struct device_node *np)
{
	int i, irq, irq_base;
	unsigned int max_nr, nr_irq;

	if (np) {
		if (of_property_read_u32(np, "samsung,combiner-nr", &max_nr)) {
			pr_warning("%s: number of combiners not specified, "
				"setting default as %d.\n",
				__func__, EXYNOS4_MAX_COMBINER_NR);
			max_nr = EXYNOS4_MAX_COMBINER_NR;
		}
	} else {
		max_nr = soc_is_exynos5250() ? EXYNOS5_MAX_COMBINER_NR :
						EXYNOS4_MAX_COMBINER_NR;
	}
	nr_irq = max_nr * MAX_IRQ_IN_COMBINER;

	irq_base = irq_alloc_descs(COMBINER_IRQ(0, 0), 1, nr_irq, 0);
	if (IS_ERR_VALUE(irq_base)) {
		irq_base = COMBINER_IRQ(0, 0);
		pr_warning("%s: irq desc alloc failed. Continuing with %d as linux irq base\n", __func__, irq_base);
	}

	combiner_irq_domain = irq_domain_add_legacy(np, nr_irq, irq_base, 0,
				&combiner_irq_domain_ops, &combiner_data);
	if (WARN_ON(!combiner_irq_domain)) {
		pr_warning("%s: irq domain init failed\n", __func__);
		return;
	}

	for (i = 0; i < max_nr; i++) {
		combiner_init_one(i, combiner_base + (i >> 2) * 0x10);
		irq = IRQ_SPI(i);
#ifdef CONFIG_OF
		if (np)
			irq = irq_of_parse_and_map(np, i);
#endif
		combiner_cascade_irq(i, irq);
	}
}

#ifdef CONFIG_OF
static int __init combiner_of_init(struct device_node *np,
				   struct device_node *parent)
{
	void __iomem *combiner_base;

	combiner_base = of_iomap(np, 0);
	if (!combiner_base) {
		pr_err("%s: failed to map combiner registers\n", __func__);
		return -ENXIO;
	}

	combiner_init(combiner_base, np);

	return 0;
}
IRQCHIP_DECLARE(exynos4210_combiner, "samsung,exynos4210-combiner",
		combiner_of_init);
#endif
