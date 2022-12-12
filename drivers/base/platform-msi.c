// SPDX-License-Identifier: GPL-2.0
/*
 * MSI framework for platform devices
 *
 * Copyright (C) 2015 ARM Limited, All Rights Reserved.
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#include <linux/device.h>
#include <linux/idr.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/msi.h>
#include <linux/slab.h>

#define DEV_ID_SHIFT	21
#define MAX_DEV_MSIS	(1 << (32 - DEV_ID_SHIFT))

/*
 * Internal data structure containing a (made up, but unique) devid
 * and the callback to write the MSI message.
 */
struct platform_msi_priv_data {
	struct device			*dev;
	void				*host_data;
	msi_alloc_info_t		arg;
	irq_write_msi_msg_t		write_msg;
	int				devid;
};

/* The devid allocator */
static DEFINE_IDA(platform_msi_devid_ida);

#ifdef GENERIC_MSI_DOMAIN_OPS
/*
 * Convert an msi_desc to a globaly unique identifier (per-device
 * devid + msi_desc position in the msi_list).
 */
static irq_hw_number_t platform_msi_calc_hwirq(struct msi_desc *desc)
{
	u32 devid = desc->dev->msi.data->platform_data->devid;

	return (devid << (32 - DEV_ID_SHIFT)) | desc->msi_index;
}

static void platform_msi_set_desc(msi_alloc_info_t *arg, struct msi_desc *desc)
{
	arg->desc = desc;
	arg->hwirq = platform_msi_calc_hwirq(desc);
}

static int platform_msi_init(struct irq_domain *domain,
			     struct msi_domain_info *info,
			     unsigned int virq, irq_hw_number_t hwirq,
			     msi_alloc_info_t *arg)
{
	return irq_domain_set_hwirq_and_chip(domain, virq, hwirq,
					     info->chip, info->chip_data);
}

static void platform_msi_set_proxy_dev(msi_alloc_info_t *arg)
{
	arg->flags |= MSI_ALLOC_FLAGS_PROXY_DEVICE;
}
#else
#define platform_msi_set_desc		NULL
#define platform_msi_init		NULL
#define platform_msi_set_proxy_dev(x)	do {} while(0)
#endif

static void platform_msi_update_dom_ops(struct msi_domain_info *info)
{
	struct msi_domain_ops *ops = info->ops;

	BUG_ON(!ops);

	if (ops->msi_init == NULL)
		ops->msi_init = platform_msi_init;
	if (ops->set_desc == NULL)
		ops->set_desc = platform_msi_set_desc;
}

static void platform_msi_write_msg(struct irq_data *data, struct msi_msg *msg)
{
	struct msi_desc *desc = irq_data_get_msi_desc(data);

	desc->dev->msi.data->platform_data->write_msg(desc, msg);
}

static void platform_msi_update_chip_ops(struct msi_domain_info *info)
{
	struct irq_chip *chip = info->chip;

	BUG_ON(!chip);
	if (!chip->irq_mask)
		chip->irq_mask = irq_chip_mask_parent;
	if (!chip->irq_unmask)
		chip->irq_unmask = irq_chip_unmask_parent;
	if (!chip->irq_eoi)
		chip->irq_eoi = irq_chip_eoi_parent;
	if (!chip->irq_set_affinity)
		chip->irq_set_affinity = msi_domain_set_affinity;
	if (!chip->irq_write_msi_msg)
		chip->irq_write_msi_msg = platform_msi_write_msg;
	if (WARN_ON((info->flags & MSI_FLAG_LEVEL_CAPABLE) &&
		    !(chip->flags & IRQCHIP_SUPPORTS_LEVEL_MSI)))
		info->flags &= ~MSI_FLAG_LEVEL_CAPABLE;
}

/**
 * platform_msi_create_irq_domain - Create a platform MSI interrupt domain
 * @fwnode:		Optional fwnode of the interrupt controller
 * @info:	MSI domain info
 * @parent:	Parent irq domain
 *
 * Updates the domain and chip ops and creates a platform MSI
 * interrupt domain.
 *
 * Returns:
 * A domain pointer or NULL in case of failure.
 */
