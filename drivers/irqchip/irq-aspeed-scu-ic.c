// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Aspeed AST24XX, AST25XX, and AST26XX SCU Interrupt Controller
 * Copyright 2019 IBM Corporation
 *
 * Eddie James <eajames@linux.ibm.com>
 */

#include <linux/bitops.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/mfd/syscon.h>
#include <linux/of_irq.h>
#include <linux/regmap.h>

#define ASPEED_SCU_IC_REG		0x018
#define ASPEED_SCU_IC_SHIFT		0
#define ASPEED_SCU_IC_ENABLE		GENMASK(15, ASPEED_SCU_IC_SHIFT)
#define ASPEED_SCU_IC_NUM_IRQS		7
#define ASPEED_SCU_IC_STATUS		GENMASK(28, 16)
#define ASPEED_SCU_IC_STATUS_SHIFT	16

#define ASPEED_AST2600_SCU_IC0_REG	0x560
#define ASPEED_AST2600_SCU_IC0_SHIFT	0
#define ASPEED_AST2600_SCU_IC0_ENABLE	\
	GENMASK(5, ASPEED_AST2600_SCU_IC0_SHIFT)
#define ASPEED_AST2600_SCU_IC0_NUM_IRQS	6

#define ASPEED_AST2600_SCU_IC1_REG	0x570
#define ASPEED_AST2600_SCU_IC1_SHIFT	4
#define ASPEED_AST2600_SCU_IC1_ENABLE	\
	GENMASK(5, ASPEED_AST2600_SCU_IC1_SHIFT)
#define ASPEED_AST2600_SCU_IC1_NUM_IRQS	2

#define ASPEED_AST2700_SCU_IC0_EN_REG	0x1d0
#define ASPEED_AST2700_SCU_IC0_STS_REG	0x1d4
#define ASPEED_AST2700_SCU_IC0_SHIFT	0
#define ASPEED_AST2700_SCU_IC0_ENABLE	\
	GENMASK(5, ASPEED_AST2700_SCU_IC0_SHIFT)
#define ASPEED_AST2700_SCU_IC0_NUM_IRQS	4

#define ASPEED_AST2700_SCU_IC1_EN_REG	0x1e0
#define ASPEED_AST2700_SCU_IC1_STS_REG	0x1e4
#define ASPEED_AST2700_SCU_IC1_SHIFT	0
#define ASPEED_AST2700_SCU_IC1_ENABLE	\
	GENMASK(5, ASPEED_AST2700_SCU_IC1_SHIFT)
#define ASPEED_AST2700_SCU_IC1_NUM_IRQS	4

struct aspeed_scu_ic {
	unsigned long irq_enable;
	unsigned long irq_shift;
	unsigned int num_irqs;
	bool en_sts_split;
	unsigned int reg;
	unsigned int en_reg;
	unsigned int sts_reg;
	struct regmap *scu;
	struct irq_domain *irq_domain;
};

static void aspeed_scu_ic_irq_handler(struct irq_desc *desc)
{
	unsigned int val;
	unsigned long bit;
	unsigned long enabled;
	unsigned long max;
	unsigned long status;
	struct aspeed_scu_ic *scu_ic = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned int mask = scu_ic->irq_enable << ASPEED_SCU_IC_STATUS_SHIFT;

	chained_irq_enter(chip, desc);

	if (!scu_ic->en_sts_split) {
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
		regmap_read(scu_ic->scu, scu_ic->reg, &val);
		enabled = val & scu_ic->irq_enable;
		status = (val >> ASPEED_SCU_IC_STATUS_SHIFT) & enabled;

		bit = scu_ic->irq_shift;
		max = scu_ic->num_irqs + bit;

		for_each_set_bit_from(bit, &status, max) {
			generic_handle_domain_irq(scu_ic->irq_domain,
						  bit - scu_ic->irq_shift);

			regmap_write_bits(scu_ic->scu, scu_ic->reg, mask,
					  BIT(bit + ASPEED_SCU_IC_STATUS_SHIFT));
		}
	} else {
		regmap_read(scu_ic->scu, scu_ic->en_reg, &val);
		enabled = val & scu_ic->irq_enable;
		regmap_read(scu_ic->scu, scu_ic->sts_reg, &val);
		status = val & enabled;

		bit = scu_ic->irq_shift;
		max = scu_ic->num_irqs + bit;

		for_each_set_bit_from(bit, &status, max) {
			generic_handle_domain_irq(scu_ic->irq_domain, bit - scu_ic->irq_shift);

			regmap_write_bits(scu_ic->scu, scu_ic->sts_reg, mask, BIT(bit));
		}
	}

	chained_irq_exit(chip, desc);
}

