/*
 * Altera PCIe MSI support
 *
 * Author: Ley Foon Tan <lftan@altera.com>
 *
 * Copyright Altera Corporation (C) 2013-2015. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/interrupt.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/init.h>
#include <linux/msi.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define MSI_STATUS		0x0
#define MSI_ERROR		0x4
#define MSI_INTMASK		0x8

#define MAX_MSI_VECTORS		32

struct altera_msi {
	DECLARE_BITMAP(used, MAX_MSI_VECTORS);
	struct mutex		lock;	/* protect "used" bitmap */
	struct platform_device	*pdev;
	struct irq_domain	*msi_domain;
	struct irq_domain	*inner_domain;
	void __iomem		*csr_base;
	void __iomem		*vector_base;
	phys_addr_t		vector_phy;
	u32			num_of_vectors;
	int			irq;
};

static inline void msi_writel(struct altera_msi *msi, const u32 value,
			      const u32 reg)
{
	writel_relaxed(value, msi->csr_base + reg);
}

static inline u32 msi_readl(struct altera_msi *msi, const u32 reg)
{
	return readl_relaxed(msi->csr_base + reg);
}

static void altera_msi_isr(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct altera_msi *msi;
	unsigned long status;
	u32 bit;
	u32 virq;

	chained_irq_enter(chip, desc);
	msi = irq_desc_get_handler_data(desc);

	while ((status = msi_readl(msi, MSI_STATUS)) != 0) {
		for_each_set_bit(bit, &status, msi->num_of_vectors) {
			/* Dummy read from vector to clear the interrupt */
			readl_relaxed(msi->vector_base + (bit * sizeof(u32)));

			virq = irq_find_mapping(msi->inner_domain, bit);
			if (virq)
				generic_handle_irq(virq);
			else
				dev_err(&msi->pdev->dev, "unexpected MSI\n");
		}
	}

	chained_irq_exit(chip, desc);
}

static struct irq_chip altera_msi_irq_chip = {
	.name = "Altera PCIe MSI",
	.irq_mask = pci_msi_mask_irq,
	.irq_unmask = pci_msi_unmask_irq,
};

static struct msi_domain_info altera_msi_domain_info = {
	.flags	= (MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
		     MSI_FLAG_PCI_MSIX),
	.chip	= &altera_msi_irq_chip,
};

static void altera_compose_msi_msg(struct irq_data *data, struct msi_msg *msg)
{
	struct altera_msi *msi = irq_data_get_irq_chip_data(data);
	phys_addr_t addr = msi->vector_phy + (data->hwirq * sizeof(u32));

	msg->address_lo = lower_32_bits(addr);
	msg->address_hi = upper_32_bits(addr);
	msg->data = data->hwirq;

	dev_dbg(&msi->pdev->dev, "msi#%d address_hi %#x address_lo %#x\n",
		(int)data->hwirq, msg->address_hi, msg->address_lo);
}

static int altera_msi_set_affinity(struct irq_data *irq_data,
				   const struct cpumask *mask, bool force)
{
	 return -EINVAL;
}

static struct irq_chip altera_msi_bottom_irq_chip = {
	.name			= "Altera MSI",
	.irq_compose_msi_msg	= altera_compose_msi_msg,
	.irq_set_affinity	= altera_msi_set_affinity,
};

static int altera_irq_domain_alloc(struct irq_domain *domain, unsigned int virq,
				   unsigned int nr_irqs, void *args)
{
	struct altera_msi *msi = domain->host_data;
	unsigned long bit;
	u32 mask;

	WARN_ON(nr_irqs != 1);
	mutex_lock(&msi->lock);

	bit = find_first_zero_bit(msi->used, msi->num_of_vectors);
	if (bit >= msi->num_of_vectors) {
		mutex_unlock(&msi->lock);
		return -ENOSPC;
	}

	set_bit(bit, msi->used);

	mutex_unlock(&msi->lock);

	irq_domain_set_info(domain, virq, bit, &altera_msi_bottom_irq_chip,
			    domain->host_data, handle_simple_irq,
			    NULL, NULL);

	mask = msi_readl(msi, MSI_INTMASK);
	mask |= 1 << bit;
	msi_writel(msi, mask, MSI_INTMASK);

	return 0;
}

