// SPDX-License-Identifier: GPL-2.0
/*
 * PCI Message Signaled Interrupt (MSI) - irqdomain support
 */
#include <linux/acpi_iort.h>
#include <linux/irqdomain.h>
#include <linux/of_irq.h>

#include "msi.h"

int pci_msi_setup_msi_irqs(struct pci_dev *dev, int nvec, int type)
{
	struct irq_domain *domain;

	domain = dev_get_msi_domain(&dev->dev);
	if (domain && irq_domain_is_hierarchy(domain))
		return msi_domain_alloc_irqs_all_locked(&dev->dev, MSI_DEFAULT_DOMAIN, nvec);

	return pci_msi_legacy_setup_msi_irqs(dev, nvec, type);
}

void pci_msi_teardown_msi_irqs(struct pci_dev *dev)
{
	struct irq_domain *domain;

	domain = dev_get_msi_domain(&dev->dev);
	if (domain && irq_domain_is_hierarchy(domain)) {
		msi_domain_free_irqs_all_locked(&dev->dev, MSI_DEFAULT_DOMAIN);
	} else {
		pci_msi_legacy_teardown_msi_irqs(dev);
		msi_free_msi_descs(&dev->dev);
	}
}

/**
 * pci_msi_domain_write_msg - Helper to write MSI message to PCI config space
 * @irq_data:	Pointer to interrupt data of the MSI interrupt
 * @msg:	Pointer to the message
 */
static void pci_msi_domain_write_msg(struct irq_data *irq_data, struct msi_msg *msg)
{
	struct msi_desc *desc = irq_data_get_msi_desc(irq_data);

	/*
	 * For MSI-X desc->irq is always equal to irq_data->irq. For
	 * MSI only the first interrupt of MULTI MSI passes the test.
	 */
	if (desc->irq == irq_data->irq)
		__pci_write_msi_msg(desc, msg);
}

/**
 * pci_msi_domain_calc_hwirq - Generate a unique ID for an MSI source
 * @desc:	Pointer to the MSI descriptor
 *
 * The ID number is only used within the irqdomain.
 */
static irq_hw_number_t pci_msi_domain_calc_hwirq(struct msi_desc *desc)
{
	struct pci_dev *dev = msi_desc_to_pci_dev(desc);

	return (irq_hw_number_t)desc->msi_index |
		pci_dev_id(dev) << 11 |
		(pci_domain_nr(dev->bus) & 0xFFFFFFFF) << 27;
}

static void pci_msi_domain_set_desc(msi_alloc_info_t *arg,
				    struct msi_desc *desc)
{
	arg->desc = desc;
	arg->hwirq = pci_msi_domain_calc_hwirq(desc);
}

static struct msi_domain_ops pci_msi_domain_ops_default = {
	.set_desc	= pci_msi_domain_set_desc,
};

static void pci_msi_domain_update_dom_ops(struct msi_domain_info *info)
{
	struct msi_domain_ops *ops = info->ops;

	if (ops == NULL) {
		info->ops = &pci_msi_domain_ops_default;
	} else {
		if (ops->set_desc == NULL)
			ops->set_desc = pci_msi_domain_set_desc;
	}
}

static void pci_msi_domain_update_chip_ops(struct msi_domain_info *info)
{
	struct irq_chip *chip = info->chip;

	BUG_ON(!chip);
	if (!chip->irq_write_msi_msg)
		chip->irq_write_msi_msg = pci_msi_domain_write_msg;
	if (!chip->irq_mask)
		chip->irq_mask = pci_msi_mask_irq;
	if (!chip->irq_unmask)
		chip->irq_unmask = pci_msi_unmask_irq;
}

/**
 * pci_msi_create_irq_domain - Create a MSI interrupt domain
 * @fwnode:	Optional fwnode of the interrupt controller
 * @info:	MSI domain info
 * @parent:	Parent irq domain
 *
 * Updates the domain and chip ops and creates a MSI interrupt domain.
 *
 * Returns:
 * A domain pointer or NULL in case of failure.
 */
struct irq_domain *pci_msi_create_irq_domain(struct fwnode_handle *fwnode,
					     struct msi_domain_info *info,
					     struct irq_domain *parent)
{
	if (WARN_ON(info->flags & MSI_FLAG_LEVEL_CAPABLE))
		info->flags &= ~MSI_FLAG_LEVEL_CAPABLE;

	if (info->flags & MSI_FLAG_USE_DEF_DOM_OPS)
		pci_msi_domain_update_dom_ops(info);
	if (info->flags & MSI_FLAG_USE_DEF_CHIP_OPS)
		pci_msi_domain_update_chip_ops(info);

