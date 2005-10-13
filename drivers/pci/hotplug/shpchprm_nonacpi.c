/*
 * SHPCHPRM NONACPI: PHP Resource Manager for Non-ACPI/Legacy platform
 *
 * Copyright (C) 1995,2001 Compaq Computer Corporation
 * Copyright (C) 2001 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2001 IBM Corp.
 * Copyright (C) 2003-2004 Intel Corporation
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
 * Send feedback to <greg@kroah.com>, <kristen.c.accardi@intel.com>
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include "shpchp.h"
#include "shpchprm.h"

void shpchprm_cleanup(void)
{
	return;
}

int shpchprm_get_physical_slot_number(struct controller *ctrl, u32 *sun, u8 busnum, u8 devnum)
{
	int	offset = devnum - ctrl->slot_device_offset;

	dbg("%s: ctrl->slot_num_inc %d, offset %d\n", __FUNCTION__, ctrl->slot_num_inc, offset);
	*sun = (u8) (ctrl->first_slot + ctrl->slot_num_inc * offset);
	return 0;
}

int shpchprm_set_hpp(
	struct controller *ctrl,
	struct pci_func *func,
	u8	card_type)
{
	u32 rc;
	u8 temp_byte;
	struct pci_bus lpci_bus, *pci_bus;
	unsigned int	devfn;
	memcpy(&lpci_bus, ctrl->pci_bus, sizeof(lpci_bus));
	pci_bus = &lpci_bus;
	pci_bus->number = func->bus;
	devfn = PCI_DEVFN(func->device, func->function);

	temp_byte = 0x40;	/* hard coded value for LT */
	if (card_type == PCI_HEADER_TYPE_BRIDGE) {
		/* set subordinate Latency Timer */
		rc = pci_bus_write_config_byte(pci_bus, devfn, PCI_SEC_LATENCY_TIMER, temp_byte);

		if (rc) {
			dbg("%s: set secondary LT error. b:d:f(%02x:%02x:%02x)\n", __FUNCTION__, func->bus, 
				func->device, func->function);
			return rc;
		}
	}

	/* set base Latency Timer */
	rc = pci_bus_write_config_byte(pci_bus, devfn, PCI_LATENCY_TIMER, temp_byte);

	if (rc) {
		dbg("%s: set LT error. b:d:f(%02x:%02x:%02x)\n", __FUNCTION__, func->bus, func->device, func->function);
		return rc;
	}

	/* set Cache Line size */
	temp_byte = 0x08;	/* hard coded value for CLS */

	rc = pci_bus_write_config_byte(pci_bus, devfn, PCI_CACHE_LINE_SIZE, temp_byte);

	if (rc) {
		dbg("%s: set CLS error. b:d:f(%02x:%02x:%02x)\n", __FUNCTION__, func->bus, func->device, func->function);
	}

	/* set enable_perr */
	/* set enable_serr */

	return rc;
}

void shpchprm_enable_card(
	struct controller *ctrl,
	struct pci_func *func,
	u8 card_type)
{
	u16 command, bcommand;
	struct pci_bus lpci_bus, *pci_bus;
	unsigned int devfn;
	int rc;

	memcpy(&lpci_bus, ctrl->pci_bus, sizeof(lpci_bus));
	pci_bus = &lpci_bus;
	pci_bus->number = func->bus;
	devfn = PCI_DEVFN(func->device, func->function);

	rc = pci_bus_read_config_word(pci_bus, devfn, PCI_COMMAND, &command);

	command |= PCI_COMMAND_PARITY | PCI_COMMAND_SERR
		| PCI_COMMAND_MASTER | PCI_COMMAND_INVALIDATE
		| PCI_COMMAND_IO | PCI_COMMAND_MEMORY;

	rc = pci_bus_write_config_word(pci_bus, devfn, PCI_COMMAND, command);

	if (card_type == PCI_HEADER_TYPE_BRIDGE) {

		rc = pci_bus_read_config_word(pci_bus, devfn, PCI_BRIDGE_CONTROL, &bcommand);

		bcommand |= PCI_BRIDGE_CTL_PARITY | PCI_BRIDGE_CTL_SERR
			| PCI_BRIDGE_CTL_NO_ISA;

		rc = pci_bus_write_config_word(pci_bus, devfn, PCI_BRIDGE_CONTROL, bcommand);
	}
}

static int legacy_shpchprm_init_pci(void)
{
	return 0;
}

int shpchprm_init(enum php_ctlr_type ctrl_type)
{
	int retval;

	switch (ctrl_type) {
	case PCI:
		retval = legacy_shpchprm_init_pci();
		break;
	default:
		retval = -ENODEV;
		break;
	}

	return retval;
}
