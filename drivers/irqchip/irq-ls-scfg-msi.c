/*
 * Freescale SCFG MSI(-X) support
 *
 * Copyright (C) 2016 Freescale Semiconductor.
 *
 * Author: Minghuan Lian <Minghuan.Lian@nxp.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/spinlock.h>

#define MSI_MAX_IRQS	32
#define MSI_IBS_SHIFT	3
#define MSIR		4

struct ls_scfg_msi {
	spinlock_t		lock;
	struct platform_device	*pdev;
	struct irq_domain	*parent;
	struct irq_domain	*msi_domain;
	void __iomem		*regs;
	phys_addr_t		msiir_addr;
	int			irq;
	DECLARE_BITMAP(used, MSI_MAX_IRQS);
};

static struct irq_chip ls_scfg_msi_irq_chip = {
	.name = "MSI",
	.irq_mask	= pci_msi_mask_irq,
	.irq_unmask	= pci_msi_unmask_irq,
};

static struct msi_domain_info ls_scfg_msi_domain_info = {
	.flags	= (MSI_FLAG_USE_DEF_DOM_OPS |
		   MSI_FLAG_USE_DEF_CHIP_OPS |
		   MSI_FLAG_PCI_MSIX),
	.chip	= &ls_scfg_msi_irq_chip,
};

static void ls_scfg_msi_compose_msg(struct irq_data *data, struct msi_msg *msg)
{
	struct ls_scfg_msi *msi_data = irq_data_get_irq_chip_data(data);

	msg->address_hi = upper_32_bits(msi_data->msiir_addr);
	msg->address_lo = lower_32_bits(msi_data->msiir_addr);
	msg->data = data->hwirq << MSI_IBS_SHIFT;
}

static int ls_scfg_msi_set_affinity(struct irq_data *irq_data,
				    const struct cpumask *mask, bool force)
{
	return -EINVAL;
}

static struct irq_chip ls_scfg_msi_parent_chip = {
	.name			= "SCFG",
	.irq_compose_msi_msg	= ls_scfg_msi_compose_msg,
	.irq_set_affinity	= ls_scfg_msi_set_affinity,
};

static int ls_scfg_msi_domain_irq_alloc(struct irq_domain *domain,
					unsigned int virq,
					unsigned int nr_irqs,
					void *args)
{
	struct ls_scfg_msi *msi_data = domain->host_data;
	int pos, err = 0;

	WARN_ON(nr_irqs != 1);

	spin_lock(&msi_data->lock);
	pos = find_first_zero_bit(msi_data->used, MSI_MAX_IRQS);
	if (pos < MSI_MAX_IRQS)
		__set_bit(pos, msi_data->used);
	else
		err = -ENOSPC;
	spin_unlock(&msi_data->lock);

	if (err)
		return err;

	irq_domain_set_info(domain, virq, pos,
			    &ls_scfg_msi_parent_chip, msi_data,
			    handle_simple_irq, NULL, NULL);

	return 0;
}

static void ls_scfg_msi_domain_irq_free(struct irq_domain *domain,
				   unsigned int virq, unsigned int nr_irqs)
{
	struct irq_data *d = irq_domain_get_irq_data(domain, virq);
	struct ls_scfg_msi *msi_data = irq_data_get_irq_chip_data(d);
	int pos;

	pos = d->hwirq;
	if (pos < 0 || pos >= MSI_MAX_IRQS) {
		pr_err("failed to teardown msi. Invalid hwirq %d\n", pos);
		return;
	}

	spin_lock(&msi_data->lock);
	__clear_bit(pos, msi_data->used);
	spin_unlock(&msi_data->lock);
}

static const struct irq_domain_ops ls_scfg_msi_domain_ops = {
	.alloc	= ls_scfg_msi_domain_irq_alloc,
	.free	= ls_scfg_msi_domain_irq_free,
};

static void ls_scfg_msi_irq_handler(struct irq_desc *desc)
{
	struct ls_scfg_msi *msi_data = irq_desc_get_handler_data(desc);
	unsigned long val;
	int pos, virq;

	chained_irq_enter(irq_desc_get_chip(desc), desc);

	val = ioread32be(msi_data->regs + MSIR);
	for_each_set_bit(pos, &val, MSI_MAX_IRQS) {
		virq = irq_find_mapping(msi_data->parent, (31 - pos));
		if (virq)
			generic_handle_irq(virq);
	}

	chained_irq_exit(irq_desc_get_chip(desc), desc);
}

static int ls_scfg_msi_domains_init(struct ls_scfg_msi *msi_data)
{
	/* Initialize MSI domain parent */
	msi_data->parent = irq_domain_add_linear(NULL,
						 MSI_MAX_IRQS,
						 &ls_scfg_msi_domain_ops,
						 msi_data);
	if (!msi_data->parent) {
		dev_err(&msi_data->pdev->dev, "failed to create IRQ domain\n");
		return -ENOMEM;
	}

	msi_data->msi_domain = pci_msi_create_irq_domain(
				of_node_to_fwnode(msi_data->pdev->dev.of_node),
				&ls_scfg_msi_domain_info,
				msi_data->parent);
	if (!msi_data->msi_domain) {
		dev_err(&msi_data->pdev->dev, "failed to create MSI domain\n");
		irq_domain_remove(msi_data->parent);
		return -ENOMEM;
	}

	return 0;
}

static int ls_scfg_msi_probe(struct platform_device *pdev)
{
	struct ls_scfg_msi *msi_data;
	struct resource *res;
	int ret;

	msi_data = devm_kzalloc(&pdev->dev, sizeof(*msi_data), GFP_KERNEL);
	if (!msi_data)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	msi_data->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(msi_data->regs)) {
		dev_err(&pdev->dev, "failed to initialize 'regs'\n");
		return PTR_ERR(msi_data->regs);
	}
	msi_data->msiir_addr = res->start;

	msi_data->irq = platform_get_irq(pdev, 0);
	if (msi_data->irq <= 0) {
		dev_err(&pdev->dev, "failed to get MSI irq\n");
		return -ENODEV;
	}

	msi_data->pdev = pdev;
	spin_lock_init(&msi_data->lock);

	ret = ls_scfg_msi_domains_init(msi_data);
	if (ret)
		return ret;

	irq_set_chained_handler_and_data(msi_data->irq,
					 ls_scfg_msi_irq_handler,
					 msi_data);

	platform_set_drvdata(pdev, msi_data);

	return 0;
}

static int ls_scfg_msi_remove(struct platform_device *pdev)
{
	struct ls_scfg_msi *msi_data = platform_get_drvdata(pdev);

	irq_set_chained_handler_and_data(msi_data->irq, NULL, NULL);

	irq_domain_remove(msi_data->msi_domain);
	irq_domain_remove(msi_data->parent);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const struct of_device_id ls_scfg_msi_id[] = {
	{ .compatible = "fsl,1s1021a-msi", },
	{ .compatible = "fsl,1s1043a-msi", },
	{},
};

static struct platform_driver ls_scfg_msi_driver = {
	.driver = {
		.name = "ls-scfg-msi",
		.of_match_table = ls_scfg_msi_id,
	},
	.probe = ls_scfg_msi_probe,
	.remove = ls_scfg_msi_remove,
};

module_platform_driver(ls_scfg_msi_driver);

MODULE_AUTHOR("Minghuan Lian <Minghuan.Lian@nxp.com>");
MODULE_DESCRIPTION("Freescale Layerscape SCFG MSI controller driver");
MODULE_LICENSE("GPL v2");