static void aspeed_scu_ic_irq_mask(struct irq_data *data)
{
	struct aspeed_scu_ic *scu_ic = irq_data_get_irq_chip_data(data);
	unsigned int mask = BIT(data->hwirq + scu_ic->irq_shift) |
		(scu_ic->irq_enable << ASPEED_SCU_IC_STATUS_SHIFT);

	if (!scu_ic->en_sts_split) {
		mask = BIT(data->hwirq + scu_ic->irq_shift) |
		       (scu_ic->irq_enable << ASPEED_SCU_IC_STATUS_SHIFT);
		/*
		 * Status bits are cleared by writing 1. In order to prevent the mask
		 * operation from clearing the status bits, they should be under the
		 * mask and written with 0.
		 */
		regmap_update_bits(scu_ic->scu, scu_ic->reg, mask, 0);
	} else {
		mask = BIT(data->hwirq + scu_ic->irq_shift);
		regmap_update_bits(scu_ic->scu, scu_ic->en_reg, mask, 0);
	}
}

static void aspeed_scu_ic_irq_unmask(struct irq_data *data)
{
	struct aspeed_scu_ic *scu_ic = irq_data_get_irq_chip_data(data);
	unsigned int bit = BIT(data->hwirq + scu_ic->irq_shift);
	unsigned int mask;

	if (!scu_ic->en_sts_split) {
		mask = bit | (scu_ic->irq_enable << ASPEED_SCU_IC_STATUS_SHIFT);
		/*
		 * Status bits are cleared by writing 1. In order to prevent the unmask
		 * operation from clearing the status bits, they should be under the
		 * mask and written with 0.
		 */
		regmap_update_bits(scu_ic->scu, scu_ic->reg, mask, bit);
	} else {
		mask = bit;
		regmap_update_bits(scu_ic->scu, scu_ic->en_reg, mask, bit);
	}
}

static int aspeed_scu_ic_irq_set_affinity(struct irq_data *data,
					  const struct cpumask *dest,
					  bool force)
{
	return -EINVAL;
}

static struct irq_chip aspeed_scu_ic_chip = {
	.name			= "aspeed-scu-ic",
	.irq_mask		= aspeed_scu_ic_irq_mask,
	.irq_unmask		= aspeed_scu_ic_irq_unmask,
	.irq_set_affinity	= aspeed_scu_ic_irq_set_affinity,
};

static int aspeed_scu_ic_map(struct irq_domain *domain, unsigned int irq,
			     irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &aspeed_scu_ic_chip, handle_level_irq);
	irq_set_chip_data(irq, domain->host_data);

	return 0;
}

static const struct irq_domain_ops aspeed_scu_ic_domain_ops = {
	.map = aspeed_scu_ic_map,
};

static int aspeed_scu_ic_of_init_common(struct aspeed_scu_ic *scu_ic,
					struct device_node *node)
{
	int irq;
	int rc = 0;

	if (!node->parent) {
		rc = -ENODEV;
		goto err;
	}

	scu_ic->scu = syscon_node_to_regmap(node->parent);
	if (IS_ERR(scu_ic->scu)) {
		rc = PTR_ERR(scu_ic->scu);
		goto err;
	}
	regmap_write_bits(scu_ic->scu, scu_ic->reg, ASPEED_SCU_IC_STATUS, ASPEED_SCU_IC_STATUS);
	regmap_write_bits(scu_ic->scu, scu_ic->reg, ASPEED_SCU_IC_ENABLE, 0);

	irq = irq_of_parse_and_map(node, 0);
	if (!irq) {
		rc = -EINVAL;
		goto err;
	}

	scu_ic->irq_domain = irq_domain_add_linear(node, scu_ic->num_irqs,
						   &aspeed_scu_ic_domain_ops,
						   scu_ic);
	if (!scu_ic->irq_domain) {
		rc = -ENOMEM;
		goto err;
	}

	irq_set_chained_handler_and_data(irq, aspeed_scu_ic_irq_handler,
					 scu_ic);

	return 0;

err:
	kfree(scu_ic);

	return rc;
}

static int __init aspeed_scu_ic_of_init(struct device_node *node,
					struct device_node *parent)
{
	struct aspeed_scu_ic *scu_ic = kzalloc(sizeof(*scu_ic), GFP_KERNEL);

	if (!scu_ic)
		return -ENOMEM;

