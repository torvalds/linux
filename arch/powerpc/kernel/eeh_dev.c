// SPDX-License-Identifier: GPL-2.0-or-later
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
 *
 * It will create EEH device according to the given OF node. The function
 * might be called by PCI emunation, DR, PHB hotplug.
 */
struct eeh_dev *eeh_dev_init(struct pci_dn *pdn)
{
	struct eeh_dev *edev;

	/* Allocate EEH device */
	edev = kzalloc(sizeof(*edev), GFP_KERNEL);
	if (!edev)
		return NULL;

	/* Associate EEH device with OF node */
	pdn->edev = edev;
	edev->pdn = pdn;

	return edev;
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
	/* EEH PE for PHB */
	eeh_phb_pe_create(phb);
}
