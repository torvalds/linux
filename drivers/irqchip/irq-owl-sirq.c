// SPDX-License-Identifier: GPL-2.0+
/*
 * Actions Semi Owl SoCs SIRQ interrupt controller driver
 *
 * Copyright (C) 2014 Actions Semi Inc.
 * David Liu <liuwei@actions-semi.com>
 *
 * Author: Parthiban Nallathambi <pn@denx.de>
 * Author: Saravanan Sekar <sravanhome@gmail.com>
 * Author: Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#include <linux/bitfield.h>
#include <linux/interrupt.h>
#include <linux/irqchip.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include <dt-bindings/interrupt-controller/arm-gic.h>

#define NUM_SIRQ			3

#define INTC_EXTCTL_PENDING		BIT(0)
#define INTC_EXTCTL_CLK_SEL		BIT(4)
#define INTC_EXTCTL_EN			BIT(5)
#define INTC_EXTCTL_TYPE_MASK		GENMASK(7, 6)
#define INTC_EXTCTL_TYPE_HIGH		0
#define INTC_EXTCTL_TYPE_LOW		BIT(6)
#define INTC_EXTCTL_TYPE_RISING		BIT(7)
#define INTC_EXTCTL_TYPE_FALLING	(BIT(6) | BIT(7))

/* S500 & S700 SIRQ control register masks */
#define INTC_EXTCTL_SIRQ0_MASK		GENMASK(23, 16)
#define INTC_EXTCTL_SIRQ1_MASK		GENMASK(15, 8)
#define INTC_EXTCTL_SIRQ2_MASK		GENMASK(7, 0)

/* S900 SIRQ control register offsets, relative to controller base address */
#define INTC_EXTCTL0			0x0000
#define INTC_EXTCTL1			0x0328
#define INTC_EXTCTL2			0x032c

struct owl_sirq_params {
	/* INTC_EXTCTL reg shared for all three SIRQ lines */
	bool reg_shared;
	/* INTC_EXTCTL reg offsets relative to controller base address */
	u16 reg_offset[NUM_SIRQ];
};

struct owl_sirq_chip_data {
	const struct owl_sirq_params	*params;
	void __iomem			*base;
	raw_spinlock_t			lock;
	u32				ext_irqs[NUM_SIRQ];
};

/* S500 & S700 SoCs */
static const struct owl_sirq_params owl_sirq_s500_params = {
	.reg_shared = true,
	.reg_offset = { 0, 0, 0 },
};

/* S900 SoC */
static const struct owl_sirq_params owl_sirq_s900_params = {
	.reg_shared = false,
	.reg_offset = { INTC_EXTCTL0, INTC_EXTCTL1, INTC_EXTCTL2 },
};

static u32 owl_field_get(u32 val, u32 index)
{
	switch (index) {
	case 0:
		return FIELD_GET(INTC_EXTCTL_SIRQ0_MASK, val);
	case 1:
		return FIELD_GET(INTC_EXTCTL_SIRQ1_MASK, val);
	case 2:
	default:
		return FIELD_GET(INTC_EXTCTL_SIRQ2_MASK, val);
	}
}

static u32 owl_field_prep(u32 val, u32 index)
{
	switch (index) {
	case 0:
		return FIELD_PREP(INTC_EXTCTL_SIRQ0_MASK, val);
	case 1:
		return FIELD_PREP(INTC_EXTCTL_SIRQ1_MASK, val);
	case 2:
	default:
		return FIELD_PREP(INTC_EXTCTL_SIRQ2_MASK, val);
	}
}

static u32 owl_sirq_read_extctl(struct owl_sirq_chip_data *data, u32 index)
{
	u32 val;

	val = readl_relaxed(data->base + data->params->reg_offset[index]);
	if (data->params->reg_shared)
		val = owl_field_get(val, index);

	return val;
}

static void owl_sirq_write_extctl(struct owl_sirq_chip_data *data,
				  u32 extctl, u32 index)
{
	u32 val;

	if (data->params->reg_shared) {
		val = readl_relaxed(data->base + data->params->reg_offset[index]);
		val &= ~owl_field_prep(0xff, index);
		extctl = owl_field_prep(extctl, index) | val;
	}

	writel_relaxed(extctl, data->base + data->params->reg_offset[index]);
}

