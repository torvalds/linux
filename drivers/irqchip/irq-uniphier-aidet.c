// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for UniPhier AIDET (ARM Interrupt Detector)
 *
 * Copyright (C) 2017 Socionext Inc.
 *   Author: Masahiro Yamada <yamada.masahiro@socionext.com>
 */

#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#define UNIPHIER_AIDET_NR_IRQS		256

#define UNIPHIER_AIDET_DETCONF		0x04	/* inverter register base */

struct uniphier_aidet_priv {
	struct irq_domain *domain;
	void __iomem *reg_base;
	spinlock_t lock;
	u32 saved_vals[UNIPHIER_AIDET_NR_IRQS / 32];
};

static void uniphier_aidet_reg_update(struct uniphier_aidet_priv *priv,
				      unsigned int reg, u32 mask, u32 val)
{
	unsigned long flags;
	u32 tmp;

	spin_lock_irqsave(&priv->lock, flags);
	tmp = readl_relaxed(priv->reg_base + reg);
	tmp &= ~mask;
	tmp |= mask & val;
	writel_relaxed(tmp, priv->reg_base + reg);
	spin_unlock_irqrestore(&priv->lock, flags);
}

static void uniphier_aidet_detconf_update(struct uniphier_aidet_priv *priv,
					  unsigned long index, unsigned int val)
{
	unsigned int reg;
	u32 mask;

	reg = UNIPHIER_AIDET_DETCONF + index / 32 * 4;
	mask = BIT(index % 32);

	uniphier_aidet_reg_update(priv, reg, mask, val ? mask : 0);
}

static int uniphier_aidet_irq_set_type(struct irq_data *data, unsigned int type)
{
	struct uniphier_aidet_priv *priv = data->chip_data;
	unsigned int val;

	/* enable inverter for active low triggers */
	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
	case IRQ_TYPE_LEVEL_HIGH:
		val = 0;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		val = 1;
		type = IRQ_TYPE_EDGE_RISING;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		val = 1;
		type = IRQ_TYPE_LEVEL_HIGH;
		break;
	default:
		return -EINVAL;
	}

	uniphier_aidet_detconf_update(priv, data->hwirq, val);

	return irq_chip_set_type_parent(data, type);
}

static struct irq_chip uniphier_aidet_irq_chip = {
	.name = "AIDET",
	.irq_mask = irq_chip_mask_parent,
	.irq_unmask = irq_chip_unmask_parent,
	.irq_eoi = irq_chip_eoi_parent,
	.irq_set_affinity = irq_chip_set_affinity_parent,
	.irq_set_type = uniphier_aidet_irq_set_type,
};

static int uniphier_aidet_domain_translate(struct irq_domain *domain,
					   struct irq_fwspec *fwspec,
					   unsigned long *out_hwirq,
					   unsigned int *out_type)
{
	if (WARN_ON(fwspec->param_count < 2))
		return -EINVAL;

	*out_hwirq = fwspec->param[0];
	*out_type = fwspec->param[1] & IRQ_TYPE_SENSE_MASK;

	return 0;
}

static int uniphier_aidet_domain_alloc(struct irq_domain *domain,
				       unsigned int virq, unsigned int nr_irqs,
				       void *arg)
{
	struct irq_fwspec parent_fwspec;
	irq_hw_number_t hwirq;
	unsigned int type;
	int ret;

	if (nr_irqs != 1)
		return -EINVAL;

	ret = uniphier_aidet_domain_translate(domain, arg, &hwirq, &type);
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

	if (hwirq >= UNIPHIER_AIDET_NR_IRQS)
		return -ENXIO;

	ret = irq_domain_set_hwirq_and_chip(domain, virq, hwirq,
					    &uniphier_aidet_irq_chip,
					    domain->host_data);
	if (ret)
		return ret;

	/* parent is GIC */
	parent_fwspec.fwnode = domain->parent->fwnode;
	parent_fwspec.param_count = 3;
	parent_fwspec.param[0] = 0;		/* SPI */
	parent_fwspec.param[1] = hwirq;
	parent_fwspec.param[2] = type;

	return irq_domain_alloc_irqs_parent(domain, virq, 1, &parent_fwspec);
}

static const struct irq_domain_ops uniphier_aidet_domain_ops = {
	.alloc = uniphier_aidet_domain_alloc,
	.free = irq_domain_free_irqs_common,
	.translate = uniphier_aidet_domain_translate,
};

static int uniphier_aidet_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *parent_np;
	struct irq_domain *parent_domain;
	struct uniphier_aidet_priv *priv;

	parent_np = of_irq_find_parent(dev->of_node);
	if (!parent_np)
		return -ENXIO;

	parent_domain = irq_find_host(parent_np);
	of_node_put(parent_np);
	if (!parent_domain)
		return -EPROBE_DEFER;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->reg_base))
		return PTR_ERR(priv->reg_base);

	spin_lock_init(&priv->lock);

	priv->domain = irq_domain_create_hierarchy(
					parent_domain, 0,
					UNIPHIER_AIDET_NR_IRQS,
					of_node_to_fwnode(dev->of_node),
					&uniphier_aidet_domain_ops, priv);
	if (!priv->domain)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);

	return 0;
}

static int __maybe_unused uniphier_aidet_suspend(struct device *dev)
{
	struct uniphier_aidet_priv *priv = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < ARRAY_SIZE(priv->saved_vals); i++)
		priv->saved_vals[i] = readl_relaxed(
			priv->reg_base + UNIPHIER_AIDET_DETCONF + i * 4);

	return 0;
}

static int __maybe_unused uniphier_aidet_resume(struct device *dev)
{
	struct uniphier_aidet_priv *priv = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < ARRAY_SIZE(priv->saved_vals); i++)
		writel_relaxed(priv->saved_vals[i],
			       priv->reg_base + UNIPHIER_AIDET_DETCONF + i * 4);

	return 0;
}

static const struct dev_pm_ops uniphier_aidet_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(uniphier_aidet_suspend,
				      uniphier_aidet_resume)
};

static const struct of_device_id uniphier_aidet_match[] = {
	{ .compatible = "socionext,uniphier-ld4-aidet" },
	{ .compatible = "socionext,uniphier-pro4-aidet" },
	{ .compatible = "socionext,uniphier-sld8-aidet" },
	{ .compatible = "socionext,uniphier-pro5-aidet" },
	{ .compatible = "socionext,uniphier-pxs2-aidet" },
	{ .compatible = "socionext,uniphier-ld11-aidet" },
	{ .compatible = "socionext,uniphier-ld20-aidet" },
	{ .compatible = "socionext,uniphier-pxs3-aidet" },
	{ /* sentinel */ }
};

static struct platform_driver uniphier_aidet_driver = {
	.probe = uniphier_aidet_probe,
	.driver = {
		.name = "uniphier-aidet",
		.of_match_table = uniphier_aidet_match,
		.pm = &uniphier_aidet_pm_ops,
	},
};
builtin_platform_driver(uniphier_aidet_driver);
