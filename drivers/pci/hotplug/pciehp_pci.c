// SPDX-License-Identifier: GPL-2.0+
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
 * Send feedback to <greg@kroah.com>, <kristen.c.accardi@intel.com>
 *
 */

#define dev_fmt(fmt) "pciehp: " fmt

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include "../pci.h"
#include "pciehp.h"

/**
 * pciehp_configure_device() - enumerate PCI devices below a hotplug bridge
 * @ctrl: PCIe hotplug controller
 *
 * Enumerate PCI devices below a hotplug bridge and add them to the system.
 * Return 0 on success, %-EEXIST if the devices are already enumerated or
 * %-ENODEV if enumeration failed.
 */
int pciehp_configure_device(struct controller *ctrl)
{
	struct pci_dev *dev;
	struct pci_dev *bridge = ctrl->pcie->port;
	struct pci_bus *parent = bridge->subordinate;
	int num, ret = 0;

	pci_lock_rescan_remove();

	dev = pci_get_slot(parent, PCI_DEVFN(0, 0));
	if (dev) {
		/*
		 * The device is already there. Either configured by the
		 * boot firmware or a previous hotplug event.
		 */
		ctrl_dbg(ctrl, "Device %s already exists at %04x:%02x:00, skipping hot-add\n",
			 pci_name(dev), pci_domain_nr(parent), parent->number);
		pci_dev_put(dev);
		ret = -EEXIST;
		goto out;
	}

	num = pci_scan_slot(parent, PCI_DEVFN(0, 0));
	if (num == 0) {
		ctrl_err(ctrl, "No new device found\n");
		ret = -ENODEV;
		goto out;
	}

	for_each_pci_bridge(dev, parent)
		pci_hp_add_bridge(dev);

	pci_assign_unassigned_bridge_resources(bridge);
	pcie_bus_configure_settings(parent);

	/*
	 * Release reset_lock during driver binding
	 * to avoid AB-BA deadlock with device_lock.
	 */
	up_read(&ctrl->reset_lock);
	pci_bus_add_devices(parent);
	down_read_nested(&ctrl->reset_lock, ctrl->depth);

 out:
	pci_unlock_rescan_remove();
	return ret;
}

/**
 * pciehp_unconfigure_device() - remove PCI devices below a hotplug bridge
 * @ctrl: PCIe hotplug controller
 * @presence: whether the card is still present in the slot;
 *	true for safe removal via sysfs or an Attention Button press,
 *	false for surprise removal
 *
 * Unbind PCI devices below a hotplug bridge from their drivers and remove
 * them from the system.  Safely removed devices are quiesced.  Surprise
 * removed devices are marked as such to prevent further accesses.
 */
void pciehp_unconfigure_device(struct controller *ctrl, bool presence)
{
	struct pci_dev *dev, *temp;
	struct pci_bus *parent = ctrl->pcie->port->subordinate;
	u16 command;

	ctrl_dbg(ctrl, "%s: domain:bus:dev = %04x:%02x:00\n",
		 __func__, pci_domain_nr(parent), parent->number);

	if (!presence)
		pci_walk_bus(parent, pci_dev_set_disconnected, NULL);

	pci_lock_rescan_remove();

	/*
	 * Stopping an SR-IOV PF device removes all the associated VFs,
	 * which will update the bus->devices list and confuse the
	 * iterator.  Therefore, iterate in reverse so we remove the VFs
	 * first, then the PF.  We do the same in pci_stop_bus_device().
	 */
	list_for_each_entry_safe_reverse(dev, temp, &parent->devices,
					 bus_list) {
		pci_dev_get(dev);

		/*
		 * Release reset_lock during driver unbinding
		 * to avoid AB-BA deadlock with device_lock.
		 */
		up_read(&ctrl->reset_lock);
		pci_stop_and_remove_bus_device(dev);
		down_read_nested(&ctrl->reset_lock, ctrl->depth);

		/*
		 * Ensure that no new Requests will be generated from
		 * the device.
		 */
		if (presence) {
			pci_read_config_word(dev, PCI_COMMAND, &command);
			command &= ~(PCI_COMMAND_MASTER | PCI_COMMAND_SERR);
			command |= PCI_COMMAND_INTX_DISABLE;
			pci_write_config_word(dev, PCI_COMMAND, command);
		}
		pci_dev_put(dev);
	}

	pci_unlock_rescan_remove();
}
