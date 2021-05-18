// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 ARM Limited, All Rights Reserved.
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#define pr_fmt(fmt) "GICv3: " fmt

#include <linux/dma-iommu.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/msi.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <linux/irqchip/arm-gic-v3.h>

struct mbi_range {
	u32			spi_start;
	u32			nr_spis;
	unsigned long		*bm;
};

static DEFINE_MUTEX(mbi_lock);
static phys_addr_t		mbi_phys_base;
static struct mbi_range		*mbi_ranges;
static unsigned int		mbi_range_nr;

static struct irq_chip mbi_irq_chip = {
	.name			= "MBI",
	.irq_mask		= irq_chip_mask_parent,
	.irq_unmask		= irq_chip_unmask_parent,
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_set_type		= irq_chip_set_type_parent,
	.irq_set_affinity	= irq_chip_set_affinity_parent,
};

static int mbi_irq_gic_domain_alloc(struct irq_domain *domain,
				       unsigned int virq,
				       irq_hw_number_t hwirq)
{
	struct irq_fwspec fwspec;
	struct irq_data *d;
	int err;

	/*
	 * Using ACPI? There is no MBI support in the spec, you
	 * shouldn't even be here.
	 */
	if (!is_of_node(domain->parent->fwnode))
		return -EINVAL;

	/*
	 * Let's default to edge. This is consistent with traditional
	 * MSIs, and systems requiring level signaling will just
	 * enforce the trigger on their own.
	 */
	fwspec.fwnode = domain->parent->fwnode;
	fwspec.param_count = 3;
	fwspec.param[0] = 0;
	fwspec.param[1] = hwirq - 32;
	fwspec.param[2] = IRQ_TYPE_EDGE_RISING;

	err = irq_domain_alloc_irqs_parent(domain, virq, 1, &fwspec);
	if (err)
		return err;

	d = irq_domain_get_irq_data(domain->parent, virq);
	return d->chip->irq_set_type(d, IRQ_TYPE_EDGE_RISING);
}

static void mbi_free_msi(struct mbi_range *mbi, unsigned int hwirq,
			 int nr_irqs)
{
	mutex_lock(&mbi_lock);
	bitmap_release_region(mbi->bm, hwirq - mbi->spi_start,
			      get_count_order(nr_irqs));
	mutex_unlock(&mbi_lock);
}

static int mbi_irq_domain_alloc(struct irq_domain *domain, unsigned int virq,
				   unsigned int nr_irqs, void *args)
{
	msi_alloc_info_t *info = args;
	struct mbi_range *mbi = NULL;
	int hwirq, offset, i, err = 0;

	mutex_lock(&mbi_lock);
	for (i = 0; i < mbi_range_nr; i++) {
		offset = bitmap_find_free_region(mbi_ranges[i].bm,
						 mbi_ranges[i].nr_spis,
						 get_count_order(nr_irqs));
		if (offset >= 0) {
			mbi = &mbi_ranges[i];
			break;
		}
	}
	mutex_unlock(&mbi_lock);

	if (!mbi)
		return -ENOSPC;

	hwirq = mbi->spi_start + offset;

	err = iommu_dma_prepare_msi(info->desc,
				    mbi_phys_base + GICD_SETSPI_NSR);
	if (err)
		return err;

	for (i = 0; i < nr_irqs; i++) {
		err = mbi_irq_gic_domain_alloc(domain, virq + i, hwirq + i);
		if (err)
			goto fail;

		irq_domain_set_hwirq_and_chip(domain, virq + i, hwirq + i,
					      &mbi_irq_chip, mbi);
	}

	return 0;

fail:
	irq_domain_free_irqs_parent(domain, virq, nr_irqs);
	mbi_free_msi(mbi, hwirq, nr_irqs);
	return err;
}

static void mbi_irq_domain_free(struct irq_domain *domain,
				unsigned int virq, unsigned int nr_irqs)
{
	struct irq_data *d = irq_domain_get_irq_data(domain, virq);
	struct mbi_range *mbi = irq_data_get_irq_chip_data(d);

	mbi_free_msi(mbi, d->hwirq, nr_irqs);
	irq_domain_free_irqs_parent(domain, virq, nr_irqs);
}

static const struct irq_domain_ops mbi_domain_ops = {
	.alloc			= mbi_irq_domain_alloc,
	.free			= mbi_irq_domain_free,
};

static void mbi_compose_msi_msg(struct irq_data *data, struct msi_msg *msg)
{
	msg[0].address_hi = upper_32_bits(mbi_phys_base + GICD_SETSPI_NSR);
	msg[0].address_lo = lower_32_bits(mbi_phys_base + GICD_SETSPI_NSR);
	msg[0].data = data->parent_data->hwirq;

	iommu_dma_compose_msi_msg(irq_data_get_msi_desc(data), msg);
}

#ifdef CONFIG_PCI_MSI
/* PCI-specific irqchip */
static void mbi_mask_msi_irq(struct irq_data *d)
{
	pci_msi_mask_irq(d);
	irq_chip_mask_parent(d);
}

static void mbi_unmask_msi_irq(struct irq_data *d)
{
	pci_msi_unmask_irq(d);
	irq_chip_unmask_parent(d);
}

static struct irq_chip mbi_msi_irq_chip = {
	.name			= "MSI",
	.irq_mask		= mbi_mask_msi_irq,
	.irq_unmask		= mbi_unmask_msi_irq,
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_compose_msi_msg	= mbi_compose_msi_msg,
	.irq_write_msi_msg	= pci_msi_domain_write_msg,
};

