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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/proc_fs.h>
#include <linux/pci.h>
#include "../pci.h"
#include "shpchp.h"

void program_fw_provided_values(struct pci_dev *dev)
{
	u16 pci_cmd, pci_bctl;
	struct pci_dev *cdev;
	struct hotplug_params hpp = {0x8, 0x40, 0, 0}; /* defaults */

	/* Program hpp values for this device */
	if (!(dev->hdr_type == PCI_HEADER_TYPE_NORMAL ||
			(dev->hdr_type == PCI_HEADER_TYPE_BRIDGE &&
			(dev->class >> 8) == PCI_CLASS_BRIDGE_PCI)))
		return;

	get_hp_params_from_firmware(dev, &hpp);

	pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, hpp.cache_line_size);
	pci_write_config_byte(dev, PCI_LATENCY_TIMER, hpp.latency_timer);
	pci_read_config_word(dev, PCI_COMMAND, &pci_cmd);
	if (hpp.enable_serr)
		pci_cmd |= PCI_COMMAND_SERR;
	else
		pci_cmd &= ~PCI_COMMAND_SERR;
	if (hpp.enable_perr)
		pci_cmd |= PCI_COMMAND_PARITY;
	else
		pci_cmd &= ~PCI_COMMAND_PARITY;
	pci_write_config_word(dev, PCI_COMMAND, pci_cmd);

	/* Program bridge control value and child devices */
	if ((dev->class >> 8) == PCI_CLASS_BRIDGE_PCI) {
		pci_write_config_byte(dev, PCI_SEC_LATENCY_TIMER,
				hpp.latency_timer);
		pci_read_config_word(dev, PCI_BRIDGE_CONTROL, &pci_bctl);
		if (hpp.enable_serr)
			pci_bctl |= PCI_BRIDGE_CTL_SERR;
		else
			pci_bctl &= ~PCI_BRIDGE_CTL_SERR;
		if (hpp.enable_perr)
			pci_bctl |= PCI_BRIDGE_CTL_PARITY;
		else
			pci_bctl &= ~PCI_BRIDGE_CTL_PARITY;
		pci_write_config_word(dev, PCI_BRIDGE_CONTROL, pci_bctl);
		if (dev->subordinate) {
			list_for_each_entry(cdev, &dev->subordinate->devices,
					bus_list)
				program_fw_provided_values(cdev);
		}
	}
}

int shpchp_configure_device(struct slot *p_slot)
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
		if (!(dev = pci_find_slot(p_slot->bus,
					PCI_DEVFN(p_slot->device, fn))))
			continue;
		if ((dev->class >> 16) == PCI_BASE_CLASS_DISPLAY) {
			err("Cannot hot-add display device %s\n",
					pci_name(dev));
			continue;
		}
		if ((dev->hdr_type == PCI_HEADER_TYPE_BRIDGE) ||
				(dev->hdr_type == PCI_HEADER_TYPE_CARDBUS)) {
			/* Find an unused bus number for the new bridge */
			struct pci_bus *child;
			unsigned char busnr, start = parent->secondary;
			unsigned char end = parent->subordinate;
			for (busnr = start; busnr <= end; busnr++) {
				if (!pci_find_bus(pci_domain_nr(parent),
							busnr))
					break;
			}
			if (busnr >= end) {
				err("No free bus for hot-added bridge\n");
				continue;
			}
			child = pci_add_new_bus(parent, dev, busnr);
			if (!child) {
				err("Cannot add new bus for %s\n",
						pci_name(dev));
				continue;
			}
			child->subordinate = pci_do_scan_bus(child);
			pci_bus_size_bridges(child);
		}
		program_fw_provided_values(dev);
	}

	pci_bus_assign_resources(parent);
	pci_bus_add_devices(parent);
	pci_enable_bridges(parent);
	return 0;
}

int shpchp_unconfigure_device(struct pci_func* func) 
{
	int rc = 0;
	int j;
	
	dbg("%s: bus/dev/func = %x/%x/%x\n", __FUNCTION__, func->bus,
				func->device, func->function);

	for (j=0; j<8 ; j++) {
		struct pci_dev* temp = pci_find_slot(func->bus,
				(func->device << 3) | j);
		if (temp) {
			pci_remove_bus_device(temp);
		}
	}
	return rc;
}

/* More PCI configuration routines; this time centered around hotplug controller */


/*
 * shpchp_save_config
 *
 * Reads configuration for all slots in a PCI bus and saves info.
 *
 * Note:  For non-hot plug busses, the slot # saved is the device #
 *
 * returns 0 if success
 */
