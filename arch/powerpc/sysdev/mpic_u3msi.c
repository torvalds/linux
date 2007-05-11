/*
 * Copyright 2006, Segher Boessenkool, IBM Corporation.
 * Copyright 2006-2007, Michael Ellerman, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2 of the
 * License.
 *
 */

#include <linux/irq.h>
#include <linux/bootmem.h>
#include <linux/msi.h>
#include <asm/mpic.h>
#include <asm/prom.h>
#include <asm/hw_irq.h>
#include <asm/ppc-pci.h>

#include "mpic.h"

/* A bit ugly, can we get this from the pci_dev somehow? */
static struct mpic *msi_mpic;

static void mpic_u3msi_mask_irq(unsigned int irq)
{
	mask_msi_irq(irq);
	mpic_mask_irq(irq);
}

static void mpic_u3msi_unmask_irq(unsigned int irq)
{
	mpic_unmask_irq(irq);
	unmask_msi_irq(irq);
}

static struct irq_chip mpic_u3msi_chip = {
	.shutdown	= mpic_u3msi_mask_irq,
	.mask		= mpic_u3msi_mask_irq,
	.unmask		= mpic_u3msi_unmask_irq,
	.eoi		= mpic_end_irq,
	.set_type	= mpic_set_irq_type,
	.typename	= "MPIC-U3MSI",
};

static u64 read_ht_magic_addr(struct pci_dev *pdev, unsigned int pos)
{
	u8 flags;
	u32 tmp;
	u64 addr;

	pci_read_config_byte(pdev, pos + HT_MSI_FLAGS, &flags);

	if (flags & HT_MSI_FLAGS_FIXED)
		return HT_MSI_FIXED_ADDR;

	pci_read_config_dword(pdev, pos + HT_MSI_ADDR_LO, &tmp);
	addr = tmp & HT_MSI_ADDR_LO_MASK;
	pci_read_config_dword(pdev, pos + HT_MSI_ADDR_HI, &tmp);
	addr = addr | ((u64)tmp << 32);

	return addr;
}

static u64 find_ht_magic_addr(struct pci_dev *pdev)
{
	struct pci_bus *bus;
	unsigned int pos;

	for (bus = pdev->bus; bus; bus = bus->parent) {
		pos = pci_find_ht_capability(bus->self, HT_CAPTYPE_MSI_MAPPING);
		if (pos)
			return read_ht_magic_addr(bus->self, pos);
	}

	return 0;
}

static int u3msi_msi_check_device(struct pci_dev *pdev, int nvec, int type)
{
	if (type == PCI_CAP_ID_MSIX)
		pr_debug("u3msi: MSI-X untested, trying anyway.\n");

	/* If we can't find a magic address then MSI ain't gonna work */
	if (find_ht_magic_addr(pdev) == 0) {
		pr_debug("u3msi: no magic address found for %s\n",
			 pci_name(pdev));
		return -ENXIO;
	}

	return 0;
}

static void u3msi_teardown_msi_irqs(struct pci_dev *pdev)
{
	struct msi_desc *entry;

        list_for_each_entry(entry, &pdev->msi_list, list) {
		if (entry->irq == NO_IRQ)
			continue;

		set_irq_msi(entry->irq, NULL);
		mpic_msi_free_hwirqs(msi_mpic, virq_to_hw(entry->irq), 1);
		irq_dispose_mapping(entry->irq);
	}

	return;
}

static void u3msi_compose_msi_msg(struct pci_dev *pdev, int virq,
				  struct msi_msg *msg)
{
	u64 addr;

	addr = find_ht_magic_addr(pdev);
	msg->address_lo = addr & 0xFFFFFFFF;
	msg->address_hi = addr >> 32;
	msg->data = virq_to_hw(virq);

	pr_debug("u3msi: allocated virq 0x%x (hw 0x%lx) at address 0x%lx\n",
		 virq, virq_to_hw(virq), addr);
}

static int u3msi_setup_msi_irqs(struct pci_dev *pdev, int nvec, int type)
{
	irq_hw_number_t hwirq;
	int rc;
	unsigned int virq;
	struct msi_desc *entry;
	struct msi_msg msg;

	list_for_each_entry(entry, &pdev->msi_list, list) {
		hwirq = mpic_msi_alloc_hwirqs(msi_mpic, 1);
		if (hwirq < 0) {
			rc = hwirq;
			pr_debug("u3msi: failed allocating hwirq\n");
			goto out_free;
		}

		virq = irq_create_mapping(msi_mpic->irqhost, hwirq);
		if (virq == NO_IRQ) {
			pr_debug("u3msi: failed mapping hwirq 0x%lx\n", hwirq);
			mpic_msi_free_hwirqs(msi_mpic, hwirq, 1);
			rc = -ENOSPC;
			goto out_free;
		}

		set_irq_msi(virq, entry);
		set_irq_chip(virq, &mpic_u3msi_chip);
		set_irq_type(virq, IRQ_TYPE_EDGE_RISING);

		u3msi_compose_msi_msg(pdev, virq, &msg);
		write_msi_msg(virq, &msg);

		hwirq++;
	}

	return 0;

 out_free:
	u3msi_teardown_msi_irqs(pdev);
	return rc;
}

int mpic_u3msi_init(struct mpic *mpic)
{
	int rc;

	rc = mpic_msi_init_allocator(mpic);
	if (rc) {
		pr_debug("u3msi: Error allocating bitmap!\n");
		return rc;
	}

	pr_debug("u3msi: Registering MPIC U3 MSI callbacks.\n");

	BUG_ON(msi_mpic);
	msi_mpic = mpic;

	WARN_ON(ppc_md.setup_msi_irqs);
	ppc_md.setup_msi_irqs = u3msi_setup_msi_irqs;
	ppc_md.teardown_msi_irqs = u3msi_teardown_msi_irqs;
	ppc_md.msi_check_device = u3msi_msi_check_device;

	return 0;
}
