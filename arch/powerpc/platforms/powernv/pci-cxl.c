/*
 * Copyright 2014-2016 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/msi.h>
#include <asm/pci-bridge.h>
#include <asm/pnv-pci.h>
#include <asm/opal.h>
#include <misc/cxl.h>

#include "pci.h"

struct device_node *pnv_pci_get_phb_node(struct pci_dev *dev)
{
	struct pci_controller *hose = pci_bus_to_host(dev->bus);

	return of_node_get(hose->dn);
}
EXPORT_SYMBOL(pnv_pci_get_phb_node);

int pnv_phb_to_cxl_mode(struct pci_dev *dev, uint64_t mode)
{
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	struct pnv_phb *phb = hose->private_data;
	struct pnv_ioda_pe *pe;
	int rc;

	pe = pnv_ioda_get_pe(dev);
	if (!pe)
		return -ENODEV;

	pe_info(pe, "Switching PHB to CXL\n");

	rc = opal_pci_set_phb_cxl_mode(phb->opal_id, mode, pe->pe_number);
	if (rc == OPAL_UNSUPPORTED)
		dev_err(&dev->dev, "Required cxl mode not supported by firmware - update skiboot\n");
	else if (rc)
		dev_err(&dev->dev, "opal_pci_set_phb_cxl_mode failed: %i\n", rc);

	return rc;
}
EXPORT_SYMBOL(pnv_phb_to_cxl_mode);

/* Find PHB for cxl dev and allocate MSI hwirqs?
 * Returns the absolute hardware IRQ number
 */
int pnv_cxl_alloc_hwirqs(struct pci_dev *dev, int num)
{
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	struct pnv_phb *phb = hose->private_data;
	int hwirq = msi_bitmap_alloc_hwirqs(&phb->msi_bmp, num);

	if (hwirq < 0) {
		dev_warn(&dev->dev, "Failed to find a free MSI\n");
		return -ENOSPC;
	}

	return phb->msi_base + hwirq;
}
EXPORT_SYMBOL(pnv_cxl_alloc_hwirqs);

void pnv_cxl_release_hwirqs(struct pci_dev *dev, int hwirq, int num)
{
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	struct pnv_phb *phb = hose->private_data;

	msi_bitmap_free_hwirqs(&phb->msi_bmp, hwirq - phb->msi_base, num);
}
EXPORT_SYMBOL(pnv_cxl_release_hwirqs);

void pnv_cxl_release_hwirq_ranges(struct cxl_irq_ranges *irqs,
				  struct pci_dev *dev)
{
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	struct pnv_phb *phb = hose->private_data;
	int i, hwirq;

	for (i = 1; i < CXL_IRQ_RANGES; i++) {
		if (!irqs->range[i])
			continue;
		pr_devel("cxl release irq range 0x%x: offset: 0x%lx  limit: %ld\n",
			 i, irqs->offset[i],
			 irqs->range[i]);
		hwirq = irqs->offset[i] - phb->msi_base;
		msi_bitmap_free_hwirqs(&phb->msi_bmp, hwirq,
				       irqs->range[i]);
	}
}
EXPORT_SYMBOL(pnv_cxl_release_hwirq_ranges);

int pnv_cxl_alloc_hwirq_ranges(struct cxl_irq_ranges *irqs,
			       struct pci_dev *dev, int num)
{
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	struct pnv_phb *phb = hose->private_data;
	int i, hwirq, try;

	memset(irqs, 0, sizeof(struct cxl_irq_ranges));

	/* 0 is reserved for the multiplexed PSL DSI interrupt */
	for (i = 1; i < CXL_IRQ_RANGES && num; i++) {
		try = num;
		while (try) {
			hwirq = msi_bitmap_alloc_hwirqs(&phb->msi_bmp, try);
			if (hwirq >= 0)
				break;
			try /= 2;
		}
		if (!try)
			goto fail;

		irqs->offset[i] = phb->msi_base + hwirq;
		irqs->range[i] = try;
		pr_devel("cxl alloc irq range 0x%x: offset: 0x%lx  limit: %li\n",
			 i, irqs->offset[i], irqs->range[i]);
		num -= try;
	}
	if (num)
		goto fail;

