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

static int __ref pciehp_add_bridge(struct pci_dev *dev)
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

	return 0;
}

int pciehp_configure_device(struct slot *p_slot)
{
	struct pci_dev *dev;
	struct pci_dev *bridge = p_slot->ctrl->pcie->port;
	struct pci_bus *parent = bridge->subordinate;
	int num, fn;
	struct controller *ctrl = p_slot->ctrl;

	dev = pci_get_slot(parent, PCI_DEVFN(0, 0));
	if (dev) {
		ctrl_err(ctrl, "Device %s already exists "
			 "at %04x:%02x:00, cannot hot-add\n", pci_name(dev),
			 pci_domain_nr(parent), parent->number);
		pci_dev_put(dev);
		return -EINVAL;
	}

	num = pci_scan_slot(parent, PCI_DEVFN(0, 0));
	if (num == 0) {
		ctrl_err(ctrl, "No new device found\n");
		return -ENODEV;
	}

	for (fn = 0; fn < 8; fn++) {
		dev = pci_get_slot(parent, PCI_DEVFN(0, fn));
		if (!dev)
			continue;
		if ((dev->hdr_type == PCI_HEADER_TYPE_BRIDGE) ||
				(dev->hdr_type == PCI_HEADER_TYPE_CARDBUS)) {
			pciehp_add_bridge(dev);
		}
		pci_dev_put(dev);
	}

	pci_assign_unassigned_bridge_resources(bridge);

	for (fn = 0; fn < 8; fn++) {
		dev = pci_get_slot(parent, PCI_DEVFN(0, fn));
		if (!dev)
			continue;
		if ((dev->class >> 16) == PCI_BASE_CLASS_DISPLAY) {
			pci_dev_put(dev);
			continue;
		}
		pci_configure_slot(dev);
		pci_dev_put(dev);
	}

	pci_bus_add_devices(parent);

	return 0;
}

int pciehp_unconfigure_device(struct slot *p_slot)
{
	int ret, rc = 0;
	int j;
	u8 bctl = 0;
	u8 presence = 0;
	struct pci_bus *parent = p_slot->ctrl->pcie->port->subordinate;
	u16 command;
	struct controller *ctrl = p_slot->ctrl;

	ctrl_dbg(ctrl, "%s: domain:bus:dev = %04x:%02x:00\n",
		 __func__, pci_domain_nr(parent), parent->number);
	ret = pciehp_get_adapter_status(p_slot, &presence);
	if (ret)
		presence = 0;

	for (j = 0; j < 8; j++) {
		struct pci_dev *temp = pci_get_slot(parent, PCI_DEVFN(0, j));
		if (!temp)
			continue;
		if (temp->hdr_type == PCI_HEADER_TYPE_BRIDGE && presence) {
			pci_read_config_byte(temp, PCI_BRIDGE_CONTROL, &bctl);
			if (bctl & PCI_BRIDGE_CTL_VGA) {
				ctrl_err(ctrl,
					 "Cannot remove display device %s\n",
					 pci_name(temp));
				pci_dev_put(temp);
				rc = -EINVAL;
				break;
			}
		}
		pci_stop_and_remove_bus_device(temp);
		/*
		 * Ensure that no new Requests will be generated from
		 * the device.
		 */
		if (presence) {
			pci_read_config_word(temp, PCI_COMMAND, &command);
			command &= ~(PCI_COMMAND_MASTER | PCI_COMMAND_SERR);
			command |= PCI_COMMAND_INTX_DISABLE;
			pci_write_config_word(temp, PCI_COMMAND, command);
		}
		pci_dev_put(temp);
	}

	return rc;
}
