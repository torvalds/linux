/*
 * pci_dn.c
 *
 * Copyright (C) 2001 Todd Inglett, IBM Corporation
 *
 * PCI manipulation via device_nodes.
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
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/gfp.h>

#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/ppc-pci.h>
#include <asm/firmware.h>

/*
 * The function is used to find the firmware data of one
 * specific PCI device, which is attached to the indicated
 * PCI bus. For VFs, their firmware data is linked to that
 * one of PF's bridge. For other devices, their firmware
 * data is linked to that of their bridge.
 */
static struct pci_dn *pci_bus_to_pdn(struct pci_bus *bus)
{
	struct pci_bus *pbus;
	struct device_node *dn;
	struct pci_dn *pdn;

	/*
	 * We probably have virtual bus which doesn't
	 * have associated bridge.
	 */
	pbus = bus;
	while (pbus) {
		if (pci_is_root_bus(pbus) || pbus->self)
			break;

		pbus = pbus->parent;
	}

	/*
	 * Except virtual bus, all PCI buses should
	 * have device nodes.
	 */
	dn = pci_bus_to_OF_node(pbus);
	pdn = dn ? PCI_DN(dn) : NULL;

	return pdn;
}

struct pci_dn *pci_get_pdn_by_devfn(struct pci_bus *bus,
				    int devfn)
{
	struct device_node *dn = NULL;
	struct pci_dn *parent, *pdn;
	struct pci_dev *pdev = NULL;

	/* Fast path: fetch from PCI device */
	list_for_each_entry(pdev, &bus->devices, bus_list) {
		if (pdev->devfn == devfn) {
			if (pdev->dev.archdata.pci_data)
				return pdev->dev.archdata.pci_data;

			dn = pci_device_to_OF_node(pdev);
			break;
		}
	}

	/* Fast path: fetch from device node */
	pdn = dn ? PCI_DN(dn) : NULL;
	if (pdn)
		return pdn;

	/* Slow path: fetch from firmware data hierarchy */
	parent = pci_bus_to_pdn(bus);
	if (!parent)
		return NULL;

	list_for_each_entry(pdn, &parent->child_list, list) {
		if (pdn->busno == bus->number &&
                    pdn->devfn == devfn)
                        return pdn;
        }

	return NULL;
}

struct pci_dn *pci_get_pdn(struct pci_dev *pdev)
{
	struct device_node *dn;
	struct pci_dn *parent, *pdn;

	/* Search device directly */
	if (pdev->dev.archdata.pci_data)
		return pdev->dev.archdata.pci_data;

	/* Check device node */
	dn = pci_device_to_OF_node(pdev);
	pdn = dn ? PCI_DN(dn) : NULL;
	if (pdn)
		return pdn;

	/*
	 * VFs don't have device nodes. We hook their
	 * firmware data to PF's bridge.
	 */
	parent = pci_bus_to_pdn(pdev->bus);
	if (!parent)
		return NULL;

	list_for_each_entry(pdn, &parent->child_list, list) {
		if (pdn->busno == pdev->bus->number &&
		    pdn->devfn == pdev->devfn)
			return pdn;
	}

	return NULL;
}

/*
 * Traverse_func that inits the PCI fields of the device node.
 * NOTE: this *must* be done before read/write config to the device.
 */
void *update_dn_pci_info(struct device_node *dn, void *data)
{
	struct pci_controller *phb = data;
	const __be32 *type = of_get_property(dn, "ibm,pci-config-space-type", NULL);
	const __be32 *regs;
	struct device_node *parent;
	struct pci_dn *pdn;

	pdn = zalloc_maybe_bootmem(sizeof(*pdn), GFP_KERNEL);
	if (pdn == NULL)
		return NULL;
	dn->data = pdn;
	pdn->node = dn;
	pdn->phb = phb;
#ifdef CONFIG_PPC_POWERNV
	pdn->pe_number = IODA_INVALID_PE;
#endif
	regs = of_get_property(dn, "reg", NULL);
	if (regs) {
		u32 addr = of_read_number(regs, 1);

		/* First register entry is addr (00BBSS00)  */
		pdn->busno = (addr >> 16) & 0xff;
		pdn->devfn = (addr >> 8) & 0xff;
	}

	pdn->pci_ext_config_space = (type && of_read_number(type, 1) == 1);

	/* Attach to parent node */
	INIT_LIST_HEAD(&pdn->child_list);
	INIT_LIST_HEAD(&pdn->list);
	parent = of_get_parent(dn);
	pdn->parent = parent ? PCI_DN(parent) : NULL;
	if (pdn->parent)
		list_add_tail(&pdn->list, &pdn->parent->child_list);

	return NULL;
}

