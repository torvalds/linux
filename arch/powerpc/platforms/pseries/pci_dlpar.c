// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PCI Dynamic LPAR, PCI Hot Plug and PCI EEH recovery code
 * for RPA-compliant PPC64 platform.
 * Copyright (C) 2003 Linda Xie <lxie@us.ibm.com>
 * Copyright (C) 2005 International Business Machines
 *
 * Updates, 2005, John Rose <johnrose@austin.ibm.com>
 * Updates, 2005, Linas Vepstas <linas@austin.ibm.com>
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

	pr_debug("PCI: Initializing new hotplug PHB %pOF\n", dn);

	phb = pcibios_alloc_controller(dn);
	if (!phb)
		return NULL;
	rtas_setup_phb(phb);
	pci_process_bridge_OF_ranges(phb, dn, 0);
	phb->controller_ops = pseries_pci_controller_ops;

	pci_devs_phb_init_dynamic(phb);

	pseries_msi_allocate_domains(phb);

	/* Create EEH devices for the PHB */
	eeh_phb_pe_create(phb);

	if (dn->child)
		pseries_eeh_init_edev_recursive(PCI_DN(dn));

	pcibios_scan_phb(phb);
	pcibios_finish_adding_to_bus(phb->bus);

	return phb;
}
EXPORT_SYMBOL_GPL(init_phb_dynamic);

/* RPA-specific bits for removing PHBs */
int remove_phb_dynamic(struct pci_controller *phb)
{
	struct pci_bus *b = phb->bus;
	struct pci_host_bridge *host_bridge = to_pci_host_bridge(b->bridge);
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

	pseries_msi_free_domains(phb);

	/* Remove the PCI bus and unregister the bridge device from sysfs */
	phb->bus = NULL;
	pci_remove_bus(b);
	host_bridge->bus = NULL;
	device_unregister(&host_bridge->dev);

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

	/*
	 * The pci_controller data structure is freed by
	 * the pcibios_free_controller_deferred() callback;
	 * see pseries_root_bridge_prepare().
	 */

	return 0;
}
EXPORT_SYMBOL_GPL(remove_phb_dynamic);
