/*
 * PCI Express Hot Plug Controller Driver
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include "../pci.h"
#include "pciehp.h"

static int pciehp_add_bridge(struct pci_dev *dev)
{
	struct pci_bus *parent = dev->bus;
	int pass, busnr, start = parent->secondary;
	int end = parent->subordinate;

	for (busnr = start; busnr <= end; busnr++) {
		if (!pci_find_bus(pci_domain_nr(parent), busnr))
			break;
	}
	if (busnr-- > end) {
		err("No bus number available for hot-added bridge %s\n",
				pci_name(dev));
		return -1;
	}
	for (pass = 0; pass < 2; pass++)
		busnr = pci_scan_bridge(parent, dev, busnr, pass);
	if (!dev->subordinate)
		return -1;
	pci_bus_size_bridges(dev->subordinate);
	pci_bus_assign_resources(parent);
	pci_enable_bridges(parent);
	pci_bus_add_devices(parent);
	return 0;
}

int pciehp_configure_device(struct slot *p_slot)
{
	struct pci_dev *dev;
	struct pci_bus *parent = p_slot->ctrl->pci_dev->subordinate;
	int num, fn;

	dev = pci_find_slot(p_slot->bus, PCI_DEVFN(p_slot->device, 0));
	if (dev) {
		err("Device %s already exists at %x:%x, cannot hot-add\n",
				pci_name(dev), p_slot->bus, p_slot->device);
		return -EINVAL;
	}

	num = pci_scan_slot(parent, PCI_DEVFN(p_slot->device, 0));
	if (num == 0) {
		err("No new device found\n");
		return -ENODEV;
	}

	for (fn = 0; fn < 8; fn++) {
		dev = pci_get_slot(parent, PCI_DEVFN(p_slot->device, fn));
		if (!dev)
			continue;
		if ((dev->class >> 16) == PCI_BASE_CLASS_DISPLAY) {
			err("Cannot hot-add display device %s\n",
					pci_name(dev));
			continue;
		}
		if ((dev->hdr_type == PCI_HEADER_TYPE_BRIDGE) ||
				(dev->hdr_type == PCI_HEADER_TYPE_CARDBUS)) {
			pciehp_add_bridge(dev);
		}
		/* TBD: program firmware provided _HPP values */
		/* program_fw_provided_values(dev); */
	}

	pci_bus_assign_resources(parent);
	pci_bus_add_devices(parent);
	return 0;
}

int pciehp_unconfigure_device(struct slot *p_slot)
{
	int rc = 0;
	int j;
	u8 bctl = 0;

	dbg("%s: bus/dev = %x/%x\n", __FUNCTION__, p_slot->bus,
				p_slot->device);

	for (j=0; j<8 ; j++) {
		struct pci_dev* temp = pci_find_slot(p_slot->bus,
				(p_slot->device << 3) | j);
		if (!temp)
			continue;
		if ((temp->class >> 16) == PCI_BASE_CLASS_DISPLAY) {
			err("Cannot remove display device %s\n",
					pci_name(temp));
			continue;
		}
		if (temp->hdr_type == PCI_HEADER_TYPE_BRIDGE) {
			pci_read_config_byte(temp, PCI_BRIDGE_CONTROL, &bctl);
			if (bctl & PCI_BRIDGE_CTL_VGA) {
				err("Cannot remove display device %s\n",
						pci_name(temp));
				continue;
			}
		}
		pci_remove_bus_device(temp);
	}
	/* 
	 * Some PCI Express root ports require fixup after hot-plug operation.
	 */
	if (pcie_mch_quirk) 
		pci_fixup_device(pci_fixup_final, p_slot->ctrl->pci_dev);
	
	return rc;
}

