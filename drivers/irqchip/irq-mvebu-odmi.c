/*
 * Copyright (C) 2016 Marvell
 *
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#define pr_fmt(fmt) "GIC-ODMI: " fmt

#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/msi.h>
#include <linux/of_address.h>
#include <linux/slab.h>

#include "irq-msi-lib.h"

#include <dt-bindings/interrupt-controller/arm-gic.h>

#define GICP_ODMIN_SET			0x40
#define   GICP_ODMI_INT_NUM_SHIFT	12
#define GICP_ODMIN_GM_EP_R0		0x110
#define GICP_ODMIN_GM_EP_R1		0x114
#define GICP_ODMIN_GM_EA_R0		0x108
#define GICP_ODMIN_GM_EA_R1		0x118

/*
 * We don't support the group events, so we simply have 8 interrupts
 * per frame.
 */
#define NODMIS_SHIFT		3
#define NODMIS_PER_FRAME	(1 << NODMIS_SHIFT)
#define NODMIS_MASK		(NODMIS_PER_FRAME - 1)

struct odmi_data {
	struct resource res;
	void __iomem *base;
	unsigned int spi_base;
};

static struct odmi_data *odmis;
static unsigned long *odmis_bm;
static unsigned int odmis_count;

/* Protects odmis_bm */
static DEFINE_SPINLOCK(odmis_bm_lock);

static void odmi_compose_msi_msg(struct irq_data *d, struct msi_msg *msg)
{
	struct odmi_data *odmi;
	phys_addr_t addr;
	unsigned int odmin;

	if (WARN_ON(d->hwirq >= odmis_count * NODMIS_PER_FRAME))
		return;

	odmi = &odmis[d->hwirq >> NODMIS_SHIFT];
	odmin = d->hwirq & NODMIS_MASK;

	addr = odmi->res.start + GICP_ODMIN_SET;

	msg->address_hi = upper_32_bits(addr);
	msg->address_lo = lower_32_bits(addr);
	msg->data = odmin << GICP_ODMI_INT_NUM_SHIFT;
}

static struct irq_chip odmi_irq_chip = {
	.name			= "ODMI",
	.irq_mask		= irq_chip_mask_parent,
	.irq_unmask		= irq_chip_unmask_parent,
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_set_affinity	= irq_chip_set_affinity_parent,
	.irq_compose_msi_msg	= odmi_compose_msi_msg,
};

static int odmi_irq_domain_alloc(struct irq_domain *domain, unsigned int virq,
				 unsigned int nr_irqs, void *args)
{
	struct odmi_data *odmi = NULL;
	struct irq_fwspec fwspec;
	struct irq_data *d;
	unsigned int hwirq, odmin;
	int ret;

	spin_lock(&odmis_bm_lock);
	hwirq = find_first_zero_bit(odmis_bm, NODMIS_PER_FRAME * odmis_count);
	if (hwirq >= NODMIS_PER_FRAME * odmis_count) {
		spin_unlock(&odmis_bm_lock);
		return -ENOSPC;
	}

	__set_bit(hwirq, odmis_bm);
	spin_unlock(&odmis_bm_lock);

	odmi = &odmis[hwirq >> NODMIS_SHIFT];
	odmin = hwirq & NODMIS_MASK;

	fwspec.fwnode = domain->parent->fwnode;
	fwspec.param_count = 3;
	fwspec.param[0] = GIC_SPI;
	fwspec.param[1] = odmi->spi_base - 32 + odmin;
	fwspec.param[2] = IRQ_TYPE_EDGE_RISING;

	ret = irq_domain_alloc_irqs_parent(domain, virq, 1, &fwspec);
	if (ret) {
		pr_err("Cannot allocate parent IRQ\n");
		spin_lock(&odmis_bm_lock);
		__clear_bit(odmin, odmis_bm);
		spin_unlock(&odmis_bm_lock);
		return ret;
	}

	/* Configure the interrupt line to be edge */
	d = irq_domain_get_irq_data(domain->parent, virq);
	d->chip->irq_set_type(d, IRQ_TYPE_EDGE_RISING);

	irq_domain_set_hwirq_and_chip(domain, virq, hwirq,
				      &odmi_irq_chip, NULL);

	return 0;
}

