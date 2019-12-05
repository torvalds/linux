// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013-2015 ARM Limited, All Rights Reserved.
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#include <linux/acpi_iort.h>
#include <linux/pci.h>
#include <linux/msi.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>

static void its_mask_msi_irq(struct irq_data *d)
{
	pci_msi_mask_irq(d);
	irq_chip_mask_parent(d);
}

static void its_unmask_msi_irq(struct irq_data *d)
{
	pci_msi_unmask_irq(d);
	irq_chip_unmask_parent(d);
}

static struct irq_chip its_msi_irq_chip = {
	.name			= "ITS-MSI",
	.irq_unmask		= its_unmask_msi_irq,
	.irq_mask		= its_mask_msi_irq,
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_write_msi_msg	= pci_msi_domain_write_msg,
};

static int its_pci_msi_vec_count(struct pci_dev *pdev, void *data)
{
	int msi, msix, *count = data;

	msi = max(pci_msi_vec_count(pdev), 0);
	msix = max(pci_msix_vec_count(pdev), 0);
	*count += max(msi, msix);

	return 0;
}

static int its_get_pci_alias(struct pci_dev *pdev, u16 alias, void *data)
{
	struct pci_dev **alias_dev = data;

	*alias_dev = pdev;

	return 0;
}

static int its_pci_msi_prepare(struct irq_domain *domain, struct device *dev,
			       int nvec, msi_alloc_info_t *info)
{
	struct pci_dev *pdev, *alias_dev;
	struct msi_domain_info *msi_info;
	int alias_count = 0, minnvec = 1;

	if (!dev_is_pci(dev))
		return -EINVAL;

	msi_info = msi_get_domain_info(domain->parent);

	pdev = to_pci_dev(dev);
	/*
	 * If pdev is downstream of any aliasing bridges, take an upper
	 * bound of how many other vectors could map to the same DevID.
	 */
	pci_for_each_dma_alias(pdev, its_get_pci_alias, &alias_dev);
	if (alias_dev != pdev && alias_dev->subordinate)
		pci_walk_bus(alias_dev->subordinate, its_pci_msi_vec_count,
			     &alias_count);

	/* ITS specific DeviceID, as the core ITS ignores dev. */
	info->scratchpad[0].ul = pci_msi_domain_get_msi_rid(domain, pdev);

	/*
	 * Always allocate a power of 2, and special case device 0 for
	 * broken systems where the DevID is not wired (and all devices
	 * appear as DevID 0). For that reason, we generously allocate a
	 * minimum of 32 MSIs for DevID 0. If you want more because all
	 * your devices are aliasing to DevID 0, consider fixing your HW.
	 */
	nvec = max(nvec, alias_count);
	if (!info->scratchpad[0].ul)
		minnvec = 32;
	nvec = max_t(int, minnvec, roundup_pow_of_two(nvec));
	return msi_info->ops->msi_prepare(domain->parent, dev, nvec, info);
}

static struct msi_domain_ops its_pci_msi_ops = {
	.msi_prepare	= its_pci_msi_prepare,
};

static struct msi_domain_info its_pci_msi_domain_info = {
	.flags	= (MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
		   MSI_FLAG_MULTI_PCI_MSI | MSI_FLAG_PCI_MSIX),
	.ops	= &its_pci_msi_ops,
	.chip	= &its_msi_irq_chip,
};

static struct of_device_id its_device_id[] = {
	{	.compatible	= "arm,gic-v3-its",	},
	{},
};

static int __init its_pci_msi_init_one(struct fwnode_handle *handle,
				       const char *name)
{
	struct irq_domain *parent;

	parent = irq_find_matching_fwnode(handle, DOMAIN_BUS_NEXUS);
	if (!parent || !msi_get_domain_info(parent)) {
		pr_err("%s: Unable to locate ITS domain\n", name);
		return -ENXIO;
	}

	if (!pci_msi_create_irq_domain(handle, &its_pci_msi_domain_info,
				       parent)) {
		pr_err("%s: Unable to create PCI domain\n", name);
		return -ENOMEM;
	}

	return 0;
}

static int __init its_pci_of_msi_init(void)
{
	struct device_node *np;

	for (np = of_find_matching_node(NULL, its_device_id); np;
	     np = of_find_matching_node(np, its_device_id)) {
		if (!of_device_is_available(np))
			continue;
		if (!of_property_read_bool(np, "msi-controller"))
			continue;

		if (its_pci_msi_init_one(of_node_to_fwnode(np), np->full_name))
			continue;

		pr_info("PCI/MSI: %pOF domain created\n", np);
	}

	return 0;
}

#ifdef CONFIG_ACPI

static int __init
its_pci_msi_parse_madt(union acpi_subtable_headers *header,
		       const unsigned long end)
{
	struct acpi_madt_generic_translator *its_entry;
	struct fwnode_handle *dom_handle;
	const char *node_name;
	int err = -ENXIO;

	its_entry = (struct acpi_madt_generic_translator *)header;
	node_name = kasprintf(GFP_KERNEL, "ITS@0x%lx",
			      (long)its_entry->base_address);
	dom_handle = iort_find_domain_token(its_entry->translation_id);
	if (!dom_handle) {
		pr_err("%s: Unable to locate ITS domain handle\n", node_name);
		goto out;
	}

	err = its_pci_msi_init_one(dom_handle, node_name);
	if (!err)
		pr_info("PCI/MSI: %s domain created\n", node_name);

out:
	kfree(node_name);
	return err;
}

static int __init its_pci_acpi_msi_init(void)
{
	acpi_table_parse_madt(ACPI_MADT_TYPE_GENERIC_TRANSLATOR,
			      its_pci_msi_parse_madt, 0);
	return 0;
}
#else
static int __init its_pci_acpi_msi_init(void)
{
	return 0;
}
#endif

static int __init its_pci_msi_init(void)
{
	its_pci_of_msi_init();
	its_pci_acpi_msi_init();

	return 0;
}
early_initcall(its_pci_msi_init);
