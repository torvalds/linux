/*
 * Derived from "arch/powerpc/platforms/pseries/pci_dlpar.c"
 *
 * Copyright (C) 2003 Linda Xie <lxie@us.ibm.com>
 * Copyright (C) 2005 International Business Machines
 *
 * Updates, 2005, John Rose <johnrose@austin.ibm.com>
 * Updates, 2005, Linas Vepstas <linas@austin.ibm.com>
 * Updates, 2013, Gavin Shan <shangw@linux.vnet.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/pci.h>
#include <linux/export.h>
#include <asm/pci-bridge.h>
#include <asm/ppc-pci.h>
#include <asm/firmware.h>
#include <asm/eeh.h>

/**
 * __pcibios_remove_pci_devices - remove all devices under this bus
 * @bus: the indicated PCI bus
 * @purge_pe: destroy the PE on removal of PCI devices
 *
 * Remove all of the PCI devices under this bus both from the
 * linux pci device tree, and from the powerpc EEH address cache.
 * By default, the corresponding PE will be destroied during the
 * normal PCI hotplug path. For PCI hotplug during EEH recovery,
 * the corresponding PE won't be destroied and deallocated.
 */
void __pcibios_remove_pci_devices(struct pci_bus *bus, int purge_pe)
{
	struct pci_dev *dev, *tmp;
	struct pci_bus *child_bus;

	/* First go down child busses */
	list_for_each_entry(child_bus, &bus->children, node)
		__pcibios_remove_pci_devices(child_bus, purge_pe);

	pr_debug("PCI: Removing devices on bus %04x:%02x\n",
		 pci_domain_nr(bus),  bus->number);
	list_for_each_entry_safe(dev, tmp, &bus->devices, bus_list) {
		pr_debug("     * Removing %s...\n", pci_name(dev));
		eeh_remove_bus_device(dev, purge_pe);
		pci_stop_and_remove_bus_device(dev);
	}
}

/**
 * pcibios_remove_pci_devices - remove all devices under this bus
 * @bus: the indicated PCI bus
 *
 * Remove all of the PCI devices under this bus both from the
 * linux pci device tree, and from the powerpc EEH address cache.
 */
void pcibios_remove_pci_devices(struct pci_bus *bus)
{
	__pcibios_remove_pci_devices(bus, 1);
}
EXPORT_SYMBOL_GPL(pcibios_remove_pci_devices);

/**
 * pcibios_add_pci_devices - adds new pci devices to bus
 * @bus: the indicated PCI bus
 *
 * This routine will find and fixup new pci devices under
 * the indicated bus. This routine presumes that there
 * might already be some devices under this bridge, so
 * it carefully tries to add only new devices.  (And that
 * is how this routine differs from other, similar pcibios
 * routines.)
 */
void pcibios_add_pci_devices(struct pci_bus * bus)
{
	int slotno, num, mode, pass, max;
	struct pci_dev *dev;
	struct device_node *dn = pci_bus_to_OF_node(bus);

	eeh_add_device_tree_early(dn);

	mode = PCI_PROBE_NORMAL;
	if (ppc_md.pci_probe_mode)
		mode = ppc_md.pci_probe_mode(bus);

	if (mode == PCI_PROBE_DEVTREE) {
		/* use ofdt-based probe */
		of_rescan_bus(dn, bus);
	} else if (mode == PCI_PROBE_NORMAL) {
		/* use legacy probe */
		slotno = PCI_SLOT(PCI_DN(dn->child)->devfn);
		num = pci_scan_slot(bus, PCI_DEVFN(slotno, 0));
		if (!num)
			return;
		pcibios_setup_bus_devices(bus);
		max = bus->busn_res.start;
		for (pass = 0; pass < 2; pass++) {
			list_for_each_entry(dev, &bus->devices, bus_list) {
				if (dev->hdr_type == PCI_HEADER_TYPE_BRIDGE ||
				    dev->hdr_type == PCI_HEADER_TYPE_CARDBUS)
					max = pci_scan_bridge(bus, dev,
							      max, pass);
			}
		}
	}
	pcibios_finish_adding_to_bus(bus);
}
EXPORT_SYMBOL_GPL(pcibios_add_pci_devices);
