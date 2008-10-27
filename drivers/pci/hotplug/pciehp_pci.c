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

static void program_hpp_type0(struct pci_dev *dev, struct hpp_type0 *hpp)
{
	u16 pci_cmd, pci_bctl;

	if (hpp->revision > 1) {
		warn("Rev.%d type0 record not supported\n", hpp->revision);
		return;
	}

	pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, hpp->cache_line_size);
	pci_write_config_byte(dev, PCI_LATENCY_TIMER, hpp->latency_timer);
	pci_read_config_word(dev, PCI_COMMAND, &pci_cmd);
	if (hpp->enable_serr)
		pci_cmd |= PCI_COMMAND_SERR;
	else
		pci_cmd &= ~PCI_COMMAND_SERR;
	if (hpp->enable_perr)
		pci_cmd |= PCI_COMMAND_PARITY;
	else
		pci_cmd &= ~PCI_COMMAND_PARITY;
	pci_write_config_word(dev, PCI_COMMAND, pci_cmd);

	/* Program bridge control value */
	if ((dev->class >> 8) == PCI_CLASS_BRIDGE_PCI) {
		pci_write_config_byte(dev, PCI_SEC_LATENCY_TIMER,
				      hpp->latency_timer);
		pci_read_config_word(dev, PCI_BRIDGE_CONTROL, &pci_bctl);
		if (hpp->enable_serr)
			pci_bctl |= PCI_BRIDGE_CTL_SERR;
		else
			pci_bctl &= ~PCI_BRIDGE_CTL_SERR;
		if (hpp->enable_perr)
			pci_bctl |= PCI_BRIDGE_CTL_PARITY;
		else
			pci_bctl &= ~PCI_BRIDGE_CTL_PARITY;
		pci_write_config_word(dev, PCI_BRIDGE_CONTROL, pci_bctl);
	}
}

static void program_hpp_type2(struct pci_dev *dev, struct hpp_type2 *hpp)
{
	int pos;
	u16 reg16;
	u32 reg32;

	if (hpp->revision > 1) {
		warn("Rev.%d type2 record not supported\n", hpp->revision);
		return;
	}

	/* Find PCI Express capability */
	pos = pci_find_capability(dev, PCI_CAP_ID_EXP);
	if (!pos)
		return;

	/* Initialize Device Control Register */
	pci_read_config_word(dev, pos + PCI_EXP_DEVCTL, &reg16);
	reg16 = (reg16 & hpp->pci_exp_devctl_and) | hpp->pci_exp_devctl_or;
	pci_write_config_word(dev, pos + PCI_EXP_DEVCTL, reg16);

	/* Initialize Link Control Register */
	if (dev->subordinate) {
		pci_read_config_word(dev, pos + PCI_EXP_LNKCTL, &reg16);
		reg16 = (reg16 & hpp->pci_exp_lnkctl_and)
			| hpp->pci_exp_lnkctl_or;
		pci_write_config_word(dev, pos + PCI_EXP_LNKCTL, reg16);
	}

	/* Find Advanced Error Reporting Enhanced Capability */
	pos = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_ERR);
	if (!pos)
		return;

	/* Initialize Uncorrectable Error Mask Register */
	pci_read_config_dword(dev, pos + PCI_ERR_UNCOR_MASK, &reg32);
	reg32 = (reg32 & hpp->unc_err_mask_and) | hpp->unc_err_mask_or;
	pci_write_config_dword(dev, pos + PCI_ERR_UNCOR_MASK, reg32);

	/* Initialize Uncorrectable Error Severity Register */
	pci_read_config_dword(dev, pos + PCI_ERR_UNCOR_SEVER, &reg32);
	reg32 = (reg32 & hpp->unc_err_sever_and) | hpp->unc_err_sever_or;
	pci_write_config_dword(dev, pos + PCI_ERR_UNCOR_SEVER, reg32);

	/* Initialize Correctable Error Mask Register */
	pci_read_config_dword(dev, pos + PCI_ERR_COR_MASK, &reg32);
	reg32 = (reg32 & hpp->cor_err_mask_and) | hpp->cor_err_mask_or;
	pci_write_config_dword(dev, pos + PCI_ERR_COR_MASK, reg32);

	/* Initialize Advanced Error Capabilities and Control Register */
	pci_read_config_dword(dev, pos + PCI_ERR_CAP, &reg32);
	reg32 = (reg32 & hpp->adv_err_cap_and) | hpp->adv_err_cap_or;
	pci_write_config_dword(dev, pos + PCI_ERR_CAP, reg32);

	/*
	 * FIXME: The following two registers are not supported yet.
	 *
	 *   o Secondary Uncorrectable Error Severity Register
	 *   o Secondary Uncorrectable Error Mask Register
	 */
}

