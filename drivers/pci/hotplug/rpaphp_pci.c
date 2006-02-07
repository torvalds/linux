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

int rpaphp_get_sensor_state(struct slot *slot, int *state)
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
			bus = pcibios_find_pci_bus(slot->dn);
			if (bus && !list_empty(&bus->devices))
				*value = CONFIGURED;
			else
				*value = NOT_CONFIGURED;
		}
	}
exit:
	return rc;
}

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

static int setup_pci_hotplug_slot_info(struct slot *slot)
{
	struct hotplug_slot_info *hotplug_slot_info = slot->hotplug_slot->info;

	dbg("%s Initilize the PCI slot's hotplug->info structure ...\n",
	    __FUNCTION__);
	rpaphp_get_power_status(slot, &hotplug_slot_info->power_status);
	rpaphp_get_pci_adapter_status(slot, 1,
				      &hotplug_slot_info->adapter_status);
	if (hotplug_slot_info->adapter_status == NOT_VALID) {
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
	bus = pcibios_find_pci_bus(dn);
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
			pcibios_add_pci_devices(slot->bus);

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

int rpaphp_register_pci_slot(struct slot *slot)
{
	int rc = -EINVAL;

	if (setup_pci_hotplug_slot_info(slot))
		goto exit_rc;
	if (setup_pci_slot(slot))
		goto exit_rc;
	rc = rpaphp_register_slot(slot);
exit_rc:
	return rc;
}

