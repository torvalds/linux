/*
 * PCI Hot Plug Controller Driver for RPA-compliant PPC64 platform.
 * Copyright (C) 2003 Linda Xie <lxie@us.ibm.com>
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
 *
 * Send feedback to <lxie@us.ibm.com>
 *
 */
#include <linux/pci.h>
#include <asm/pci-bridge.h>
#include <asm/rtas.h>
#include <asm/machdep.h>
#include "../pci.h"		/* for pci_add_new_bus */

#include "rpaphp.h"

struct pci_dev *rpaphp_find_pci_dev(struct device_node *dn)
{
	struct pci_dev *dev = NULL;
	char bus_id[BUS_ID_SIZE];

	sprintf(bus_id, "%04x:%02x:%02x.%d", dn->phb->global_number,
		dn->busno, PCI_SLOT(dn->devfn), PCI_FUNC(dn->devfn));
	for_each_pci_dev(dev) {
		if (!strcmp(pci_name(dev), bus_id)) {
			break;
		}
	}
	return dev;
}

EXPORT_SYMBOL_GPL(rpaphp_find_pci_dev);

int rpaphp_claim_resource(struct pci_dev *dev, int resource)
{
	struct resource *res = &dev->resource[resource];
	struct resource *root = pci_find_parent_resource(dev, res);
	char *dtype = resource < PCI_BRIDGE_RESOURCES ? "device" : "bridge";
	int err = -EINVAL;

	if (root != NULL) {
		err = request_resource(root, res);
	}

	if (err) {
		err("PCI: %s region %d of %s %s [%lx:%lx]\n",
		    root ? "Address space collision on" :
		    "No parent found for",
		    resource, dtype, pci_name(dev), res->start, res->end);
	}
	return err;
}

EXPORT_SYMBOL_GPL(rpaphp_claim_resource);

static struct pci_dev *rpaphp_find_bridge_pdev(struct slot *slot)
{
	return rpaphp_find_pci_dev(slot->dn);
}

static int rpaphp_get_sensor_state(struct slot *slot, int *state)
{
	int rc;
	int setlevel;

	rc = rtas_get_sensor(DR_ENTITY_SENSE, slot->index, state);

	if (rc < 0) {
		if (rc == -EFAULT || rc == -EEXIST) {
			dbg("%s: slot must be power up to get sensor-state\n",
			    __FUNCTION__);

			/* some slots have to be powered up 
			 * before get-sensor will succeed.
			 */
			rc = rtas_set_power_level(slot->power_domain, POWER_ON,
						  &setlevel);
			if (rc < 0) {
				dbg("%s: power on slot[%s] failed rc=%d.\n",
				    __FUNCTION__, slot->name, rc);
			} else {
				rc = rtas_get_sensor(DR_ENTITY_SENSE,
						     slot->index, state);
			}
		} else if (rc == -ENODEV)
			info("%s: slot is unusable\n", __FUNCTION__);
		else
			err("%s failed to get sensor state\n", __FUNCTION__);
	}
	return rc;
}

/**
 * get_pci_adapter_status - get the status of a slot
 * 
 * 0-- slot is empty
 * 1-- adapter is configured
 * 2-- adapter is not configured
 * 3-- not valid
 */
int rpaphp_get_pci_adapter_status(struct slot *slot, int is_init, u8 * value)
{
	int state, rc;
 	struct device_node *child_dn;
 	struct pci_dev *child_dev = NULL;

	*value = NOT_VALID;
	rc = rpaphp_get_sensor_state(slot, &state);
	if (rc)
		goto exit;

 	if ((state == EMPTY) || (slot->type == PHB)) {
 		dbg("slot is empty\n");
 		*value = EMPTY;
 	}
 	else if (state == PRESENT) {
		if (!is_init) {
			/* at run-time slot->state can be changed by */
			/* config/unconfig adapter */
			*value = slot->state;
		} else {
 			child_dn = slot->dn->child;
 			if (child_dn)
 				child_dev = rpaphp_find_pci_dev(child_dn);

 			if (child_dev)
 				*value = CONFIGURED;
 			else if (!child_dn)
				dbg("%s: %s is not valid OFDT node\n",
				    __FUNCTION__, slot->dn->full_name);
			else {
				err("%s: can't find pdev of adapter in slot[%s]\n", 
					__FUNCTION__, slot->dn->full_name);
				*value = NOT_CONFIGURED;
			}
		}
	}
exit:
	return rc;
}

