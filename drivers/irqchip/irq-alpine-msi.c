/*
 * Annapurna Labs MSIX support services
 *
 * Copyright (C) 2016, Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Antoine Tenart <antoine.tenart@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/irqchip.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/msi.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include <asm/irq.h>
#include <asm/msi.h>

/* MSIX message address format: local GIC target */
#define ALPINE_MSIX_SPI_TARGET_CLUSTER0		BIT(16)

struct alpine_msix_data {
	spinlock_t msi_map_lock;
	phys_addr_t addr;
	u32 spi_first;		/* The SGI number that MSIs start */
	u32 num_spis;		/* The number of SGIs for MSIs */
	unsigned long *msi_map;
};

static void alpine_msix_mask_msi_irq(struct irq_data *d)
{
	pci_msi_mask_irq(d);
	irq_chip_mask_parent(d);
}

static void alpine_msix_unmask_msi_irq(struct irq_data *d)
{
	pci_msi_unmask_irq(d);
	irq_chip_unmask_parent(d);
}

static struct irq_chip alpine_msix_irq_chip = {
	.name			= "MSIx",
	.irq_mask		= alpine_msix_mask_msi_irq,
	.irq_unmask		= alpine_msix_unmask_msi_irq,
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_set_affinity	= irq_chip_set_affinity_parent,
};

static int alpine_msix_allocate_sgi(struct alpine_msix_data *priv, int num_req)
{
	int first;

	spin_lock(&priv->msi_map_lock);

	first = bitmap_find_next_zero_area(priv->msi_map, priv->num_spis, 0,
					   num_req, 0);
	if (first >= priv->num_spis) {
		spin_unlock(&priv->msi_map_lock);
		return -ENOSPC;
	}

	bitmap_set(priv->msi_map, first, num_req);

	spin_unlock(&priv->msi_map_lock);

	return priv->spi_first + first;
}

static void alpine_msix_free_sgi(struct alpine_msix_data *priv, unsigned sgi,
				 int num_req)
{
	int first = sgi - priv->spi_first;

	spin_lock(&priv->msi_map_lock);

	bitmap_clear(priv->msi_map, first, num_req);

	spin_unlock(&priv->msi_map_lock);
}

static void alpine_msix_compose_msi_msg(struct irq_data *data,
					struct msi_msg *msg)
{
	struct alpine_msix_data *priv = irq_data_get_irq_chip_data(data);
	phys_addr_t msg_addr = priv->addr;

	msg_addr |= (data->hwirq << 3);

	msg->address_hi = upper_32_bits(msg_addr);
	msg->address_lo = lower_32_bits(msg_addr);
	msg->data = 0;
}

static struct msi_domain_info alpine_msix_domain_info = {
	.flags	= MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
		  MSI_FLAG_PCI_MSIX,
	.chip	= &alpine_msix_irq_chip,
};

static struct irq_chip middle_irq_chip = {
	.name			= "alpine_msix_middle",
	.irq_mask		= irq_chip_mask_parent,
	.irq_unmask		= irq_chip_unmask_parent,
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_set_affinity	= irq_chip_set_affinity_parent,
	.irq_compose_msi_msg	= alpine_msix_compose_msi_msg,
};

static int alpine_msix_gic_domain_alloc(struct irq_domain *domain,
					unsigned int virq, int sgi)
{
	struct irq_fwspec fwspec;
	struct irq_data *d;
	int ret;

	if (!is_of_node(domain->parent->fwnode))
		return -EINVAL;

	fwspec.fwnode = domain->parent->fwnode;
	fwspec.param_count = 3;
	fwspec.param[0] = 0;
	fwspec.param[1] = sgi;
	fwspec.param[2] = IRQ_TYPE_EDGE_RISING;

	ret = irq_domain_alloc_irqs_parent(domain, virq, 1, &fwspec);
	if (ret)
		return ret;

	d = irq_domain_get_irq_data(domain->parent, virq);
	d->chip->irq_set_type(d, IRQ_TYPE_EDGE_RISING);

	return 0;
}

