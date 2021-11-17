// SPDX-License-Identifier: GPL-2.0
/*
 * Texas Instruments' K3 Interrupt Aggregator MSI bus
 *
 * Copyright (C) 2018-2019 Texas Instruments Incorporated - http://www.ti.com/
 *	Lokesh Vutla <lokeshvutla@ti.com>
 */

#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/msi.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/soc/ti/ti_sci_inta_msi.h>
#include <linux/soc/ti/ti_sci_protocol.h>

static void ti_sci_inta_msi_write_msg(struct irq_data *data,
				      struct msi_msg *msg)
{
	/* Nothing to do */
}

static void ti_sci_inta_msi_compose_msi_msg(struct irq_data *data,
					    struct msi_msg *msg)
{
	/* Nothing to do */
}

static void ti_sci_inta_msi_update_chip_ops(struct msi_domain_info *info)
{
	struct irq_chip *chip = info->chip;

	if (WARN_ON(!chip))
		return;

	chip->irq_request_resources = irq_chip_request_resources_parent;
	chip->irq_release_resources = irq_chip_release_resources_parent;
	chip->irq_compose_msi_msg = ti_sci_inta_msi_compose_msi_msg;
	chip->irq_write_msi_msg = ti_sci_inta_msi_write_msg;
	chip->irq_set_type = irq_chip_set_type_parent;
	chip->irq_unmask = irq_chip_unmask_parent;
	chip->irq_mask = irq_chip_mask_parent;
	chip->irq_ack = irq_chip_ack_parent;
}

struct irq_domain *ti_sci_inta_msi_create_irq_domain(struct fwnode_handle *fwnode,
						     struct msi_domain_info *info,
						     struct irq_domain *parent)
{
	struct irq_domain *domain;

	ti_sci_inta_msi_update_chip_ops(info);

	domain = msi_create_irq_domain(fwnode, info, parent);
	if (domain)
		irq_domain_update_bus_token(domain, DOMAIN_BUS_TI_SCI_INTA_MSI);

	return domain;
}
EXPORT_SYMBOL_GPL(ti_sci_inta_msi_create_irq_domain);

static void ti_sci_inta_msi_free_descs(struct device *dev)
{
	struct msi_desc *desc, *tmp;

	list_for_each_entry_safe(desc, tmp, dev_to_msi_list(dev), list) {
		list_del(&desc->list);
		free_msi_entry(desc);
	}
}

static int ti_sci_inta_msi_alloc_descs(struct device *dev,
				       struct ti_sci_resource *res)
{
	struct msi_desc *msi_desc;
	int set, i, count = 0;

	for (set = 0; set < res->sets; set++) {
		for (i = 0; i < res->desc[set].num; i++) {
			msi_desc = alloc_msi_entry(dev, 1, NULL);
			if (!msi_desc) {
				ti_sci_inta_msi_free_descs(dev);
				return -ENOMEM;
			}

			msi_desc->inta.dev_index = res->desc[set].start + i;
			INIT_LIST_HEAD(&msi_desc->list);
			list_add_tail(&msi_desc->list, dev_to_msi_list(dev));
			count++;
		}
		for (i = 0; i < res->desc[set].num_sec; i++) {
			msi_desc = alloc_msi_entry(dev, 1, NULL);
			if (!msi_desc) {
				ti_sci_inta_msi_free_descs(dev);
				return -ENOMEM;
			}

			msi_desc->inta.dev_index = res->desc[set].start_sec + i;
			INIT_LIST_HEAD(&msi_desc->list);
			list_add_tail(&msi_desc->list, dev_to_msi_list(dev));
			count++;
		}
	}

	return count;
}

int ti_sci_inta_msi_domain_alloc_irqs(struct device *dev,
				      struct ti_sci_resource *res)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct irq_domain *msi_domain;
	int ret, nvec;

	msi_domain = dev_get_msi_domain(dev);
	if (!msi_domain)
		return -EINVAL;

	if (pdev->id < 0)
		return -ENODEV;

	nvec = ti_sci_inta_msi_alloc_descs(dev, res);
	if (nvec <= 0)
		return nvec;

	ret = msi_domain_alloc_irqs(msi_domain, dev, nvec);
	if (ret) {
		dev_err(dev, "Failed to allocate IRQs %d\n", ret);
		goto cleanup;
	}

	return 0;

cleanup:
	ti_sci_inta_msi_free_descs(&pdev->dev);
	return ret;
}
EXPORT_SYMBOL_GPL(ti_sci_inta_msi_domain_alloc_irqs);

void ti_sci_inta_msi_domain_free_irqs(struct device *dev)
{
	msi_domain_free_irqs(dev->msi_domain, dev);
	ti_sci_inta_msi_free_descs(dev);
}
EXPORT_SYMBOL_GPL(ti_sci_inta_msi_domain_free_irqs);

unsigned int ti_sci_inta_msi_get_virq(struct device *dev, u32 dev_index)
{
	struct msi_desc *desc;

	for_each_msi_entry(desc, dev)
		if (desc->inta.dev_index == dev_index)
			return desc->irq;

	return -ENODEV;
}
EXPORT_SYMBOL_GPL(ti_sci_inta_msi_get_virq);
