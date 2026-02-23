// SPDX-License-Identifier: GPL-2.0

#define pr_fmt(fmt) "irq-ls-extirq: " fmt

#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>

#include <dt-bindings/interrupt-controller/arm-gic.h>

#define MAXIRQ 12
#define LS1021A_SCFGREVCR 0x200

struct ls_extirq_data {
	void __iomem		*intpcr;
	raw_spinlock_t		lock;
	bool			big_endian;
	bool			is_ls1021a_or_ls1043a;
	u32			nirq;
	struct irq_fwspec	map[MAXIRQ];
};

static void ls_extirq_intpcr_rmw(struct ls_extirq_data *priv, u32 mask,
				 u32 value)
{
	u32 intpcr;

	/*
	 * Serialize concurrent calls to ls_extirq_set_type() from multiple
	 * IRQ descriptors, making sure the read-modify-write is atomic.
	 */
	raw_spin_lock(&priv->lock);

	if (priv->big_endian)
		intpcr = ioread32be(priv->intpcr);
	else
		intpcr = ioread32(priv->intpcr);

	intpcr &= ~mask;
	intpcr |= value;

	if (priv->big_endian)
		iowrite32be(intpcr, priv->intpcr);
	else
		iowrite32(intpcr, priv->intpcr);

	raw_spin_unlock(&priv->lock);
}

static int
ls_extirq_set_type(struct irq_data *data, unsigned int type)
{
	struct ls_extirq_data *priv = data->chip_data;
	irq_hw_number_t hwirq = data->hwirq;
	u32 value, mask;

	if (priv->is_ls1021a_or_ls1043a)
		mask = 1U << (31 - hwirq);
	else
		mask = 1U << hwirq;

	switch (type) {
	case IRQ_TYPE_LEVEL_LOW:
		type = IRQ_TYPE_LEVEL_HIGH;
		value = mask;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		type = IRQ_TYPE_EDGE_RISING;
		value = mask;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
	case IRQ_TYPE_EDGE_RISING:
		value = 0;
		break;
	default:
		return -EINVAL;
	}

	ls_extirq_intpcr_rmw(priv, mask, value);

	return irq_chip_set_type_parent(data, type);
}

static struct irq_chip ls_extirq_chip = {
	.name			= "ls-extirq",
	.irq_mask		= irq_chip_mask_parent,
	.irq_unmask		= irq_chip_unmask_parent,
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_set_type		= ls_extirq_set_type,
	.irq_retrigger		= irq_chip_retrigger_hierarchy,
	.irq_set_affinity	= irq_chip_set_affinity_parent,
	.flags                  = IRQCHIP_SET_TYPE_MASKED | IRQCHIP_SKIP_SET_WAKE,
};

static int
ls_extirq_domain_alloc(struct irq_domain *domain, unsigned int virq,
		       unsigned int nr_irqs, void *arg)
{
	struct ls_extirq_data *priv = domain->host_data;
	struct irq_fwspec *fwspec = arg;
	irq_hw_number_t hwirq;

	if (fwspec->param_count != 2)
		return -EINVAL;

	hwirq = fwspec->param[0];
	if (hwirq >= priv->nirq)
		return -EINVAL;

	irq_domain_set_hwirq_and_chip(domain, virq, hwirq, &ls_extirq_chip,
				      priv);

	return irq_domain_alloc_irqs_parent(domain, virq, 1, &priv->map[hwirq]);
}

static const struct irq_domain_ops extirq_domain_ops = {
	.xlate		= irq_domain_xlate_twocell,
	.alloc		= ls_extirq_domain_alloc,
	.free		= irq_domain_free_irqs_common,
};

static int
ls_extirq_parse_map(struct ls_extirq_data *priv, struct device_node *node)
{
	struct of_imap_parser imap_parser;
	struct of_imap_item imap_item;
	int ret;

	ret = of_imap_parser_init(&imap_parser, node, &imap_item);
	if (ret)
		return ret;

	for_each_of_imap_item(&imap_parser, &imap_item) {
		struct device_node *ipar;
		u32 hwirq;
		int i;

		hwirq = imap_item.child_imap[0];
		if (hwirq >= MAXIRQ) {
			of_node_put(imap_item.parent_args.np);
			return -EINVAL;
		}
		priv->nirq = max(priv->nirq, hwirq + 1);

		ipar = of_node_get(imap_item.parent_args.np);
		priv->map[hwirq].fwnode = of_fwnode_handle(ipar);

		priv->map[hwirq].param_count = imap_item.parent_args.args_count;
		for (i = 0; i < priv->map[hwirq].param_count; i++)
			priv->map[hwirq].param[i] = imap_item.parent_args.args[i];
	}
	return 0;
}

static int ls_extirq_probe(struct platform_device *pdev)
{
	struct irq_domain *domain, *parent_domain;
	struct device_node *node, *parent;
	struct device *dev = &pdev->dev;
	struct ls_extirq_data *priv;
	int ret;

	node = dev->of_node;
	parent = of_irq_find_parent(node);
	if (!parent)
		return dev_err_probe(dev, -ENODEV, "Failed to get IRQ parent node\n");

	parent_domain = irq_find_host(parent);
	if (!parent_domain)
		return dev_err_probe(dev, -EPROBE_DEFER, "Cannot find parent domain\n");

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return dev_err_probe(dev, -ENOMEM, "Failed to allocate memory\n");

	priv->intpcr = devm_of_iomap(dev, node, 0, NULL);
	if (!priv->intpcr)
		return dev_err_probe(dev, -ENOMEM, "Cannot ioremap OF node %pOF\n", node);

	ret = ls_extirq_parse_map(priv, node);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to parse IRQ map\n");

	priv->big_endian = of_device_is_big_endian(node->parent);
	priv->is_ls1021a_or_ls1043a = of_device_is_compatible(node, "fsl,ls1021a-extirq") ||
				      of_device_is_compatible(node, "fsl,ls1043a-extirq");
	raw_spin_lock_init(&priv->lock);

	domain = irq_domain_create_hierarchy(parent_domain, 0, priv->nirq, of_fwnode_handle(node),
					     &extirq_domain_ops, priv);
	if (!domain)
		return dev_err_probe(dev, -ENOMEM, "Failed to add IRQ domain\n");

	return 0;
}

static const struct of_device_id ls_extirq_dt_ids[] = {
	{ .compatible = "fsl,ls1021a-extirq" },
	{ .compatible = "fsl,ls1043a-extirq" },
	{ .compatible = "fsl,ls1088a-extirq" },
	{}
};
MODULE_DEVICE_TABLE(of, ls_extirq_dt_ids);

static struct platform_driver ls_extirq_driver = {
	.probe = ls_extirq_probe,
	.driver = {
		.name = "ls-extirq",
		.of_match_table = ls_extirq_dt_ids,
	}
};

builtin_platform_driver(ls_extirq_driver);
