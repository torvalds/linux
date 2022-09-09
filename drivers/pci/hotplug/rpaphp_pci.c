// SPDX-License-Identifier: GPL-2.0+
/*
 * PCI Hot Plug Controller Driver for RPA-compliant PPC64 platform.
 * Copyright (C) 2003 Linda Xie <lxie@us.ibm.com>
 *
 * All rights reserved.
 *
 * Send feedback to <lxie@us.ibm.com>
 *
 */
#include <linux/of.h>
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
			    __func__);

			/* some slots have to be powered up
			 * before get-sensor will succeed.
			 */
			rc = rtas_set_power_level(slot->power_domain, POWER_ON,
						  &setlevel);
			if (rc < 0) {
				dbg("%s: power on slot[%s] failed rc=%d.\n",
				    __func__, slot->name, rc);
			} else {
				rc = rtas_get_sensor(DR_ENTITY_SENSE,
						     slot->index, state);
			}
		} else if (rc == -ENODEV)
			info("%s: slot is unusable\n", __func__);
		else
			err("%s failed to get sensor state\n", __func__);
	}
	return rc;
}

/**
 * rpaphp_enable_slot - record slot state, config pci device
 * @slot: target &slot
 *
 * Initialize values in the slot structure to indicate if there is a pci card
 * plugged into the slot. If the slot is not empty, run the pcibios routine
 * to get pcibios stuff correctly set up.
 */
int rpaphp_enable_slot(struct slot *slot)
{
	int rc, level, state;
	struct pci_bus *bus;

	slot->state = EMPTY;

	/* Find out if the power is turned on for the slot */
	rc = rtas_get_power_level(slot->power_domain, &level);
	if (rc)
		return rc;

	/* Figure out if there is an adapter in the slot */
	rc = rpaphp_get_sensor_state(slot, &state);
	if (rc)
		return rc;

	bus = pci_find_bus_by_node(slot->dn);
	if (!bus) {
		err("%s: no pci_bus for dn %pOF\n", __func__, slot->dn);
		return -EINVAL;
	}

	slot->bus = bus;
	slot->pci_devs = &bus->devices;

	/* if there's an adapter in the slot, go add the pci devices */
	if (state == PRESENT) {
		slot->state = NOT_CONFIGURED;

		/* non-empty slot has to have child */
		if (!slot->dn->child) {
			err("%s: slot[%s]'s device_node doesn't have child for adapter\n",
			    __func__, slot->name);
			return -EINVAL;
		}

		if (list_empty(&bus->devices)) {
			pseries_eeh_init_edev_recursive(PCI_DN(slot->dn));
			pci_hp_add_devices(bus);
		}

		if (!list_empty(&bus->devices)) {
			slot->state = CONFIGURED;
		}

		if (rpaphp_debug) {
			struct pci_dev *dev;
			dbg("%s: pci_devs of slot[%pOF]\n", __func__, slot->dn);
			list_for_each_entry(dev, &bus->devices, bus_list)
				dbg("\t%s\n", pci_name(dev));
		}
	}

	return 0;
}
