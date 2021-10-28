// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZ/A1 IRQC Driver
 *
 * Copyright (C) 2019 Glider bvba
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irqdomain.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <dt-bindings/interrupt-controller/arm-gic.h>

#define IRQC_NUM_IRQ		8

#define ICR0			0	/* Interrupt Control Register 0 */

#define ICR0_NMIL		BIT(15)	/* NMI Input Level (0=low, 1=high) */
#define ICR0_NMIE		BIT(8)	/* Edge Select (0=falling, 1=rising) */
#define ICR0_NMIF		BIT(1)	/* NMI Interrupt Request */

#define ICR1			2	/* Interrupt Control Register 1 */

#define ICR1_IRQS(n, sense)	((sense) << ((n) * 2))	/* IRQ Sense Select */
#define ICR1_IRQS_LEVEL_LOW	0
#define ICR1_IRQS_EDGE_FALLING	1
#define ICR1_IRQS_EDGE_RISING	2
#define ICR1_IRQS_EDGE_BOTH	3
#define ICR1_IRQS_MASK(n)	ICR1_IRQS((n), 3)

#define IRQRR			4	/* IRQ Interrupt Request Register */


struct rza1_irqc_priv {
	struct device *dev;
	void __iomem *base;
	struct irq_chip chip;
	struct irq_domain *irq_domain;
	struct of_phandle_args map[IRQC_NUM_IRQ];
};

static struct rza1_irqc_priv *irq_data_to_priv(struct irq_data *data)
{
	return data->domain->host_data;
}

static void rza1_irqc_eoi(struct irq_data *d)
{
	struct rza1_irqc_priv *priv = irq_data_to_priv(d);
	u16 bit = BIT(irqd_to_hwirq(d));
	u16 tmp;

	tmp = readw_relaxed(priv->base + IRQRR);
	if (tmp & bit)
		writew_relaxed(GENMASK(IRQC_NUM_IRQ - 1, 0) & ~bit,
			       priv->base + IRQRR);

	irq_chip_eoi_parent(d);
}

static int rza1_irqc_set_type(struct irq_data *d, unsigned int type)
{
	struct rza1_irqc_priv *priv = irq_data_to_priv(d);
	unsigned int hw_irq = irqd_to_hwirq(d);
	u16 sense, tmp;

	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_LEVEL_LOW:
		sense = ICR1_IRQS_LEVEL_LOW;
		break;

	case IRQ_TYPE_EDGE_FALLING:
		sense = ICR1_IRQS_EDGE_FALLING;
		break;

	case IRQ_TYPE_EDGE_RISING:
		sense = ICR1_IRQS_EDGE_RISING;
		break;

	case IRQ_TYPE_EDGE_BOTH:
		sense = ICR1_IRQS_EDGE_BOTH;
		break;

	default:
		return -EINVAL;
	}

	tmp = readw_relaxed(priv->base + ICR1);
	tmp &= ~ICR1_IRQS_MASK(hw_irq);
	tmp |= ICR1_IRQS(hw_irq, sense);
	writew_relaxed(tmp, priv->base + ICR1);
	return 0;
}

static int rza1_irqc_alloc(struct irq_domain *domain, unsigned int virq,
			   unsigned int nr_irqs, void *arg)
{
	struct rza1_irqc_priv *priv = domain->host_data;
	struct irq_fwspec *fwspec = arg;
	unsigned int hwirq = fwspec->param[0];
	struct irq_fwspec spec;
	unsigned int i;
	int ret;

	ret = irq_domain_set_hwirq_and_chip(domain, virq, hwirq, &priv->chip,
					    priv);
	if (ret)
		return ret;

	spec.fwnode = &priv->dev->of_node->fwnode;
	spec.param_count = priv->map[hwirq].args_count;
	for (i = 0; i < spec.param_count; i++)
		spec.param[i] = priv->map[hwirq].args[i];

	return irq_domain_alloc_irqs_parent(domain, virq, nr_irqs, &spec);
}

static int rza1_irqc_translate(struct irq_domain *domain,
			       struct irq_fwspec *fwspec, unsigned long *hwirq,
			       unsigned int *type)
{
	if (fwspec->param_count != 2 || fwspec->param[0] >= IRQC_NUM_IRQ)
		return -EINVAL;

	*hwirq = fwspec->param[0];
	*type = fwspec->param[1];
	return 0;
}

