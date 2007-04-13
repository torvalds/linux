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

/**
 * rpaphp_enable_slot - record slot state, config pci device
 *
 * Initialize values in the slot, and the hotplug_slot info
 * structures to indicate if there is a pci card plugged into
 * the slot. If the slot is not empty, run the pcibios routine
 * to get pcibios stuff correctly set up.
 */
int rpaphp_enable_slot(struct slot *slot)
{
	int rc, level, state;
	struct pci_bus *bus;
	struct hotplug_slot_info *info = slot->hotplug_slot->info;

	info->adapter_status = NOT_VALID;
	slot->state = EMPTY;

	/* Find out if the power is turned on for the slot */
	rc = rtas_get_power_level(slot->power_domain, &level);
	if (rc)
		return rc;
	info->power_status = level;

	/* Figure out if there is an adapter in the slot */
	rc = rpaphp_get_sensor_state(slot, &state);
	if (rc)
		return rc;

	bus = pcibios_find_pci_bus(slot->dn);
	if (!bus) {
		err("%s: no pci_bus for dn %s\n", __FUNCTION__, slot->dn->full_name);
		return -EINVAL;
	}

	info->adapter_status = EMPTY;
	slot->bus = bus;
	slot->pci_devs = &bus->devices;
	set_slot_name(slot);

	/* if there's an adapter in the slot, go add the pci devices */
	if (state == PRESENT) {
		info->adapter_status = NOT_CONFIGURED;
		slot->state = NOT_CONFIGURED;

		/* non-empty slot has to have child */
		if (!slot->dn->child) {
			err("%s: slot[%s]'s device_node doesn't have child for adapter\n",
			    __FUNCTION__, slot->name);
			return -EINVAL;
		}

		if (list_empty(&bus->devices))
			pcibios_add_pci_devices(bus);

		if (!list_empty(&bus->devices)) {
			info->adapter_status = CONFIGURED;
			slot->state = CONFIGURED;
		}

		if (debug) {
			struct pci_dev *dev;
			dbg("%s: pci_devs of slot[%s]\n", __FUNCTION__, slot->dn->full_name);
			list_for_each_entry (dev, &bus->devices, bus_list)
				dbg("\t%s\n", pci_name(dev));
		}
	}

	return 0;
}