	scu_ic->irq_enable = ASPEED_SCU_IC_ENABLE;
	scu_ic->irq_shift = ASPEED_SCU_IC_SHIFT;
	scu_ic->num_irqs = ASPEED_SCU_IC_NUM_IRQS;
	scu_ic->reg = ASPEED_SCU_IC_REG;

	return aspeed_scu_ic_of_init_common(scu_ic, node);
}

static int __init aspeed_ast2600_scu_ic0_of_init(struct device_node *node,
						 struct device_node *parent)
{
	struct aspeed_scu_ic *scu_ic = kzalloc(sizeof(*scu_ic), GFP_KERNEL);

	if (!scu_ic)
		return -ENOMEM;

	scu_ic->irq_enable = ASPEED_AST2600_SCU_IC0_ENABLE;
	scu_ic->irq_shift = ASPEED_AST2600_SCU_IC0_SHIFT;
	scu_ic->num_irqs = ASPEED_AST2600_SCU_IC0_NUM_IRQS;
	scu_ic->reg = ASPEED_AST2600_SCU_IC0_REG;

	return aspeed_scu_ic_of_init_common(scu_ic, node);
}

static int __init aspeed_ast2600_scu_ic1_of_init(struct device_node *node,
						 struct device_node *parent)
{
	struct aspeed_scu_ic *scu_ic = kzalloc(sizeof(*scu_ic), GFP_KERNEL);

	if (!scu_ic)
		return -ENOMEM;

	scu_ic->irq_enable = ASPEED_AST2600_SCU_IC1_ENABLE;
	scu_ic->irq_shift = ASPEED_AST2600_SCU_IC1_SHIFT;
	scu_ic->num_irqs = ASPEED_AST2600_SCU_IC1_NUM_IRQS;
	scu_ic->reg = ASPEED_AST2600_SCU_IC1_REG;

	return aspeed_scu_ic_of_init_common(scu_ic, node);
}

static int __init aspeed_ast2700_scu_ic0_of_init(struct device_node *node,
						 struct device_node *parent)
{
	struct aspeed_scu_ic *scu_ic = kzalloc(sizeof(*scu_ic), GFP_KERNEL);

	if (!scu_ic)
		return -ENOMEM;

	scu_ic->irq_enable = ASPEED_AST2700_SCU_IC0_ENABLE;
	scu_ic->irq_shift = ASPEED_AST2700_SCU_IC0_SHIFT;
	scu_ic->num_irqs = ASPEED_AST2700_SCU_IC0_NUM_IRQS;
	scu_ic->en_sts_split = true;
	scu_ic->en_reg = ASPEED_AST2700_SCU_IC0_EN_REG;
	scu_ic->sts_reg = ASPEED_AST2700_SCU_IC0_STS_REG;

	return aspeed_scu_ic_of_init_common(scu_ic, node);
}

static int __init aspeed_ast2700_scu_ic1_of_init(struct device_node *node,
						 struct device_node *parent)
{
	struct aspeed_scu_ic *scu_ic = kzalloc(sizeof(*scu_ic), GFP_KERNEL);

	if (!scu_ic)
		return -ENOMEM;

	scu_ic->irq_enable = ASPEED_AST2700_SCU_IC1_ENABLE;
	scu_ic->irq_shift = ASPEED_AST2700_SCU_IC1_SHIFT;
	scu_ic->num_irqs = ASPEED_AST2700_SCU_IC1_NUM_IRQS;
	scu_ic->en_sts_split = true;
	scu_ic->en_reg = ASPEED_AST2700_SCU_IC1_EN_REG;
	scu_ic->sts_reg = ASPEED_AST2700_SCU_IC1_STS_REG;

	return aspeed_scu_ic_of_init_common(scu_ic, node);
}

IRQCHIP_DECLARE(ast2400_scu_ic, "aspeed,ast2400-scu-ic", aspeed_scu_ic_of_init);
IRQCHIP_DECLARE(ast2500_scu_ic, "aspeed,ast2500-scu-ic", aspeed_scu_ic_of_init);
IRQCHIP_DECLARE(ast2600_scu_ic0, "aspeed,ast2600-scu-ic0",
		aspeed_ast2600_scu_ic0_of_init);
IRQCHIP_DECLARE(ast2600_scu_ic1, "aspeed,ast2600-scu-ic1",
		aspeed_ast2600_scu_ic1_of_init);
IRQCHIP_DECLARE(ast2700_scu_ic0, "aspeed,ast2700-scu-ic0",
		aspeed_ast2700_scu_ic0_of_init);
IRQCHIP_DECLARE(ast2700_scu_ic1, "aspeed,ast2700-scu-ic1",
		aspeed_ast2700_scu_ic1_of_init);