static struct msi_domain_info mbi_msi_domain_info = {
	.flags	= (MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
		   MSI_FLAG_PCI_MSIX | MSI_FLAG_MULTI_PCI_MSI),
	.chip	= &mbi_msi_irq_chip,
};

static int mbi_allocate_pci_domain(struct irq_domain *nexus_domain,
				   struct irq_domain **pci_domain)
{
	*pci_domain = pci_msi_create_irq_domain(nexus_domain->parent->fwnode,
						&mbi_msi_domain_info,
						nexus_domain);
	if (!*pci_domain)
		return -ENOMEM;

	return 0;
}
#else
static int mbi_allocate_pci_domain(struct irq_domain *nexus_domain,
				   struct irq_domain **pci_domain)
{
	*pci_domain = NULL;
	return 0;
}
#endif

static void mbi_compose_mbi_msg(struct irq_data *data, struct msi_msg *msg)
{
	mbi_compose_msi_msg(data, msg);

	msg[1].address_hi = upper_32_bits(mbi_phys_base + GICD_CLRSPI_NSR);
	msg[1].address_lo = lower_32_bits(mbi_phys_base + GICD_CLRSPI_NSR);
	msg[1].data = data->parent_data->hwirq;

	iommu_dma_compose_msi_msg(irq_data_get_msi_desc(data), &msg[1]);
}

/* Platform-MSI specific irqchip */
static struct irq_chip mbi_pmsi_irq_chip = {
	.name			= "pMSI",
	.irq_set_type		= irq_chip_set_type_parent,
	.irq_compose_msi_msg	= mbi_compose_mbi_msg,
	.flags			= IRQCHIP_SUPPORTS_LEVEL_MSI,
};

static struct msi_domain_ops mbi_pmsi_ops = {
};

static struct msi_domain_info mbi_pmsi_domain_info = {
	.flags	= (MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
		   MSI_FLAG_LEVEL_CAPABLE),
	.ops	= &mbi_pmsi_ops,
	.chip	= &mbi_pmsi_irq_chip,
};

static int mbi_allocate_domains(struct irq_domain *parent)
{
	struct irq_domain *nexus_domain, *pci_domain, *plat_domain;
	int err;

	nexus_domain = irq_domain_create_tree(parent->fwnode,
					      &mbi_domain_ops, NULL);
	if (!nexus_domain)
		return -ENOMEM;

	irq_domain_update_bus_token(nexus_domain, DOMAIN_BUS_NEXUS);
	nexus_domain->parent = parent;

	err = mbi_allocate_pci_domain(nexus_domain, &pci_domain);

	plat_domain = platform_msi_create_irq_domain(parent->fwnode,
						     &mbi_pmsi_domain_info,
						     nexus_domain);

	if (err || !plat_domain) {
		if (plat_domain)
			irq_domain_remove(plat_domain);
		if (pci_domain)
			irq_domain_remove(pci_domain);
		irq_domain_remove(nexus_domain);
		return -ENOMEM;
	}

	return 0;
}

int __init mbi_init(struct fwnode_handle *fwnode, struct irq_domain *parent)
{
	struct device_node *np;
	const __be32 *reg;
	int ret, n;

	np = to_of_node(fwnode);

	if (!of_property_read_bool(np, "msi-controller"))
		return 0;

	n = of_property_count_elems_of_size(np, "mbi-ranges", sizeof(u32));
	if (n <= 0 || n % 2)
		return -EINVAL;

	mbi_range_nr = n / 2;
	mbi_ranges = kcalloc(mbi_range_nr, sizeof(*mbi_ranges), GFP_KERNEL);
	if (!mbi_ranges)
		return -ENOMEM;

	for (n = 0; n < mbi_range_nr; n++) {
		ret = of_property_read_u32_index(np, "mbi-ranges", n * 2,
						 &mbi_ranges[n].spi_start);
		if (ret)
			goto err_free_mbi;
		ret = of_property_read_u32_index(np, "mbi-ranges", n * 2 + 1,
						 &mbi_ranges[n].nr_spis);
		if (ret)
			goto err_free_mbi;

		mbi_ranges[n].bm = kcalloc(BITS_TO_LONGS(mbi_ranges[n].nr_spis),
					   sizeof(long), GFP_KERNEL);
		if (!mbi_ranges[n].bm) {
			ret = -ENOMEM;
			goto err_free_mbi;
		}
		pr_info("MBI range [%d:%d]\n", mbi_ranges[n].spi_start,
			mbi_ranges[n].spi_start + mbi_ranges[n].nr_spis - 1);
	}

	reg = of_get_property(np, "mbi-alias", NULL);
	if (reg) {
		mbi_phys_base = of_translate_address(np, reg);
		if (mbi_phys_base == (phys_addr_t)OF_BAD_ADDR) {
			ret = -ENXIO;
			goto err_free_mbi;
		}
	} else {
		struct resource res;

		if (of_address_to_resource(np, 0, &res)) {
			ret = -ENXIO;
			goto err_free_mbi;
		}

		mbi_phys_base = res.start;
	}

	pr_info("Using MBI frame %pa\n", &mbi_phys_base);

	ret = mbi_allocate_domains(parent);
	if (ret)
		goto err_free_mbi;

	return 0;

err_free_mbi:
	if (mbi_ranges) {
		for (n = 0; n < mbi_range_nr; n++)
			kfree(mbi_ranges[n].bm);
		kfree(mbi_ranges);
	}

	return ret;
}
