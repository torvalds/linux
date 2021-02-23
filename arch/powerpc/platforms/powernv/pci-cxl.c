// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2014-2016 IBM Corp.
 */

#include <linux/module.h>
#include <asm/pnv-pci.h>
#include <asm/opal.h>

#include "pci.h"

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
