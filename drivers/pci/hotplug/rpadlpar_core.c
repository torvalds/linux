/*
 * Interface for Dynamic Logical Partitioning of I/O Slots on
 * RPA-compliant PPC64 platform.
 *
 * John Rose <johnrose@austin.ibm.com>
 * Linda Xie <lxie@us.ibm.com>
 *
 * October 2003
 *
 * Copyright (C) 2003 IBM.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */
#include <linux/init.h>
#include <linux/pci.h>
#include <asm/pci-bridge.h>
#include <asm/semaphore.h>
#include <asm/rtas.h>
#include "../pci.h"
#include "rpaphp.h"
#include "rpadlpar.h"

static DECLARE_MUTEX(rpadlpar_sem);

#define NODE_TYPE_VIO  1
#define NODE_TYPE_SLOT 2
#define NODE_TYPE_PHB  3

static struct device_node *find_php_slot_vio_node(char *drc_name)
{
	struct device_node *child;
	struct device_node *parent = of_find_node_by_name(NULL, "vdevice");
	char *loc_code;

	if (!parent)
		return NULL;

	for (child = of_get_next_child(parent, NULL);
		child; child = of_get_next_child(parent, child)) {
		loc_code = get_property(child, "ibm,loc-code", NULL);
		if (loc_code && !strncmp(loc_code, drc_name, strlen(drc_name)))
			return child;
	}

	return NULL;
}

/* Find dlpar-capable pci node that contains the specified name and type */
static struct device_node *find_php_slot_pci_node(char *drc_name,
						  char *drc_type)
{
	struct device_node *np = NULL;
	char *name;
	char *type;
	int rc;

	while ((np = of_find_node_by_type(np, "pci"))) {
		rc = rpaphp_get_drc_props(np, NULL, &name, &type, NULL);
		if (rc == 0)
			if (!strcmp(drc_name, name) && !strcmp(drc_type, type))
				break;
	}

	return np;
}

static struct device_node *find_newly_added_node(char *drc_name, int *node_type)
{
	struct device_node *dn;

	dn = find_php_slot_pci_node(drc_name, "SLOT");
	if (dn) {
		*node_type = NODE_TYPE_SLOT;
		return dn;
	}

	dn = find_php_slot_pci_node(drc_name, "PHB");
	if (dn) {
		*node_type = NODE_TYPE_PHB;
		return dn;
	}

	dn = find_php_slot_vio_node(drc_name);
	if (dn) {
		*node_type = NODE_TYPE_VIO;
		return dn;
	}

	return NULL;
}

static struct slot *find_slot(char *drc_name)
{
	struct list_head *tmp, *n;
	struct slot *slot;

        list_for_each_safe(tmp, n, &rpaphp_slot_head) {
                slot = list_entry(tmp, struct slot, rpaphp_slot_list);
                if (strcmp(slot->location, drc_name) == 0)
                        return slot;
        }

        return NULL;
}

static void rpadlpar_claim_one_bus(struct pci_bus *b)
{
	struct list_head *ld;
	struct pci_bus *child_bus;

	for (ld = b->devices.next; ld != &b->devices; ld = ld->next) {
		struct pci_dev *dev = pci_dev_b(ld);
		int i;

		for (i = 0; i < PCI_NUM_RESOURCES; i++) {
			struct resource *r = &dev->resource[i];

			if (r->parent || !r->start || !r->flags)
				continue;
			rpaphp_claim_resource(dev, i);
		}
	}

	list_for_each_entry(child_bus, &b->children, node)
		rpadlpar_claim_one_bus(child_bus);
}

static int pci_add_secondary_bus(struct device_node *dn,
		struct pci_dev *bridge_dev)
{
	struct pci_controller *hose = dn->phb;
	struct pci_bus *child;
	u8 sec_busno;

	/* Get busno of downstream bus */
	pci_read_config_byte(bridge_dev, PCI_SECONDARY_BUS, &sec_busno);

	/* Allocate and add to children of bridge_dev->bus */
	child = pci_add_new_bus(bridge_dev->bus, bridge_dev, sec_busno);
	if (!child) {
		printk(KERN_ERR "%s: could not add secondary bus\n", __FUNCTION__);
		return -ENOMEM;
	}

	sprintf(child->name, "PCI Bus #%02x", child->number);

	/* Fixup subordinate bridge bases and resources */
	pcibios_fixup_bus(child);

	/* Claim new bus resources */
	rpadlpar_claim_one_bus(bridge_dev->bus);

	if (hose->last_busno < child->number)
		hose->last_busno = child->number;

	dn->bussubno = child->number;

	/* ioremap() for child bus, which may or may not succeed */
	remap_bus_range(child);