static const struct irq_domain_ops rza1_irqc_domain_ops = {
	.alloc = rza1_irqc_alloc,
	.translate = rza1_irqc_translate,
};

static int rza1_irqc_parse_map(struct rza1_irqc_priv *priv,
			       struct device_node *gic_node)
{
	unsigned int imaplen, i, j, ret;
	struct device *dev = priv->dev;
	struct device_node *ipar;
	const __be32 *imap;
	u32 intsize;

	imap = of_get_property(dev->of_node, "interrupt-map", &imaplen);
	if (!imap)
		return -EINVAL;

	for (i = 0; i < IRQC_NUM_IRQ; i++) {
		if (imaplen < 3)
			return -EINVAL;

		/* Check interrupt number, ignore sense */
		if (be32_to_cpup(imap) != i)
			return -EINVAL;

		ipar = of_find_node_by_phandle(be32_to_cpup(imap + 2));
		if (ipar != gic_node) {
			of_node_put(ipar);
			return -EINVAL;
		}

		imap += 3;
		imaplen -= 3;

		ret = of_property_read_u32(ipar, "#interrupt-cells", &intsize);
		of_node_put(ipar);
		if (ret)
			return ret;

		if (imaplen < intsize)
			return -EINVAL;

		priv->map[i].args_count = intsize;
		for (j = 0; j < intsize; j++)
			priv->map[i].args[j] = be32_to_cpup(imap++);

		imaplen -= intsize;
	}

	return 0;
}

static int rza1_irqc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct irq_domain *parent = NULL;
	struct device_node *gic_node;
	struct rza1_irqc_priv *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);
	priv->dev = dev;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	gic_node = of_irq_find_parent(np);
	if (gic_node)
		parent = irq_find_host(gic_node);

	if (!parent) {
		dev_err(dev, "cannot find parent domain\n");
		ret = -ENODEV;
		goto out_put_node;
	}

	ret = rza1_irqc_parse_map(priv, gic_node);
	if (ret) {
		dev_err(dev, "cannot parse %s: %d\n", "interrupt-map", ret);
		goto out_put_node;
	}

	priv->chip.name = "rza1-irqc";
	priv->chip.irq_mask = irq_chip_mask_parent;
	priv->chip.irq_unmask = irq_chip_unmask_parent;
	priv->chip.irq_eoi = rza1_irqc_eoi;
	priv->chip.irq_retrigger = irq_chip_retrigger_hierarchy;
	priv->chip.irq_set_type = rza1_irqc_set_type;
	priv->chip.flags = IRQCHIP_MASK_ON_SUSPEND | IRQCHIP_SKIP_SET_WAKE;

	priv->irq_domain = irq_domain_add_hierarchy(parent, 0, IRQC_NUM_IRQ,
						    np, &rza1_irqc_domain_ops,
						    priv);
	if (!priv->irq_domain) {
		dev_err(dev, "cannot initialize irq domain\n");
		ret = -ENOMEM;
	}

out_put_node:
	of_node_put(gic_node);
	return ret;
}

static int rza1_irqc_remove(struct platform_device *pdev)
{
	struct rza1_irqc_priv *priv = platform_get_drvdata(pdev);

	irq_domain_remove(priv->irq_domain);
	return 0;
}

static const struct of_device_id rza1_irqc_dt_ids[] = {
	{ .compatible = "renesas,rza1-irqc" },
	{},
};
MODULE_DEVICE_TABLE(of, rza1_irqc_dt_ids);

static struct platform_driver rza1_irqc_device_driver = {
	.probe		= rza1_irqc_probe,
	.remove		= rza1_irqc_remove,
	.driver		= {
		.name	= "renesas_rza1_irqc",
		.of_match_table	= rza1_irqc_dt_ids,
	}
};

static int __init rza1_irqc_init(void)
{
	return platform_driver_register(&rza1_irqc_device_driver);
}
postcore_initcall(rza1_irqc_init);

static void __exit rza1_irqc_exit(void)
{
	platform_driver_unregister(&rza1_irqc_device_driver);
}
module_exit(rza1_irqc_exit);

MODULE_AUTHOR("Geert Uytterhoeven <geert+renesas@glider.be>");
MODULE_DESCRIPTION("Renesas RZ/A1 IRQC Driver");
MODULE_LICENSE("GPL v2");
