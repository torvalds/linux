// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * pci_dn.c
 *
 * Copyright (C) 2001 Todd Inglett, IBM Corporation
 *
 * PCI manipulation via device_nodes.
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
#include <asm/eeh.h>

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

#ifdef CONFIG_EEH
static struct eeh_dev *eeh_dev_init(struct pci_dn *pdn)
{
	struct eeh_dev *edev;

	/* Allocate EEH device */
	edev = kzalloc(sizeof(*edev), GFP_KERNEL);
	if (!edev)
		return NULL;

	/* Associate EEH device with OF node */
	pdn->edev = edev;
	edev->pdn = pdn;
	edev->bdfn = (pdn->busno << 8) | pdn->devfn;
	edev->controller = pdn->phb;

	return edev;
}
#endif /* CONFIG_EEH */

#ifdef CONFIG_PCI_IOV
static struct pci_dn *add_one_sriov_vf_pdn(struct pci_dn *parent,
					   int busno, int devfn)
{
	struct pci_dn *pdn;

	/* Except PHB, we always have the parent */
	if (!parent)
		return NULL;

	pdn = kzalloc(sizeof(*pdn), GFP_KERNEL);
	if (!pdn)
		return NULL;

	pdn->phb = parent->phb;
	pdn->parent = parent;
	pdn->busno = busno;
	pdn->devfn = devfn;
	pdn->pe_number = IODA_INVALID_PE;
	INIT_LIST_HEAD(&pdn->child_list);
	INIT_LIST_HEAD(&pdn->list);
	list_add_tail(&pdn->list, &parent->child_list);

	return pdn;
}

struct pci_dn *add_sriov_vf_pdns(struct pci_dev *pdev)
{
	struct pci_dn *parent, *pdn;
	int i;

	/* Only support IOV for now */
	if (WARN_ON(!pdev->is_physfn))
		return NULL;

	/* Check if VFs have been populated */
	pdn = pci_get_pdn(pdev);
	if (!pdn || (pdn->flags & PCI_DN_FLAG_IOV_VF))
		return NULL;

	pdn->flags |= PCI_DN_FLAG_IOV_VF;
	parent = pci_bus_to_pdn(pdev->bus);
	if (!parent)
		return NULL;

	for (i = 0; i < pci_sriov_get_totalvfs(pdev); i++) {
		struct eeh_dev *edev __maybe_unused;

		pdn = add_one_sriov_vf_pdn(parent,
					   pci_iov_virtfn_bus(pdev, i),
					   pci_iov_virtfn_devfn(pdev, i));
		if (!pdn) {
			dev_warn(&pdev->dev, "%s: Cannot create firmware data for VF#%d\n",
				 __func__, i);
			return NULL;
		}

#ifdef CONFIG_EEH
		/* Create the EEH device for the VF */
		edev = eeh_dev_init(pdn);
		BUG_ON(!edev);

		/* FIXME: these should probably be populated by the EEH probe */
		edev->physfn = pdev;
		edev->vf_index = i;
#endif /* CONFIG_EEH */
	}
	return pci_get_pdn(pdev);
}

void remove_sriov_vf_pdns(struct pci_dev *pdev)
{
	struct pci_dn *parent;
	struct pci_dn *pdn, *tmp;
	int i;

	/* Only support IOV PF for now */
	if (WARN_ON(!pdev->is_physfn))
		return;

	/* Check if VFs have been populated */
	pdn = pci_get_pdn(pdev);
	if (!pdn || !(pdn->flags & PCI_DN_FLAG_IOV_VF))
		return;

	pdn->flags &= ~PCI_DN_FLAG_IOV_VF;
	parent = pci_bus_to_pdn(pdev->bus);
	if (!parent)
		return;

	/*
	 * We might introduce flag to pci_dn in future
	 * so that we can release VF's firmware data in
	 * a batch mode.
	 */
	for (i = 0; i < pci_sriov_get_totalvfs(pdev); i++) {
		struct eeh_dev *edev __maybe_unused;

		list_for_each_entry_safe(pdn, tmp,
			&parent->child_list, list) {
			if (pdn->busno != pci_iov_virtfn_bus(pdev, i) ||
			    pdn->devfn != pci_iov_virtfn_devfn(pdev, i))
				continue;

#ifdef CONFIG_EEH
			/*
			 * Release EEH state for this VF. The PCI core
			 * has already torn down the pci_dev for this VF, but
			 * we're responsible to removing the eeh_dev since it
			 * has the same lifetime as the pci_dn that spawned it.
			 */
			edev = pdn_to_eeh_dev(pdn);
			if (edev) {
				/*
				 * We allocate pci_dn's for the totalvfs count,
				 * but only only the vfs that were activated
				 * have a configured PE.
				 */
				if (edev->pe)
					eeh_pe_tree_remove(edev);

				pdn->edev = NULL;
				kfree(edev);
			}
#endif /* CONFIG_EEH */

			if (!list_empty(&pdn->list))
				list_del(&pdn->list);

			kfree(pdn);
		}
	}
}
#endif /* CONFIG_PCI_IOV */

struct pci_dn *pci_add_device_node_info(struct pci_controller *hose,
					struct device_node *dn)
{
	const __be32 *type = of_get_property(dn, "ibm,pci-config-space-type", NULL);
	const __be32 *regs;
	struct device_node *parent;
	struct pci_dn *pdn;
#ifdef CONFIG_EEH
	struct eeh_dev *edev;
#endif