	return 0;
}

static struct pci_dev *dlpar_pci_add_bus(struct device_node *dn)
{
	struct pci_controller *hose = dn->phb;
	struct pci_dev *dev = NULL;

	/* Scan phb bus for EADS device, adding new one to bus->devices */
	if (!pci_scan_single_device(hose->bus, dn->devfn)) {
		printk(KERN_ERR "%s: found no device on bus\n", __FUNCTION__);
		return NULL;
	}

	/* Add new devices to global lists.  Register in proc, sysfs. */
	pci_bus_add_devices(hose->bus);

	/* Confirm new bridge dev was created */
	dev = rpaphp_find_pci_dev(dn);
	if (!dev) {
		printk(KERN_ERR "%s: failed to add pci device\n", __FUNCTION__);
		return NULL;
	}

	if (dev->hdr_type != PCI_HEADER_TYPE_BRIDGE) {
		printk(KERN_ERR "%s: unexpected header type %d\n",
			__FUNCTION__, dev->hdr_type);
		return NULL;
	}

	if (pci_add_secondary_bus(dn, dev))
		return NULL;

	return dev;
}

static int dlpar_pci_remove_bus(struct pci_dev *bridge_dev)
{
	struct pci_bus *secondary_bus;

	if (!bridge_dev) {
		printk(KERN_ERR "%s: unexpected null device\n",
			__FUNCTION__);
		return -EINVAL;
	}

	secondary_bus = bridge_dev->subordinate;

	if (unmap_bus_range(secondary_bus)) {
		printk(KERN_ERR "%s: failed to unmap bus range\n",
			__FUNCTION__);
		return -ERANGE;
	}

	pci_remove_bus_device(bridge_dev);
	return 0;
}

static inline int dlpar_add_pci_slot(char *drc_name, struct device_node *dn)
{
	struct pci_dev *dev;

	/* Add pci bus */
	dev = dlpar_pci_add_bus(dn);
	if (!dev) {
		printk(KERN_ERR "%s: unable to add bus %s\n", __FUNCTION__,
			drc_name);
		return -EIO;
	}

	return 0;
}

static int dlpar_remove_root_bus(struct pci_controller *phb)
{
	struct pci_bus *phb_bus;
	int rc;

	phb_bus = phb->bus;
	if (!(list_empty(&phb_bus->children) &&
	      list_empty(&phb_bus->devices))) {
		return -EBUSY;
	}

	rc = pcibios_remove_root_bus(phb);
	if (rc)
		return -EIO;

	device_unregister(phb_bus->bridge);
	pci_remove_bus(phb_bus);

	return 0;
}

static int dlpar_remove_phb(struct slot *slot)
{
	struct pci_controller *phb;
	struct device_node *dn;
	int rc = 0;

	dn = slot->dn;
	if (!dn) {
		printk(KERN_ERR "%s: unexpected NULL slot device node\n",
				__FUNCTION__);
		return -EIO;
	}

	phb = dn->phb;
	if (!phb) {
		printk(KERN_ERR "%s: unexpected NULL phb pointer\n",
				__FUNCTION__);
		return -EIO;
	}

	if (rpaphp_remove_slot(slot)) {
		printk(KERN_ERR "%s: unable to remove hotplug slot %s\n",
			__FUNCTION__, slot->location);
		return -EIO;
	}

	rc = dlpar_remove_root_bus(phb);
	if (rc < 0)
		return rc;

	return 0;
}

static int dlpar_add_phb(struct device_node *dn)
{
	struct pci_controller *phb;

	phb = init_phb_dynamic(dn);
	if (!phb)
		return -EINVAL;

	return 0;
}

/**
 * dlpar_add_slot - DLPAR add an I/O Slot
 * @drc_name: drc-name of newly added slot
 *
 * Make the hotplug module and the kernel aware
 * of a newly added I/O Slot.
 * Return Codes -
 * 0			Success
 * -ENODEV		Not a valid drc_name
 * -EINVAL		Slot already added
 * -ERESTARTSYS		Signalled before obtaining lock
 * -EIO			Internal PCI Error
 */
