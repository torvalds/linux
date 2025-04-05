// SPDX-License-Identifier: GPL-2.0
/*
 * SG2042 MSI Controller
 *
 * Copyright (C) 2024 Sophgo Technology Inc.
 * Copyright (C) 2024 Chen Wang <unicorn_wang@outlook.com>
 */

#include <linux/cleanup.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>

#include "irq-msi-lib.h"

#define SG2042_MAX_MSI_VECTOR	32

struct sg2042_msi_chipdata {
	void __iomem	*reg_clr;	// clear reg, see TRM, 10.1.33, GP_INTR0_CLR

	phys_addr_t	doorbell_addr;	// see TRM, 10.1.32, GP_INTR0_SET

	u32		irq_first;	// The vector number that MSIs starts
	u32		num_irqs;	// The number of vectors for MSIs

	DECLARE_BITMAP(msi_map, SG2042_MAX_MSI_VECTOR);
	struct mutex	msi_map_lock;	// lock for msi_map
};

static int sg2042_msi_allocate_hwirq(struct sg2042_msi_chipdata *data, int num_req)
{
	int first;

	guard(mutex)(&data->msi_map_lock);
	first = bitmap_find_free_region(data->msi_map, data->num_irqs,
					get_count_order(num_req));
	return first >= 0 ? first : -ENOSPC;
}

static void sg2042_msi_free_hwirq(struct sg2042_msi_chipdata *data, int hwirq, int num_req)
{
	guard(mutex)(&data->msi_map_lock);
	bitmap_release_region(data->msi_map, hwirq, get_count_order(num_req));
}

static void sg2042_msi_irq_ack(struct irq_data *d)
{
	struct sg2042_msi_chipdata *data  = irq_data_get_irq_chip_data(d);
	int bit_off = d->hwirq;

	writel(1 << bit_off, data->reg_clr);

	irq_chip_ack_parent(d);
}

static void sg2042_msi_irq_compose_msi_msg(struct irq_data *d, struct msi_msg *msg)
{
	struct sg2042_msi_chipdata *data = irq_data_get_irq_chip_data(d);

	msg->address_hi = upper_32_bits(data->doorbell_addr);
	msg->address_lo = lower_32_bits(data->doorbell_addr);
	msg->data = 1 << d->hwirq;
}

static const struct irq_chip sg2042_msi_middle_irq_chip = {
	.name			= "SG2042 MSI",
	.irq_ack		= sg2042_msi_irq_ack,
	.irq_mask		= irq_chip_mask_parent,
	.irq_unmask		= irq_chip_unmask_parent,
#ifdef CONFIG_SMP
	.irq_set_affinity	= irq_chip_set_affinity_parent,
#endif
	.irq_compose_msi_msg	= sg2042_msi_irq_compose_msi_msg,
};

static int sg2042_msi_parent_domain_alloc(struct irq_domain *domain, unsigned int virq, int hwirq)
{
	struct sg2042_msi_chipdata *data = domain->host_data;
	struct irq_fwspec fwspec;
	struct irq_data *d;
	int ret;

	fwspec.fwnode = domain->parent->fwnode;
	fwspec.param_count = 2;
	fwspec.param[0] = data->irq_first + hwirq;
	fwspec.param[1] = IRQ_TYPE_EDGE_RISING;

	ret = irq_domain_alloc_irqs_parent(domain, virq, 1, &fwspec);
	if (ret)
		return ret;

	d = irq_domain_get_irq_data(domain->parent, virq);
	return d->chip->irq_set_type(d, IRQ_TYPE_EDGE_RISING);
}

static int sg2042_msi_middle_domain_alloc(struct irq_domain *domain, unsigned int virq,
					  unsigned int nr_irqs, void *args)
{
	struct sg2042_msi_chipdata *data = domain->host_data;
	int hwirq, err, i;

	hwirq = sg2042_msi_allocate_hwirq(data, nr_irqs);
	if (hwirq < 0)
		return hwirq;

	for (i = 0; i < nr_irqs; i++) {
		err = sg2042_msi_parent_domain_alloc(domain, virq + i, hwirq + i);
		if (err)
			goto err_hwirq;

		irq_domain_set_hwirq_and_chip(domain, virq + i, hwirq + i,
					      &sg2042_msi_middle_irq_chip, data);
	}

	return 0;

err_hwirq:
	sg2042_msi_free_hwirq(data, hwirq, nr_irqs);
	irq_domain_free_irqs_parent(domain, virq, i);

	return err;
}

