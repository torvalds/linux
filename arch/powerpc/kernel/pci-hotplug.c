// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Derived from "arch/powerpc/platforms/pseries/pci_dlpar.c"
 *
 * Copyright (C) 2003 Linda Xie <lxie@us.ibm.com>
 * Copyright (C) 2005 International Business Machines
 *
 * Updates, 2005, John Rose <johnrose@austin.ibm.com>
 * Updates, 2005, Linas Vepstas <linas@austin.ibm.com>
 * Updates, 2013, Gavin Shan <shangw@linux.vnet.ibm.com>
 */

#include <linux/pci.h>
#include <linux/export.h>
#include <asm/pci-bridge.h>
#include <asm/ppc-pci.h>
#include <asm/firmware.h>
#include <asm/eeh.h>

static struct pci_bus *find_bus_among_children(struct pci_bus *bus,
					       struct device_node *dn)
{
	struct pci_bus *child = NULL;
	struct pci_bus *tmp;

	if (pci_bus_to_OF_node(bus) == dn)
		return bus;

	list_for_each_entry(tmp, &bus->children, node) {
		child = find_bus_among_children(tmp, dn);
		if (child)
			break;
	}

	return child;
}

struct pci_bus *pci_find_bus_by_node(struct device_node *dn)
{
	struct pci_dn *pdn = PCI_DN(dn);

	if (!pdn  || !pdn->phb || !pdn->phb->bus)
		return NULL;

	return find_bus_among_children(pdn->phb->bus, dn);
}
EXPORT_SYMBOL_GPL(pci_find_bus_by_node);

/**
 * pcibios_release_device - release PCI device
 * @dev: PCI device
 *
 * The function is called before releasing the indicated PCI device.
 */
void pcibios_release_device(struct pci_dev *dev)
{
	struct pci_controller *phb = pci_bus_to_host(dev->bus);
	struct pci_dn *pdn = pci_get_pdn(dev);

	eeh_remove_device(dev);

	if (phb->controller_ops.release_device)
		phb->controller_ops.release_device(dev);

	/* free()ing the pci_dn has been deferred to us, do it now */
	if (pdn && (pdn->flags & PCI_DN_FLAG_DEAD)) {
		pci_dbg(dev, "freeing dead pdn\n");
		kfree(pdn);
	}
}

/**
 * pci_hp_remove_devices - remove all devices under this bus
 * @bus: the indicated PCI bus
 *
 * Remove all of the PCI devices under this bus both from the
 * linux pci device tree, and from the powerpc EEH address cache.
 */
void pci_hp_remove_devices(struct pci_bus *bus)
{
	struct pci_dev *dev, *tmp;
	struct pci_bus *child_bus;

	/* First go down child busses */
	list_for_each_entry(child_bus, &bus->children, node)
		pci_hp_remove_devices(child_bus);

	pr_debug("PCI: Removing devices on bus %04x:%02x\n",
		 pci_domain_nr(bus),  bus->number);
	list_for_each_entry_safe_reverse(dev, tmp, &bus->devices, bus_list) {
		pr_debug("   Removing %s...\n", pci_name(dev));
		pci_stop_and_remove_bus_device(dev);
	}
}
EXPORT_SYMBOL_GPL(pci_hp_remove_devices);

/**
 * pci_hp_add_devices - adds new pci devices to bus
 * @bus: the indicated PCI bus
 *
 * This routine will find and fixup new pci devices under
 * the indicated bus. This routine presumes that there
 * might already be some devices under this bridge, so
 * it carefully tries to add only new devices.  (And that
 * is how this routine differs from other, similar pcibios
 * routines.)
 */
void pci_hp_add_devices(struct pci_bus *bus)
{
	int slotno, mode, max;
	struct pci_dev *dev;
	struct pci_controller *phb;
	struct device_node *dn = pci_bus_to_OF_node(bus);

	eeh_add_device_tree_early(PCI_DN(dn));

	phb = pci_bus_to_host(bus);

	mode = PCI_PROBE_NORMAL;
	if (phb->controller_ops.probe_mode)
		mode = phb->controller_ops.probe_mode(bus);

	if (mode == PCI_PROBE_DEVTREE) {
		/* use ofdt-based probe */
		of_rescan_bus(dn, bus);
	} else if (mode == PCI_PROBE_NORMAL &&
		   dn->child && PCI_DN(dn->child)) {
		/*
		 * Use legacy probe. In the partial hotplug case, we
		 * probably have grandchildren devices unplugged. So
		 * we don't check the return value from pci_scan_slot() in
		 * order for fully rescan all the way down to pick them up.
		 * They can have been removed during partial hotplug.
		 */
		slotno = PCI_SLOT(PCI_DN(dn->child)->devfn);
		pci_scan_slot(bus, PCI_DEVFN(slotno, 0));
		max = bus->busn_res.start;
		/*
		 * Scan bridges that are already configured. We don't touch
		 * them unless they are misconfigured (which will be done in
		 * the second scan below).
		 */
		for_each_pci_bridge(dev, bus)
			max = pci_scan_bridge(bus, dev, max, 0);

		/* Scan bridges that need to be reconfigured */
		for_each_pci_bridge(dev, bus)
			max = pci_scan_bridge(bus, dev, max, 1);
	}
	pcibios_finish_adding_to_bus(bus);
}
EXPORT_SYMBOL_GPL(pci_hp_add_devices);
