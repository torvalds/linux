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
#include <linux/string.h>

#include <asm/pci-bridge.h>
#include <asm/rtas.h>
#include <asm/machdep.h>

#include "../pci.h"		/* for pci_add_new_bus */
#include "rpaphp.h"

static struct pci_bus *find_bus_among_children(struct pci_bus *bus,
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
	}
	return child;
}

struct pci_bus *rpaphp_find_pci_bus(struct device_node *dn)
{
	struct pci_dn *pdn = dn->data;

	if (!pdn  || !pdn->phb || !pdn->phb->bus)
		return NULL;

	return find_bus_among_children(pdn->phb->bus, dn);
}
EXPORT_SYMBOL_GPL(rpaphp_find_pci_bus);

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
	struct pci_bus *bus;
	int state, rc;

	*value = NOT_VALID;
	rc = rpaphp_get_sensor_state(slot, &state);
	if (rc)
		goto exit;

 	if (state == EMPTY)
 		*value = EMPTY;
 	else if (state == PRESENT) {
		if (!is_init) {
			/* at run-time slot->state can be changed by */
			/* config/unconfig adapter */
			*value = slot->state;
		} else {
			bus = rpaphp_find_pci_bus(slot->dn);
			if (bus && !list_empty(&bus->devices))
				*value = CONFIGURED;
			else
				*value = NOT_CONFIGURED;
		}
	}
exit:
	return rc;
}

/* Must be called before pci_bus_add_devices */
void rpaphp_fixup_new_pci_devices(struct pci_bus *bus, int fix_bus)
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

static void rpaphp_eeh_add_bus_device(struct pci_bus *bus)
{
	struct pci_dev *dev;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		eeh_add_device_late(dev);
		if (dev->hdr_type == PCI_HEADER_TYPE_BRIDGE) {
			struct pci_bus *subbus = dev->subordinate;
			if (subbus)
				rpaphp_eeh_add_bus_device (subbus);
		}
	}
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

void rpaphp_init_new_devs(struct pci_bus *bus)
{
	rpaphp_fixup_new_pci_devices(bus, 0);
	rpaphp_eeh_add_bus_device(bus);
}
EXPORT_SYMBOL_GPL(rpaphp_init_new_devs);

/*****************************************************************************
 rpaphp_pci_config_slot() will  configure all devices under the
 given slot->dn and return the the first pci_dev.
 *****************************************************************************/
static struct pci_dev *
rpaphp_pci_config_slot(struct pci_bus *bus)
{
	struct device_node *dn = pci_bus_to_OF_node(bus);
	struct pci_dev *dev = NULL;
	int slotno;
	int num;

	dbg("Enter %s: dn=%s bus=%s\n", __FUNCTION__, dn->full_name, bus->name);
	if (!dn || !dn->child)
		return NULL;

	if (_machine == PLATFORM_PSERIES_LPAR) {
		of_scan_bus(dn, bus);
		if (list_empty(&bus->devices)) {
			err("%s: No new device found\n", __FUNCTION__);
			return NULL;
		}

		rpaphp_init_new_devs(bus);
		pci_bus_add_devices(bus);
		dev = list_entry(&bus->devices, struct pci_dev, bus_list);
	} else {
		slotno = PCI_SLOT(PCI_DN(dn->child)->devfn);

		/* pci_scan_slot should find all children */
		num = pci_scan_slot(bus, PCI_DEVFN(slotno, 0));
		if (num) {
			rpaphp_fixup_new_pci_devices(bus, 1);
			pci_bus_add_devices(bus);
		}
		if (list_empty(&bus->devices)) {
			err("%s: No new device found\n", __FUNCTION__);
			return NULL;
		}
		list_for_each_entry(dev, &bus->devices, bus_list) {
			if (dev->hdr_type == PCI_HEADER_TYPE_BRIDGE)
				rpaphp_pci_config_bridge(dev);

			rpaphp_eeh_add_bus_device(bus);
		}
	}

	return dev;
}

