/*
 * Copyright 2001 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 *
 * arch/mips/gt64120/momenco_ocelot/pci.c
 *     Board-specific PCI routines for gt64120 controller.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/pci.h>


void __devinit pcibios_fixup_bus(struct pci_bus *bus)
{
	struct pci_bus *current_bus = bus;
	struct pci_dev *devices;
	struct list_head *devices_link;
	u16 cmd;

	list_for_each(devices_link, &(current_bus->devices)) {

		devices = pci_dev_b(devices_link);
		if (devices == NULL)
			continue;

		if (PCI_SLOT(devices->devfn) == 1) {
			/*
			 * Slot 1 is primary ether port, i82559
			 * we double-check against that assumption
			 */
			if ((devices->vendor != 0x8086) ||
			    (devices->device != 0x1209)) {
				panic("pcibios_fixup_bus: found "
				     "unexpected PCI device in slot 1.");
			}
			devices->irq = 2;	/* irq_nr is 2 for INT0 */
		} else if (PCI_SLOT(devices->devfn) == 2) {
			/*
			 * Slot 2 is secondary ether port, i21143
			 * we double-check against that assumption
			 */
			if ((devices->vendor != 0x1011) ||
			    (devices->device != 0x19)) {
				panic("galileo_pcibios_fixup_bus: "
				      "found unexpected PCI device in slot 2.");
			}
			devices->irq = 3;	/* irq_nr is 3 for INT1 */
		} else if (PCI_SLOT(devices->devfn) == 4) {
			/* PMC Slot 1 */
			devices->irq = 8;	/* irq_nr is 8 for INT6 */
		} else if (PCI_SLOT(devices->devfn) == 5) {
			/* PMC Slot 1 */
			devices->irq = 9;	/* irq_nr is 9 for INT7 */
		} else {
			/* We don't have assign interrupts for other devices. */
			devices->irq = 0xff;
		}

		/* Assign an interrupt number for the device */
		bus->ops->write_byte(devices, PCI_INTERRUPT_LINE,
				     devices->irq);

		/* enable master */
		bus->ops->read_word(devices, PCI_COMMAND, &cmd);
		cmd |= PCI_COMMAND_MASTER;
		bus->ops->write_word(devices, PCI_COMMAND, cmd);
	}
}