int shpchp_save_config(struct controller *ctrl, int busnumber, int num_ctlr_slots, int first_device_num)
{
	int rc;
	u8 class_code;
	u8 header_type;
	u32 ID;
	u8 secondary_bus;
	struct pci_func *new_slot;
	int sub_bus;
	int FirstSupported;
	int LastSupported;
	int max_functions;
	int function;
	u8 DevError;
	int device = 0;
	int cloop = 0;
	int stop_it;
	int index;
	int is_hot_plug = num_ctlr_slots || first_device_num;
	struct pci_bus lpci_bus, *pci_bus;

	dbg("%s: num_ctlr_slots = %d, first_device_num = %d\n", __FUNCTION__,
				num_ctlr_slots, first_device_num);

	memcpy(&lpci_bus, ctrl->pci_dev->subordinate, sizeof(lpci_bus));
	pci_bus = &lpci_bus;

	dbg("%s: num_ctlr_slots = %d, first_device_num = %d\n", __FUNCTION__,
				num_ctlr_slots, first_device_num);

	/*   Decide which slots are supported */
	if (is_hot_plug) {
		/*********************************
		 *  is_hot_plug is the slot mask
		 *********************************/
		FirstSupported = first_device_num;
		LastSupported = FirstSupported + num_ctlr_slots - 1;
	} else {
		FirstSupported = 0;
		LastSupported = 0x1F;
	}

	dbg("FirstSupported = %d, LastSupported = %d\n", FirstSupported,
					LastSupported);

	/*   Save PCI configuration space for all devices in supported slots */
	pci_bus->number = busnumber;
	for (device = FirstSupported; device <= LastSupported; device++) {
		ID = 0xFFFFFFFF;
		rc = pci_bus_read_config_dword(pci_bus, PCI_DEVFN(device, 0),
					PCI_VENDOR_ID, &ID);

		if (ID != 0xFFFFFFFF) {	  /*  device in slot */
			rc = pci_bus_read_config_byte(pci_bus, PCI_DEVFN(device, 0),
					0x0B, &class_code);
			if (rc)
				return rc;

			rc = pci_bus_read_config_byte(pci_bus, PCI_DEVFN(device, 0),
					PCI_HEADER_TYPE, &header_type);
			if (rc)
				return rc;

			dbg("class_code = %x, header_type = %x\n", class_code, header_type);

			/* If multi-function device, set max_functions to 8 */
			if (header_type & 0x80)
				max_functions = 8;
			else
				max_functions = 1;

			function = 0;

			do {
				DevError = 0;

				if ((header_type & 0x7F) == PCI_HEADER_TYPE_BRIDGE) {   /* P-P Bridge */
					/* Recurse the subordinate bus
					 * get the subordinate bus number
					 */
					rc = pci_bus_read_config_byte(pci_bus,
						PCI_DEVFN(device, function), 
						PCI_SECONDARY_BUS, &secondary_bus);
					if (rc) {
						return rc;
					} else {
						sub_bus = (int) secondary_bus;

						/* Save secondary bus cfg spc with this recursive call. */
						rc = shpchp_save_config(ctrl, sub_bus, 0, 0);
						if (rc)
							return rc;
					}
				}

				index = 0;
				new_slot = shpchp_slot_find(busnumber, device, index++);

				dbg("new_slot = %p\n", new_slot);

				while (new_slot && (new_slot->function != (u8) function)) {
					new_slot = shpchp_slot_find(busnumber, device, index++);
					dbg("new_slot = %p\n", new_slot);
				}
				if (!new_slot) {
					/* Setup slot structure. */
					new_slot = shpchp_slot_create(busnumber);
					dbg("new_slot = %p\n", new_slot);

					if (new_slot == NULL)
						return(1);
				}

				new_slot->bus = (u8) busnumber;
				new_slot->device = (u8) device;
				new_slot->function = (u8) function;
				new_slot->is_a_board = 1;
				new_slot->switch_save = 0x10;
				new_slot->pwr_save = 1;
				/* In case of unsupported board */
				new_slot->status = DevError;
				new_slot->pci_dev = pci_find_slot(new_slot->bus,
					(new_slot->device << 3) | new_slot->function);
				dbg("new_slot->pci_dev = %p\n", new_slot->pci_dev);

				for (cloop = 0; cloop < 0x20; cloop++) {
					rc = pci_bus_read_config_dword(pci_bus,
						PCI_DEVFN(device, function), 
						cloop << 2,
						(u32 *) &(new_slot->config_space [cloop]));
					/* dbg("new_slot->config_space[%x] = %x\n",
						cloop, new_slot->config_space[cloop]); */
					if (rc)
						return rc;
				}

				function++;

				stop_it = 0;

				/*  this loop skips to the next present function
				 *  reading in Class Code and Header type.
				 */

				while ((function < max_functions)&&(!stop_it)) {
					rc = pci_bus_read_config_dword(pci_bus,
						PCI_DEVFN(device, function),
						PCI_VENDOR_ID, &ID);

					if (ID == 0xFFFFFFFF) {  /* nothing there. */
						function++;
						dbg("Nothing there\n");
					} else {  /* Something there */
						rc = pci_bus_read_config_byte(pci_bus,
							PCI_DEVFN(device, function), 
							0x0B, &class_code);
						if (rc)
							return rc;

						rc = pci_bus_read_config_byte(pci_bus,
							PCI_DEVFN(device, function), 
							PCI_HEADER_TYPE, &header_type);
						if (rc)
							return rc;

						dbg("class_code = %x, header_type = %x\n",
							class_code, header_type);
						stop_it++;
					}
				}

			} while (function < max_functions);
			/* End of IF (device in slot?) */
		} else if (is_hot_plug) {
			/* Setup slot structure with entry for empty slot */
			new_slot = shpchp_slot_create(busnumber);

			if (new_slot == NULL) {
				return(1);
			}
			dbg("new_slot = %p\n", new_slot);

			new_slot->bus = (u8) busnumber;
			new_slot->device = (u8) device;
			new_slot->function = 0;
			new_slot->is_a_board = 0;
			new_slot->presence_save = 0;
			new_slot->switch_save = 0;
		}
	}			/* End of FOR loop */

	return(0);
}


