// SPDX-License-Identifier: GPL-2.0
/*
 * Freescale Management Complex (MC) bus driver MSI support
 *
 * Copyright (C) 2015-2016 Freescale Semiconductor, Inc.
 * Author: German Rivera <German.Rivera@freescale.com>
 *
 */

#include <linux/of_irq.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/msi.h>
#include <linux/acpi_iort.h>

#include "fsl-mc-private.h"

static void fsl_mc_write_msi_msg(struct msi_desc *msi_desc, struct msi_msg *msg)
{
	struct fsl_mc_device *mc_bus_dev = to_fsl_mc_device(msi_desc->dev);
	struct fsl_mc_bus *mc_bus = to_fsl_mc_bus(mc_bus_dev);
	struct fsl_mc_device_irq *mc_dev_irq = &mc_bus->irq_resources[msi_desc->msi_index];
	struct fsl_mc_device *owner_mc_dev = mc_dev_irq->mc_dev;
	struct dprc_irq_cfg irq_cfg;
	int error;

	msi_desc->msg = *msg;

	/*
	 * msi_desc->msg.address is 0x0 when this function is invoked in
	 * the free_irq() code path. In this case, for the MC, we don't
	 * really need to "unprogram" the MSI, so we just return.
	 */
	if (msi_desc->msg.address_lo == 0x0 && msi_desc->msg.address_hi == 0x0)
		return;

	if (!owner_mc_dev)
		return;

	irq_cfg.paddr = ((u64)msi_desc->msg.address_hi << 32) |
			msi_desc->msg.address_lo;
	irq_cfg.val = msi_desc->msg.data;
	irq_cfg.irq_num = msi_desc->irq;

	if (owner_mc_dev == mc_bus_dev) {
		/*
		 * IRQ is for the mc_bus_dev's DPRC itself
		 */
		error = dprc_set_irq(mc_bus_dev->mc_io,
				     MC_CMD_FLAG_INTR_DIS | MC_CMD_FLAG_PRI,
				     mc_bus_dev->mc_handle,
				     mc_dev_irq->dev_irq_index,
				     &irq_cfg);
		if (error < 0) {
			dev_err(&owner_mc_dev->dev,
				"dprc_set_irq() failed: %d\n", error);
		}
	} else {
		/*
		 * IRQ is for for a child device of mc_bus_dev
		 */
		error = dprc_set_obj_irq(mc_bus_dev->mc_io,
					 MC_CMD_FLAG_INTR_DIS | MC_CMD_FLAG_PRI,
					 mc_bus_dev->mc_handle,
					 owner_mc_dev->obj_desc.type,
					 owner_mc_dev->obj_desc.id,
					 mc_dev_irq->dev_irq_index,
					 &irq_cfg);
		if (error < 0) {
			dev_err(&owner_mc_dev->dev,
				"dprc_obj_set_irq() failed: %d\n", error);
		}
	}
}

struct irq_domain *fsl_mc_get_msi_parent(struct device *dev)
{
	struct fsl_mc_device *mc_dev = to_fsl_mc_device(dev);
	struct device *root_dprc_dev;
	struct device *bus_dev;

	fsl_mc_get_root_dprc(dev, &root_dprc_dev);
	bus_dev = root_dprc_dev->parent;

	return (bus_dev->of_node ?
		of_msi_get_domain(bus_dev, bus_dev->of_node, DOMAIN_BUS_NEXUS) :
		iort_get_device_domain(bus_dev, mc_dev->icid, DOMAIN_BUS_NEXUS));
}

int fsl_mc_msi_domain_alloc_irqs(struct device *dev,  unsigned int irq_count)
{
	int error = msi_setup_device_data(dev);
	if (error)
		return error;

	error = platform_device_msi_init_and_alloc_irqs(dev, irq_count, fsl_mc_write_msi_msg);
	if (error)
		dev_err(dev, "Failed to allocate IRQs\n");
	return error;
}

void fsl_mc_msi_domain_free_irqs(struct device *dev)
{
	msi_domain_free_irqs_all(dev, MSI_DEFAULT_DOMAIN);
}

u32 fsl_mc_get_msi_id(struct device *dev)
{
	struct fsl_mc_device *mc_dev = to_fsl_mc_device(dev);
	struct device *root_dprc_dev;

	fsl_mc_get_root_dprc(dev, &root_dprc_dev);

	return (root_dprc_dev->parent->of_node ?
		of_msi_xlate(dev, NULL, mc_dev->icid) :
		iort_msi_map_id(dev, mc_dev->icid));
}