static void program_fw_provided_values(struct pci_dev *dev)
{
	struct pci_dev *cdev;
	struct hotplug_params hpp;

	/* Program hpp values for this device */
	if (!(dev->hdr_type == PCI_HEADER_TYPE_NORMAL ||
			(dev->hdr_type == PCI_HEADER_TYPE_BRIDGE &&
			(dev->class >> 8) == PCI_CLASS_BRIDGE_PCI)))
		return;

	if (pciehp_get_hp_params_from_firmware(dev, &hpp)) {
		warn("Could not get hotplug parameters\n");
		return;
	}

	if (hpp.t2)
		program_hpp_type2(dev, hpp.t2);
	if (hpp.t0)
		program_hpp_type0(dev, hpp.t0);

	/* Program child devices */
	if (dev->subordinate) {
		list_for_each_entry(cdev, &dev->subordinate->devices,
				    bus_list)
			program_fw_provided_values(cdev);
	}
}

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
	struct controller *ctrl = p_slot->ctrl;

	dev = pci_get_slot(parent, PCI_DEVFN(p_slot->device, 0));
	if (dev) {
		ctrl_err(ctrl, "Device %s already exists "
			 "at %04x:%02x:%02x, cannot hot-add\n", pci_name(dev),
			 pci_domain_nr(parent), p_slot->bus, p_slot->device);
		pci_dev_put(dev);
		return -EINVAL;
	}

	num = pci_scan_slot(parent, PCI_DEVFN(p_slot->device, 0));
	if (num == 0) {
		ctrl_err(ctrl, "No new device found\n");
		return -ENODEV;
	}

	for (fn = 0; fn < 8; fn++) {
		dev = pci_get_slot(parent, PCI_DEVFN(p_slot->device, fn));
		if (!dev)
			continue;
		if ((dev->class >> 16) == PCI_BASE_CLASS_DISPLAY) {
			ctrl_err(ctrl, "Cannot hot-add display device %s\n",
				 pci_name(dev));
			pci_dev_put(dev);
			continue;
		}
		if ((dev->hdr_type == PCI_HEADER_TYPE_BRIDGE) ||
				(dev->hdr_type == PCI_HEADER_TYPE_CARDBUS)) {
			pciehp_add_bridge(dev);
		}
		program_fw_provided_values(dev);
		pci_dev_put(dev);
	}

	pci_bus_assign_resources(parent);
	pci_bus_add_devices(parent);
	return 0;
}

int pciehp_unconfigure_device(struct slot *p_slot)
{
	int ret, rc = 0;
	int j;
	u8 bctl = 0;
	u8 presence = 0;
	struct pci_bus *parent = p_slot->ctrl->pci_dev->subordinate;
	u16 command;
	struct controller *ctrl = p_slot->ctrl;

	ctrl_dbg(ctrl, "%s: domain:bus:dev = %04x:%02x:%02x\n",
		 __func__, pci_domain_nr(parent), p_slot->bus, p_slot->device);
	ret = p_slot->hpc_ops->get_adapter_status(p_slot, &presence);
	if (ret)
		presence = 0;

	for (j = 0; j < 8; j++) {
		struct pci_dev* temp = pci_get_slot(parent,
				(p_slot->device << 3) | j);
		if (!temp)
			continue;
		if ((temp->class >> 16) == PCI_BASE_CLASS_DISPLAY) {
			ctrl_err(ctrl, "Cannot remove display device %s\n",
				 pci_name(temp));
			pci_dev_put(temp);
			continue;
		}
		if (temp->hdr_type == PCI_HEADER_TYPE_BRIDGE && presence) {
			pci_read_config_byte(temp, PCI_BRIDGE_CONTROL, &bctl);
			if (bctl & PCI_BRIDGE_CTL_VGA) {
				ctrl_err(ctrl,
					 "Cannot remove display device %s\n",
					 pci_name(temp));
				pci_dev_put(temp);
				continue;
			}
		}
		pci_remove_bus_device(temp);
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
	/*
	 * Some PCI Express root ports require fixup after hot-plug operation.
	 */
	if (pcie_mch_quirk)
		pci_fixup_device(pci_fixup_final, p_slot->ctrl->pci_dev);

	return rc;
}