static void owl_sirq_clear_set_extctl(struct owl_sirq_chip_data *d,
				      u32 clear, u32 set, u32 index)
{
	unsigned long flags;
	u32 val;

	raw_spin_lock_irqsave(&d->lock, flags);
	val = owl_sirq_read_extctl(d, index);
	val &= ~clear;
	val |= set;
	owl_sirq_write_extctl(d, val, index);
	raw_spin_unlock_irqrestore(&d->lock, flags);
}

static void owl_sirq_eoi(struct irq_data *data)
{
	struct owl_sirq_chip_data *chip_data = irq_data_get_irq_chip_data(data);

	/*
	 * Software must clear external interrupt pending, when interrupt type
	 * is edge triggered, so we need per SIRQ based clearing.
	 */
	if (!irqd_is_level_type(data))
		owl_sirq_clear_set_extctl(chip_data, 0, INTC_EXTCTL_PENDING,
					  data->hwirq);

	irq_chip_eoi_parent(data);
}

static void owl_sirq_mask(struct irq_data *data)
{
	struct owl_sirq_chip_data *chip_data = irq_data_get_irq_chip_data(data);

	owl_sirq_clear_set_extctl(chip_data, INTC_EXTCTL_EN, 0, data->hwirq);
	irq_chip_mask_parent(data);
}

static void owl_sirq_unmask(struct irq_data *data)
{
	struct owl_sirq_chip_data *chip_data = irq_data_get_irq_chip_data(data);

	owl_sirq_clear_set_extctl(chip_data, 0, INTC_EXTCTL_EN, data->hwirq);
	irq_chip_unmask_parent(data);
}

/*
 * GIC does not handle falling edge or active low, hence SIRQ shall be
 * programmed to convert falling edge to rising edge signal and active
 * low to active high signal.
 */
static int owl_sirq_set_type(struct irq_data *data, unsigned int type)
{
	struct owl_sirq_chip_data *chip_data = irq_data_get_irq_chip_data(data);
	u32 sirq_type;

	switch (type) {
	case IRQ_TYPE_LEVEL_LOW:
		sirq_type = INTC_EXTCTL_TYPE_LOW;
		type = IRQ_TYPE_LEVEL_HIGH;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		sirq_type = INTC_EXTCTL_TYPE_HIGH;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		sirq_type = INTC_EXTCTL_TYPE_FALLING;
		type = IRQ_TYPE_EDGE_RISING;
		break;
	case IRQ_TYPE_EDGE_RISING:
		sirq_type = INTC_EXTCTL_TYPE_RISING;
		break;
	default:
		return -EINVAL;
	}

	owl_sirq_clear_set_extctl(chip_data, INTC_EXTCTL_TYPE_MASK, sirq_type,
				  data->hwirq);

	return irq_chip_set_type_parent(data, type);
}

static struct irq_chip owl_sirq_chip = {
	.name		= "owl-sirq",
	.irq_mask	= owl_sirq_mask,
	.irq_unmask	= owl_sirq_unmask,
	.irq_eoi	= owl_sirq_eoi,
	.irq_set_type	= owl_sirq_set_type,
	.irq_retrigger	= irq_chip_retrigger_hierarchy,
#ifdef CONFIG_SMP
	.irq_set_affinity = irq_chip_set_affinity_parent,
#endif
};

static int owl_sirq_domain_translate(struct irq_domain *d,
				     struct irq_fwspec *fwspec,
				     unsigned long *hwirq,
				     unsigned int *type)
{
	if (!is_of_node(fwspec->fwnode))
		return -EINVAL;

	if (fwspec->param_count != 2 || fwspec->param[0] >= NUM_SIRQ)
		return -EINVAL;

	*hwirq = fwspec->param[0];
	*type = fwspec->param[1];

	return 0;
}