/*
 * shpchp_save_slot_config
 *
 * Saves configuration info for all PCI devices in a given slot
 * including subordinate busses.
 *
 * returns 0 if success
 */
int shpchp_save_slot_config(struct controller *ctrl, struct pci_func * new_slot)
{
	int rc;
	u8 class_code;
	u8 header_type;
	u32 ID;
	u8 secondary_bus;
	int sub_bus;
	int max_functions;
	int function;
	int cloop = 0;
	int stop_it;
	struct pci_bus lpci_bus, *pci_bus;
	memcpy(&lpci_bus, ctrl->pci_dev->subordinate, sizeof(lpci_bus));
	pci_bus = &lpci_bus;
	pci_bus->number = new_slot->bus;

	ID = 0xFFFFFFFF;

	pci_bus_read_config_dword(pci_bus, PCI_DEVFN(new_slot->device, 0),
					PCI_VENDOR_ID, &ID);

	if (ID != 0xFFFFFFFF) {	  /*  device in slot */
		pci_bus_read_config_byte(pci_bus, PCI_DEVFN(new_slot->device, 0),
					0x0B, &class_code);

		pci_bus_read_config_byte(pci_bus, PCI_DEVFN(new_slot->device, 0),
					PCI_HEADER_TYPE, &header_type);

		if (header_type & 0x80)	/* Multi-function device */
			max_functions = 8;
		else
			max_functions = 1;

		function = 0;

		do {
			if ((header_type & 0x7F) == PCI_HEADER_TYPE_BRIDGE) {	  /* PCI-PCI Bridge */
				/*  Recurse the subordinate bus */
				pci_bus_read_config_byte(pci_bus,
					PCI_DEVFN(new_slot->device, function), 
					PCI_SECONDARY_BUS, &secondary_bus);

				sub_bus = (int) secondary_bus;

				/* Save the config headers for the secondary bus. */
				rc = shpchp_save_config(ctrl, sub_bus, 0, 0);

				if (rc)
					return rc;

			}	/* End of IF */

			new_slot->status = 0;

			for (cloop = 0; cloop < 0x20; cloop++) {
				pci_bus_read_config_dword(pci_bus,
					PCI_DEVFN(new_slot->device, function), 
					cloop << 2,
					(u32 *) &(new_slot->config_space [cloop]));
			}

			function++;

			stop_it = 0;

			/*  this loop skips to the next present function
			 *  reading in the Class Code and the Header type.
			 */

			while ((function < max_functions) && (!stop_it)) {
				pci_bus_read_config_dword(pci_bus,
					PCI_DEVFN(new_slot->device, function),
					PCI_VENDOR_ID, &ID);

				if (ID == 0xFFFFFFFF) {	 /* nothing there. */
					function++;
				} else {  /* Something there */
					pci_bus_read_config_byte(pci_bus,
						PCI_DEVFN(new_slot->device, function),
						0x0B, &class_code);

					pci_bus_read_config_byte(pci_bus,
						PCI_DEVFN(new_slot->device, function),
						PCI_HEADER_TYPE, &header_type);

					stop_it++;
				}
			}

		} while (function < max_functions);
	}			/* End of IF (device in slot?) */
	else {
		return 2;
	}

	return 0;
}