	/* Let the core code free MSI descriptors when freeing interrupts */
	info->flags |= MSI_FLAG_FREE_MSI_DESCS;

	info->flags |= MSI_FLAG_ACTIVATE_EARLY | MSI_FLAG_DEV_SYSFS;
	if (IS_ENABLED(CONFIG_GENERIC_IRQ_RESERVATION_MODE))
		info->flags |= MSI_FLAG_MUST_REACTIVATE;

	/* PCI-MSI is oneshot-safe */
	info->chip->flags |= IRQCHIP_ONESHOT_SAFE;
	/* Let the core update the bus token */
	info->bus_token = DOMAIN_BUS_PCI_MSI;

	return msi_create_irq_domain(fwnode, info, parent);
}
EXPORT_SYMBOL_GPL(pci_msi_create_irq_domain);

/*
 * Per device MSI[-X] domain functionality
 */
static void pci_device_domain_set_desc(msi_alloc_info_t *arg, struct msi_desc *desc)
{
	arg->desc = desc;
	arg->hwirq = desc->msi_index;
}

static void pci_irq_mask_msi(struct irq_data *data)
{
	struct msi_desc *desc = irq_data_get_msi_desc(data);

	pci_msi_mask(desc, BIT(data->irq - desc->irq));
}

static void pci_irq_unmask_msi(struct irq_data *data)
{
	struct msi_desc *desc = irq_data_get_msi_desc(data);

	pci_msi_unmask(desc, BIT(data->irq - desc->irq));
}

#ifdef CONFIG_GENERIC_IRQ_RESERVATION_MODE
# define MSI_REACTIVATE		MSI_FLAG_MUST_REACTIVATE
#else
# define MSI_REACTIVATE		0
#endif

#define MSI_COMMON_FLAGS	(MSI_FLAG_FREE_MSI_DESCS |	\
				 MSI_FLAG_ACTIVATE_EARLY |	\
				 MSI_FLAG_DEV_SYSFS |		\
				 MSI_REACTIVATE)

static const struct msi_domain_template pci_msi_template = {
	.chip = {
		.name			= "PCI-MSI",
		.irq_mask		= pci_irq_mask_msi,
		.irq_unmask		= pci_irq_unmask_msi,
		.irq_write_msi_msg	= pci_msi_domain_write_msg,
		.flags			= IRQCHIP_ONESHOT_SAFE,
	},

	.ops = {
		.set_desc		= pci_device_domain_set_desc,
	},

	.info = {
		.flags			= MSI_COMMON_FLAGS | MSI_FLAG_MULTI_PCI_MSI,
		.bus_token		= DOMAIN_BUS_PCI_DEVICE_MSI,
	},
};

static void pci_irq_mask_msix(struct irq_data *data)
{
	pci_msix_mask(irq_data_get_msi_desc(data));
}

static void pci_irq_unmask_msix(struct irq_data *data)
{
	pci_msix_unmask(irq_data_get_msi_desc(data));
}

static void pci_msix_prepare_desc(struct irq_domain *domain, msi_alloc_info_t *arg,
				  struct msi_desc *desc)
{
	/* Don't fiddle with preallocated MSI descriptors */
	if (!desc->pci.mask_base)
		msix_prepare_msi_desc(to_pci_dev(desc->dev), desc);
}

static const struct msi_domain_template pci_msix_template = {
	.chip = {
		.name			= "PCI-MSIX",
		.irq_mask		= pci_irq_mask_msix,
		.irq_unmask		= pci_irq_unmask_msix,
		.irq_write_msi_msg	= pci_msi_domain_write_msg,
		.flags			= IRQCHIP_ONESHOT_SAFE,
	},

	.ops = {
		.prepare_desc		= pci_msix_prepare_desc,
		.set_desc		= pci_device_domain_set_desc,
	},

	.info = {
		.flags			= MSI_COMMON_FLAGS | MSI_FLAG_PCI_MSIX |
					  MSI_FLAG_PCI_MSIX_ALLOC_DYN,
		.bus_token		= DOMAIN_BUS_PCI_DEVICE_MSIX,
	},
};

static bool pci_match_device_domain(struct pci_dev *pdev, enum irq_domain_bus_token bus_token)
{
	return msi_match_device_irq_domain(&pdev->dev, MSI_DEFAULT_DOMAIN, bus_token);
}

static bool pci_create_device_domain(struct pci_dev *pdev, const struct msi_domain_template *tmpl,
				     unsigned int hwsize)
{
	struct irq_domain *domain = dev_get_msi_domain(&pdev->dev);

	if (!domain || !irq_domain_is_msi_parent(domain))
		return true;

	return msi_create_device_irq_domain(&pdev->dev, MSI_DEFAULT_DOMAIN, tmpl,
					    hwsize, NULL, NULL);
}

