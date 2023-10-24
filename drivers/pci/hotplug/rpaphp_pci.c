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

/*
 * RTAS call get-sensor-state(DR_ENTITY_SENSE) return values as per PAPR:
 * -- generic return codes ---
 *    -1: Hardware Error
 *    -2: RTAS_BUSY
 *    -3: Invalid sensor. RTAS Parameter Error.
 * -- rtas_get_sensor function specific return codes ---
 * -9000: Need DR entity to be powered up and unisolated before RTAS call
 * -9001: Need DR entity to be powered up, but not unisolated, before RTAS call
 * -9002: DR entity unusable
 *  990x: Extended delay - where x is a number in the range of 0-5
 */
#define RTAS_SLOT_UNISOLATED		-9000
#define RTAS_SLOT_NOT_UNISOLATED	-9001
#define RTAS_SLOT_NOT_USABLE		-9002

static int rtas_get_sensor_errno(int rtas_rc)
{
	switch (rtas_rc) {
	case 0:
		/* Success case */
		return 0;
	case RTAS_SLOT_UNISOLATED:
	case RTAS_SLOT_NOT_UNISOLATED:
		return -EFAULT;
	case RTAS_SLOT_NOT_USABLE:
		return -ENODEV;
	case RTAS_BUSY:
	case RTAS_EXTENDED_DELAY_MIN...RTAS_EXTENDED_DELAY_MAX:
		return -EBUSY;
	default:
		return rtas_error_rc(rtas_rc);
	}
}

/*
 * get_adapter_status() can be called by the EEH handler during EEH recovery.
 * On certain PHB failures, the RTAS call rtas_call(get-sensor-state) returns
 * extended busy error (9902) until PHB is recovered by pHyp. The RTAS call
 * interface rtas_get_sensor() loops over the RTAS call on extended delay
 * return code (9902) until the return value is either success (0) or error
 * (-1). This causes the EEH handler to get stuck for ~6 seconds before it
 * could notify that the PCI error has been detected and stop any active
 * operations. This sometimes causes EEH recovery to fail. To avoid this issue,
 * invoke rtas_call(get-sensor-state) directly if the respective PE is in EEH
 * recovery state and return -EBUSY error based on RTAS return status. This
 * will help the EEH handler to notify the driver about the PCI error
 * immediately and successfully proceed with EEH recovery steps.
 */

static int __rpaphp_get_sensor_state(struct slot *slot, int *state)
{
	int rc;
	int token = rtas_token("get-sensor-state");
	struct pci_dn *pdn;
	struct eeh_pe *pe;
	struct pci_controller *phb = PCI_DN(slot->dn)->phb;

	if (token == RTAS_UNKNOWN_SERVICE)
		return -ENOENT;

	/*
	 * Fallback to existing method for empty slot or PE isn't in EEH
	 * recovery.
	 */
	pdn = list_first_entry_or_null(&PCI_DN(phb->dn)->child_list,
					struct pci_dn, list);
	if (!pdn)
		goto fallback;

	pe = eeh_dev_to_pe(pdn->edev);
	if (pe && (pe->state & EEH_PE_RECOVERING)) {
		rc = rtas_call(token, 2, 2, state, DR_ENTITY_SENSE,
			       slot->index);
		return rtas_get_sensor_errno(rc);
	}
fallback:
	return rtas_get_sensor(DR_ENTITY_SENSE, slot->index, state);
}

int rpaphp_get_sensor_state(struct slot *slot, int *state)
{
	int rc;
	int setlevel;

	rc = __rpaphp_get_sensor_state(slot, state);

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
				rc = __rpaphp_get_sensor_state(slot, state);
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
