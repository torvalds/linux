/*
 * Standard Hot Plug Controller Driver
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
#include "shpchp.h"

int shpchp_configure_device(struct slot *p_slot)
{
	struct pci_dev *dev;
	struct controller *ctrl = p_slot->ctrl;
	struct pci_dev *bridge = ctrl->pci_dev;
	struct pci_bus *parent = bridge->subordinate;
	int num, ret = 0;

	pci_lock_rescan_remove();

	dev = pci_get_slot(parent, PCI_DEVFN(p_slot->device, 0));
	if (dev) {
		ctrl_err(ctrl, "Device %s already exists at %04x:%02x:%02x, cannot hot-add\n",
			 pci_name(dev), pci_domain_nr(parent),
			 p_slot->bus, p_slot->device);
		pci_dev_put(dev);
		ret = -EINVAL;
		goto out;
	}

	num = pci_scan_slot(parent, PCI_DEVFN(p_slot->device, 0));
	if (num == 0) {
		ctrl_err(ctrl, "No new device found\n");
		ret = -ENODEV;
		goto out;
	}

	list_for_each_entry(dev, &parent->devices, bus_list) {
		if (PCI_SLOT(dev->devfn) != p_slot->device)
			continue;
		if (pci_is_bridge(dev))
			pci_hp_add_bridge(dev);
	}

	pci_assign_unassigned_bridge_resources(bridge);

	list_for_each_entry(dev, &parent->devices, bus_list) {
		if (PCI_SLOT(dev->devfn) != p_slot->device)
			continue;
		pci_configure_slot(dev);
	}

	pci_bus_add_devices(parent);

 out:
	pci_unlock_rescan_remove();
	return ret;
}

int shpchp_unconfigure_device(struct slot *p_slot)
{
	int rc = 0;
	u8 bctl = 0;
	struct pci_bus *parent = p_slot->ctrl->pci_dev->subordinate;
	struct pci_dev *dev, *temp;
	struct controller *ctrl = p_slot->ctrl;

	ctrl_dbg(ctrl, "%s: domain:bus:dev = %04x:%02x:%02x\n",
		 __func__, pci_domain_nr(parent), p_slot->bus, p_slot->device);

	pci_lock_rescan_remove();

	list_for_each_entry_safe(dev, temp, &parent->devices, bus_list) {
		if (PCI_SLOT(dev->devfn) != p_slot->device)
			continue;

		pci_dev_get(dev);
		if (dev->hdr_type == PCI_HEADER_TYPE_BRIDGE) {
			pci_read_config_byte(dev, PCI_BRIDGE_CONTROL, &bctl);
			if (bctl & PCI_BRIDGE_CTL_VGA) {
				ctrl_err(ctrl,
					 "Cannot remove display device %s\n",
					 pci_name(dev));
				pci_dev_put(dev);
				rc = -EINVAL;
				break;
			}
		}
		pci_stop_and_remove_bus_device(dev);
		pci_dev_put(dev);
	}

	pci_unlock_rescan_remove();
	return rc;
}

