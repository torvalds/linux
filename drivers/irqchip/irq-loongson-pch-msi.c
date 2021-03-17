// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 2020, Jiaxun Yang <jiaxun.yang@flygoat.com>
 *  Loongson PCH MSI support
 */

#define pr_fmt(fmt) "pch-msi: " fmt

#include <linux/irqchip.h>
#include <linux/msi.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>
#include <linux/pci.h>
#include <linux/slab.h>

struct pch_msi_data {
	struct mutex	msi_map_lock;
	phys_addr_t	doorbell;
	u32		irq_first;	/* The vector number that MSIs starts */
	u32		num_irqs;	/* The number of vectors for MSIs */
	unsigned long	*msi_map;
};

static void pch_msi_mask_msi_irq(struct irq_data *d)
{
	pci_msi_mask_irq(d);
	irq_chip_mask_parent(d);
}

static void pch_msi_unmask_msi_irq(struct irq_data *d)
{
	irq_chip_unmask_parent(d);
	pci_msi_unmask_irq(d);
}

static struct irq_chip pch_msi_irq_chip = {
	.name			= "PCH PCI MSI",
	.irq_mask		= pch_msi_mask_msi_irq,
	.irq_unmask		= pch_msi_unmask_msi_irq,
	.irq_ack		= irq_chip_ack_parent,
	.irq_set_affinity	= irq_chip_set_affinity_parent,
};

static int pch_msi_allocate_hwirq(struct pch_msi_data *priv, int num_req)
{
	int first;

	mutex_lock(&priv->msi_map_lock);

	first = bitmap_find_free_region(priv->msi_map, priv->num_irqs,
					get_count_order(num_req));
	if (first < 0) {
		mutex_unlock(&priv->msi_map_lock);
		return -ENOSPC;
	}

	mutex_unlock(&priv->msi_map_lock);

	return priv->irq_first + first;
}

static void pch_msi_free_hwirq(struct pch_msi_data *priv,
				int hwirq, int num_req)
{
	int first = hwirq - priv->irq_first;

	mutex_lock(&priv->msi_map_lock);
	bitmap_release_region(priv->msi_map, first, get_count_order(num_req));
	mutex_unlock(&priv->msi_map_lock);
}

static void pch_msi_compose_msi_msg(struct irq_data *data,
					struct msi_msg *msg)
{
	struct pch_msi_data *priv = irq_data_get_irq_chip_data(data);

	msg->address_hi = upper_32_bits(priv->doorbell);
	msg->address_lo = lower_32_bits(priv->doorbell);
	msg->data = data->hwirq;
}

static struct msi_domain_info pch_msi_domain_info = {
	.flags	= MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
		  MSI_FLAG_MULTI_PCI_MSI | MSI_FLAG_PCI_MSIX,
	.chip	= &pch_msi_irq_chip,
};

static struct irq_chip middle_irq_chip = {
	.name			= "PCH MSI",
	.irq_mask		= irq_chip_mask_parent,
	.irq_unmask		= irq_chip_unmask_parent,
	.irq_ack		= irq_chip_ack_parent,
	.irq_set_affinity	= irq_chip_set_affinity_parent,
	.irq_compose_msi_msg	= pch_msi_compose_msi_msg,
};

static int pch_msi_parent_domain_alloc(struct irq_domain *domain,
					unsigned int virq, int hwirq)
{
	struct irq_fwspec fwspec;

	fwspec.fwnode = domain->parent->fwnode;
	fwspec.param_count = 1;
	fwspec.param[0] = hwirq;

	return irq_domain_alloc_irqs_parent(domain, virq, 1, &fwspec);
}

