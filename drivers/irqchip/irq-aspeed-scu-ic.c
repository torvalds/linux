// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Aspeed AST24XX, AST25XX, AST26XX, and AST27XX SCU Interrupt Controller
 * Copyright 2019 IBM Corporation
 *
 * Eddie James <eajames@linux.ibm.com>
 */

#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#define ASPEED_SCU_IC_STATUS		GENMASK(28, 16)
#define ASPEED_SCU_IC_STATUS_SHIFT	16
#define AST2700_SCU_IC_STATUS		GENMASK(15, 0)

struct aspeed_scu_ic_variant {
	const char	*compatible;
	unsigned long	irq_enable;
	unsigned long	irq_shift;
	unsigned int	num_irqs;
	unsigned long	ier;
	unsigned long	isr;
};

#define SCU_VARIANT(_compat, _shift, _enable, _num, _ier, _isr) {	\
	.compatible		=	_compat,	\
	.irq_shift		=	_shift,		\
	.irq_enable		=	_enable,	\
	.num_irqs		=	_num,		\
	.ier			=	_ier,		\
	.isr			=	_isr,		\
}

static const struct aspeed_scu_ic_variant scu_ic_variants[]	__initconst = {
	SCU_VARIANT("aspeed,ast2400-scu-ic",	0, GENMASK(15, 0),	7, 0x00, 0x00),
	SCU_VARIANT("aspeed,ast2500-scu-ic",	0, GENMASK(15, 0),	7, 0x00, 0x00),
	SCU_VARIANT("aspeed,ast2600-scu-ic0",	0, GENMASK(5, 0),	6, 0x00, 0x00),
	SCU_VARIANT("aspeed,ast2600-scu-ic1",	4, GENMASK(5, 4),	2, 0x00, 0x00),
	SCU_VARIANT("aspeed,ast2700-scu-ic0",	0, GENMASK(3, 0),	4, 0x00, 0x04),
	SCU_VARIANT("aspeed,ast2700-scu-ic1",	0, GENMASK(3, 0),	4, 0x00, 0x04),
	SCU_VARIANT("aspeed,ast2700-scu-ic2",	0, GENMASK(3, 0),	4, 0x04, 0x00),
	SCU_VARIANT("aspeed,ast2700-scu-ic3",	0, GENMASK(1, 0),	2, 0x04, 0x00),
};

struct aspeed_scu_ic {
	unsigned long		irq_enable;
	unsigned long		irq_shift;
	unsigned int		num_irqs;
	void __iomem		*base;
	struct irq_domain	*irq_domain;
	unsigned long		ier;
	unsigned long		isr;
};

static inline bool scu_has_split_isr(struct aspeed_scu_ic *scu)
{
	return scu->ier != scu->isr;
}

static void aspeed_scu_ic_irq_handler_combined(struct irq_desc *desc)
{
	struct aspeed_scu_ic *scu_ic = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned long bit, enabled, max, status;
	unsigned int sts, mask;

	chained_irq_enter(chip, desc);

	mask = scu_ic->irq_enable << ASPEED_SCU_IC_STATUS_SHIFT;
	/*
	 * The SCU IC has just one register to control its operation and read
	 * status. The interrupt enable bits occupy the lower 16 bits of the
	 * register, while the interrupt status bits occupy the upper 16 bits.
	 * The status bit for a given interrupt is always 16 bits shifted from
	 * the enable bit for the same interrupt.
	 * Therefore, perform the IRQ operations in the enable bit space by
	 * shifting the status down to get the mapping and then back up to
	 * clear the bit.
	 */
	sts = readl(scu_ic->base);
	enabled = sts & scu_ic->irq_enable;
	status = (sts >> ASPEED_SCU_IC_STATUS_SHIFT) & enabled;

	bit = scu_ic->irq_shift;
	max = scu_ic->num_irqs + bit;

	for_each_set_bit_from(bit, &status, max) {
		generic_handle_domain_irq(scu_ic->irq_domain, bit - scu_ic->irq_shift);
		writel((readl(scu_ic->base) & ~mask) | BIT(bit + ASPEED_SCU_IC_STATUS_SHIFT),
		       scu_ic->base);
	}

	chained_irq_exit(chip, desc);
}