/*
 * Traverse a device tree stopping each PCI device in the tree.
 * This is done depth first.  As each node is processed, a "pre"
 * function is called and the children are processed recursively.
 *
 * The "pre" func returns a value.  If non-zero is returned from
 * the "pre" func, the traversal stops and this value is returned.
 * This return value is useful when using traverse as a method of
 * finding a device.
 *
 * NOTE: we do not run the func for devices that do not appear to
 * be PCI except for the start node which we assume (this is good
 * because the start node is often a phb which may be missing PCI
 * properties).
 * We use the class-code as an indicator. If we run into
 * one of these nodes we also assume its siblings are non-pci for
 * performance.
 */
void *traverse_pci_devices(struct device_node *start, traverse_func pre,
		void *data)
{
	struct device_node *dn, *nextdn;
	void *ret;

	/* We started with a phb, iterate all childs */
	for (dn = start->child; dn; dn = nextdn) {
		const __be32 *classp;
		u32 class = 0;

		nextdn = NULL;
		classp = of_get_property(dn, "class-code", NULL);
		if (classp)
			class = of_read_number(classp, 1);

		if (pre && ((ret = pre(dn, data)) != NULL))
			return ret;

		/* If we are a PCI bridge, go down */
		if (dn->child && ((class >> 8) == PCI_CLASS_BRIDGE_PCI ||
				  (class >> 8) == PCI_CLASS_BRIDGE_CARDBUS))
			/* Depth first...do children */
			nextdn = dn->child;
		else if (dn->sibling)
			/* ok, try next sibling instead. */
			nextdn = dn->sibling;
		if (!nextdn) {
			/* Walk up to next valid sibling. */
			do {
				dn = dn->parent;
				if (dn == start)
					return NULL;
			} while (dn->sibling == NULL);
			nextdn = dn->sibling;
		}
	}
	return NULL;
}

/** 
 * pci_devs_phb_init_dynamic - setup pci devices under this PHB
 * phb: pci-to-host bridge (top-level bridge connecting to cpu)
 *
 * This routine is called both during boot, (before the memory
 * subsystem is set up, before kmalloc is valid) and during the 
 * dynamic lpar operation of adding a PHB to a running system.
 */
void pci_devs_phb_init_dynamic(struct pci_controller *phb)
{
	struct device_node *dn = phb->dn;
	struct pci_dn *pdn;

	/* PHB nodes themselves must not match */
	update_dn_pci_info(dn, phb);
	pdn = dn->data;
	if (pdn) {
		pdn->devfn = pdn->busno = -1;
		pdn->phb = phb;
		phb->pci_data = pdn;
	}

	/* Update dn->phb ptrs for new phb and children devices */
	traverse_pci_devices(dn, update_dn_pci_info, phb);
}

/** 
 * pci_devs_phb_init - Initialize phbs and pci devs under them.
 * 
 * This routine walks over all phb's (pci-host bridges) on the
 * system, and sets up assorted pci-related structures 
 * (including pci info in the device node structs) for each
 * pci device found underneath.  This routine runs once,
 * early in the boot sequence.
 */
void __init pci_devs_phb_init(void)
{
	struct pci_controller *phb, *tmp;

	/* This must be done first so the device nodes have valid pci info! */
	list_for_each_entry_safe(phb, tmp, &hose_list, list_node)
		pci_devs_phb_init_dynamic(phb);
}

static void pci_dev_pdn_setup(struct pci_dev *pdev)
{
	struct pci_dn *pdn;

	if (pdev->dev.archdata.pci_data)
		return;

	/* Setup the fast path */
	pdn = pci_get_pdn(pdev);
	pdev->dev.archdata.pci_data = pdn;
}
DECLARE_PCI_FIXUP_EARLY(PCI_ANY_ID, PCI_ANY_ID, pci_dev_pdn_setup);
