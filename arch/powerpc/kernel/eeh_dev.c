/*
 * The file intends to implement dynamic creation of EEH device, which will
 * be bound with OF node and PCI device simutaneously. The EEH devices would
 * be foundamental information for EEH core components to work proerly. Besides,
 * We have to support multiple situations where dynamic creation of EEH device
 * is required:
 *
 * 1) Before PCI emunation starts, we need create EEH devices according to the
 *    PCI sensitive OF nodes.
 * 2) When PCI emunation is done, we need do the binding between PCI device and
 *    the associated EEH device.
 * 3) DR (Dynamic Reconfiguration) would create PCI sensitive OF node. EEH device
 *    will be created while PCI sensitive OF node is detected from DR.
 * 4) PCI hotplug needs redoing the binding between PCI device and EEH device. If
 *    PHB is newly inserted, we also need create EEH devices accordingly.
 *
 * Copyright Benjamin Herrenschmidt & Gavin Shan, IBM Corporation 2012.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/export.h>
#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/string.h>

#include <asm/pci-bridge.h>
#include <asm/ppc-pci.h>

/**
 * eeh_dev_init - Create EEH device according to OF node
 * @pdn: PCI device node
 * @data: PHB
 *
 * It will create EEH device according to the given OF node. The function
 * might be called by PCI emunation, DR, PHB hotplug.
 */
void *eeh_dev_init(struct pci_dn *pdn, void *data)
{
	struct pci_controller *phb = data;
	struct eeh_dev *edev;

	/* Allocate EEH device */
	edev = kzalloc(sizeof(*edev), GFP_KERNEL);
	if (!edev) {
		pr_warn("%s: out of memory\n",
			__func__);
		return NULL;
	}

	/* Associate EEH device with OF node */
	pdn->edev = edev;
	edev->pdn = pdn;
	edev->phb = phb;
	INIT_LIST_HEAD(&edev->list);
	INIT_LIST_HEAD(&edev->rmv_list);

	return NULL;
}

/**
 * eeh_dev_phb_init_dynamic - Create EEH devices for devices included in PHB
 * @phb: PHB
 *
 * Scan the PHB OF node and its child association, then create the
 * EEH devices accordingly
 */
void eeh_dev_phb_init_dynamic(struct pci_controller *phb)
{
	struct pci_dn *root = phb->pci_data;

	/* EEH PE for PHB */
	eeh_phb_pe_create(phb);

	/* EEH device for PHB */
	eeh_dev_init(root, phb);

	/* EEH devices for children OF nodes */
	traverse_pci_dn(root, eeh_dev_init, phb);
}

/**
 * eeh_dev_phb_init - Create EEH devices for devices included in existing PHBs
 *
 * Scan all the existing PHBs and create EEH devices for their OF
 * nodes and their children OF nodes
 */
static int __init eeh_dev_phb_init(void)
{
	struct pci_controller *phb, *tmp;

	list_for_each_entry_safe(phb, tmp, &hose_list, list_node)
		eeh_dev_phb_init_dynamic(phb);

	pr_info("EEH: devices created\n");

	return 0;
}

core_initcall(eeh_dev_phb_init);