struct irq_domain *platform_msi_create_irq_domain(struct fwnode_handle *fwnode,
						  struct msi_domain_info *info,
						  struct irq_domain *parent)
{
	struct irq_domain *domain;

	if (info->flags & MSI_FLAG_USE_DEF_DOM_OPS)
		platform_msi_update_dom_ops(info);
	if (info->flags & MSI_FLAG_USE_DEF_CHIP_OPS)
		platform_msi_update_chip_ops(info);
	info->flags |= MSI_FLAG_DEV_SYSFS | MSI_FLAG_ALLOC_SIMPLE_MSI_DESCS |
		       MSI_FLAG_FREE_MSI_DESCS;

	domain = msi_create_irq_domain(fwnode, info, parent);
	if (domain)
		irq_domain_update_bus_token(domain, DOMAIN_BUS_PLATFORM_MSI);

	return domain;
}
EXPORT_SYMBOL_GPL(platform_msi_create_irq_domain);

static int platform_msi_alloc_priv_data(struct device *dev, unsigned int nvec,
					irq_write_msi_msg_t write_msi_msg)
{
	struct platform_msi_priv_data *datap;
	int err;

	/*
	 * Limit the number of interrupts to 2048 per device. Should we
	 * need to bump this up, DEV_ID_SHIFT should be adjusted
	 * accordingly (which would impact the max number of MSI
	 * capable devices).
	 */
	if (!dev->msi.domain || !write_msi_msg || !nvec || nvec > MAX_DEV_MSIS)
		return -EINVAL;

	if (dev->msi.domain->bus_token != DOMAIN_BUS_PLATFORM_MSI) {
		dev_err(dev, "Incompatible msi_domain, giving up\n");
		return -EINVAL;
	}

	err = msi_setup_device_data(dev);
	if (err)
		return err;

	/* Already initialized? */
	if (dev->msi.data->platform_data)
		return -EBUSY;

	datap = kzalloc(sizeof(*datap), GFP_KERNEL);
	if (!datap)
		return -ENOMEM;

	datap->devid = ida_simple_get(&platform_msi_devid_ida,
				      0, 1 << DEV_ID_SHIFT, GFP_KERNEL);
	if (datap->devid < 0) {
		err = datap->devid;
		kfree(datap);
		return err;
	}

	datap->write_msg = write_msi_msg;
	datap->dev = dev;
	dev->msi.data->platform_data = datap;
	return 0;
}

static void platform_msi_free_priv_data(struct device *dev)
{
	struct platform_msi_priv_data *data = dev->msi.data->platform_data;

	dev->msi.data->platform_data = NULL;
	ida_simple_remove(&platform_msi_devid_ida, data->devid);
	kfree(data);
}

/**
 * platform_msi_domain_alloc_irqs - Allocate MSI interrupts for @dev
 * @dev:		The device for which to allocate interrupts
 * @nvec:		The number of interrupts to allocate
 * @write_msi_msg:	Callback to write an interrupt message for @dev
 *
 * Returns:
 * Zero for success, or an error code in case of failure
 */
int platform_msi_domain_alloc_irqs(struct device *dev, unsigned int nvec,
				   irq_write_msi_msg_t write_msi_msg)
{
	int err;

	err = platform_msi_alloc_priv_data(dev, nvec, write_msi_msg);
	if (err)
		return err;

	err = msi_domain_alloc_irqs_range(dev, MSI_DEFAULT_DOMAIN, 0, nvec - 1);
	if (err)
		platform_msi_free_priv_data(dev);

	return err;
}
EXPORT_SYMBOL_GPL(platform_msi_domain_alloc_irqs);

/**
 * platform_msi_domain_free_irqs - Free MSI interrupts for @dev
 * @dev:	The device for which to free interrupts
 */
void platform_msi_domain_free_irqs(struct device *dev)
{
	msi_domain_free_irqs_all(dev, MSI_DEFAULT_DOMAIN);
	platform_msi_free_priv_data(dev);
}
EXPORT_SYMBOL_GPL(platform_msi_domain_free_irqs);

