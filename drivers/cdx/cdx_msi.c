// SPDX-License-Identifier: GPL-2.0
/*
 * AMD CDX bus driver MSI support
 *
 * Copyright (C) 2022-2023, Advanced Micro Devices, Inc.
 */

#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/msi.h>
#include <linux/cdx/cdx_bus.h>

#include "cdx.h"

static void cdx_msi_write_msg(struct irq_data *irq_data, struct msi_msg *msg)
{
	struct msi_desc *msi_desc = irq_data_get_msi_desc(irq_data);
	struct cdx_device *cdx_dev = to_cdx_device(msi_desc->dev);

	/* We would not operate on msg here rather we wait for irq_bus_sync_unlock()
	 * to be called from preemptible task context.
	 */
	msi_desc->msg = *msg;
	cdx_dev->msi_write_pending = true;
}

static void cdx_msi_write_irq_lock(struct irq_data *irq_data)
{
	struct msi_desc *msi_desc = irq_data_get_msi_desc(irq_data);
	struct cdx_device *cdx_dev = to_cdx_device(msi_desc->dev);

	mutex_lock(&cdx_dev->irqchip_lock);
}

static void cdx_msi_write_irq_unlock(struct irq_data *irq_data)
{
	struct msi_desc *msi_desc = irq_data_get_msi_desc(irq_data);
	struct cdx_device *cdx_dev = to_cdx_device(msi_desc->dev);
	struct cdx_controller *cdx = cdx_dev->cdx;
	struct cdx_device_config dev_config;

	if (!cdx_dev->msi_write_pending) {
		mutex_unlock(&cdx_dev->irqchip_lock);
		return;
	}

	cdx_dev->msi_write_pending = false;
	mutex_unlock(&cdx_dev->irqchip_lock);

	dev_config.msi.msi_index = msi_desc->msi_index;
	dev_config.msi.data = msi_desc->msg.data;
	dev_config.msi.addr = ((u64)(msi_desc->msg.address_hi) << 32) | msi_desc->msg.address_lo;

	/*
	 * dev_configure() is a controller callback which can interact with
	 * Firmware or other entities, and can sleep, so invoke this function
	 * outside of the mutex held region.
	 */
	dev_config.type = CDX_DEV_MSI_CONF;
	if (cdx->ops->dev_configure)
		cdx->ops->dev_configure(cdx, cdx_dev->bus_num, cdx_dev->dev_num, &dev_config);
}

int cdx_enable_msi(struct cdx_device *cdx_dev)
{
	struct cdx_controller *cdx = cdx_dev->cdx;
	struct cdx_device_config dev_config;

	dev_config.type = CDX_DEV_MSI_ENABLE;
	dev_config.msi_enable = true;
	if (cdx->ops->dev_configure) {
		return cdx->ops->dev_configure(cdx, cdx_dev->bus_num, cdx_dev->dev_num,
					       &dev_config);
	}

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(cdx_enable_msi);

void cdx_disable_msi(struct cdx_device *cdx_dev)
{
	struct cdx_controller *cdx = cdx_dev->cdx;
	struct cdx_device_config dev_config;

	dev_config.type = CDX_DEV_MSI_ENABLE;
	dev_config.msi_enable = false;
	if (cdx->ops->dev_configure)
		cdx->ops->dev_configure(cdx, cdx_dev->bus_num, cdx_dev->dev_num, &dev_config);
}
EXPORT_SYMBOL_GPL(cdx_disable_msi);

static struct irq_chip cdx_msi_irq_chip = {
	.name			= "CDX-MSI",
	.irq_mask		= irq_chip_mask_parent,
	.irq_unmask		= irq_chip_unmask_parent,
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_set_affinity	= msi_domain_set_affinity,
	.irq_write_msi_msg	= cdx_msi_write_msg,
	.irq_bus_lock		= cdx_msi_write_irq_lock,
	.irq_bus_sync_unlock	= cdx_msi_write_irq_unlock
};

/* Convert an msi_desc to a unique identifier within the domain. */
static irq_hw_number_t cdx_domain_calc_hwirq(struct cdx_device *dev,
					     struct msi_desc *desc)
{
	return ((irq_hw_number_t)dev->msi_dev_id << 10) | desc->msi_index;
}

static void cdx_msi_set_desc(msi_alloc_info_t *arg, struct msi_desc *desc)
{
	arg->desc = desc;
	arg->hwirq = cdx_domain_calc_hwirq(to_cdx_device(desc->dev), desc);
}

static int cdx_msi_prepare(struct irq_domain *msi_domain,
			   struct device *dev,
			   int nvec, msi_alloc_info_t *info)
{
	struct cdx_device *cdx_dev = to_cdx_device(dev);
	struct device *parent = cdx_dev->cdx->dev;
	struct msi_domain_info *msi_info;
	u32 dev_id;
	int ret;

	/* Retrieve device ID from requestor ID using parent device */
	ret = of_map_id(parent->of_node, cdx_dev->msi_dev_id, "msi-map", "msi-map-mask",
			NULL, &dev_id);
	if (ret) {
		dev_err(dev, "of_map_id failed for MSI: %d\n", ret);
		return ret;
	}

#ifdef GENERIC_MSI_DOMAIN_OPS
	/* Set the device Id to be passed to the GIC-ITS */
	info->scratchpad[0].ul = dev_id;
#endif

	msi_info = msi_get_domain_info(msi_domain->parent);

	return msi_info->ops->msi_prepare(msi_domain->parent, dev, nvec, info);
}

static struct msi_domain_ops cdx_msi_ops = {
	.msi_prepare	= cdx_msi_prepare,
	.set_desc	= cdx_msi_set_desc
};

static struct msi_domain_info cdx_msi_domain_info = {
	.ops	= &cdx_msi_ops,
	.chip	= &cdx_msi_irq_chip,
	.flags	= MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
		  MSI_FLAG_ALLOC_SIMPLE_MSI_DESCS | MSI_FLAG_FREE_MSI_DESCS
};

struct irq_domain *cdx_msi_domain_init(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct fwnode_handle *fwnode_handle;
	struct irq_domain *cdx_msi_domain;
	struct device_node *parent_node;
	struct irq_domain *parent;

	fwnode_handle = of_fwnode_handle(np);

	parent_node = of_parse_phandle(np, "msi-map", 1);
	if (!parent_node) {
		dev_err(dev, "msi-map not present on cdx controller\n");
		return NULL;
	}

	parent = irq_find_matching_fwnode(of_fwnode_handle(parent_node), DOMAIN_BUS_NEXUS);
	of_node_put(parent_node);
	if (!parent || !msi_get_domain_info(parent)) {
		dev_err(dev, "unable to locate ITS domain\n");
		return NULL;
	}

	cdx_msi_domain = msi_create_irq_domain(fwnode_handle, &cdx_msi_domain_info, parent);
	if (!cdx_msi_domain) {
		dev_err(dev, "unable to create CDX-MSI domain\n");
		return NULL;
	}

	dev_dbg(dev, "CDX-MSI domain created\n");

	return cdx_msi_domain;
}
EXPORT_SYMBOL_NS_GPL(cdx_msi_domain_init, "CDX_BUS_CONTROLLER");
