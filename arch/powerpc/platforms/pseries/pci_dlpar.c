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
#include <linux/export.h>
#include <asm/pci-bridge.h>
#include <asm/ppc-pci.h>
#include <asm/firmware.h>
#include <asm/eeh.h>

#include "pseries.h"

struct pci_controller *init_phb_dynamic(struct device_node *dn)
{
	struct pci_controller *phb;

	pr_debug("PCI: Initializing new hotplug PHB %s\n", dn->full_name);

	phb = pcibios_alloc_controller(dn);
	if (!phb)
		return NULL;
	rtas_setup_phb(phb);
	pci_process_bridge_OF_ranges(phb, dn, 0);
	phb->controller_ops = pseries_pci_controller_ops;

	pci_devs_phb_init_dynamic(phb);

	/* Create EEH devices for the PHB */
	eeh_dev_phb_init_dynamic(phb);

	if (dn->child)
		eeh_add_device_tree_early(PCI_DN(dn));

	pcibios_scan_phb(phb);
	pcibios_finish_adding_to_bus(phb->bus);

	return phb;
}
EXPORT_SYMBOL_GPL(init_phb_dynamic);

/* RPA-specific bits for removing PHBs */
int remove_phb_dynamic(struct pci_controller *phb)
{
	struct pci_bus *b = phb->bus;
	struct resource *res;
	int rc, i;

	pr_debug("PCI: Removing PHB %04x:%02x...\n",
		 pci_domain_nr(b), b->number);

	/* We cannot to remove a root bus that has children */
	if (!(list_empty(&b->children) && list_empty(&b->devices)))
		return -EBUSY;

	/* We -know- there aren't any child devices anymore at this stage
	 * and thus, we can safely unmap the IO space as it's not in use
	 */
	res = &phb->io_resource;
	if (res->flags & IORESOURCE_IO) {
		rc = pcibios_unmap_io_space(b);
		if (rc) {
			printk(KERN_ERR "%s: failed to unmap IO on bus %s\n",
			       __func__, b->name);
			return 1;
		}
	}

	/* Remove the PCI bus and unregister the bridge device from sysfs */
	phb->bus = NULL;
	pci_remove_bus(b);
	device_unregister(b->bridge);

	/* Now release the IO resource */
	if (res->flags & IORESOURCE_IO)
		release_resource(res);

	/* Release memory resources */
	for (i = 0; i < 3; ++i) {
		res = &phb->mem_resources[i];
		if (!(res->flags & IORESOURCE_MEM))
			continue;
		release_resource(res);
	}

	/* Free pci_controller data structure */
	pcibios_free_controller(phb);

	return 0;
}
EXPORT_SYMBOL_GPL(remove_phb_dynamic);