int dlpar_add_slot(char *drc_name)
{
	struct device_node *dn = NULL;
	int node_type;
	int rc = 0;

	if (down_interruptible(&rpadlpar_sem))
		return -ERESTARTSYS;

	/* Check for existing hotplug slot */
	if (find_slot(drc_name)) {
		rc = -EINVAL;
		goto exit;
	}

	dn = find_newly_added_node(drc_name, &node_type);
	if (!dn) {
		rc = -ENODEV;
		goto exit;
	}

	switch (node_type) {
		case NODE_TYPE_VIO:
			/* Just add hotplug slot */
			break;
		case NODE_TYPE_SLOT:
			rc = dlpar_add_pci_slot(drc_name, dn);
			break;
		case NODE_TYPE_PHB:
			rc = dlpar_add_phb(dn);
			break;
		default:
			printk("%s: unexpected node type\n", __FUNCTION__);
			return -EIO;
	}

	if (!rc && rpaphp_add_slot(dn)) {
		printk(KERN_ERR "%s: unable to add hotplug slot %s\n",
			__FUNCTION__, drc_name);
		rc = -EIO;
	}
exit:
	up(&rpadlpar_sem);
	return rc;
}

/**
 * dlpar_remove_vio_slot - DLPAR remove a virtual I/O Slot
 * @drc_name: drc-name of newly added slot
 *
 * Remove the kernel and hotplug representations
 * of an I/O Slot.
 * Return Codes:
 * 0			Success
 * -EIO			Internal  Error
 */
int dlpar_remove_vio_slot(struct slot *slot, char *drc_name)
{
	/* Remove hotplug slot */

	if (rpaphp_remove_slot(slot)) {
		printk(KERN_ERR "%s: unable to remove hotplug slot %s\n",
			__FUNCTION__, drc_name);
		return -EIO;
	}
	return 0;
}

/**
 * dlpar_remove_slot - DLPAR remove a PCI I/O Slot
 * @drc_name: drc-name of newly added slot
 *
 * Remove the kernel and hotplug representations
 * of a PCI I/O Slot.
 * Return Codes:
 * 0			Success
 * -ENODEV		Not a valid drc_name
 * -EIO			Internal PCI Error
 */
int dlpar_remove_pci_slot(struct slot *slot, char *drc_name)
{
	struct pci_dev *bridge_dev;

	bridge_dev = slot->bridge;
	if (!bridge_dev) {
		printk(KERN_ERR "%s: unexpected null bridge device\n",
			__FUNCTION__);
		return -EIO;
	}

	/* Remove hotplug slot */
	if (rpaphp_remove_slot(slot)) {
		printk(KERN_ERR "%s: unable to remove hotplug slot %s\n",
			__FUNCTION__, drc_name);
		return -EIO;
	}

	/* Remove pci bus */

	if (dlpar_pci_remove_bus(bridge_dev)) {
		printk(KERN_ERR "%s: unable to remove pci bus %s\n",
			__FUNCTION__, drc_name);
		return -EIO;
	}
	return 0;
}

/**
 * dlpar_remove_slot - DLPAR remove an I/O Slot
 * @drc_name: drc-name of newly added slot
 *
 * Remove the kernel and hotplug representations
 * of an I/O Slot.
 * Return Codes:
 * 0			Success
 * -ENODEV		Not a valid drc_name
 * -EINVAL		Slot already removed
 * -ERESTARTSYS		Signalled before obtaining lock
 * -EIO			Internal Error
 */
int dlpar_remove_slot(char *drc_name)
{
	struct slot *slot;
	int rc = 0;

	if (down_interruptible(&rpadlpar_sem))
		return -ERESTARTSYS;

	if (!find_php_slot_vio_node(drc_name) &&
	    !find_php_slot_pci_node(drc_name, "SLOT") &&
	    !find_php_slot_pci_node(drc_name, "PHB")) {
		rc = -ENODEV;
		goto exit;
	}

	slot = find_slot(drc_name);
	if (!slot) {
		rc = -EINVAL;
		goto exit;
	}
	
	if (slot->type == PHB) {
		rc = dlpar_remove_phb(slot);
	} else {
		switch (slot->dev_type) {
			case PCI_DEV:
				rc = dlpar_remove_pci_slot(slot, drc_name);
				break;

			case VIO_DEV:
				rc = dlpar_remove_vio_slot(slot, drc_name);
				break;
		}
	}
exit:
	up(&rpadlpar_sem);
	return rc;
}

static inline int is_dlpar_capable(void)
{
	int rc = rtas_token("ibm,configure-connector");

	return (int) (rc != RTAS_UNKNOWN_SERVICE);
}

int __init rpadlpar_io_init(void)
{
	int rc = 0;

	if (!is_dlpar_capable()) {
		printk(KERN_WARNING "%s: partition not DLPAR capable\n",
			__FUNCTION__);
		return -EPERM;
	}

	rc = dlpar_sysfs_init();
	return rc;
}

void rpadlpar_io_exit(void)
{
	dlpar_sysfs_exit();
	return;
}

module_init(rpadlpar_io_init);
module_exit(rpadlpar_io_exit);
MODULE_LICENSE("GPL");