static void altera_irq_domain_free(struct irq_domain *domain,
				   unsigned int virq, unsigned int nr_irqs)
{
	struct irq_data *d = irq_domain_get_irq_data(domain, virq);
	struct altera_msi *msi = irq_data_get_irq_chip_data(d);
	u32 mask;

	mutex_lock(&msi->lock);

	if (!test_bit(d->hwirq, msi->used)) {
		dev_err(&msi->pdev->dev, "trying to free unused MSI#%lu\n",
			d->hwirq);
	} else {
		__clear_bit(d->hwirq, msi->used);
		mask = msi_readl(msi, MSI_INTMASK);
		mask &= ~(1 << d->hwirq);
		msi_writel(msi, mask, MSI_INTMASK);
	}

	mutex_unlock(&msi->lock);
}

static const struct irq_domain_ops msi_domain_ops = {
	.alloc	= altera_irq_domain_alloc,
	.free	= altera_irq_domain_free,
};

static int altera_allocate_domains(struct altera_msi *msi)
{
	struct fwnode_handle *fwnode = of_node_to_fwnode(msi->pdev->dev.of_node);

	msi->inner_domain = irq_domain_add_linear(NULL, msi->num_of_vectors,
					     &msi_domain_ops, msi);
	if (!msi->inner_domain) {
		dev_err(&msi->pdev->dev, "failed to create IRQ domain\n");
		return -ENOMEM;
	}

	msi->msi_domain = pci_msi_create_irq_domain(fwnode,
				&altera_msi_domain_info, msi->inner_domain);
	if (!msi->msi_domain) {
		dev_err(&msi->pdev->dev, "failed to create MSI domain\n");
		irq_domain_remove(msi->inner_domain);
		return -ENOMEM;
	}

	return 0;
}

static void altera_free_domains(struct altera_msi *msi)
{
	irq_domain_remove(msi->msi_domain);
	irq_domain_remove(msi->inner_domain);
}

static int altera_msi_remove(struct platform_device *pdev)
{
	struct altera_msi *msi = platform_get_drvdata(pdev);

	msi_writel(msi, 0, MSI_INTMASK);
	irq_set_chained_handler(msi->irq, NULL);
	irq_set_handler_data(msi->irq, NULL);

	altera_free_domains(msi);

	platform_set_drvdata(pdev, NULL);
	return 0;
}

static int altera_msi_probe(struct platform_device *pdev)
{
	struct altera_msi *msi;
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;
	int ret;

	msi = devm_kzalloc(&pdev->dev, sizeof(struct altera_msi),
			   GFP_KERNEL);
	if (!msi)
		return -ENOMEM;

	mutex_init(&msi->lock);
	msi->pdev = pdev;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "csr");
	msi->csr_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(msi->csr_base)) {
		dev_err(&pdev->dev, "failed to map csr memory\n");
		return PTR_ERR(msi->csr_base);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "vector_slave");
	msi->vector_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(msi->vector_base)) {
		dev_err(&pdev->dev, "failed to map vector_slave memory\n");
		return PTR_ERR(msi->vector_base);
	}

	msi->vector_phy = res->start;

	if (of_property_read_u32(np, "num-vectors", &msi->num_of_vectors)) {
		dev_err(&pdev->dev, "failed to parse the number of vectors\n");
		return -EINVAL;
	}

	ret = altera_allocate_domains(msi);
	if (ret)
		return ret;

	msi->irq = platform_get_irq(pdev, 0);
	if (msi->irq <= 0) {
		dev_err(&pdev->dev, "failed to map IRQ: %d\n", msi->irq);
		ret = -ENODEV;
		goto err;
	}

	irq_set_chained_handler_and_data(msi->irq, altera_msi_isr, msi);
	platform_set_drvdata(pdev, msi);

	return 0;

err:
	altera_msi_remove(pdev);
	return ret;
}

static const struct of_device_id altera_msi_of_match[] = {
	{ .compatible = "altr,msi-1.0", NULL },
	{ },
};

static struct platform_driver altera_msi_driver = {
	.driver = {
		.name = "altera-msi",
		.of_match_table = altera_msi_of_match,
	},
	.probe = altera_msi_probe,
	.remove = altera_msi_remove,
};

static int __init altera_msi_init(void)
{
	return platform_driver_register(&altera_msi_driver);
}
subsys_initcall(altera_msi_init);