static void sg2042_msi_middle_domain_free(struct irq_domain *domain, unsigned int virq,
					  unsigned int nr_irqs)
{
	struct irq_data *d = irq_domain_get_irq_data(domain, virq);
	struct sg2042_msi_chipdata *data = irq_data_get_irq_chip_data(d);

	irq_domain_free_irqs_parent(domain, virq, nr_irqs);
	sg2042_msi_free_hwirq(data, d->hwirq, nr_irqs);
}

static const struct irq_domain_ops sg2042_msi_middle_domain_ops = {
	.alloc	= sg2042_msi_middle_domain_alloc,
	.free	= sg2042_msi_middle_domain_free,
	.select	= msi_lib_irq_domain_select,
};

#define SG2042_MSI_FLAGS_REQUIRED (MSI_FLAG_USE_DEF_DOM_OPS |	\
				   MSI_FLAG_USE_DEF_CHIP_OPS)

#define SG2042_MSI_FLAGS_SUPPORTED MSI_GENERIC_FLAGS_MASK

static const struct msi_parent_ops sg2042_msi_parent_ops = {
	.required_flags		= SG2042_MSI_FLAGS_REQUIRED,
	.supported_flags	= SG2042_MSI_FLAGS_SUPPORTED,
	.chip_flags		= MSI_CHIP_FLAG_SET_ACK,
	.bus_select_mask	= MATCH_PCI_MSI,
	.bus_select_token	= DOMAIN_BUS_NEXUS,
	.prefix			= "SG2042-",
	.init_dev_msi_info	= msi_lib_init_dev_msi_info,
};

static int sg2042_msi_init_domains(struct sg2042_msi_chipdata *data,
				   struct irq_domain *plic_domain, struct device *dev)
{
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	struct irq_domain *middle_domain;

	middle_domain = irq_domain_create_hierarchy(plic_domain, 0, data->num_irqs, fwnode,
						    &sg2042_msi_middle_domain_ops, data);
	if (!middle_domain) {
		pr_err("Failed to create the MSI middle domain\n");
		return -ENOMEM;
	}

	irq_domain_update_bus_token(middle_domain, DOMAIN_BUS_NEXUS);

	middle_domain->flags |= IRQ_DOMAIN_FLAG_MSI_PARENT;
	middle_domain->msi_parent_ops = &sg2042_msi_parent_ops;

	return 0;
}

static int sg2042_msi_probe(struct platform_device *pdev)
{
	struct fwnode_reference_args args = { };
	struct sg2042_msi_chipdata *data;
	struct device *dev = &pdev->dev;
	struct irq_domain *plic_domain;
	struct resource *res;
	int ret;

	data = devm_kzalloc(dev, sizeof(struct sg2042_msi_chipdata), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->reg_clr = devm_platform_ioremap_resource_byname(pdev, "clr");
	if (IS_ERR(data->reg_clr)) {
		dev_err(dev, "Failed to map clear register\n");
		return PTR_ERR(data->reg_clr);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "doorbell");
	if (!res) {
		dev_err(dev, "Failed get resource from set\n");
		return -EINVAL;
	}
	data->doorbell_addr = res->start;

	ret = fwnode_property_get_reference_args(dev_fwnode(dev), "msi-ranges",
						 "#interrupt-cells", 0, 0, &args);
	if (ret) {
		dev_err(dev, "Unable to parse MSI vec base\n");
		return ret;
	}
	fwnode_handle_put(args.fwnode);

	ret = fwnode_property_get_reference_args(dev_fwnode(dev), "msi-ranges", NULL,
						 args.nargs + 1, 0, &args);
	if (ret) {
		dev_err(dev, "Unable to parse MSI vec number\n");
		return ret;
	}

	plic_domain = irq_find_matching_fwnode(args.fwnode, DOMAIN_BUS_ANY);
	fwnode_handle_put(args.fwnode);
	if (!plic_domain) {
		pr_err("Failed to find the PLIC domain\n");
		return -ENXIO;
	}

	data->irq_first = (u32)args.args[0];
	data->num_irqs = (u32)args.args[args.nargs - 1];

	mutex_init(&data->msi_map_lock);

	return sg2042_msi_init_domains(data, plic_domain, dev);
}

static const struct of_device_id sg2042_msi_of_match[] = {
	{ .compatible	= "sophgo,sg2042-msi" },
	{ }
};

static struct platform_driver sg2042_msi_driver = {
	.driver = {
		.name		= "sg2042-msi",
		.of_match_table	= sg2042_msi_of_match,
	},
	.probe = sg2042_msi_probe,
};
builtin_platform_driver(sg2042_msi_driver);