static int alpine_msix_middle_domain_alloc(struct irq_domain *domain,
					   unsigned int virq,
					   unsigned int nr_irqs, void *args)
{
	struct alpine_msix_data *priv = domain->host_data;
	int sgi, err, i;

	sgi = alpine_msix_allocate_sgi(priv, nr_irqs);
	if (sgi < 0)
		return sgi;

	for (i = 0; i < nr_irqs; i++) {
		err = alpine_msix_gic_domain_alloc(domain, virq + i, sgi + i);
		if (err)
			goto err_sgi;

		irq_domain_set_hwirq_and_chip(domain, virq + i, sgi + i,
					      &middle_irq_chip, priv);
	}

	return 0;

err_sgi:
	irq_domain_free_irqs_parent(domain, virq, i - 1);
	alpine_msix_free_sgi(priv, sgi, nr_irqs);
	return err;
}

static void alpine_msix_middle_domain_free(struct irq_domain *domain,
					   unsigned int virq,
					   unsigned int nr_irqs)
{
	struct irq_data *d = irq_domain_get_irq_data(domain, virq);
	struct alpine_msix_data *priv = irq_data_get_irq_chip_data(d);

	irq_domain_free_irqs_parent(domain, virq, nr_irqs);
	alpine_msix_free_sgi(priv, d->hwirq, nr_irqs);
}

static const struct irq_domain_ops alpine_msix_middle_domain_ops = {
	.alloc	= alpine_msix_middle_domain_alloc,
	.free	= alpine_msix_middle_domain_free,
};

static int alpine_msix_init_domains(struct alpine_msix_data *priv,
				    struct device_node *node)
{
	struct irq_domain *middle_domain, *msi_domain, *gic_domain;
	struct device_node *gic_node;

	gic_node = of_irq_find_parent(node);
	if (!gic_node) {
		pr_err("Failed to find the GIC node\n");
		return -ENODEV;
	}

	gic_domain = irq_find_host(gic_node);
	of_node_put(gic_node);
	if (!gic_domain) {
		pr_err("Failed to find the GIC domain\n");
		return -ENXIO;
	}

	middle_domain = irq_domain_add_hierarchy(gic_domain, 0, 0, NULL,
						 &alpine_msix_middle_domain_ops,
						 priv);
	if (!middle_domain) {
		pr_err("Failed to create the MSIX middle domain\n");
		return -ENOMEM;
	}

	msi_domain = pci_msi_create_irq_domain(of_node_to_fwnode(node),
					       &alpine_msix_domain_info,
					       middle_domain);
	if (!msi_domain) {
		pr_err("Failed to create MSI domain\n");
		irq_domain_remove(middle_domain);
		return -ENOMEM;
	}

	return 0;
}

static int alpine_msix_init(struct device_node *node,
			    struct device_node *parent)
{
	struct alpine_msix_data *priv;
	struct resource res;
	int ret;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	spin_lock_init(&priv->msi_map_lock);

	ret = of_address_to_resource(node, 0, &res);
	if (ret) {
		pr_err("Failed to allocate resource\n");
		goto err_priv;
	}

	/*
	 * The 20 least significant bits of addr provide direct information
	 * regarding the interrupt destination.
	 *
	 * To select the primary GIC as the target GIC, bits [18:17] must be set
	 * to 0x0. In this case, bit 16 (SPI_TARGET_CLUSTER0) must be set.
	 */
	priv->addr = res.start & GENMASK_ULL(63,20);
	priv->addr |= ALPINE_MSIX_SPI_TARGET_CLUSTER0;

	if (of_property_read_u32(node, "al,msi-base-spi", &priv->spi_first)) {
		pr_err("Unable to parse MSI base\n");
		ret = -EINVAL;
		goto err_priv;
	}

	if (of_property_read_u32(node, "al,msi-num-spis", &priv->num_spis)) {
		pr_err("Unable to parse MSI numbers\n");
		ret = -EINVAL;
		goto err_priv;
	}

	priv->msi_map = bitmap_zalloc(priv->num_spis, GFP_KERNEL);
	if (!priv->msi_map) {
		ret = -ENOMEM;
		goto err_priv;
	}

	pr_debug("Registering %d msixs, starting at %d\n",
		 priv->num_spis, priv->spi_first);

	ret = alpine_msix_init_domains(priv, node);
	if (ret)
		goto err_map;

	return 0;

err_map:
	bitmap_free(priv->msi_map);
err_priv:
	kfree(priv);
	return ret;
}
IRQCHIP_DECLARE(alpine_msix, "al,alpine-msix", alpine_msix_init);
