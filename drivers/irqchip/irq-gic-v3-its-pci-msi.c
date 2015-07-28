/*
 * Copyright (C) 2013-2015 ARM Limited, All Rights Reserved.
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/msi.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>

#include <linux/irqchip/arm-gic-v3.h>

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

struct its_pci_alias {
	struct pci_dev	*pdev;
	u32		dev_id;
	u32		count;
};

static int its_pci_msi_vec_count(struct pci_dev *pdev)
{
	int msi, msix;

	msi = max(pci_msi_vec_count(pdev), 0);
	msix = max(pci_msix_vec_count(pdev), 0);

	return max(msi, msix);
}

static int its_get_pci_alias(struct pci_dev *pdev, u16 alias, void *data)
{
	struct its_pci_alias *dev_alias = data;

	dev_alias->dev_id = alias;
	if (pdev != dev_alias->pdev)
		dev_alias->count += its_pci_msi_vec_count(dev_alias->pdev);

	return 0;
}

static int its_pci_msi_prepare(struct irq_domain *domain, struct device *dev,
			       int nvec, msi_alloc_info_t *info)
{
	struct pci_dev *pdev;
	struct its_pci_alias dev_alias;

	if (!dev_is_pci(dev))
		return -EINVAL;

	pdev = to_pci_dev(dev);
	dev_alias.pdev = pdev;
	dev_alias.count = nvec;

	pci_for_each_dma_alias(pdev, its_get_pci_alias, &dev_alias);

	return its_msi_prepare(domain, dev_alias.dev_id, dev_alias.count, info);
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

struct irq_domain *its_pci_msi_alloc_domain(struct device_node *np,
					    struct irq_domain *parent)
{
	return pci_msi_create_irq_domain(np, &its_pci_msi_domain_info, parent);
}