static int owl_sirq_domain_alloc(struct irq_domain *domain, unsigned int virq,
				 unsigned int nr_irqs, void *data)
{
	struct owl_sirq_chip_data *chip_data = domain->host_data;
	struct irq_fwspec *fwspec = data;
	struct irq_fwspec parent_fwspec;
	irq_hw_number_t hwirq;
	unsigned int type;
	int ret;

	if (WARN_ON(nr_irqs != 1))
		return -EINVAL;

	ret = owl_sirq_domain_translate(domain, fwspec, &hwirq, &type);
	if (ret)
		return ret;

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
	case IRQ_TYPE_LEVEL_HIGH:
		break;
	case IRQ_TYPE_EDGE_FALLING:
		type = IRQ_TYPE_EDGE_RISING;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		type = IRQ_TYPE_LEVEL_HIGH;
		break;
	default:
		return -EINVAL;
	}

	irq_domain_set_hwirq_and_chip(domain, virq, hwirq, &owl_sirq_chip,
				      chip_data);

	parent_fwspec.fwnode = domain->parent->fwnode;
	parent_fwspec.param_count = 3;
	parent_fwspec.param[0] = GIC_SPI;
	parent_fwspec.param[1] = chip_data->ext_irqs[hwirq];
	parent_fwspec.param[2] = type;

	return irq_domain_alloc_irqs_parent(domain, virq, 1, &parent_fwspec);
}

static const struct irq_domain_ops owl_sirq_domain_ops = {
	.translate	= owl_sirq_domain_translate,
	.alloc		= owl_sirq_domain_alloc,
	.free		= irq_domain_free_irqs_common,
};

static int __init owl_sirq_init(const struct owl_sirq_params *params,
				struct device_node *node,
				struct device_node *parent)
{
	struct irq_domain *domain, *parent_domain;
	struct owl_sirq_chip_data *chip_data;
	int ret, i;

	parent_domain = irq_find_host(parent);
	if (!parent_domain) {
		pr_err("%pOF: failed to find sirq parent domain\n", node);
		return -ENXIO;
	}

	chip_data = kzalloc(sizeof(*chip_data), GFP_KERNEL);
	if (!chip_data)
		return -ENOMEM;

	raw_spin_lock_init(&chip_data->lock);

	chip_data->params = params;

	chip_data->base = of_iomap(node, 0);
	if (!chip_data->base) {
		pr_err("%pOF: failed to map sirq registers\n", node);
		ret = -ENXIO;
		goto out_free;
	}

	for (i = 0; i < NUM_SIRQ; i++) {
		struct of_phandle_args irq;

		ret = of_irq_parse_one(node, i, &irq);
		if (ret) {
			pr_err("%pOF: failed to parse interrupt %d\n", node, i);
			goto out_unmap;
		}

		if (WARN_ON(irq.args_count != 3)) {
			ret = -EINVAL;
			goto out_unmap;
		}

		chip_data->ext_irqs[i] = irq.args[1];

		/* Set 24MHz external interrupt clock freq */
		owl_sirq_clear_set_extctl(chip_data, 0, INTC_EXTCTL_CLK_SEL, i);
	}

	domain = irq_domain_create_hierarchy(parent_domain, 0, NUM_SIRQ, of_fwnode_handle(node),
					     &owl_sirq_domain_ops, chip_data);
	if (!domain) {
		pr_err("%pOF: failed to add domain\n", node);
		ret = -ENOMEM;
		goto out_unmap;
	}

	return 0;

out_unmap:
	iounmap(chip_data->base);
out_free:
	kfree(chip_data);

	return ret;
}

static int __init owl_sirq_s500_of_init(struct device_node *node,
					struct device_node *parent)
{
	return owl_sirq_init(&owl_sirq_s500_params, node, parent);
}

IRQCHIP_DECLARE(owl_sirq_s500, "actions,s500-sirq", owl_sirq_s500_of_init);
IRQCHIP_DECLARE(owl_sirq_s700, "actions,s700-sirq", owl_sirq_s500_of_init);

static int __init owl_sirq_s900_of_init(struct device_node *node,
					struct device_node *parent)
{
	return owl_sirq_init(&owl_sirq_s900_params, node, parent);
}

IRQCHIP_DECLARE(owl_sirq_s900, "actions,s900-sirq", owl_sirq_s900_of_init);
