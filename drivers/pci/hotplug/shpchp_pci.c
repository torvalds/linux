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
 * Send feedback to <greg@kroah.com>, <dely.l.sy@intel.com>
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
#ifndef CONFIG_IA64
#include "../../../arch/i386/pci/pci.h"    /* horrible hack showing how processor dependant we are... */
#endif

int shpchp_configure_device (struct controller* ctrl, struct pci_func* func)  
{
	unsigned char bus;
	struct pci_bus *child;
	int num;

	if (func->pci_dev == NULL)
		func->pci_dev = pci_find_slot(func->bus, PCI_DEVFN(func->device, func->function));

	/* Still NULL ? Well then scan for it ! */
	if (func->pci_dev == NULL) {
		num = pci_scan_slot(ctrl->pci_dev->subordinate, PCI_DEVFN(func->device, func->function));
		if (num) {
			dbg("%s: subordiante %p number %x\n", __FUNCTION__, ctrl->pci_dev->subordinate,
				ctrl->pci_dev->subordinate->number);
			pci_bus_add_devices(ctrl->pci_dev->subordinate);
		}
		
		func->pci_dev = pci_find_slot(func->bus, PCI_DEVFN(func->device, func->function));
		if (func->pci_dev == NULL) {
			dbg("ERROR: pci_dev still null\n");
			return 0;
		}
	}

	if (func->pci_dev->hdr_type == PCI_HEADER_TYPE_BRIDGE) {
		pci_read_config_byte(func->pci_dev, PCI_SECONDARY_BUS, &bus);
		child = pci_add_new_bus(func->pci_dev->bus, (func->pci_dev), bus);
		pci_do_scan_bus(child);

	}

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

/*
 * shpchp_set_irq
 *
 * @bus_num: bus number of PCI device
 * @dev_num: device number of PCI device
 * @slot: pointer to u8 where slot number will be returned
 */
int shpchp_set_irq (u8 bus_num, u8 dev_num, u8 int_pin, u8 irq_num)
{
#if defined(CONFIG_X86) && !defined(CONFIG_X86_IO_APIC) && !defined(CONFIG_X86_64)
	int rc;
	u16 temp_word;
	struct pci_dev fakedev;
	struct pci_bus fakebus;

	fakedev.devfn = dev_num << 3;
	fakedev.bus = &fakebus;
	fakebus.number = bus_num;
	dbg("%s: dev %d, bus %d, pin %d, num %d\n",
	    __FUNCTION__, dev_num, bus_num, int_pin, irq_num);
	rc = pcibios_set_irq_routing(&fakedev, int_pin - 0x0a, irq_num);
	dbg("%s: rc %d\n", __FUNCTION__, rc);
	if (!rc)
		return !rc;

	/* set the Edge Level Control Register (ELCR) */
	temp_word = inb(0x4d0);
	temp_word |= inb(0x4d1) << 8;

	temp_word |= 0x01 << irq_num;

	/* This should only be for x86 as it sets the Edge Level Control Register */
	outb((u8) (temp_word & 0xFF), 0x4d0);
	outb((u8) ((temp_word & 0xFF00) >> 8), 0x4d1);
#endif
	return 0;
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


/*
 * shpchp_save_used_resources
 *
 * Stores used resource information for existing boards.  this is
 * for boards that were in the system when this driver was loaded.
 * this function is for hot plug ADD
 *
 * returns 0 if success
 * if disable  == 1(DISABLE_CARD),
 *  it loops for all functions of the slot and disables them.
 * else, it just get resources of the function and return.
 */
int shpchp_save_used_resources(struct controller *ctrl, struct pci_func *func, int disable)
{
	u8 cloop;
	u8 header_type;
	u8 secondary_bus;
	u8 temp_byte;
	u16 command;
	u16 save_command;
	u16 w_base, w_length;
	u32 temp_register;
	u32 save_base;
	u32 base, length;
	u64 base64 = 0;
	int index = 0;
	unsigned int devfn;
	struct pci_resource *mem_node = NULL;
	struct pci_resource *p_mem_node = NULL;
	struct pci_resource *t_mem_node;
	struct pci_resource *io_node;
	struct pci_resource *bus_node;
	struct pci_bus lpci_bus, *pci_bus;
	memcpy(&lpci_bus, ctrl->pci_dev->subordinate, sizeof(lpci_bus));
	pci_bus = &lpci_bus;

	if (disable)
		func = shpchp_slot_find(func->bus, func->device, index++);

	while ((func != NULL) && func->is_a_board) {
		pci_bus->number = func->bus;
		devfn = PCI_DEVFN(func->device, func->function);

		/* Save the command register */
		pci_bus_read_config_word(pci_bus, devfn, PCI_COMMAND, &save_command);

		if (disable) {
			/* disable card */
			command = 0x00;
			pci_bus_write_config_word(pci_bus, devfn, PCI_COMMAND, command);
		}

		/* Check for Bridge */
		pci_bus_read_config_byte(pci_bus, devfn, PCI_HEADER_TYPE, &header_type);

		if ((header_type & 0x7F) == PCI_HEADER_TYPE_BRIDGE) {     /* PCI-PCI Bridge */
			dbg("Save_used_res of PCI bridge b:d=0x%x:%x, sc=0x%x\n",
					func->bus, func->device, save_command);
			if (disable) {
				/* Clear Bridge Control Register */
				command = 0x00;
				pci_bus_write_config_word(pci_bus, devfn, PCI_BRIDGE_CONTROL, command);
			}

			pci_bus_read_config_byte(pci_bus, devfn, PCI_SECONDARY_BUS, &secondary_bus);
			pci_bus_read_config_byte(pci_bus, devfn, PCI_SUBORDINATE_BUS, &temp_byte);

			bus_node = kmalloc(sizeof(struct pci_resource),
						GFP_KERNEL);
			if (!bus_node)
				return -ENOMEM;

			bus_node->base = (ulong)secondary_bus;
			bus_node->length = (ulong)(temp_byte - secondary_bus + 1);

			bus_node->next = func->bus_head;
			func->bus_head = bus_node;

			/* Save IO base and Limit registers */
			pci_bus_read_config_byte(pci_bus, devfn, PCI_IO_BASE, &temp_byte);
			base = temp_byte;
			pci_bus_read_config_byte(pci_bus, devfn, PCI_IO_LIMIT, &temp_byte);
			length = temp_byte;

			if ((base <= length) && (!disable || (save_command & PCI_COMMAND_IO))) {
				io_node = kmalloc(sizeof(struct pci_resource),
							GFP_KERNEL);
				if (!io_node)
					return -ENOMEM;

				io_node->base = (ulong)(base & PCI_IO_RANGE_MASK) << 8;
				io_node->length = (ulong)(length - base + 0x10) << 8;

				io_node->next = func->io_head;
				func->io_head = io_node;
			}

			/* Save memory base and Limit registers */
			pci_bus_read_config_word(pci_bus, devfn, PCI_MEMORY_BASE, &w_base);
			pci_bus_read_config_word(pci_bus, devfn, PCI_MEMORY_LIMIT, &w_length);

			if ((w_base <= w_length) && (!disable || (save_command & PCI_COMMAND_MEMORY))) {
				mem_node = kmalloc(sizeof(struct pci_resource),
						GFP_KERNEL);
				if (!mem_node)
					return -ENOMEM;

				mem_node->base = (ulong)w_base << 16;
				mem_node->length = (ulong)(w_length - w_base + 0x10) << 16;

				mem_node->next = func->mem_head;
				func->mem_head = mem_node;
			}
			/* Save prefetchable memory base and Limit registers */
			pci_bus_read_config_word(pci_bus, devfn, PCI_PREF_MEMORY_BASE, &w_base);
			pci_bus_read_config_word(pci_bus, devfn, PCI_PREF_MEMORY_LIMIT, &w_length);

			if ((w_base <= w_length) && (!disable || (save_command & PCI_COMMAND_MEMORY))) {
				p_mem_node = kmalloc(sizeof(struct pci_resource),
						GFP_KERNEL);
				if (!p_mem_node)
					return -ENOMEM;

				p_mem_node->base = (ulong)w_base << 16;
				p_mem_node->length = (ulong)(w_length - w_base + 0x10) << 16;

				p_mem_node->next = func->p_mem_head;
				func->p_mem_head = p_mem_node;
			}
		} else if ((header_type & 0x7F) == PCI_HEADER_TYPE_NORMAL) {
			dbg("Save_used_res of PCI adapter b:d=0x%x:%x, sc=0x%x\n",
					func->bus, func->device, save_command);

			/* Figure out IO and memory base lengths */
			for (cloop = PCI_BASE_ADDRESS_0; cloop <= PCI_BASE_ADDRESS_5; cloop += 4) {
				pci_bus_read_config_dword(pci_bus, devfn, cloop, &save_base);

				temp_register = 0xFFFFFFFF;
				pci_bus_write_config_dword(pci_bus, devfn, cloop, temp_register);
				pci_bus_read_config_dword(pci_bus, devfn, cloop, &temp_register);

				if (!disable)
					pci_bus_write_config_dword(pci_bus, devfn, cloop, save_base);

				if (!temp_register)
					continue;

				base = temp_register;

				if ((base & PCI_BASE_ADDRESS_SPACE_IO) &&
						(!disable || (save_command & PCI_COMMAND_IO))) {
					/* IO base */
					/* set temp_register = amount of IO space requested */
					base = base & 0xFFFFFFFCL;
					base = (~base) + 1;

					io_node =  kmalloc(sizeof (struct pci_resource),
								GFP_KERNEL);
					if (!io_node)
						return -ENOMEM;

					io_node->base = (ulong)save_base & PCI_BASE_ADDRESS_IO_MASK;
					io_node->length = (ulong)base;
					dbg("sur adapter: IO bar=0x%x(length=0x%x)\n",
						io_node->base, io_node->length);

					io_node->next = func->io_head;
					func->io_head = io_node;
				} else {  /* map Memory */
					int prefetchable = 1;
					/* struct pci_resources **res_node; */
					char *res_type_str = "PMEM";
					u32 temp_register2;

					t_mem_node = kmalloc(sizeof (struct pci_resource),
								GFP_KERNEL);
					if (!t_mem_node)
						return -ENOMEM;

					if (!(base & PCI_BASE_ADDRESS_MEM_PREFETCH) &&
							(!disable || (save_command & PCI_COMMAND_MEMORY))) {
						prefetchable = 0;
						mem_node = t_mem_node;
						res_type_str++;
					} else
						p_mem_node = t_mem_node;

					base = base & 0xFFFFFFF0L;
					base = (~base) + 1;

					switch (temp_register & PCI_BASE_ADDRESS_MEM_TYPE_MASK) {
					case PCI_BASE_ADDRESS_MEM_TYPE_32:
						if (prefetchable) {
							p_mem_node->base = (ulong)save_base & PCI_BASE_ADDRESS_MEM_MASK;
							p_mem_node->length = (ulong)base;
							dbg("sur adapter: 32 %s bar=0x%x(length=0x%x)\n",
								res_type_str, 
								p_mem_node->base,
								p_mem_node->length);

							p_mem_node->next = func->p_mem_head;
							func->p_mem_head = p_mem_node;
						} else {
							mem_node->base = (ulong)save_base & PCI_BASE_ADDRESS_MEM_MASK;
							mem_node->length = (ulong)base;
							dbg("sur adapter: 32 %s bar=0x%x(length=0x%x)\n",
								res_type_str, 
								mem_node->base,
								mem_node->length);

							mem_node->next = func->mem_head;
							func->mem_head = mem_node;
						}
						break;
					case PCI_BASE_ADDRESS_MEM_TYPE_64:
						pci_bus_read_config_dword(pci_bus, devfn, cloop+4, &temp_register2);
						base64 = temp_register2;
						base64 = (base64 << 32) | save_base;

						if (temp_register2) {
							dbg("sur adapter: 64 %s high dword of base64(0x%x:%x) masked to 0\n", 
								res_type_str, temp_register2, (u32)base64);
							base64 &= 0x00000000FFFFFFFFL;
						}

						if (prefetchable) {
							p_mem_node->base = base64 & PCI_BASE_ADDRESS_MEM_MASK;
							p_mem_node->length = base;
							dbg("sur adapter: 64 %s base=0x%x(len=0x%x)\n",
								res_type_str, 
								p_mem_node->base,
								p_mem_node->length);

							p_mem_node->next = func->p_mem_head;
							func->p_mem_head = p_mem_node;
						} else {
							mem_node->base = base64 & PCI_BASE_ADDRESS_MEM_MASK;
							mem_node->length = base;
							dbg("sur adapter: 64 %s base=0x%x(len=0x%x)\n",
								res_type_str, 
								mem_node->base,
								mem_node->length);

							mem_node->next = func->mem_head;
							func->mem_head = mem_node;
						}
						cloop += 4;
						break;
					default:
						dbg("asur: reserved BAR type=0x%x\n",
							temp_register);
						break;
					}
				} 
			}	/* End of base register loop */
		} else {	/* Some other unknown header type */
			dbg("Save_used_res of PCI unknown type b:d=0x%x:%x. skip.\n",
					func->bus, func->device);
		}

		/* find the next device in this slot */
		if (!disable)
			break;
		func = shpchp_slot_find(func->bus, func->device, index++);
	}

	return 0;
}

/**
 * kfree_resource_list: release memory of all list members
 * @res: resource list to free
 */
static inline void
return_resource_list(struct pci_resource **func, struct pci_resource **res)
{
	struct pci_resource *node;
	struct pci_resource *t_node;

	node = *func;
	*func = NULL;
	while (node) {
		t_node = node->next;
		return_resource(res, node);
		node = t_node;
	}
}

/*
 * shpchp_return_board_resources
 *
 * this routine returns all resources allocated to a board to
 * the available pool.
 *
 * returns 0 if success
 */
int shpchp_return_board_resources(struct pci_func * func,
					struct resource_lists * resources)
{
	int rc;
	dbg("%s\n", __FUNCTION__);

	if (!func)
		return 1;

	return_resource_list(&(func->io_head),&(resources->io_head));
	return_resource_list(&(func->mem_head),&(resources->mem_head));
	return_resource_list(&(func->p_mem_head),&(resources->p_mem_head));
	return_resource_list(&(func->bus_head),&(resources->bus_head));

	rc = shpchp_resource_sort_and_combine(&(resources->mem_head));
	rc |= shpchp_resource_sort_and_combine(&(resources->p_mem_head));
	rc |= shpchp_resource_sort_and_combine(&(resources->io_head));
	rc |= shpchp_resource_sort_and_combine(&(resources->bus_head));

	return rc;
}

/**
 * kfree_resource_list: release memory of all list members
 * @res: resource list to free
 */
static inline void
kfree_resource_list(struct pci_resource **r)
{
	struct pci_resource *res, *tres;

	res = *r;
	*r = NULL;

	while (res) {
		tres = res;
		res = res->next;
		kfree(tres);
	}
}

/**
 * shpchp_destroy_resource_list: put node back in the resource list
 * @resources: list to put nodes back
 */
void shpchp_destroy_resource_list(struct resource_lists *resources)
{
	kfree_resource_list(&(resources->io_head));
	kfree_resource_list(&(resources->mem_head));
	kfree_resource_list(&(resources->p_mem_head));
	kfree_resource_list(&(resources->bus_head));
}

/**
 * shpchp_destroy_board_resources: put node back in the resource list
 * @resources: list to put nodes back
 */
void shpchp_destroy_board_resources(struct pci_func * func)
{
	kfree_resource_list(&(func->io_head));
	kfree_resource_list(&(func->mem_head));
	kfree_resource_list(&(func->p_mem_head));
	kfree_resource_list(&(func->bus_head));
}