/* Must be called before pci_bus_add_devices */
static void 
rpaphp_fixup_new_pci_devices(struct pci_bus *bus, int fix_bus)
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
				rpaphp_claim_resource(dev, i);
			}
		}
	}
}

static int rpaphp_pci_config_bridge(struct pci_dev *dev);

/*****************************************************************************
 rpaphp_pci_config_slot() will  configure all devices under the 
 given slot->dn and return the the first pci_dev.
 *****************************************************************************/
static struct pci_dev *
rpaphp_pci_config_slot(struct device_node *dn, struct pci_bus *bus)
{
	struct device_node *eads_first_child = dn->child;
	struct pci_dev *dev = NULL;
	int num;
	
	dbg("Enter %s: dn=%s bus=%s\n", __FUNCTION__, dn->full_name, bus->name);

	if (eads_first_child) {
		/* pci_scan_slot should find all children of EADs */
		num = pci_scan_slot(bus, PCI_DEVFN(PCI_SLOT(eads_first_child->devfn), 0));
		if (num) {
			rpaphp_fixup_new_pci_devices(bus, 1); 
			pci_bus_add_devices(bus);
		}
		dev = rpaphp_find_pci_dev(eads_first_child);
		if (!dev) {
			err("No new device found\n");
			return NULL;
		}
		if (dev->hdr_type == PCI_HEADER_TYPE_BRIDGE) 
			rpaphp_pci_config_bridge(dev);
	}
	return dev;
}

static int rpaphp_pci_config_bridge(struct pci_dev *dev)
{
	u8 sec_busno;
	struct pci_bus *child_bus;
	struct pci_dev *child_dev;

	dbg("Enter %s:  BRIDGE dev=%s\n", __FUNCTION__, pci_name(dev));

	/* get busno of downstream bus */
	pci_read_config_byte(dev, PCI_SECONDARY_BUS, &sec_busno);
		
	/* add to children of PCI bridge dev->bus */
	child_bus = pci_add_new_bus(dev->bus, dev, sec_busno);
	if (!child_bus) {
		err("%s: could not add second bus\n", __FUNCTION__);
		return -EIO;
	}
	sprintf(child_bus->name, "PCI Bus #%02x", child_bus->number);
	/* do pci_scan_child_bus */
	pci_scan_child_bus(child_bus);

	list_for_each_entry(child_dev, &child_bus->devices, bus_list) {
		eeh_add_device_late(child_dev);
	}

	 /* fixup new pci devices without touching bus struct */
	rpaphp_fixup_new_pci_devices(child_bus, 0);

	/* Make the discovered devices available */
	pci_bus_add_devices(child_bus);
	return 0;
}

static void enable_eeh(struct device_node *dn)
{
	struct device_node *sib;

	for (sib = dn->child; sib; sib = sib->sibling) 
		enable_eeh(sib);
	eeh_add_device_early(dn);
	return;
	
}

static void print_slot_pci_funcs(struct slot *slot)
{
	struct pci_dev *dev;

	if (slot->dev_type == PCI_DEV) {
		dbg("%s: pci_devs of slot[%s]\n", __FUNCTION__, slot->name);
		list_for_each_entry (dev, slot->dev.pci_devs, bus_list)
			dbg("\t%s\n", pci_name(dev));
	}
	return;
}