/**
 * pci_setup_msi_device_domain - Setup a device MSI interrupt domain
 * @pdev:	The PCI device to create the domain on
 *
 * Return:
 *  True when:
 *	- The device does not have a MSI parent irq domain associated,
 *	  which keeps the legacy architecture specific and the global
 *	  PCI/MSI domain models working
 *	- The MSI domain exists already
 *	- The MSI domain was successfully allocated
 *  False when:
 *	- MSI-X is enabled
 *	- The domain creation fails.
 *
 * The created MSI domain is preserved until:
 *	- The device is removed
 *	- MSI is disabled and a MSI-X domain is created
 */
bool pci_setup_msi_device_domain(struct pci_dev *pdev)
{
	if (WARN_ON_ONCE(pdev->msix_enabled))
		return false;

	if (pci_match_device_domain(pdev, DOMAIN_BUS_PCI_DEVICE_MSI))
		return true;
	if (pci_match_device_domain(pdev, DOMAIN_BUS_PCI_DEVICE_MSIX))
		msi_remove_device_irq_domain(&pdev->dev, MSI_DEFAULT_DOMAIN);

	return pci_create_device_domain(pdev, &pci_msi_template, 1);
}

/**
 * pci_setup_msix_device_domain - Setup a device MSI-X interrupt domain
 * @pdev:	The PCI device to create the domain on
 * @hwsize:	The size of the MSI-X vector table
 *
 * Return:
 *  True when:
 *	- The device does not have a MSI parent irq domain associated,
 *	  which keeps the legacy architecture specific and the global
 *	  PCI/MSI domain models working
 *	- The MSI-X domain exists already
 *	- The MSI-X domain was successfully allocated
 *  False when:
 *	- MSI is enabled
 *	- The domain creation fails.
 *
 * The created MSI-X domain is preserved until:
 *	- The device is removed
 *	- MSI-X is disabled and a MSI domain is created
 */
bool pci_setup_msix_device_domain(struct pci_dev *pdev, unsigned int hwsize)
{
	if (WARN_ON_ONCE(pdev->msi_enabled))
		return false;

	if (pci_match_device_domain(pdev, DOMAIN_BUS_PCI_DEVICE_MSIX))
		return true;
	if (pci_match_device_domain(pdev, DOMAIN_BUS_PCI_DEVICE_MSI))
		msi_remove_device_irq_domain(&pdev->dev, MSI_DEFAULT_DOMAIN);

	return pci_create_device_domain(pdev, &pci_msix_template, hwsize);
}

/**
 * pci_msi_domain_supports - Check for support of a particular feature flag
 * @pdev:		The PCI device to operate on
 * @feature_mask:	The feature mask to check for (full match)
 * @mode:		If ALLOW_LEGACY this grants the feature when there is no irq domain
 *			associated to the device. If DENY_LEGACY the lack of an irq domain
 *			makes the feature unsupported
 */
bool pci_msi_domain_supports(struct pci_dev *pdev, unsigned int feature_mask,
			     enum support_mode mode)
{
	struct msi_domain_info *info;
	struct irq_domain *domain;
	unsigned int supported;

	domain = dev_get_msi_domain(&pdev->dev);

	if (!domain || !irq_domain_is_hierarchy(domain))
		return mode == ALLOW_LEGACY;

	if (!irq_domain_is_msi_parent(domain)) {
		/*
		 * For "global" PCI/MSI interrupt domains the associated
		 * msi_domain_info::flags is the authoritive source of
		 * information.
		 */
		info = domain->host_data;
		supported = info->flags;
	} else {
		/*
		 * For MSI parent domains the supported feature set
		 * is avaliable in the parent ops. This makes checks
		 * possible before actually instantiating the
		 * per device domain because the parent is never
		 * expanding the PCI/MSI functionality.
		 */
		supported = domain->msi_parent_ops->supported_flags;
	}

	return (supported & feature_mask) == feature_mask;
}