static void aspeed_scu_ic_irq_handler_split(struct irq_desc *desc)
{
	struct aspeed_scu_ic *scu_ic = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned long bit, enabled, max, status;
	unsigned int sts, mask;

	chained_irq_enter(chip, desc);

	mask = scu_ic->irq_enable;
	sts = readl(scu_ic->base + scu_ic->isr);
	enabled = sts & scu_ic->irq_enable;
	sts = readl(scu_ic->base + scu_ic->isr);
	status = sts & enabled;

	bit = scu_ic->irq_shift;
	max = scu_ic->num_irqs + bit;

	for_each_set_bit_from(bit, &status, max) {
		generic_handle_domain_irq(scu_ic->irq_domain, bit - scu_ic->irq_shift);
		/* Clear interrupt */
		writel(BIT(bit), scu_ic->base + scu_ic->isr);
	}

	chained_irq_exit(chip, desc);
}

static void aspeed_scu_ic_irq_mask_combined(struct irq_data *data)
{
	struct aspeed_scu_ic *scu_ic = irq_data_get_irq_chip_data(data);
	unsigned int bit = BIT(data->hwirq + scu_ic->irq_shift);
	unsigned int mask = bit | (scu_ic->irq_enable << ASPEED_SCU_IC_STATUS_SHIFT);

	/*
	 * Status bits are cleared by writing 1. In order to prevent the mask
	 * operation from clearing the status bits, they should be under the
	 * mask and written with 0.
	 */
	writel(readl(scu_ic->base) & ~mask, scu_ic->base);
}

static void aspeed_scu_ic_irq_unmask_combined(struct irq_data *data)
{
	struct aspeed_scu_ic *scu_ic = irq_data_get_irq_chip_data(data);
	unsigned int bit = BIT(data->hwirq + scu_ic->irq_shift);
	unsigned int mask = bit | (scu_ic->irq_enable << ASPEED_SCU_IC_STATUS_SHIFT);

	/*
	 * Status bits are cleared by writing 1. In order to prevent the unmask
	 * operation from clearing the status bits, they should be under the
	 * mask and written with 0.
	 */
	writel((readl(scu_ic->base) & ~mask) | bit, scu_ic->base);
}

static void aspeed_scu_ic_irq_mask_split(struct irq_data *data)
{
	struct aspeed_scu_ic *scu_ic = irq_data_get_irq_chip_data(data);
	unsigned int mask = BIT(data->hwirq + scu_ic->irq_shift);

	writel(readl(scu_ic->base) & ~mask, scu_ic->base + scu_ic->ier);
}

static void aspeed_scu_ic_irq_unmask_split(struct irq_data *data)
{
	struct aspeed_scu_ic *scu_ic = irq_data_get_irq_chip_data(data);
	unsigned int bit = BIT(data->hwirq + scu_ic->irq_shift);

	writel(readl(scu_ic->base) | bit, scu_ic->base + scu_ic->ier);
}

static int aspeed_scu_ic_irq_set_affinity(struct irq_data *data,
					  const struct cpumask *dest,
					  bool force)
{
	return -EINVAL;
}

static struct irq_chip aspeed_scu_ic_chip_combined = {
	.name			= "aspeed-scu-ic",
	.irq_mask		= aspeed_scu_ic_irq_mask_combined,
	.irq_unmask		= aspeed_scu_ic_irq_unmask_combined,
	.irq_set_affinity       = aspeed_scu_ic_irq_set_affinity,
};

static struct irq_chip aspeed_scu_ic_chip_split = {
	.name			= "ast2700-scu-ic",
	.irq_mask		= aspeed_scu_ic_irq_mask_split,
	.irq_unmask		= aspeed_scu_ic_irq_unmask_split,
	.irq_set_affinity       = aspeed_scu_ic_irq_set_affinity,
};