	pdn = kzalloc(sizeof(*pdn), GFP_KERNEL);
	if (pdn == NULL)
		return NULL;
	dn->data = pdn;
	pdn->phb = hose;
	pdn->pe_number = IODA_INVALID_PE;
	regs = of_get_property(dn, "reg", NULL);
	if (regs) {
		u32 addr = of_read_number(regs, 1);

		/* First register entry is addr (00BBSS00)  */
		pdn->busno = (addr >> 16) & 0xff;
		pdn->devfn = (addr >> 8) & 0xff;
	}

	/* vendor/device IDs and class code */
	regs = of_get_property(dn, "vendor-id", NULL);
	pdn->vendor_id = regs ? of_read_number(regs, 1) : 0;
	regs = of_get_property(dn, "device-id", NULL);
	pdn->device_id = regs ? of_read_number(regs, 1) : 0;
	regs = of_get_property(dn, "class-code", NULL);
	pdn->class_code = regs ? of_read_number(regs, 1) : 0;

	/* Extended config space */
	pdn->pci_ext_config_space = (type && of_read_number(type, 1) == 1);

	/* Create EEH device */
#ifdef CONFIG_EEH
	edev = eeh_dev_init(pdn);
	if (!edev) {
		kfree(pdn);
		return NULL;
	}
#endif

	/* Attach to parent node */
	INIT_LIST_HEAD(&pdn->child_list);
	INIT_LIST_HEAD(&pdn->list);
	parent = of_get_parent(dn);
	pdn->parent = parent ? PCI_DN(parent) : NULL;
	of_node_put(parent);
	if (pdn->parent)
		list_add_tail(&pdn->list, &pdn->parent->child_list);

	return pdn;
}
EXPORT_SYMBOL_GPL(pci_add_device_node_info);

void pci_remove_device_node_info(struct device_node *dn)
{
	struct pci_dn *pdn = dn ? PCI_DN(dn) : NULL;
	struct device_node *parent;
	struct pci_dev *pdev;
#ifdef CONFIG_EEH
	struct eeh_dev *edev = pdn_to_eeh_dev(pdn);

	if (edev)
		edev->pdn = NULL;
#endif

	if (!pdn)
		return;

	WARN_ON(!list_empty(&pdn->child_list));
	list_del(&pdn->list);

	/* Drop the parent pci_dn's ref to our backing dt node */
	parent = of_get_parent(dn);
	if (parent)
		of_node_put(parent);

	/*
	 * At this point we *might* still have a pci_dev that was
	 * instantiated from this pci_dn. So defer free()ing it until
	 * the pci_dev's release function is called.
	 */
	pdev = pci_get_domain_bus_and_slot(pdn->phb->global_number,
			pdn->busno, pdn->devfn);
	if (pdev) {
		/* NB: pdev has a ref to dn */
		pci_dbg(pdev, "marked pdn (from %pOF) as dead\n", dn);
		pdn->flags |= PCI_DN_FLAG_DEAD;
	} else {
		dn->data = NULL;
		kfree(pdn);
	}

	pci_dev_put(pdev);
}
EXPORT_SYMBOL_GPL(pci_remove_device_node_info);

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
void *pci_traverse_device_nodes(struct device_node *start,
				void *(*fn)(struct device_node *, void *),
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

		if (fn) {
			ret = fn(dn, data);
			if (ret)
				return ret;
		}

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
EXPORT_SYMBOL_GPL(pci_traverse_device_nodes);

static struct pci_dn *pci_dn_next_one(struct pci_dn *root,
				      struct pci_dn *pdn)
{
	struct list_head *next = pdn->child_list.next;

	if (next != &pdn->child_list)
		return list_entry(next, struct pci_dn, list);

	while (1) {
		if (pdn == root)
			return NULL;

		next = pdn->list.next;
		if (next != &pdn->parent->child_list)
			break;

		pdn = pdn->parent;
	}

	return list_entry(next, struct pci_dn, list);
}

void *traverse_pci_dn(struct pci_dn *root,
		      void *(*fn)(struct pci_dn *, void *),
		      void *data)
{
	struct pci_dn *pdn = root;
	void *ret;

	/* Only scan the child nodes */
	for (pdn = pci_dn_next_one(root, pdn); pdn;
	     pdn = pci_dn_next_one(root, pdn)) {
		ret = fn(pdn, data);
		if (ret)
			return ret;
	}

	return NULL;
}

static void *add_pdn(struct device_node *dn, void *data)
{
	struct pci_controller *hose = data;
	struct pci_dn *pdn;

	pdn = pci_add_device_node_info(hose, dn);
	if (!pdn)
		return ERR_PTR(-ENOMEM);

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
	pdn = pci_add_device_node_info(phb, dn);
	if (pdn) {
		pdn->devfn = pdn->busno = -1;
		pdn->vendor_id = pdn->device_id = pdn->class_code = 0;
		pdn->phb = phb;
		phb->pci_data = pdn;
	}

	/* Update dn->phb ptrs for new phb and children devices */
	pci_traverse_device_nodes(dn, add_pdn, phb);
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
static int __init pci_devs_phb_init(void)
{
	struct pci_controller *phb, *tmp;

	/* This must be done first so the device nodes have valid pci info! */
	list_for_each_entry_safe(phb, tmp, &hose_list, list_node)
		pci_devs_phb_init_dynamic(phb);

	return 0;
}

core_initcall(pci_devs_phb_init);

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
