/*
 * Copyright (C) 2017 Marvell
 *
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/msi.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include <dt-bindings/interrupt-controller/arm-gic.h>

#define GICP_SETSPI_NSR_OFFSET	0x0
#define GICP_CLRSPI_NSR_OFFSET	0x8

struct mvebu_gicp_spi_range {
	unsigned int start;
	unsigned int count;
};

struct mvebu_gicp {
	struct mvebu_gicp_spi_range *spi_ranges;
	unsigned int spi_ranges_cnt;
	unsigned int spi_cnt;
	unsigned long *spi_bitmap;
	spinlock_t spi_lock;
	struct resource *res;
	struct device *dev;
};

static int gicp_idx_to_spi(struct mvebu_gicp *gicp, int idx)
{
	int i;

	for (i = 0; i < gicp->spi_ranges_cnt; i++) {
		struct mvebu_gicp_spi_range *r = &gicp->spi_ranges[i];

		if (idx < r->count)
			return r->start + idx;

		idx -= r->count;
	}

	return -EINVAL;
}

static void gicp_compose_msi_msg(struct irq_data *data, struct msi_msg *msg)
{
	struct mvebu_gicp *gicp = data->chip_data;
	phys_addr_t setspi = gicp->res->start + GICP_SETSPI_NSR_OFFSET;
	phys_addr_t clrspi = gicp->res->start + GICP_CLRSPI_NSR_OFFSET;

	msg[0].data = data->hwirq;
	msg[0].address_lo = lower_32_bits(setspi);
	msg[0].address_hi = upper_32_bits(setspi);
	msg[1].data = data->hwirq;
	msg[1].address_lo = lower_32_bits(clrspi);
	msg[1].address_hi = upper_32_bits(clrspi);
}

static struct irq_chip gicp_irq_chip = {
	.name			= "GICP",
	.irq_mask		= irq_chip_mask_parent,
	.irq_unmask		= irq_chip_unmask_parent,
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_set_affinity	= irq_chip_set_affinity_parent,
	.irq_set_type		= irq_chip_set_type_parent,
	.irq_compose_msi_msg	= gicp_compose_msi_msg,
};

static int gicp_irq_domain_alloc(struct irq_domain *domain, unsigned int virq,
				 unsigned int nr_irqs, void *args)
{
	struct mvebu_gicp *gicp = domain->host_data;
	struct irq_fwspec fwspec;
	unsigned int hwirq;
	int ret;

	spin_lock(&gicp->spi_lock);
	hwirq = find_first_zero_bit(gicp->spi_bitmap, gicp->spi_cnt);
	if (hwirq == gicp->spi_cnt) {
		spin_unlock(&gicp->spi_lock);
		return -ENOSPC;
	}
	__set_bit(hwirq, gicp->spi_bitmap);
	spin_unlock(&gicp->spi_lock);

	fwspec.fwnode = domain->parent->fwnode;
	fwspec.param_count = 3;
	fwspec.param[0] = GIC_SPI;
	fwspec.param[1] = gicp_idx_to_spi(gicp, hwirq) - 32;
	/*
	 * Assume edge rising for now, it will be properly set when
	 * ->set_type() is called
	 */
	fwspec.param[2] = IRQ_TYPE_EDGE_RISING;

	ret = irq_domain_alloc_irqs_parent(domain, virq, 1, &fwspec);
	if (ret) {
		dev_err(gicp->dev, "Cannot allocate parent IRQ\n");
		goto free_hwirq;
	}

	ret = irq_domain_set_hwirq_and_chip(domain, virq, hwirq,
					    &gicp_irq_chip, gicp);
	if (ret)
		goto free_irqs_parent;

	return 0;

free_irqs_parent:
	irq_domain_free_irqs_parent(domain, virq, nr_irqs);
free_hwirq:
	spin_lock(&gicp->spi_lock);
	__clear_bit(hwirq, gicp->spi_bitmap);
	spin_unlock(&gicp->spi_lock);
	return ret;
}

