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
#include <asm/msi_bitmap.h>

#include "mpic.h"

/* A bit ugly, can we get this from the pci_dev somehow? */
static struct mpic *msi_mpic;

static void mpic_u3msi_mask_irq(struct irq_data *data)
{
	mask_msi_irq(data);
	mpic_mask_irq(data);
}

static void mpic_u3msi_unmask_irq(struct irq_data *data)
{
	mpic_unmask_irq(data);
	unmask_msi_irq(data);
}

static struct irq_chip mpic_u3msi_chip = {
	.irq_shutdown		= mpic_u3msi_mask_irq,
	.irq_mask		= mpic_u3msi_mask_irq,
	.irq_unmask		= mpic_u3msi_unmask_irq,
	.irq_eoi		= mpic_end_irq,
	.irq_set_type		= mpic_set_irq_type,
	.irq_set_affinity	= mpic_set_affinity,
	.name			= "MPIC-U3MSI",
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

static u64 find_ht_magic_addr(struct pci_dev *pdev, unsigned int hwirq)
{
	struct pci_bus *bus;
	unsigned int pos;

	for (bus = pdev->bus; bus && bus->self; bus = bus->parent) {
		pos = pci_find_ht_capability(bus->self, HT_CAPTYPE_MSI_MAPPING);
		if (pos)
			return read_ht_magic_addr(bus->self, pos);
	}

	return 0;
}

static u64 find_u4_magic_addr(struct pci_dev *pdev, unsigned int hwirq)
{
	struct pci_controller *hose = pci_bus_to_host(pdev->bus);

	/* U4 PCIe MSIs need to write to the special register in
	 * the bridge that generates interrupts. There should be
	 * theorically a register at 0xf8005000 where you just write
	 * the MSI number and that triggers the right interrupt, but
	 * unfortunately, this is busted in HW, the bridge endian swaps
	 * the value and hits the wrong nibble in the register.
	 *
	 * So instead we use another register set which is used normally
	 * for converting HT interrupts to MPIC interrupts, which decodes
	 * the interrupt number as part of the low address bits
	 *
	 * This will not work if we ever use more than one legacy MSI in
	 * a block but we never do. For one MSI or multiple MSI-X where
	 * each interrupt address can be specified separately, it works
	 * just fine.
	 */
	if (of_device_is_compatible(hose->dn, "u4-pcie") ||
	    of_device_is_compatible(hose->dn, "U4-pcie"))
		return 0xf8004000 | (hwirq << 4);

	return 0;
}

static void u3msi_teardown_msi_irqs(struct pci_dev *pdev)
{
	struct msi_desc *entry;

        list_for_each_entry(entry, &pdev->msi_list, list) {
		if (entry->irq == NO_IRQ)
			continue;

		irq_set_msi_desc(entry->irq, NULL);
		msi_bitmap_free_hwirqs(&msi_mpic->msi_bitmap,
				       virq_to_hw(entry->irq), 1);
		irq_dispose_mapping(entry->irq);
	}

	return;
}

static int u3msi_setup_msi_irqs(struct pci_dev *pdev, int nvec, int type)
{
	unsigned int virq;
	struct msi_desc *entry;
	struct msi_msg msg;
	u64 addr;
	int hwirq;

	if (type == PCI_CAP_ID_MSIX)
		pr_debug("u3msi: MSI-X untested, trying anyway.\n");

	/* If we can't find a magic address then MSI ain't gonna work */
	if (find_ht_magic_addr(pdev, 0) == 0 &&
	    find_u4_magic_addr(pdev, 0) == 0) {
		pr_debug("u3msi: no magic address found for %s\n",
			 pci_name(pdev));
		return -ENXIO;
	}

	list_for_each_entry(entry, &pdev->msi_list, list) {
		hwirq = msi_bitmap_alloc_hwirqs(&msi_mpic->msi_bitmap, 1);
		if (hwirq < 0) {
			pr_debug("u3msi: failed allocating hwirq\n");
			return hwirq;
		}

		addr = find_ht_magic_addr(pdev, hwirq);
		if (addr == 0)
			addr = find_u4_magic_addr(pdev, hwirq);
		msg.address_lo = addr & 0xFFFFFFFF;
		msg.address_hi = addr >> 32;

		virq = irq_create_mapping(msi_mpic->irqhost, hwirq);
		if (virq == NO_IRQ) {
			pr_debug("u3msi: failed mapping hwirq 0x%x\n", hwirq);
			msi_bitmap_free_hwirqs(&msi_mpic->msi_bitmap, hwirq, 1);
			return -ENOSPC;
		}

		irq_set_msi_desc(virq, entry);
		irq_set_chip(virq, &mpic_u3msi_chip);
		irq_set_irq_type(virq, IRQ_TYPE_EDGE_RISING);

		pr_debug("u3msi: allocated virq 0x%x (hw 0x%x) addr 0x%lx\n",
			  virq, hwirq, (unsigned long)addr);

		printk("u3msi: allocated virq 0x%x (hw 0x%x) addr 0x%lx\n",
			  virq, hwirq, (unsigned long)addr);
		msg.data = hwirq;
		pci_write_msi_msg(virq, &msg);

		hwirq++;
	}

	return 0;
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

	return 0;
}