static int rpaphp_config_pci_adapter(struct slot *slot)
{
	struct pci_bus *pci_bus;
	struct pci_dev *dev;
	int rc = -ENODEV;

	dbg("Entry %s: slot[%s]\n", __FUNCTION__, slot->name);

	if (slot->bridge) {

		pci_bus = slot->bridge->subordinate;
		if (!pci_bus) {
			err("%s: can't find bus structure\n", __FUNCTION__);
			goto exit;
		}
		enable_eeh(slot->dn);
		dev = rpaphp_pci_config_slot(slot->dn, pci_bus);
		if (!dev) {
			err("%s: can't find any devices.\n", __FUNCTION__);
			goto exit;
		}
		print_slot_pci_funcs(slot);
		rc = 0;
	} else {
		/* slot is not enabled */
		err("slot doesn't have pci_dev structure\n");
	}
exit:
	dbg("Exit %s:  rc=%d\n", __FUNCTION__, rc);
	return rc;
}

static void rpaphp_eeh_remove_bus_device(struct pci_dev *dev)
{
	eeh_remove_device(dev);
	if (dev->hdr_type == PCI_HEADER_TYPE_BRIDGE) {
		struct pci_bus *bus = dev->subordinate;
		struct list_head *ln;
		if (!bus)
			return; 
		for (ln = bus->devices.next; ln != &bus->devices; ln = ln->next) {
			struct pci_dev *pdev = pci_dev_b(ln);
			if (pdev)
				rpaphp_eeh_remove_bus_device(pdev);
		}

	}
	return;
}

int rpaphp_unconfig_pci_adapter(struct slot *slot)
{
	struct pci_dev *dev;
	int retval = 0;

	list_for_each_entry(dev, slot->dev.pci_devs, bus_list)
		rpaphp_eeh_remove_bus_device(dev);

	pci_remove_behind_bridge(slot->bridge);
	slot->state = NOT_CONFIGURED;
	info("%s: devices in slot[%s] unconfigured.\n", __FUNCTION__,
	     slot->name);
	return retval;
}

static int setup_pci_hotplug_slot_info(struct slot *slot)
{
	dbg("%s Initilize the PCI slot's hotplug->info structure ...\n",
	    __FUNCTION__);
	rpaphp_get_power_status(slot, &slot->hotplug_slot->info->power_status);
	rpaphp_get_pci_adapter_status(slot, 1,
				      &slot->hotplug_slot->info->
				      adapter_status);
	if (slot->hotplug_slot->info->adapter_status == NOT_VALID) {
		err("%s: NOT_VALID: skip dn->full_name=%s\n",
		    __FUNCTION__, slot->dn->full_name);
		return -EINVAL;
	}
	return 0;
}

static int set_phb_slot_name(struct slot *slot)
{
	struct device_node *dn;
	struct pci_controller *phb;
	struct pci_bus *bus;

	dn = slot->dn;
	if (!dn) {
		return -EINVAL;
	}
	phb = dn->phb;
	if (!phb) {
		return -EINVAL;
	}
	bus = phb->bus;
	if (!bus) {
		return -EINVAL;
	}

	sprintf(slot->name, "%04x:%02x:%02x.%x", pci_domain_nr(bus),
			bus->number, 0, 0);
	return 0;
}

static int setup_pci_slot(struct slot *slot)
{
	struct pci_bus *bus;
	int rc;

	if (slot->type == PHB) {
		rc = set_phb_slot_name(slot);
		if (rc < 0) {
			err("%s: failed to set phb slot name\n", __FUNCTION__);
			goto exit_rc;
		}
	} else {
		slot->bridge = rpaphp_find_bridge_pdev(slot);
		if (!slot->bridge) {
			/* slot being added doesn't have pci_dev yet */
			err("%s: no pci_dev for bridge dn %s\n",
					__FUNCTION__, slot->name);
			goto exit_rc;
		}

		bus = slot->bridge->subordinate;
		if (!bus)
			goto exit_rc;
		slot->dev.pci_devs = &bus->devices;

		dbg("%s set slot->name to %s\n",  __FUNCTION__,
				pci_name(slot->bridge));
		strcpy(slot->name, pci_name(slot->bridge));
	}

	/* find slot's pci_dev if it's not empty */
	if (slot->hotplug_slot->info->adapter_status == EMPTY) {
		slot->state = EMPTY;	/* slot is empty */
	} else {
		/* slot is occupied */
		if (!(slot->dn->child)) {
			/* non-empty slot has to have child */
			err("%s: slot[%s]'s device_node doesn't have child for adapter\n", 
				__FUNCTION__, slot->name);
			goto exit_rc;
		}

		if (slot->hotplug_slot->info->adapter_status == NOT_CONFIGURED) {
			dbg("%s CONFIGURING pci adapter in slot[%s]\n",  
				__FUNCTION__, slot->name);
			if (rpaphp_config_pci_adapter(slot)) {
				err("%s: CONFIG pci adapter failed\n", __FUNCTION__);
				goto exit_rc;		
			}

		} else if (slot->hotplug_slot->info->adapter_status != CONFIGURED) {
			err("%s: slot[%s]'s adapter_status is NOT_VALID.\n",
				__FUNCTION__, slot->name);
			goto exit_rc;
		}
		print_slot_pci_funcs(slot);
		if (!list_empty(slot->dev.pci_devs)) {
			slot->state = CONFIGURED;
		} else {
			/* DLPAR add as opposed to 
		 	 * boot time */
			slot->state = NOT_CONFIGURED;
		}
	}
	return 0;
exit_rc:
	dealloc_slot_struct(slot);
	return -EINVAL;
}

