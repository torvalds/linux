// SPDX-License-Identifier: GPL-2.0
/*
 * MSI framework for platform devices
 *
 * Copyright (C) 2015 ARM Limited, All Rights Reserved.
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 * Copyright (C) 2022 Linutronix GmbH
 */

#include <linux/device.h>
#include <linux/irqdomain.h>
#include <linux/msi.h>

/*
 * This indirection can go when platform_device_msi_init_and_alloc_irqs()
 * is switched to a proper irq_chip::irq_write_msi_msg() callback. Keep it
 * simple for now.
 */
static void platform_msi_write_msi_msg(struct irq_data *d, struct msi_msg *msg)
{
	irq_write_msi_msg_t cb = d->chip_data;

	cb(irq_data_get_msi_desc(d), msg);
}

static void platform_msi_set_desc(msi_alloc_info_t *arg, struct msi_desc *desc)
{
	arg->desc = desc;
	arg->hwirq = desc->msi_index;
}

static const struct msi_domain_template platform_msi_template = {
	.chip = {
		.name			= "pMSI",
		.irq_mask		= irq_chip_mask_parent,
		.irq_unmask		= irq_chip_unmask_parent,
		.irq_write_msi_msg	= platform_msi_write_msi_msg,
		/* The rest is filled in by the platform MSI parent */
	},

	.ops = {
		.set_desc		= platform_msi_set_desc,
	},

	.info = {
		.bus_token		= DOMAIN_BUS_DEVICE_MSI,
	},
};

/**
 * platform_device_msi_init_and_alloc_irqs - Initialize platform device MSI
 *					     and allocate interrupts for @dev
 * @dev:		The device for which to allocate interrupts
 * @nvec:		The number of interrupts to allocate
 * @write_msi_msg:	Callback to write an interrupt message for @dev
 *
 * Returns:
 * Zero for success, or an error code in case of failure
 *
 * This creates a MSI domain on @dev which has @dev->msi.domain as
 * parent. The parent domain sets up the new domain. The domain has
 * a fixed size of @nvec. The domain is managed by devres and will
 * be removed when the device is removed.
 *
 * Note: For migration purposes this falls back to the original platform_msi code
 *	 up to the point where all platforms have been converted to the MSI
 *	 parent model.
 */
int platform_device_msi_init_and_alloc_irqs(struct device *dev, unsigned int nvec,
					    irq_write_msi_msg_t write_msi_msg)
{
	struct irq_domain *domain = dev->msi.domain;

	if (!domain || !write_msi_msg)
		return -EINVAL;

	/*
	 * @write_msi_msg is stored in the resulting msi_domain_info::data.
	 * The underlying domain creation mechanism will assign that
	 * callback to the resulting irq chip.
	 */
	if (!msi_create_device_irq_domain(dev, MSI_DEFAULT_DOMAIN,
					  &platform_msi_template,
					  nvec, NULL, write_msi_msg))
		return -ENODEV;

	return msi_domain_alloc_irqs_range(dev, MSI_DEFAULT_DOMAIN, 0, nvec - 1);
}
EXPORT_SYMBOL_GPL(platform_device_msi_init_and_alloc_irqs);

/**
 * platform_device_msi_free_irqs_all - Free all interrupts for @dev
 * @dev:	The device for which to free interrupts
 */
void platform_device_msi_free_irqs_all(struct device *dev)
{
	msi_domain_free_irqs_all(dev, MSI_DEFAULT_DOMAIN);
	msi_remove_device_irq_domain(dev, MSI_DEFAULT_DOMAIN);
}
EXPORT_SYMBOL_GPL(platform_device_msi_free_irqs_all);
