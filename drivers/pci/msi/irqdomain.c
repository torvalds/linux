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
		return msi_domain_alloc_irqs_descs_locked(domain, &dev->dev, nvec);

	return pci_msi_legacy_setup_msi_irqs(dev, nvec, type);
}

void pci_msi_teardown_msi_irqs(struct pci_dev *dev)
{
	struct irq_domain *domain;

	domain = dev_get_msi_domain(&dev->dev);
	if (domain && irq_domain_is_hierarchy(domain))
		msi_domain_free_irqs_descs_locked(domain, &dev->dev);
	else
		pci_msi_legacy_teardown_msi_irqs(dev);
	msi_free_msi_descs(&dev->dev);
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

static inline bool pci_msi_desc_is_multi_msi(struct msi_desc *desc)
{
	return !desc->pci.msi_attrib.is_msix && desc->nvec_used > 1;
}

/**
 * pci_msi_domain_check_cap - Verify that @domain supports the capabilities
 *			      for @dev
 * @domain:	The interrupt domain to check
 * @info:	The domain info for verification
 * @dev:	The device to check
 *
 * Returns:
 *  0 if the functionality is supported
 *  1 if Multi MSI is requested, but the domain does not support it
 *  -ENOTSUPP otherwise
 */
static int pci_msi_domain_check_cap(struct irq_domain *domain,
				    struct msi_domain_info *info,
				    struct device *dev)
{
	struct msi_desc *desc = msi_first_desc(dev, MSI_DESC_ALL);

	/* Special handling to support __pci_enable_msi_range() */
	if (pci_msi_desc_is_multi_msi(desc) &&
	    !(info->flags & MSI_FLAG_MULTI_PCI_MSI))
		return 1;

	if (desc->pci.msi_attrib.is_msix) {
		if (!(info->flags & MSI_FLAG_PCI_MSIX))
			return -ENOTSUPP;

		if (info->flags & MSI_FLAG_MSIX_CONTIGUOUS) {
			unsigned int idx = 0;

			/* Check for gaps in the entry indices */
			msi_for_each_desc(desc, dev, MSI_DESC_ALL) {
				if (desc->msi_index != idx++)
					return -ENOTSUPP;
			}
		}
	}
	return 0;
}

static void pci_msi_domain_set_desc(msi_alloc_info_t *arg,
				    struct msi_desc *desc)
{
	arg->desc = desc;
	arg->hwirq = pci_msi_domain_calc_hwirq(desc);
}

static struct msi_domain_ops pci_msi_domain_ops_default = {
	.set_desc	= pci_msi_domain_set_desc,
	.msi_check	= pci_msi_domain_check_cap,
};

static void pci_msi_domain_update_dom_ops(struct msi_domain_info *info)
{
	struct msi_domain_ops *ops = info->ops;

	if (ops == NULL) {
		info->ops = &pci_msi_domain_ops_default;
	} else {
		if (ops->set_desc == NULL)
			ops->set_desc = pci_msi_domain_set_desc;
		if (ops->msi_check == NULL)
			ops->msi_check = pci_msi_domain_check_cap;
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
	struct irq_domain *domain;

	if (WARN_ON(info->flags & MSI_FLAG_LEVEL_CAPABLE))
		info->flags &= ~MSI_FLAG_LEVEL_CAPABLE;

	if (info->flags & MSI_FLAG_USE_DEF_DOM_OPS)
		pci_msi_domain_update_dom_ops(info);
	if (info->flags & MSI_FLAG_USE_DEF_CHIP_OPS)
		pci_msi_domain_update_chip_ops(info);

	info->flags |= MSI_FLAG_ACTIVATE_EARLY | MSI_FLAG_DEV_SYSFS;
	if (IS_ENABLED(CONFIG_GENERIC_IRQ_RESERVATION_MODE))
		info->flags |= MSI_FLAG_MUST_REACTIVATE;

	/* PCI-MSI is oneshot-safe */
	info->chip->flags |= IRQCHIP_ONESHOT_SAFE;

	domain = msi_create_irq_domain(fwnode, info, parent);
	if (!domain)
		return NULL;

	irq_domain_update_bus_token(domain, DOMAIN_BUS_PCI_MSI);
	return domain;
}
EXPORT_SYMBOL_GPL(pci_msi_create_irq_domain);

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

/**
 * pci_dev_has_special_msi_domain - Check whether the device is handled by
 *				    a non-standard PCI-MSI domain
 * @pdev:	The PCI device to check.
 *
 * Returns: True if the device irqdomain or the bus irqdomain is
 * non-standard PCI/MSI.
 */
bool pci_dev_has_special_msi_domain(struct pci_dev *pdev)
{
	struct irq_domain *dom = dev_get_msi_domain(&pdev->dev);

	if (!dom)
		dom = dev_get_msi_domain(&pdev->bus->dev);

	if (!dom)
		return true;

	return dom->bus_token != DOMAIN_BUS_PCI_MSI;
}