void rpaphp_eeh_init_nodes(struct device_node *dn)
{
	struct device_node *sib;

	for (sib = dn->child; sib; sib = sib->sibling) 
		rpaphp_eeh_init_nodes(sib);
	eeh_add_device_early(dn);
	return;
	
}
EXPORT_SYMBOL_GPL(rpaphp_eeh_init_nodes);

static void print_slot_pci_funcs(struct pci_bus *bus)
{
	struct device_node *dn;
	struct pci_dev *dev;

	dn = pci_bus_to_OF_node(bus);
	if (!dn)
		return;

	dbg("%s: pci_devs of slot[%s]\n", __FUNCTION__, dn->full_name);
	list_for_each_entry (dev, &bus->devices, bus_list)
		dbg("\t%s\n", pci_name(dev));
	return;
}

int rpaphp_config_pci_adapter(struct pci_bus *bus)
{
	struct device_node *dn = pci_bus_to_OF_node(bus);
	struct pci_dev *dev;
	int rc = -ENODEV;

	dbg("Entry %s: slot[%s]\n", __FUNCTION__, dn->full_name);
	if (!dn)
		goto exit;

	rpaphp_eeh_init_nodes(dn);
	dev = rpaphp_pci_config_slot(bus);
	if (!dev) {
		err("%s: can't find any devices.\n", __FUNCTION__);
		goto exit;
	}
	print_slot_pci_funcs(bus);
	rc = 0;
exit:
	dbg("Exit %s:  rc=%d\n", __FUNCTION__, rc);
	return rc;
}
EXPORT_SYMBOL_GPL(rpaphp_config_pci_adapter);

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

int rpaphp_unconfig_pci_adapter(struct pci_bus *bus)
{
	struct pci_dev *dev, *tmp;

	list_for_each_entry_safe(dev, tmp, &bus->devices, bus_list) {
		rpaphp_eeh_remove_bus_device(dev);
		pci_remove_bus_device(dev);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(rpaphp_unconfig_pci_adapter);

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

static void set_slot_name(struct slot *slot)
{
	struct pci_bus *bus = slot->bus;
	struct pci_dev *bridge;

	bridge = bus->self;
	if (bridge)
		strcpy(slot->name, pci_name(bridge));
	else
		sprintf(slot->name, "%04x:%02x:00.0", pci_domain_nr(bus),
			bus->number);
}

static int setup_pci_slot(struct slot *slot)
{
	struct device_node *dn = slot->dn;
	struct pci_bus *bus;

	BUG_ON(!dn);
	bus = rpaphp_find_pci_bus(dn);
	if (!bus) {
		err("%s: no pci_bus for dn %s\n", __FUNCTION__, dn->full_name);
		goto exit_rc;
	}

	slot->bus = bus;
	slot->pci_devs = &bus->devices;
	set_slot_name(slot);

	/* find slot's pci_dev if it's not empty */
	if (slot->hotplug_slot->info->adapter_status == EMPTY) {
		slot->state = EMPTY;	/* slot is empty */
	} else {
		/* slot is occupied */
		if (!dn->child) {
			/* non-empty slot has to have child */
			err("%s: slot[%s]'s device_node doesn't have child for adapter\n", 
				__FUNCTION__, slot->name);
			goto exit_rc;
		}

		if (slot->hotplug_slot->info->adapter_status == NOT_CONFIGURED) {
			dbg("%s CONFIGURING pci adapter in slot[%s]\n",  
				__FUNCTION__, slot->name);
			if (rpaphp_config_pci_adapter(slot->bus)) {
				err("%s: CONFIG pci adapter failed\n", __FUNCTION__);
				goto exit_rc;		
			}

		} else if (slot->hotplug_slot->info->adapter_status != CONFIGURED) {
			err("%s: slot[%s]'s adapter_status is NOT_VALID.\n",
				__FUNCTION__, slot->name);
			goto exit_rc;
		}
		print_slot_pci_funcs(slot->bus);
		if (!list_empty(slot->pci_devs)) {
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
		retval = rpaphp_config_pci_adapter(slot->bus);
		if (!retval) {
			slot->state = CONFIGURED;
			info("%s: devices in slot[%s] configured\n",
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