static int aspeed_scu_ic_map(struct irq_domain *domain, unsigned int irq,
			     irq_hw_number_t hwirq)
{
	struct aspeed_scu_ic *scu_ic = domain->host_data;

	if (scu_has_split_isr(scu_ic))
		irq_set_chip_and_handler(irq, &aspeed_scu_ic_chip_split, handle_level_irq);
	else
		irq_set_chip_and_handler(irq, &aspeed_scu_ic_chip_combined, handle_level_irq);
	irq_set_chip_data(irq, domain->host_data);

	return 0;
}

static const struct irq_domain_ops aspeed_scu_ic_domain_ops = {
	.map = aspeed_scu_ic_map,
};

static int aspeed_scu_ic_of_init_common(struct aspeed_scu_ic *scu_ic,
					struct device_node *node)
{
	int irq, rc = 0;

	scu_ic->base = of_iomap(node, 0);
	if (!scu_ic->base) {
		rc = -ENOMEM;
		goto err;
	}

	if (scu_has_split_isr(scu_ic)) {
		writel(AST2700_SCU_IC_STATUS, scu_ic->base + scu_ic->isr);
		writel(0, scu_ic->base + scu_ic->ier);
	} else {
		writel(ASPEED_SCU_IC_STATUS, scu_ic->base);
		writel(0, scu_ic->base);
	}

	irq = irq_of_parse_and_map(node, 0);
	if (!irq) {
		rc = -EINVAL;
		goto err;
	}

	scu_ic->irq_domain = irq_domain_create_linear(of_fwnode_handle(node), scu_ic->num_irqs,
						      &aspeed_scu_ic_domain_ops, scu_ic);
	if (!scu_ic->irq_domain) {
		rc = -ENOMEM;
		goto err;
	}

	irq_set_chained_handler_and_data(irq, scu_has_split_isr(scu_ic) ?
					 aspeed_scu_ic_irq_handler_split :
					 aspeed_scu_ic_irq_handler_combined,
					 scu_ic);

	return 0;

err:
	kfree(scu_ic);
	return rc;
}

static const struct aspeed_scu_ic_variant *aspeed_scu_ic_find_variant(struct device_node *np)
{
	for (int i = 0; i < ARRAY_SIZE(scu_ic_variants); i++) {
		if (of_device_is_compatible(np, scu_ic_variants[i].compatible))
			return &scu_ic_variants[i];
	}
	return NULL;
}

static int __init aspeed_scu_ic_of_init(struct device_node *node, struct device_node *parent)
{
	const struct aspeed_scu_ic_variant *variant;
	struct aspeed_scu_ic *scu_ic;

	variant = aspeed_scu_ic_find_variant(node);
	if (!variant)
		return -ENODEV;

	scu_ic = kzalloc(sizeof(*scu_ic), GFP_KERNEL);
	if (!scu_ic)
		return -ENOMEM;

	scu_ic->irq_enable	= variant->irq_enable;
	scu_ic->irq_shift	= variant->irq_shift;
	scu_ic->num_irqs	= variant->num_irqs;
	scu_ic->ier		= variant->ier;
	scu_ic->isr		= variant->isr;

	return aspeed_scu_ic_of_init_common(scu_ic, node);
}

IRQCHIP_DECLARE(ast2400_scu_ic, "aspeed,ast2400-scu-ic", aspeed_scu_ic_of_init);
IRQCHIP_DECLARE(ast2500_scu_ic, "aspeed,ast2500-scu-ic", aspeed_scu_ic_of_init);
IRQCHIP_DECLARE(ast2600_scu_ic0, "aspeed,ast2600-scu-ic0", aspeed_scu_ic_of_init);
IRQCHIP_DECLARE(ast2600_scu_ic1, "aspeed,ast2600-scu-ic1", aspeed_scu_ic_of_init);
IRQCHIP_DECLARE(ast2700_scu_ic0, "aspeed,ast2700-scu-ic0", aspeed_scu_ic_of_init);
IRQCHIP_DECLARE(ast2700_scu_ic1, "aspeed,ast2700-scu-ic1", aspeed_scu_ic_of_init);
IRQCHIP_DECLARE(ast2700_scu_ic2, "aspeed,ast2700-scu-ic2", aspeed_scu_ic_of_init);
IRQCHIP_DECLARE(ast2700_scu_ic3, "aspeed,ast2700-scu-ic3", aspeed_scu_ic_of_init);