static int pch_msi_middle_domain_alloc(struct irq_domain *domain,
					   unsigned int virq,
					   unsigned int nr_irqs, void *args)
{
	struct pch_msi_data *priv = domain->host_data;
	int hwirq, err, i;

	hwirq = pch_msi_allocate_hwirq(priv, nr_irqs);
	if (hwirq < 0)
		return hwirq;

	for (i = 0; i < nr_irqs; i++) {
		err = pch_msi_parent_domain_alloc(domain, virq + i, hwirq + i);
		if (err)
			goto err_hwirq;

		irq_domain_set_hwirq_and_chip(domain, virq + i, hwirq + i,
					      &middle_irq_chip, priv);
	}

	return 0;

err_hwirq:
	pch_msi_free_hwirq(priv, hwirq, nr_irqs);
	irq_domain_free_irqs_parent(domain, virq, i - 1);

	return err;
}

static void pch_msi_middle_domain_free(struct irq_domain *domain,
					   unsigned int virq,
					   unsigned int nr_irqs)
{
	struct irq_data *d = irq_domain_get_irq_data(domain, virq);
	struct pch_msi_data *priv = irq_data_get_irq_chip_data(d);

	irq_domain_free_irqs_parent(domain, virq, nr_irqs);
	pch_msi_free_hwirq(priv, d->hwirq, nr_irqs);
}

static const struct irq_domain_ops pch_msi_middle_domain_ops = {
	.alloc	= pch_msi_middle_domain_alloc,
	.free	= pch_msi_middle_domain_free,
};

static int pch_msi_init_domains(struct pch_msi_data *priv,
				struct device_node *node,
				struct irq_domain *parent)
{
	struct irq_domain *middle_domain, *msi_domain;

	middle_domain = irq_domain_create_linear(of_node_to_fwnode(node),
						priv->num_irqs,
						&pch_msi_middle_domain_ops,
						priv);
	if (!middle_domain) {
		pr_err("Failed to create the MSI middle domain\n");
		return -ENOMEM;
	}

	middle_domain->parent = parent;
	irq_domain_update_bus_token(middle_domain, DOMAIN_BUS_NEXUS);

	msi_domain = pci_msi_create_irq_domain(of_node_to_fwnode(node),
					       &pch_msi_domain_info,
					       middle_domain);
	if (!msi_domain) {
		pr_err("Failed to create PCI MSI domain\n");
		irq_domain_remove(middle_domain);
		return -ENOMEM;
	}

	return 0;
}

static int pch_msi_init(struct device_node *node,
			    struct device_node *parent)
{
	struct pch_msi_data *priv;
	struct irq_domain *parent_domain;
	struct resource res;
	int ret;

	parent_domain = irq_find_host(parent);
	if (!parent_domain) {
		pr_err("Failed to find the parent domain\n");
		return -ENXIO;
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mutex_init(&priv->msi_map_lock);

	ret = of_address_to_resource(node, 0, &res);
	if (ret) {
		pr_err("Failed to allocate resource\n");
		goto err_priv;
	}

	priv->doorbell = res.start;

	if (of_property_read_u32(node, "loongson,msi-base-vec",
				&priv->irq_first)) {
		pr_err("Unable to parse MSI vec base\n");
		ret = -EINVAL;
		goto err_priv;
	}

	if (of_property_read_u32(node, "loongson,msi-num-vecs",
				&priv->num_irqs)) {
		pr_err("Unable to parse MSI vec number\n");
		ret = -EINVAL;
		goto err_priv;
	}

	priv->msi_map = bitmap_alloc(priv->num_irqs, GFP_KERNEL);
	if (!priv->msi_map) {
		ret = -ENOMEM;
		goto err_priv;
	}

	pr_debug("Registering %d MSIs, starting at %d\n",
		 priv->num_irqs, priv->irq_first);

	ret = pch_msi_init_domains(priv, node, parent_domain);
	if (ret)
		goto err_map;

	return 0;

err_map:
	kfree(priv->msi_map);
err_priv:
	kfree(priv);
	return ret;
}

IRQCHIP_DECLARE(pch_msi, "loongson,pch-msi-1.0", pch_msi_init);