	return 0;
fail:
	pnv_cxl_release_hwirq_ranges(irqs, dev);
	return -ENOSPC;
}
EXPORT_SYMBOL(pnv_cxl_alloc_hwirq_ranges);

int pnv_cxl_get_irq_count(struct pci_dev *dev)
{
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	struct pnv_phb *phb = hose->private_data;

	return phb->msi_bmp.irq_count;
}
EXPORT_SYMBOL(pnv_cxl_get_irq_count);

int pnv_cxl_ioda_msi_setup(struct pci_dev *dev, unsigned int hwirq,
			   unsigned int virq)
{
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	struct pnv_phb *phb = hose->private_data;
	unsigned int xive_num = hwirq - phb->msi_base;
	struct pnv_ioda_pe *pe;
	int rc;

	if (!(pe = pnv_ioda_get_pe(dev)))
		return -ENODEV;

	/* Assign XIVE to PE */
	rc = opal_pci_set_xive_pe(phb->opal_id, pe->pe_number, xive_num);
	if (rc) {
		pe_warn(pe, "%s: OPAL error %d setting msi_base 0x%x "
			"hwirq 0x%x XIVE 0x%x PE\n",
			pci_name(dev), rc, phb->msi_base, hwirq, xive_num);
		return -EIO;
	}
	pnv_set_msi_irq_chip(phb, virq);

	return 0;
}
EXPORT_SYMBOL(pnv_cxl_ioda_msi_setup);

#if IS_MODULE(CONFIG_CXL)
static inline int get_cxl_module(void)
{
	struct module *cxl_module;

	mutex_lock(&module_mutex);

	cxl_module = find_module("cxl");
	if (cxl_module)
		__module_get(cxl_module);

	mutex_unlock(&module_mutex);

	if (!cxl_module)
		return -ENODEV;

	return 0;
}
#else
static inline int get_cxl_module(void) { return 0; }
#endif

/*
 * Sets flags and switches the controller ops to enable the cxl kernel api.
 * Originally the cxl kernel API operated on a virtual PHB, but certain cards
 * such as the Mellanox CX4 use a peer model instead and for these cards the
 * cxl kernel api will operate on the real PHB.
 */
int pnv_cxl_enable_phb_kernel_api(struct pci_controller *hose, bool enable)
{
	struct pnv_phb *phb = hose->private_data;
	int rc;

	if (!enable) {
		/*
		 * Once cxl mode is enabled on the PHB, there is currently no
		 * known safe method to disable it again, and trying risks a
		 * checkstop. If we can find a way to safely disable cxl mode
		 * in the future we can revisit this, but for now the only sane
		 * thing to do is to refuse to disable cxl mode:
		 */
		return -EPERM;
	}

	/*
	 * Hold a reference to the cxl module since several PHB operations now
	 * depend on it, and it would be insane to allow it to be removed so
	 * long as we are in this mode (and since we can't safely disable this
	 * mode once enabled...).
	 */
	rc = get_cxl_module();
	if (rc)
		return rc;

	phb->flags |= PNV_PHB_FLAG_CXL;
	hose->controller_ops = pnv_cxl_cx4_ioda_controller_ops;

	return 0;
}
EXPORT_SYMBOL_GPL(pnv_cxl_enable_phb_kernel_api);

bool pnv_pci_on_cxl_phb(struct pci_dev *dev)
{
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	struct pnv_phb *phb = hose->private_data;

	return !!(phb->flags & PNV_PHB_FLAG_CXL);
}
EXPORT_SYMBOL_GPL(pnv_pci_on_cxl_phb);

struct cxl_afu *pnv_cxl_phb_to_afu(struct pci_controller *hose)
{
	struct pnv_phb *phb = hose->private_data;

	return (struct cxl_afu *)phb->cxl_afu;
}
EXPORT_SYMBOL_GPL(pnv_cxl_phb_to_afu);

void pnv_cxl_phb_set_peer_afu(struct pci_dev *dev, struct cxl_afu *afu)
{
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	struct pnv_phb *phb = hose->private_data;

	phb->cxl_afu = afu;
}
EXPORT_SYMBOL_GPL(pnv_cxl_phb_set_peer_afu);

/*
 * In the peer cxl model, the XSL/PSL is physical function 0, and will be used
 * by other functions on the device for memory access and interrupts. When the
 * other functions are enabled we explicitly take a reference on the cxl
 * function since they will use it, and allocate a default context associated
 * with that function just like the vPHB model of the cxl kernel API.
 */
