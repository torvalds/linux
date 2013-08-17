/*
 * Copyright 2011 Michael Ellerman, IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/msi.h>
#include <linux/irq.h>
#include <linux/interrupt.h>

#include "msi.h"
#include "ics.h"
#include "wsp_pci.h"

/* Magic addresses for 32 & 64-bit MSIs with hardcoded MVE 0 */
#define MSI_ADDR_32		0xFFFF0000ul
#define MSI_ADDR_64		0x1000000000000000ul

int wsp_setup_msi_irqs(struct pci_dev *dev, int nvec, int type)
{
	struct pci_controller *phb;
	struct msi_desc *entry;
	struct msi_msg msg;
	unsigned int virq;
	int hwirq;

	phb = pci_bus_to_host(dev->bus);
	if (!phb)
		return -ENOENT;

	entry = list_first_entry(&dev->msi_list, struct msi_desc, list);
	if (entry->msi_attrib.is_64) {
		msg.address_lo = 0;
		msg.address_hi = MSI_ADDR_64 >> 32;
	} else {
		msg.address_lo = MSI_ADDR_32;
		msg.address_hi = 0;
	}

	list_for_each_entry(entry, &dev->msi_list, list) {
		hwirq = wsp_ics_alloc_irq(phb->dn, 1);
		if (hwirq < 0) {
			dev_warn(&dev->dev, "wsp_msi: hwirq alloc failed!\n");
			return hwirq;
		}

		virq = irq_create_mapping(NULL, hwirq);
		if (virq == NO_IRQ) {
			dev_warn(&dev->dev, "wsp_msi: virq alloc failed!\n");
			return -1;
		}

		dev_dbg(&dev->dev, "wsp_msi: allocated irq %#x/%#x\n",
			hwirq, virq);

		wsp_ics_set_msi_chip(virq);
		irq_set_msi_desc(virq, entry);
		msg.data = hwirq & XIVE_ADDR_MASK;
		write_msi_msg(virq, &msg);
	}

	return 0;
}

void wsp_teardown_msi_irqs(struct pci_dev *dev)
{
	struct pci_controller *phb;
	struct msi_desc *entry;
	int hwirq;

	phb = pci_bus_to_host(dev->bus);

	dev_dbg(&dev->dev, "wsp_msi: tearing down msi irqs\n");

	list_for_each_entry(entry, &dev->msi_list, list) {
		if (entry->irq == NO_IRQ)
			continue;

		irq_set_msi_desc(entry->irq, NULL);
		wsp_ics_set_std_chip(entry->irq);

		hwirq = virq_to_hw(entry->irq);
		/* In this order to avoid racing with irq_create_mapping() */
		irq_dispose_mapping(entry->irq);
		wsp_ics_free_irq(phb->dn, hwirq);
	}
}

void wsp_setup_phb_msi(struct pci_controller *phb)
{
	/* Create a single MVE at offset 0 that matches everything */
	out_be64(phb->cfg_data + PCIE_REG_IODA_ADDR, PCIE_REG_IODA_AD_TBL_MVT);
	out_be64(phb->cfg_data + PCIE_REG_IODA_DATA0, 1ull << 63);

	ppc_md.setup_msi_irqs = wsp_setup_msi_irqs;
	ppc_md.teardown_msi_irqs = wsp_teardown_msi_irqs;
}