static void gicp_irq_domain_free(struct irq_domain *domain,
				 unsigned int virq, unsigned int nr_irqs)
{
	struct mvebu_gicp *gicp = domain->host_data;
	struct irq_data *d = irq_domain_get_irq_data(domain, virq);

	if (d->hwirq >= gicp->spi_cnt) {
		dev_err(gicp->dev, "Invalid hwirq %lu\n", d->hwirq);
		return;
	}

	irq_domain_free_irqs_parent(domain, virq, nr_irqs);

	spin_lock(&gicp->spi_lock);
	__clear_bit(d->hwirq, gicp->spi_bitmap);
	spin_unlock(&gicp->spi_lock);
}

static const struct irq_domain_ops gicp_domain_ops = {
	.alloc	= gicp_irq_domain_alloc,
	.free	= gicp_irq_domain_free,
};

static struct irq_chip gicp_msi_irq_chip = {
	.name		= "GICP",
	.irq_set_type	= irq_chip_set_type_parent,
	.flags		= IRQCHIP_SUPPORTS_LEVEL_MSI,
};

static struct msi_domain_ops gicp_msi_ops = {
};

static struct msi_domain_info gicp_msi_domain_info = {
	.flags	= (MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
		   MSI_FLAG_LEVEL_CAPABLE),
	.ops	= &gicp_msi_ops,
	.chip	= &gicp_msi_irq_chip,
};

static int mvebu_gicp_probe(struct platform_device *pdev)
{
	struct mvebu_gicp *gicp;
	struct irq_domain *inner_domain, *plat_domain, *parent_domain;
	struct device_node *node = pdev->dev.of_node;
	struct device_node *irq_parent_dn;
	int ret, i;

	gicp = devm_kzalloc(&pdev->dev, sizeof(*gicp), GFP_KERNEL);
	if (!gicp)
		return -ENOMEM;

	gicp->dev = &pdev->dev;
	spin_lock_init(&gicp->spi_lock);

	gicp->res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!gicp->res)
		return -ENODEV;

	ret = of_property_count_u32_elems(node, "marvell,spi-ranges");
	if (ret < 0)
		return ret;

	gicp->spi_ranges_cnt = ret / 2;

	gicp->spi_ranges =
		devm_kcalloc(&pdev->dev,
			     gicp->spi_ranges_cnt,
			     sizeof(struct mvebu_gicp_spi_range),
			     GFP_KERNEL);
	if (!gicp->spi_ranges)
		return -ENOMEM;

	for (i = 0; i < gicp->spi_ranges_cnt; i++) {
		of_property_read_u32_index(node, "marvell,spi-ranges",
					   i * 2,
					   &gicp->spi_ranges[i].start);

		of_property_read_u32_index(node, "marvell,spi-ranges",
					   i * 2 + 1,
					   &gicp->spi_ranges[i].count);

		gicp->spi_cnt += gicp->spi_ranges[i].count;
	}

	gicp->spi_bitmap = devm_kcalloc(&pdev->dev,
				BITS_TO_LONGS(gicp->spi_cnt), sizeof(long),
				GFP_KERNEL);
	if (!gicp->spi_bitmap)
		return -ENOMEM;

	irq_parent_dn = of_irq_find_parent(node);
	if (!irq_parent_dn) {
		dev_err(&pdev->dev, "failed to find parent IRQ node\n");
		return -ENODEV;
	}

	parent_domain = irq_find_host(irq_parent_dn);
	if (!parent_domain) {
		dev_err(&pdev->dev, "failed to find parent IRQ domain\n");
		return -ENODEV;
	}

	inner_domain = irq_domain_create_hierarchy(parent_domain, 0,
						   gicp->spi_cnt,
						   of_node_to_fwnode(node),
						   &gicp_domain_ops, gicp);
	if (!inner_domain)
		return -ENOMEM;


	plat_domain = platform_msi_create_irq_domain(of_node_to_fwnode(node),
						     &gicp_msi_domain_info,
						     inner_domain);
	if (!plat_domain) {
		irq_domain_remove(inner_domain);
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, gicp);

	return 0;
}

static const struct of_device_id mvebu_gicp_of_match[] = {
	{ .compatible = "marvell,ap806-gicp", },
	{},
};

static struct platform_driver mvebu_gicp_driver = {
	.probe  = mvebu_gicp_probe,
	.driver = {
		.name = "mvebu-gicp",
		.of_match_table = mvebu_gicp_of_match,
	},
};
builtin_platform_driver(mvebu_gicp_driver);