static void odmi_irq_domain_free(struct irq_domain *domain,
				 unsigned int virq, unsigned int nr_irqs)
{
	struct irq_data *d = irq_domain_get_irq_data(domain, virq);

	if (d->hwirq >= odmis_count * NODMIS_PER_FRAME) {
		pr_err("Failed to teardown msi. Invalid hwirq %lu\n", d->hwirq);
		return;
	}

	irq_domain_free_irqs_parent(domain, virq, nr_irqs);

	/* Actually free the MSI */
	spin_lock(&odmis_bm_lock);
	__clear_bit(d->hwirq, odmis_bm);
	spin_unlock(&odmis_bm_lock);
}

static const struct irq_domain_ops odmi_domain_ops = {
	.select	= msi_lib_irq_domain_select,
	.alloc	= odmi_irq_domain_alloc,
	.free	= odmi_irq_domain_free,
};

#define ODMI_MSI_FLAGS_REQUIRED  (MSI_FLAG_USE_DEF_DOM_OPS |	\
				  MSI_FLAG_USE_DEF_CHIP_OPS)

#define ODMI_MSI_FLAGS_SUPPORTED (MSI_GENERIC_FLAGS_MASK)

static const struct msi_parent_ops odmi_msi_parent_ops = {
	.supported_flags	= ODMI_MSI_FLAGS_SUPPORTED,
	.required_flags		= ODMI_MSI_FLAGS_REQUIRED,
	.bus_select_token	= DOMAIN_BUS_GENERIC_MSI,
	.bus_select_mask	= MATCH_PLATFORM_MSI,
	.prefix			= "ODMI-",
	.init_dev_msi_info	= msi_lib_init_dev_msi_info,
};

static int __init mvebu_odmi_init(struct device_node *node,
				  struct device_node *parent)
{
	struct irq_domain *parent_domain, *inner_domain;
	int ret, i;

	if (of_property_read_u32(node, "marvell,odmi-frames", &odmis_count))
		return -EINVAL;

	odmis = kcalloc(odmis_count, sizeof(struct odmi_data), GFP_KERNEL);
	if (!odmis)
		return -ENOMEM;

	odmis_bm = bitmap_zalloc(odmis_count * NODMIS_PER_FRAME, GFP_KERNEL);
	if (!odmis_bm) {
		ret = -ENOMEM;
		goto err_alloc;
	}

	for (i = 0; i < odmis_count; i++) {
		struct odmi_data *odmi = &odmis[i];

		ret = of_address_to_resource(node, i, &odmi->res);
		if (ret)
			goto err_unmap;

		odmi->base = of_io_request_and_map(node, i, "odmi");
		if (IS_ERR(odmi->base)) {
			ret = PTR_ERR(odmi->base);
			goto err_unmap;
		}

		if (of_property_read_u32_index(node, "marvell,spi-base",
					       i, &odmi->spi_base)) {
			ret = -EINVAL;
			goto err_unmap;
		}
	}

	parent_domain = irq_find_host(parent);

	inner_domain = irq_domain_create_hierarchy(parent_domain, 0,
						   odmis_count * NODMIS_PER_FRAME,
						   of_node_to_fwnode(node),
						   &odmi_domain_ops, NULL);
	if (!inner_domain) {
		ret = -ENOMEM;
		goto err_unmap;
	}

	irq_domain_update_bus_token(inner_domain, DOMAIN_BUS_GENERIC_MSI);
	inner_domain->flags |= IRQ_DOMAIN_FLAG_MSI_PARENT;
	inner_domain->msi_parent_ops = &odmi_msi_parent_ops;

	return 0;

err_unmap:
	for (i = 0; i < odmis_count; i++) {
		struct odmi_data *odmi = &odmis[i];

		if (odmi->base && !IS_ERR(odmi->base))
			iounmap(odmis[i].base);
	}
	bitmap_free(odmis_bm);
err_alloc:
	kfree(odmis);
	return ret;
}

IRQCHIP_DECLARE(mvebu_odmi, "marvell,odmi-controller", mvebu_odmi_init);
