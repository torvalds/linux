/*
 * PCI Dynamic LPAR, PCI Hot Plug and PCI EEH recovery code
 * for RPA-compliant PPC64 platform.
 * Copyright (C) 2003 Linda Xie <lxie@us.ibm.com>
 * Copyright (C) 2005 International Business Machines
 *
 * Updates, 2005, John Rose <johnrose@austin.ibm.com>
 * Updates, 2005, Linas Vepstas <linas@austin.ibm.com>
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/pci.h>
#include <asm/pci-bridge.h>
#include <asm/ppc-pci.h>
#include <asm/firmware.h>

static struct pci_bus *
find_bus_among_children(struct pci_bus *bus,
                        struct device_node *dn)
{
	struct pci_bus *child = NULL;
	struct list_head *tmp;
	struct device_node *busdn;

	busdn = pci_bus_to_OF_node(bus);
	if (busdn == dn)
		return bus;

	list_for_each(tmp, &bus->children) {
		child = find_bus_among_children(pci_bus_b(tmp), dn);
		if (child)
			break;
	};
	return child;
}

struct pci_bus *
pcibios_find_pci_bus(struct device_node *dn)
{
	struct pci_dn *pdn = dn->data;

	if (!pdn  || !pdn->phb || !pdn->phb->bus)
		return NULL;

	return find_bus_among_children(pdn->phb->bus, dn);
}
EXPORT_SYMBOL_GPL(pcibios_find_pci_bus);

/**
 * pcibios_remove_pci_devices - remove all devices under this bus
 *
 * Remove all of the PCI devices under this bus both from the
 * linux pci device tree, and from the powerpc EEH address cache.
 */
void
pcibios_remove_pci_devices(struct pci_bus *bus)
{
	struct pci_dev *dev, *tmp;

	list_for_each_entry_safe(dev, tmp, &bus->devices, bus_list) {
		eeh_remove_bus_device(dev);
		pci_remove_bus_device(dev);
	}
}

/* Must be called before pci_bus_add_devices */
void
pcibios_fixup_new_pci_devices(struct pci_bus *bus, int fix_bus)
{
	struct pci_dev *dev;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		/*
		 * Skip already-present devices (which are on the
		 * global device list.)
		 */
		if (list_empty(&dev->global_list)) {
			int i;

			/* Need to setup IOMMU tables */
			ppc_md.iommu_dev_setup(dev);

			if(fix_bus)
				pcibios_fixup_device_resources(dev, bus);
			pci_read_irq_line(dev);
			for (i = 0; i < PCI_NUM_RESOURCES; i++) {
				struct resource *r = &dev->resource[i];

				if (r->parent || !r->start || !r->flags)
					continue;
				pci_claim_resource(dev, i);
			}
		}
	}

	eeh_add_device_tree_late(bus);
}
EXPORT_SYMBOL_GPL(pcibios_fixup_new_pci_devices);

static int
pcibios_pci_config_bridge(struct pci_dev *dev)
{
	u8 sec_busno;
	struct pci_bus *child_bus;

	/* Get busno of downstream bus */
	pci_read_config_byte(dev, PCI_SECONDARY_BUS, &sec_busno);

	/* Add to children of PCI bridge dev->bus */
	child_bus = pci_add_new_bus(dev->bus, dev, sec_busno);
	if (!child_bus) {
		printk (KERN_ERR "%s: could not add second bus\n", __FUNCTION__);
		return -EIO;
	}
	sprintf(child_bus->name, "PCI Bus #%02x", child_bus->number);

	pci_scan_child_bus(child_bus);

	/* Fixup new pci devices without touching bus struct */
	pcibios_fixup_new_pci_devices(child_bus, 0);

	/* Make the discovered devices available */
	pci_bus_add_devices(child_bus);
	return 0;
}

/**
 * pcibios_add_pci_devices - adds new pci devices to bus
 *
 * This routine will find and fixup new pci devices under
 * the indicated bus. This routine presumes that there
 * might already be some devices under this bridge, so
 * it carefully tries to add only new devices.  (And that
 * is how this routine differs from other, similar pcibios
 * routines.)
 */
void
pcibios_add_pci_devices(struct pci_bus * bus)
{
	int slotno, num, mode;
	struct pci_dev *dev;
	struct device_node *dn = pci_bus_to_OF_node(bus);

	eeh_add_device_tree_early(dn);

	mode = PCI_PROBE_NORMAL;
	if (ppc_md.pci_probe_mode)
		mode = ppc_md.pci_probe_mode(bus);

	if (mode == PCI_PROBE_DEVTREE) {
		/* use ofdt-based probe */
		of_scan_bus(dn, bus);
		if (!list_empty(&bus->devices)) {
			pcibios_fixup_new_pci_devices(bus, 0);
			pci_bus_add_devices(bus);
		}
	} else if (mode == PCI_PROBE_NORMAL) {
		/* use legacy probe */
		slotno = PCI_SLOT(PCI_DN(dn->child)->devfn);
		num = pci_scan_slot(bus, PCI_DEVFN(slotno, 0));
		if (num) {
			pcibios_fixup_new_pci_devices(bus, 1);
			pci_bus_add_devices(bus);
		}

		list_for_each_entry(dev, &bus->devices, bus_list)
			if (dev->hdr_type == PCI_HEADER_TYPE_BRIDGE)
				pcibios_pci_config_bridge(dev);
	}
}
EXPORT_SYMBOL_GPL(pcibios_add_pci_devices);

struct pci_controller * __devinit init_phb_dynamic(struct device_node *dn)
{
	struct pci_controller *phb;
	int primary;

	primary = list_empty(&hose_list);
	phb = pcibios_alloc_controller(dn);
	if (!phb)
		return NULL;
	setup_phb(dn, phb);
	pci_process_bridge_OF_ranges(phb, dn, 0);

	pci_setup_phb_io_dynamic(phb, primary);

	pci_devs_phb_init_dynamic(phb);

	if (dn->child)
		eeh_add_device_tree_early(dn);

	scan_phb(phb);
	pcibios_fixup_new_pci_devices(phb->bus, 0);
	pci_bus_add_devices(phb->bus);

	return phb;
}
EXPORT_SYMBOL_GPL(init_phb_dynamic);