int register_pci_slot(struct slot *slot)
{
	int rc = -EINVAL;

	slot->dev_type = PCI_DEV;
	if ((slot->type == EMBEDDED) || (slot->type == PHB))
		slot->removable = 0;
	else
		slot->removable = 1;
	if (setup_pci_hotplug_slot_info(slot))
		goto exit_rc;
	if (setup_pci_slot(slot))
		goto exit_rc;
	rc = register_slot(slot);
exit_rc:
	return rc;
}

int rpaphp_enable_pci_slot(struct slot *slot)
{
	int retval = 0, state;

	retval = rpaphp_get_sensor_state(slot, &state);
	if (retval)
		goto exit;
	dbg("%s: sensor state[%d]\n", __FUNCTION__, state);
	/* if slot is not empty, enable the adapter */
	if (state == PRESENT) {
		dbg("%s : slot[%s] is occupied.\n", __FUNCTION__, slot->name);
		retval = rpaphp_config_pci_adapter(slot);
		if (!retval) {
			slot->state = CONFIGURED;
			dbg("%s: PCI devices in slot[%s] has been configured\n", 
				__FUNCTION__, slot->name);
		} else {
			slot->state = NOT_CONFIGURED;
			dbg("%s: no pci_dev struct for adapter in slot[%s]\n",
			    __FUNCTION__, slot->name);
		}
	} else if (state == EMPTY) {
		dbg("%s : slot[%s] is empty\n", __FUNCTION__, slot->name);
		slot->state = EMPTY;
	} else {
		err("%s: slot[%s] is in invalid state\n", __FUNCTION__,
		    slot->name);
		slot->state = NOT_VALID;
		retval = -EINVAL;
	}
exit:
	dbg("%s - Exit: rc[%d]\n", __FUNCTION__, retval);
	return retval;
}

struct hotplug_slot *rpaphp_find_hotplug_slot(struct pci_dev *dev)
{
	struct list_head	*tmp, *n;
	struct slot		*slot;

	list_for_each_safe(tmp, n, &rpaphp_slot_head) {
		struct pci_bus *bus;
		struct list_head *ln;

		slot = list_entry(tmp, struct slot, rpaphp_slot_list);
		if (slot->bridge == NULL) {
			if (slot->dev_type == PCI_DEV) {
				printk(KERN_WARNING "PCI slot missing bridge %s %s \n", 
				                    slot->name, slot->location);
			}
			continue;
		}

		bus = slot->bridge->subordinate;
		if (!bus) {
			continue;  /* should never happen? */
		}
		for (ln = bus->devices.next; ln != &bus->devices; ln = ln->next) {
                                struct pci_dev *pdev = pci_dev_b(ln);
				if (pdev == dev)
					return slot->hotplug_slot;
		}
	}

	return NULL;
}

EXPORT_SYMBOL_GPL(rpaphp_find_hotplug_slot);