bool pnv_cxl_enable_device_hook(struct pci_dev *dev)
{
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	struct pnv_phb *phb = hose->private_data;
	struct cxl_afu *afu = phb->cxl_afu;

	if (!pnv_pci_enable_device_hook(dev))
		return false;


	/* No special handling for the cxl function, which is always PF 0 */
	if (PCI_FUNC(dev->devfn) == 0)
		return true;

	if (!afu) {
		dev_WARN(&dev->dev, "Attempted to enable function > 0 on CXL PHB without a peer AFU\n");
		return false;
	}

	dev_info(&dev->dev, "Enabling function on CXL enabled PHB with peer AFU\n");

	/* Make sure the peer AFU can't go away while this device is active */
	cxl_afu_get(afu);

	return cxl_pci_associate_default_context(dev, afu);
}

void pnv_cxl_disable_device(struct pci_dev *dev)
{
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	struct pnv_phb *phb = hose->private_data;
	struct cxl_afu *afu = phb->cxl_afu;

	/* No special handling for cxl function: */
	if (PCI_FUNC(dev->devfn) == 0)
		return;

	cxl_pci_disable_device(dev);
	cxl_afu_put(afu);
}

/*
 * This is a special version of pnv_setup_msi_irqs for cards in cxl mode. This
 * function handles setting up the IVTE entries for the XSL to use.
 *
 * We are currently not filling out the MSIX table, since the only currently
 * supported adapter (CX4) uses a custom MSIX table format in cxl mode and it
 * is up to their driver to fill that out. In the future we may fill out the
 * MSIX table (and change the IVTE entries to be an index to the MSIX table)
 * for adapters implementing the Full MSI-X mode described in the CAIA.
 */
int pnv_cxl_cx4_setup_msi_irqs(struct pci_dev *pdev, int nvec, int type)
{
	struct pci_controller *hose = pci_bus_to_host(pdev->bus);
	struct pnv_phb *phb = hose->private_data;
	struct msi_desc *entry;
	struct cxl_context *ctx = NULL;
	unsigned int virq;
	int hwirq;
	int afu_irq = 0;
	int rc;

	if (WARN_ON(!phb) || !phb->msi_bmp.bitmap)
		return -ENODEV;

	if (pdev->no_64bit_msi && !phb->msi32_support)
		return -ENODEV;

	rc = cxl_cx4_setup_msi_irqs(pdev, nvec, type);
	if (rc)
		return rc;

	for_each_pci_msi_entry(entry, pdev) {
		if (!entry->msi_attrib.is_64 && !phb->msi32_support) {
			pr_warn("%s: Supports only 64-bit MSIs\n",
				pci_name(pdev));
			return -ENXIO;
		}

		hwirq = cxl_next_msi_hwirq(pdev, &ctx, &afu_irq);
		if (WARN_ON(hwirq <= 0))
			return (hwirq ? hwirq : -ENOMEM);

		virq = irq_create_mapping(NULL, hwirq);
		if (virq == NO_IRQ) {
			pr_warn("%s: Failed to map cxl mode MSI to linux irq\n",
				pci_name(pdev));
			return -ENOMEM;
		}

		rc = pnv_cxl_ioda_msi_setup(pdev, hwirq, virq);
		if (rc) {
			pr_warn("%s: Failed to setup cxl mode MSI\n", pci_name(pdev));
			irq_dispose_mapping(virq);
			return rc;
		}

		irq_set_msi_desc(virq, entry);
	}

	return 0;
}

void pnv_cxl_cx4_teardown_msi_irqs(struct pci_dev *pdev)
{
	struct pci_controller *hose = pci_bus_to_host(pdev->bus);
	struct pnv_phb *phb = hose->private_data;
	struct msi_desc *entry;
	irq_hw_number_t hwirq;

	if (WARN_ON(!phb))
		return;

	for_each_pci_msi_entry(entry, pdev) {
		if (entry->irq == NO_IRQ)
			continue;
		hwirq = virq_to_hw(entry->irq);
		irq_set_msi_desc(entry->irq, NULL);
		irq_dispose_mapping(entry->irq);
	}

	cxl_cx4_teardown_msi_irqs(pdev);
}