/**
 * platform_msi_get_host_data - Query the private data associated with
 *                              a platform-msi domain
 * @domain:	The platform-msi domain
 *
 * Return: The private data provided when calling
 * platform_msi_create_device_domain().
 */
void *platform_msi_get_host_data(struct irq_domain *domain)
{
	struct platform_msi_priv_data *data = domain->host_data;

	return data->host_data;
}

static struct lock_class_key platform_device_msi_lock_class;

/**
 * __platform_msi_create_device_domain - Create a platform-msi device domain
 *
 * @dev:		The device generating the MSIs
 * @nvec:		The number of MSIs that need to be allocated
 * @is_tree:		flag to indicate tree hierarchy
 * @write_msi_msg:	Callback to write an interrupt message for @dev
 * @ops:		The hierarchy domain operations to use
 * @host_data:		Private data associated to this domain
 *
 * Return: An irqdomain for @nvec interrupts on success, NULL in case of error.
 *
 * This is for interrupt domains which stack on a platform-msi domain
 * created by platform_msi_create_irq_domain(). @dev->msi.domain points to
 * that platform-msi domain which is the parent for the new domain.
 */
struct irq_domain *
__platform_msi_create_device_domain(struct device *dev,
				    unsigned int nvec,
				    bool is_tree,
				    irq_write_msi_msg_t write_msi_msg,
				    const struct irq_domain_ops *ops,
				    void *host_data)
{
	struct platform_msi_priv_data *data;
	struct irq_domain *domain;
	int err;

	err = platform_msi_alloc_priv_data(dev, nvec, write_msi_msg);
	if (err)
		return NULL;

	/*
	 * Use a separate lock class for the MSI descriptor mutex on
	 * platform MSI device domains because the descriptor mutex nests
	 * into the domain mutex. See alloc/free below.
	 */
	lockdep_set_class(&dev->msi.data->mutex, &platform_device_msi_lock_class);

	data = dev->msi.data->platform_data;
	data->host_data = host_data;
	domain = irq_domain_create_hierarchy(dev->msi.domain, 0,
					     is_tree ? 0 : nvec,
					     dev->fwnode, ops, data);
	if (!domain)
		goto free_priv;

	platform_msi_set_proxy_dev(&data->arg);
	err = msi_domain_prepare_irqs(domain->parent, dev, nvec, &data->arg);
	if (err)
		goto free_domain;

	return domain;

free_domain:
	irq_domain_remove(domain);
free_priv:
	platform_msi_free_priv_data(dev);
	return NULL;
}

/**
 * platform_msi_device_domain_free - Free interrupts associated with a platform-msi
 *				     device domain
 *
 * @domain:	The platform-msi device domain
 * @virq:	The base irq from which to perform the free operation
 * @nr_irqs:	How many interrupts to free from @virq
 */
void platform_msi_device_domain_free(struct irq_domain *domain, unsigned int virq,
				     unsigned int nr_irqs)
{
	struct platform_msi_priv_data *data = domain->host_data;

	msi_lock_descs(data->dev);
	irq_domain_free_irqs_common(domain, virq, nr_irqs);
	msi_free_msi_descs_range(data->dev, virq, virq + nr_irqs - 1);
	msi_unlock_descs(data->dev);
}

/**
 * platform_msi_device_domain_alloc - Allocate interrupts associated with
 *				      a platform-msi device domain
 *
 * @domain:	The platform-msi device domain
 * @virq:	The base irq from which to perform the allocate operation
 * @nr_irqs:	How many interrupts to allocate from @virq
 *
 * Return 0 on success, or an error code on failure. Must be called
 * with irq_domain_mutex held (which can only be done as part of a
 * top-level interrupt allocation).
 */
int platform_msi_device_domain_alloc(struct irq_domain *domain, unsigned int virq,
				     unsigned int nr_irqs)
{
	struct platform_msi_priv_data *data = domain->host_data;
	struct device *dev = data->dev;

	return msi_domain_populate_irqs(domain->parent, dev, virq, nr_irqs, &data->arg);
}
