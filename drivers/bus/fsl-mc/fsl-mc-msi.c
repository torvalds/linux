// SPDX-License-Identifier: GPL-2.0
/*
 * Freescale Management Complex (MC) bus driver MSI support
 *
 * Copyright (C) 2015-2016 Freescale Semiconductor, Inc.
 * Author: German Rivera <German.Rivera@freescale.com>
 *
 */

#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/msi.h>
#include <linux/acpi_iort.h>

#include "fsl-mc-private.h"

#ifdef GENERIC_MSI_DOMAIN_OPS
/*
 * Generate a unique ID identifying the interrupt (only used within the MSI
 * irqdomain.  Combine the icid with the interrupt index.
 */
static irq_hw_number_t fsl_mc_domain_calc_hwirq(struct fsl_mc_device *dev,
						struct msi_desc *desc)
{
	/*
	 * Make the base hwirq value for ICID*10000 so it is readable
	 * as a decimal value in /proc/interrupts.
	 */
	return (irq_hw_number_t)(desc->msi_index + (dev->icid * 10000));
}

static void fsl_mc_msi_set_desc(msi_alloc_info_t *arg,
				struct msi_desc *desc)
{
	arg->desc = desc;
	arg->hwirq = fsl_mc_domain_calc_hwirq(to_fsl_mc_device(desc->dev),
					      desc);
}
#else
#define fsl_mc_msi_set_desc NULL
#endif

static void fsl_mc_msi_update_dom_ops(struct msi_domain_info *info)
{
	struct msi_domain_ops *ops = info->ops;

	if (!ops)
		return;

	/*
	 * set_desc should not be set by the caller
	 */
	if (!ops->set_desc)
		ops->set_desc = fsl_mc_msi_set_desc;
}

static void __fsl_mc_msi_write_msg(struct fsl_mc_device *mc_bus_dev,
				   struct fsl_mc_device_irq *mc_dev_irq,
				   struct msi_desc *msi_desc)
{
	int error;
	struct fsl_mc_device *owner_mc_dev = mc_dev_irq->mc_dev;
	struct dprc_irq_cfg irq_cfg;

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

/*
 * NOTE: This function is invoked with interrupts disabled
 */
static void fsl_mc_msi_write_msg(struct irq_data *irq_data,
				 struct msi_msg *msg)
{
	struct msi_desc *msi_desc = irq_data_get_msi_desc(irq_data);
	struct fsl_mc_device *mc_bus_dev = to_fsl_mc_device(msi_desc->dev);
	struct fsl_mc_bus *mc_bus = to_fsl_mc_bus(mc_bus_dev);
	struct fsl_mc_device_irq *mc_dev_irq =
		&mc_bus->irq_resources[msi_desc->msi_index];

	msi_desc->msg = *msg;

	/*
	 * Program the MSI (paddr, value) pair in the device:
	 */
	__fsl_mc_msi_write_msg(mc_bus_dev, mc_dev_irq, msi_desc);
}

static void fsl_mc_msi_update_chip_ops(struct msi_domain_info *info)
{
	struct irq_chip *chip = info->chip;

	if (!chip)
		return;

	/*
	 * irq_write_msi_msg should not be set by the caller
	 */
	if (!chip->irq_write_msi_msg)
		chip->irq_write_msi_msg = fsl_mc_msi_write_msg;
}

/**
 * fsl_mc_msi_create_irq_domain - Create a fsl-mc MSI interrupt domain
 * @fwnode:	Optional firmware node of the interrupt controller
 * @info:	MSI domain info
 * @parent:	Parent irq domain
 *
 * Updates the domain and chip ops and creates a fsl-mc MSI
 * interrupt domain.
 *
 * Returns:
 * A domain pointer or NULL in case of failure.
 */
struct irq_domain *fsl_mc_msi_create_irq_domain(struct fwnode_handle *fwnode,
						struct msi_domain_info *info,
						struct irq_domain *parent)
{
	struct irq_domain *domain;

	if (WARN_ON((info->flags & MSI_FLAG_LEVEL_CAPABLE)))
		info->flags &= ~MSI_FLAG_LEVEL_CAPABLE;
	if (info->flags & MSI_FLAG_USE_DEF_DOM_OPS)
		fsl_mc_msi_update_dom_ops(info);
	if (info->flags & MSI_FLAG_USE_DEF_CHIP_OPS)
		fsl_mc_msi_update_chip_ops(info);
	info->flags |= MSI_FLAG_ALLOC_SIMPLE_MSI_DESCS | MSI_FLAG_FREE_MSI_DESCS;

	domain = msi_create_irq_domain(fwnode, info, parent);
	if (domain)
		irq_domain_update_bus_token(domain, DOMAIN_BUS_FSL_MC_MSI);

	return domain;
}

struct irq_domain *fsl_mc_find_msi_domain(struct device *dev)
{
	struct device *root_dprc_dev;
	struct device *bus_dev;
	struct irq_domain *msi_domain;
	struct fsl_mc_device *mc_dev = to_fsl_mc_device(dev);

	fsl_mc_get_root_dprc(dev, &root_dprc_dev);
	bus_dev = root_dprc_dev->parent;

	if (bus_dev->of_node) {
		msi_domain = of_msi_map_get_device_domain(dev,
						  mc_dev->icid,
						  DOMAIN_BUS_FSL_MC_MSI);

		/*
		 * if the msi-map property is missing assume that all the
		 * child containers inherit the domain from the parent
		 */
		if (!msi_domain)

			msi_domain = of_msi_get_domain(bus_dev,
						bus_dev->of_node,
						DOMAIN_BUS_FSL_MC_MSI);
	} else {
		msi_domain = iort_get_device_domain(dev, mc_dev->icid,
						    DOMAIN_BUS_FSL_MC_MSI);
	}

	return msi_domain;
}

int fsl_mc_msi_domain_alloc_irqs(struct device *dev,  unsigned int irq_count)
{
	int error = msi_setup_device_data(dev);

	if (error)
		return error;

	/*
	 * NOTE: Calling this function will trigger the invocation of the
	 * its_fsl_mc_msi_prepare() callback
	 */
	error = msi_domain_alloc_irqs_range(dev, MSI_DEFAULT_DOMAIN, 0, irq_count - 1);

	if (error)
		dev_err(dev, "Failed to allocate IRQs\n");
	return error;
}

void fsl_mc_msi_domain_free_irqs(struct device *dev)
{
	msi_domain_free_irqs_all(dev, MSI_DEFAULT_DOMAIN);
}