/**
 * pci_create_ims_domain - Create a secondary IMS domain for a PCI device
 * @pdev:	The PCI device to operate on
 * @template:	The MSI info template which describes the domain
 * @hwsize:	The size of the hardware entry table or 0 if the domain
 *		is purely software managed
 * @data:	Optional pointer to domain specific data to be stored
 *		in msi_domain_info::data
 *
 * Return: True on success, false otherwise
 *
 * An IMS domain is expected to have the following constraints:
 *	- The index space is managed by the core code
 *
 *	- There is no requirement for consecutive index ranges
 *
 *	- The interrupt chip must provide the following callbacks:
 *		- irq_mask()
 *		- irq_unmask()
 *		- irq_write_msi_msg()
 *
 *	- The interrupt chip must provide the following optional callbacks
 *	  when the irq_mask(), irq_unmask() and irq_write_msi_msg() callbacks
 *	  cannot operate directly on hardware, e.g. in the case that the
 *	  interrupt message store is in queue memory:
 *		- irq_bus_lock()
 *		- irq_bus_unlock()
 *
 *	  These callbacks are invoked from preemptible task context and are
 *	  allowed to sleep. In this case the mandatory callbacks above just
 *	  store the information. The irq_bus_unlock() callback is supposed
 *	  to make the change effective before returning.
 *
 *	- Interrupt affinity setting is handled by the underlying parent
 *	  interrupt domain and communicated to the IMS domain via
 *	  irq_write_msi_msg().
 *
 * The domain is automatically destroyed when the PCI device is removed.
 */
bool pci_create_ims_domain(struct pci_dev *pdev, const struct msi_domain_template *template,
			   unsigned int hwsize, void *data)
{
	struct irq_domain *domain = dev_get_msi_domain(&pdev->dev);

	if (!domain || !irq_domain_is_msi_parent(domain))
		return false;

	if (template->info.bus_token != DOMAIN_BUS_PCI_DEVICE_IMS ||
	    !(template->info.flags & MSI_FLAG_ALLOC_SIMPLE_MSI_DESCS) ||
	    !(template->info.flags & MSI_FLAG_FREE_MSI_DESCS) ||
	    !template->chip.irq_mask || !template->chip.irq_unmask ||
	    !template->chip.irq_write_msi_msg || template->chip.irq_set_affinity)
		return false;

	return msi_create_device_irq_domain(&pdev->dev, MSI_SECONDARY_DOMAIN, template,
					    hwsize, data, NULL);
}
EXPORT_SYMBOL_GPL(pci_create_ims_domain);

/*
 * Users of the generic MSI infrastructure expect a device to have a single ID,
 * so with DMA aliases we have to pick the least-worst compromise. Devices with
 * DMA phantom functions tend to still emit MSIs from the real function number,
 * so we ignore those and only consider topological aliases where either the
 * alias device or RID appears on a different bus number. We also make the
 * reasonable assumption that bridges are walked in an upstream direction (so
 * the last one seen wins), and the much braver assumption that the most likely
 * case is that of PCI->PCIe so we should always use the alias RID. This echoes
 * the logic from intel_irq_remapping's set_msi_sid(), which presumably works
 * well enough in practice; in the face of the horrible PCIe<->PCI-X conditions
 * for taking ownership all we can really do is close our eyes and hope...
 */
static int get_msi_id_cb(struct pci_dev *pdev, u16 alias, void *data)
{
	u32 *pa = data;
	u8 bus = PCI_BUS_NUM(*pa);

	if (pdev->bus->number != bus || PCI_BUS_NUM(alias) != bus)
		*pa = alias;

	return 0;
}

/**
 * pci_msi_domain_get_msi_rid - Get the MSI requester id (RID)
 * @domain:	The interrupt domain
 * @pdev:	The PCI device.
 *
 * The RID for a device is formed from the alias, with a firmware
 * supplied mapping applied
 *
 * Returns: The RID.
 */
u32 pci_msi_domain_get_msi_rid(struct irq_domain *domain, struct pci_dev *pdev)
{
	struct device_node *of_node;
	u32 rid = pci_dev_id(pdev);

	pci_for_each_dma_alias(pdev, get_msi_id_cb, &rid);

	of_node = irq_domain_get_of_node(domain);
	rid = of_node ? of_msi_map_id(&pdev->dev, of_node, rid) :
			iort_msi_map_id(&pdev->dev, rid);

	return rid;
}

/**
 * pci_msi_get_device_domain - Get the MSI domain for a given PCI device
 * @pdev:	The PCI device
 *
 * Use the firmware data to find a device-specific MSI domain
 * (i.e. not one that is set as a default).
 *
 * Returns: The corresponding MSI domain or NULL if none has been found.
 */
struct irq_domain *pci_msi_get_device_domain(struct pci_dev *pdev)
{
	struct irq_domain *dom;
	u32 rid = pci_dev_id(pdev);

	pci_for_each_dma_alias(pdev, get_msi_id_cb, &rid);
	dom = of_msi_map_get_device_domain(&pdev->dev, rid, DOMAIN_BUS_PCI_MSI);
	if (!dom)
		dom = iort_get_device_domain(&pdev->dev, rid,
					     DOMAIN_BUS_PCI_MSI);
	return dom;
}
