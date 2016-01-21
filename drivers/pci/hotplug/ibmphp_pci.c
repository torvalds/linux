/*
 * IBM Hot Plug Controller Driver
 *
 * Written By: Irene Zubarev, IBM Corporation
 *
 * Copyright (C) 2001 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2001,2002 IBM Corp.
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
 * Send feedback to <gregkh@us.ibm.com>
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/list.h>
#include "ibmphp.h"


static int configure_device(struct pci_func *);
static int configure_bridge(struct pci_func **, u8);
static struct res_needed *scan_behind_bridge(struct pci_func *, u8);
static int add_new_bus(struct bus_node *, struct resource_node *, struct resource_node *, struct resource_node *, u8);
static u8 find_sec_number(u8 primary_busno, u8 slotno);

/*
 * NOTE..... If BIOS doesn't provide default routing, we assign:
 * 9 for SCSI, 10 for LAN adapters, and 11 for everything else.
 * If adapter is bridged, then we assign 11 to it and devices behind it.
 * We also assign the same irq numbers for multi function devices.
 * These are PIC mode, so shouldn't matter n.e.ways (hopefully)
 */
static void assign_alt_irq(struct pci_func *cur_func, u8 class_code)
{
	int j;
	for (j = 0; j < 4; j++) {
		if (cur_func->irq[j] == 0xff) {
			switch (class_code) {
				case PCI_BASE_CLASS_STORAGE:
					cur_func->irq[j] = SCSI_IRQ;
					break;
				case PCI_BASE_CLASS_NETWORK:
					cur_func->irq[j] = LAN_IRQ;
					break;
				default:
					cur_func->irq[j] = OTHER_IRQ;
					break;
			}
		}
	}
}

/*
 * Configures the device to be added (will allocate needed resources if it
 * can), the device can be a bridge or a regular pci device, can also be
 * multi-functional
 *
 * Input: function to be added
 *
 * TO DO:  The error case with Multifunction device or multi function bridge,
 * if there is an error, will need to go through all previous functions and
 * unconfigure....or can add some code into unconfigure_card....
 */
int ibmphp_configure_card(struct pci_func *func, u8 slotno)
{
	u16 vendor_id;
	u32 class;
	u8 class_code;
	u8 hdr_type, device, sec_number;
	u8 function;
	struct pci_func *newfunc;	/* for multi devices */
	struct pci_func *cur_func, *prev_func;
	int rc, i, j;
	int cleanup_count;
	u8 flag;
	u8 valid_device = 0x00; /* to see if we are able to read from card any device info at all */

	debug("inside configure_card, func->busno = %x\n", func->busno);

	device = func->device;
	cur_func = func;

	/* We only get bus and device from IRQ routing table.  So at this point,
	 * func->busno is correct, and func->device contains only device (at the 5
	 * highest bits)
	 */

	/* For every function on the card */
	for (function = 0x00; function < 0x08; function++) {
		unsigned int devfn = PCI_DEVFN(device, function);
		ibmphp_pci_bus->number = cur_func->busno;

		cur_func->function = function;

		debug("inside the loop, cur_func->busno = %x, cur_func->device = %x, cur_func->function = %x\n",
			cur_func->busno, cur_func->device, cur_func->function);

		pci_bus_read_config_word(ibmphp_pci_bus, devfn, PCI_VENDOR_ID, &vendor_id);

		debug("vendor_id is %x\n", vendor_id);
		if (vendor_id != PCI_VENDOR_ID_NOTVALID) {
			/* found correct device!!! */
			debug("found valid device, vendor_id = %x\n", vendor_id);

			++valid_device;

			/* header: x x x x x x x x
			 *         | |___________|=> 1=PPB bridge, 0=normal device, 2=CardBus Bridge
			 *         |_=> 0 = single function device, 1 = multi-function device
			 */

			pci_bus_read_config_byte(ibmphp_pci_bus, devfn, PCI_HEADER_TYPE, &hdr_type);
			pci_bus_read_config_dword(ibmphp_pci_bus, devfn, PCI_CLASS_REVISION, &class);

			class_code = class >> 24;
			debug("hrd_type = %x, class = %x, class_code %x\n", hdr_type, class, class_code);
			class >>= 8;	/* to take revision out, class = class.subclass.prog i/f */
			if (class == PCI_CLASS_NOT_DEFINED_VGA) {
				err("The device %x is VGA compatible and as is not supported for hot plugging. "
				     "Please choose another device.\n", cur_func->device);
				return -ENODEV;
			} else if (class == PCI_CLASS_DISPLAY_VGA) {
				err("The device %x is not supported for hot plugging. Please choose another device.\n",
				     cur_func->device);
				return -ENODEV;
			}
			switch (hdr_type) {
				case PCI_HEADER_TYPE_NORMAL:
					debug("single device case.... vendor id = %x, hdr_type = %x, class = %x\n", vendor_id, hdr_type, class);
					assign_alt_irq(cur_func, class_code);
					rc = configure_device(cur_func);
					if (rc < 0) {
						/* We need to do this in case some other BARs were properly inserted */
						err("was not able to configure devfunc %x on bus %x.\n",
						     cur_func->device, cur_func->busno);
						cleanup_count = 6;
						goto error;
					}
					cur_func->next = NULL;
					function = 0x8;
					break;
				case PCI_HEADER_TYPE_MULTIDEVICE:
					assign_alt_irq(cur_func, class_code);
					rc = configure_device(cur_func);
					if (rc < 0) {
						/* We need to do this in case some other BARs were properly inserted */
						err("was not able to configure devfunc %x on bus %x...bailing out\n",
						     cur_func->device, cur_func->busno);
						cleanup_count = 6;
						goto error;
					}
					newfunc = kzalloc(sizeof(*newfunc), GFP_KERNEL);
					if (!newfunc) {
						err("out of system memory\n");
						return -ENOMEM;
					}
					newfunc->busno = cur_func->busno;
					newfunc->device = device;
					cur_func->next = newfunc;
					cur_func = newfunc;
					for (j = 0; j < 4; j++)
						newfunc->irq[j] = cur_func->irq[j];
					break;
				case PCI_HEADER_TYPE_MULTIBRIDGE:
					class >>= 8;
					if (class != PCI_CLASS_BRIDGE_PCI) {
						err("This %x is not PCI-to-PCI bridge, and as is not supported for hot-plugging.  Please insert another card.\n",
						     cur_func->device);
						return -ENODEV;
					}
					assign_alt_irq(cur_func, class_code);
					rc = configure_bridge(&cur_func, slotno);
					if (rc == -ENODEV) {
						err("You chose to insert Single Bridge, or nested bridges, this is not supported...\n");
						err("Bus %x, devfunc %x\n", cur_func->busno, cur_func->device);
						return rc;
					}
					if (rc) {
						/* We need to do this in case some other BARs were properly inserted */
						err("was not able to hot-add PPB properly.\n");
						func->bus = 1; /* To indicate to the unconfigure function that this is a PPB */
						cleanup_count = 2;
						goto error;
					}

					pci_bus_read_config_byte(ibmphp_pci_bus, devfn, PCI_SECONDARY_BUS, &sec_number);
					flag = 0;
					for (i = 0; i < 32; i++) {
						if (func->devices[i]) {
							newfunc = kzalloc(sizeof(*newfunc), GFP_KERNEL);
							if (!newfunc) {
								err("out of system memory\n");
								return -ENOMEM;
							}
							newfunc->busno = sec_number;
							newfunc->device = (u8) i;
							for (j = 0; j < 4; j++)
								newfunc->irq[j] = cur_func->irq[j];

							if (flag) {
								for (prev_func = cur_func; prev_func->next; prev_func = prev_func->next) ;
								prev_func->next = newfunc;
							} else
								cur_func->next = newfunc;

							rc = ibmphp_configure_card(newfunc, slotno);
							/* This could only happen if kmalloc failed */
							if (rc) {
								/* We need to do this in case bridge itself got configured properly, but devices behind it failed */
								func->bus = 1; /* To indicate to the unconfigure function that this is a PPB */
								cleanup_count = 2;
								goto error;
							}
							flag = 1;
						}
					}

					newfunc = kzalloc(sizeof(*newfunc), GFP_KERNEL);
					if (!newfunc) {
						err("out of system memory\n");
						return -ENOMEM;
					}
					newfunc->busno = cur_func->busno;
					newfunc->device = device;
					for (j = 0; j < 4; j++)
						newfunc->irq[j] = cur_func->irq[j];
					for (prev_func = cur_func; prev_func->next; prev_func = prev_func->next);
					prev_func->next = newfunc;
					cur_func = newfunc;
					break;
				case PCI_HEADER_TYPE_BRIDGE:
					class >>= 8;
					debug("class now is %x\n", class);
					if (class != PCI_CLASS_BRIDGE_PCI) {
						err("This %x is not PCI-to-PCI bridge, and as is not supported for hot-plugging.  Please insert another card.\n",
						     cur_func->device);
						return -ENODEV;
					}

					assign_alt_irq(cur_func, class_code);

					debug("cur_func->busno b4 configure_bridge is %x\n", cur_func->busno);
					rc = configure_bridge(&cur_func, slotno);
					if (rc == -ENODEV) {
						err("You chose to insert Single Bridge, or nested bridges, this is not supported...\n");
						err("Bus %x, devfunc %x\n", cur_func->busno, cur_func->device);
						return rc;
					}
					if (rc) {
						/* We need to do this in case some other BARs were properly inserted */
						func->bus = 1; /* To indicate to the unconfigure function that this is a PPB */
						err("was not able to hot-add PPB properly.\n");
						cleanup_count = 2;
						goto error;
					}
					debug("cur_func->busno = %x, device = %x, function = %x\n",
						cur_func->busno, device, function);
					pci_bus_read_config_byte(ibmphp_pci_bus, devfn, PCI_SECONDARY_BUS, &sec_number);
					debug("after configuring bridge..., sec_number = %x\n", sec_number);
					flag = 0;
					for (i = 0; i < 32; i++) {
						if (func->devices[i]) {
							debug("inside for loop, device is %x\n", i);
							newfunc = kzalloc(sizeof(*newfunc), GFP_KERNEL);
							if (!newfunc) {
								err(" out of system memory\n");
								return -ENOMEM;
							}
							newfunc->busno = sec_number;
							newfunc->device = (u8) i;
							for (j = 0; j < 4; j++)
								newfunc->irq[j] = cur_func->irq[j];

							if (flag) {
								for (prev_func = cur_func; prev_func->next; prev_func = prev_func->next);
								prev_func->next = newfunc;
							} else
								cur_func->next = newfunc;

							rc = ibmphp_configure_card(newfunc, slotno);

							/* Again, this case should not happen... For complete paranoia, will need to call remove_bus */
							if (rc) {
								/* We need to do this in case some other BARs were properly inserted */
								func->bus = 1; /* To indicate to the unconfigure function that this is a PPB */
								cleanup_count = 2;
								goto error;
							}
							flag = 1;
						}
					}

					function = 0x8;
					break;
				default:
					err("MAJOR PROBLEM!!!!, header type not supported? %x\n", hdr_type);
					return -ENXIO;
					break;
			}	/* end of switch */
		}	/* end of valid device */
	}	/* end of for */

	if (!valid_device) {
		err("Cannot find any valid devices on the card.  Or unable to read from card.\n");
		return -ENODEV;
	}

	return 0;

error:
	for (i = 0; i < cleanup_count; i++) {
		if (cur_func->io[i]) {
			ibmphp_remove_resource(cur_func->io[i]);
			cur_func->io[i] = NULL;
		} else if (cur_func->pfmem[i]) {
			ibmphp_remove_resource(cur_func->pfmem[i]);
			cur_func->pfmem[i] = NULL;
		} else if (cur_func->mem[i]) {
			ibmphp_remove_resource(cur_func->mem[i]);
			cur_func->mem[i] = NULL;
		}
	}
	return rc;
}

/*
 * This function configures the pci BARs of a single device.
 * Input: pointer to the pci_func
 * Output: configured PCI, 0, or error
 */
static int configure_device(struct pci_func *func)
{
	u32 bar[6];
	u32 address[] = {
		PCI_BASE_ADDRESS_0,
		PCI_BASE_ADDRESS_1,
		PCI_BASE_ADDRESS_2,
		PCI_BASE_ADDRESS_3,
		PCI_BASE_ADDRESS_4,
		PCI_BASE_ADDRESS_5,
		0
	};
	u8 irq;
	int count;
	int len[6];
	struct resource_node *io[6];
	struct resource_node *mem[6];
	struct resource_node *mem_tmp;
	struct resource_node *pfmem[6];
	unsigned int devfn;

	debug("%s - inside\n", __func__);

	devfn = PCI_DEVFN(func->device, func->function);
	ibmphp_pci_bus->number = func->busno;

	for (count = 0; address[count]; count++) {	/* for 6 BARs */

		/* not sure if i need this.  per scott, said maybe need * something like this
		   if devices don't adhere 100% to the spec, so don't want to write
		   to the reserved bits

		pcibios_read_config_byte(cur_func->busno, cur_func->device,
		PCI_BASE_ADDRESS_0 + 4 * count, &tmp);
		if (tmp & 0x01) // IO
			pcibios_write_config_dword(cur_func->busno, cur_func->device,
			PCI_BASE_ADDRESS_0 + 4 * count, 0xFFFFFFFD);
		else  // Memory
			pcibios_write_config_dword(cur_func->busno, cur_func->device,
			PCI_BASE_ADDRESS_0 + 4 * count, 0xFFFFFFFF);
		 */
		pci_bus_write_config_dword(ibmphp_pci_bus, devfn, address[count], 0xFFFFFFFF);
		pci_bus_read_config_dword(ibmphp_pci_bus, devfn, address[count], &bar[count]);

		if (!bar[count])	/* This BAR is not implemented */
			continue;

		debug("Device %x BAR %d wants %x\n", func->device, count, bar[count]);

		if (bar[count] & PCI_BASE_ADDRESS_SPACE_IO) {
			/* This is IO */
			debug("inside IO SPACE\n");

			len[count] = bar[count] & 0xFFFFFFFC;
			len[count] = ~len[count] + 1;

			debug("len[count] in IO %x, count %d\n", len[count], count);

			io[count] = kzalloc(sizeof(struct resource_node), GFP_KERNEL);

			if (!io[count]) {
				err("out of system memory\n");
				return -ENOMEM;
			}
			io[count]->type = IO;
			io[count]->busno = func->busno;
			io[count]->devfunc = PCI_DEVFN(func->device, func->function);
			io[count]->len = len[count];
			if (ibmphp_check_resource(io[count], 0) == 0) {
				ibmphp_add_resource(io[count]);
				func->io[count] = io[count];
			} else {
				err("cannot allocate requested io for bus %x device %x function %x len %x\n",
				     func->busno, func->device, func->function, len[count]);
				kfree(io[count]);
				return -EIO;
			}
			pci_bus_write_config_dword(ibmphp_pci_bus, devfn, address[count], func->io[count]->start);

			/* _______________This is for debugging purposes only_____________________ */
			debug("b4 writing, the IO address is %x\n", func->io[count]->start);
			pci_bus_read_config_dword(ibmphp_pci_bus, devfn, address[count], &bar[count]);
			debug("after writing.... the start address is %x\n", bar[count]);
			/* _________________________________________________________________________*/

		} else {
			/* This is Memory */
			if (bar[count] & PCI_BASE_ADDRESS_MEM_PREFETCH) {
				/* pfmem */
				debug("PFMEM SPACE\n");

				len[count] = bar[count] & 0xFFFFFFF0;
				len[count] = ~len[count] + 1;

				debug("len[count] in PFMEM %x, count %d\n", len[count], count);

				pfmem[count] = kzalloc(sizeof(struct resource_node), GFP_KERNEL);
				if (!pfmem[count]) {
					err("out of system memory\n");
					return -ENOMEM;
				}
				pfmem[count]->type = PFMEM;
				pfmem[count]->busno = func->busno;
				pfmem[count]->devfunc = PCI_DEVFN(func->device,
							func->function);
				pfmem[count]->len = len[count];
				pfmem[count]->fromMem = 0;
				if (ibmphp_check_resource(pfmem[count], 0) == 0) {
					ibmphp_add_resource(pfmem[count]);
					func->pfmem[count] = pfmem[count];
				} else {
					mem_tmp = kzalloc(sizeof(*mem_tmp), GFP_KERNEL);
					if (!mem_tmp) {
						err("out of system memory\n");
						kfree(pfmem[count]);
						return -ENOMEM;
					}
					mem_tmp->type = MEM;
					mem_tmp->busno = pfmem[count]->busno;
					mem_tmp->devfunc = pfmem[count]->devfunc;
					mem_tmp->len = pfmem[count]->len;
					debug("there's no pfmem... going into mem.\n");
					if (ibmphp_check_resource(mem_tmp, 0) == 0) {
						ibmphp_add_resource(mem_tmp);
						pfmem[count]->fromMem = 1;
						pfmem[count]->rangeno = mem_tmp->rangeno;
						pfmem[count]->start = mem_tmp->start;
						pfmem[count]->end = mem_tmp->end;
						ibmphp_add_pfmem_from_mem(pfmem[count]);
						func->pfmem[count] = pfmem[count];
					} else {
						err("cannot allocate requested pfmem for bus %x, device %x, len %x\n",
						     func->busno, func->device, len[count]);
						kfree(mem_tmp);
						kfree(pfmem[count]);
						return -EIO;
					}
				}

				pci_bus_write_config_dword(ibmphp_pci_bus, devfn, address[count], func->pfmem[count]->start);

				/*_______________This is for debugging purposes only______________________________*/
				debug("b4 writing, start address is %x\n", func->pfmem[count]->start);
				pci_bus_read_config_dword(ibmphp_pci_bus, devfn, address[count], &bar[count]);
				debug("after writing, start address is %x\n", bar[count]);
				/*_________________________________________________________________________________*/

				if (bar[count] & PCI_BASE_ADDRESS_MEM_TYPE_64) {	/* takes up another dword */
					debug("inside the mem 64 case, count %d\n", count);
					count += 1;
					/* on the 2nd dword, write all 0s, since we can't handle them n.e.ways */
					pci_bus_write_config_dword(ibmphp_pci_bus, devfn, address[count], 0x00000000);
				}
			} else {
				/* regular memory */
				debug("REGULAR MEM SPACE\n");

				len[count] = bar[count] & 0xFFFFFFF0;
				len[count] = ~len[count] + 1;

				debug("len[count] in Mem %x, count %d\n", len[count], count);

				mem[count] = kzalloc(sizeof(struct resource_node), GFP_KERNEL);
				if (!mem[count]) {
					err("out of system memory\n");
					return -ENOMEM;
				}
				mem[count]->type = MEM;
				mem[count]->busno = func->busno;
				mem[count]->devfunc = PCI_DEVFN(func->device,
							func->function);
				mem[count]->len = len[count];
				if (ibmphp_check_resource(mem[count], 0) == 0) {
					ibmphp_add_resource(mem[count]);
					func->mem[count] = mem[count];
				} else {
					err("cannot allocate requested mem for bus %x, device %x, len %x\n",
					     func->busno, func->device, len[count]);
					kfree(mem[count]);
					return -EIO;
				}
				pci_bus_write_config_dword(ibmphp_pci_bus, devfn, address[count], func->mem[count]->start);
				/* _______________________This is for debugging purposes only _______________________*/
				debug("b4 writing, start address is %x\n", func->mem[count]->start);
				pci_bus_read_config_dword(ibmphp_pci_bus, devfn, address[count], &bar[count]);
				debug("after writing, the address is %x\n", bar[count]);
				/* __________________________________________________________________________________*/

				if (bar[count] & PCI_BASE_ADDRESS_MEM_TYPE_64) {
					/* takes up another dword */
					debug("inside mem 64 case, reg. mem, count %d\n", count);
					count += 1;
					/* on the 2nd dword, write all 0s, since we can't handle them n.e.ways */
					pci_bus_write_config_dword(ibmphp_pci_bus, devfn, address[count], 0x00000000);
				}
			}
		}		/* end of mem */
	}			/* end of for */

	func->bus = 0;		/* To indicate that this is not a PPB */
	pci_bus_read_config_byte(ibmphp_pci_bus, devfn, PCI_INTERRUPT_PIN, &irq);
	if ((irq > 0x00) && (irq < 0x05))
		pci_bus_write_config_byte(ibmphp_pci_bus, devfn, PCI_INTERRUPT_LINE, func->irq[irq - 1]);

	pci_bus_write_config_byte(ibmphp_pci_bus, devfn, PCI_CACHE_LINE_SIZE, CACHE);
	pci_bus_write_config_byte(ibmphp_pci_bus, devfn, PCI_LATENCY_TIMER, LATENCY);

	pci_bus_write_config_dword(ibmphp_pci_bus, devfn, PCI_ROM_ADDRESS, 0x00L);
	pci_bus_write_config_word(ibmphp_pci_bus, devfn, PCI_COMMAND, DEVICEENABLE);

	return 0;
}

/******************************************************************************
 * This routine configures a PCI-2-PCI bridge and the functions behind it
 * Parameters: pci_func
 * Returns:
 ******************************************************************************/
static int configure_bridge(struct pci_func **func_passed, u8 slotno)
{
	int count;
	int i;
	int rc;
	u8 sec_number;
	u8 io_base;
	u16 pfmem_base;
	u32 bar[2];
	u32 len[2];
	u8 flag_io = 0;
	u8 flag_mem = 0;
	u8 flag_pfmem = 0;
	u8 need_io_upper = 0;
	u8 need_pfmem_upper = 0;
	struct res_needed *amount_needed = NULL;
	struct resource_node *io = NULL;
	struct resource_node *bus_io[2] = {NULL, NULL};
	struct resource_node *mem = NULL;
	struct resource_node *bus_mem[2] = {NULL, NULL};
	struct resource_node *mem_tmp = NULL;
	struct resource_node *pfmem = NULL;
	struct resource_node *bus_pfmem[2] = {NULL, NULL};
	struct bus_node *bus;
	u32 address[] = {
		PCI_BASE_ADDRESS_0,
		PCI_BASE_ADDRESS_1,
		0
	};
	struct pci_func *func = *func_passed;
	unsigned int devfn;
	u8 irq;
	int retval;

	debug("%s - enter\n", __func__);

	devfn = PCI_DEVFN(func->function, func->device);
	ibmphp_pci_bus->number = func->busno;

	/* Configuring necessary info for the bridge so that we could see the devices
	 * behind it
	 */

	pci_bus_write_config_byte(ibmphp_pci_bus, devfn, PCI_PRIMARY_BUS, func->busno);

	/* _____________________For debugging purposes only __________________________
	pci_bus_config_byte(ibmphp_pci_bus, devfn, PCI_PRIMARY_BUS, &pri_number);
	debug("primary # written into the bridge is %x\n", pri_number);
	 ___________________________________________________________________________*/

	/* in EBDA, only get allocated 1 additional bus # per slot */
	sec_number = find_sec_number(func->busno, slotno);
	if (sec_number == 0xff) {
		err("cannot allocate secondary bus number for the bridged device\n");
		return -EINVAL;
	}

	debug("after find_sec_number, the number we got is %x\n", sec_number);
	debug("AFTER FIND_SEC_NUMBER, func->busno IS %x\n", func->busno);

	pci_bus_write_config_byte(ibmphp_pci_bus, devfn, PCI_SECONDARY_BUS, sec_number);

	/* __________________For debugging purposes only __________________________________
	pci_bus_read_config_byte(ibmphp_pci_bus, devfn, PCI_SECONDARY_BUS, &sec_number);
	debug("sec_number after write/read is %x\n", sec_number);
	 ________________________________________________________________________________*/

	pci_bus_write_config_byte(ibmphp_pci_bus, devfn, PCI_SUBORDINATE_BUS, sec_number);

	/* __________________For debugging purposes only ____________________________________
	pci_bus_read_config_byte(ibmphp_pci_bus, devfn, PCI_SUBORDINATE_BUS, &sec_number);
	debug("subordinate number after write/read is %x\n", sec_number);
	 __________________________________________________________________________________*/

	pci_bus_write_config_byte(ibmphp_pci_bus, devfn, PCI_CACHE_LINE_SIZE, CACHE);
	pci_bus_write_config_byte(ibmphp_pci_bus, devfn, PCI_LATENCY_TIMER, LATENCY);
	pci_bus_write_config_byte(ibmphp_pci_bus, devfn, PCI_SEC_LATENCY_TIMER, LATENCY);

	debug("func->busno is %x\n", func->busno);
	debug("sec_number after writing is %x\n", sec_number);


	/* !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	   !!!!!!!!!!!!!!!NEED TO ADD!!!  FAST BACK-TO-BACK ENABLE!!!!!!!!!!!!!!!!!!!!
	   !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/


	/* First we need to allocate mem/io for the bridge itself in case it needs it */
	for (count = 0; address[count]; count++) {	/* for 2 BARs */
		pci_bus_write_config_dword(ibmphp_pci_bus, devfn, address[count], 0xFFFFFFFF);
		pci_bus_read_config_dword(ibmphp_pci_bus, devfn, address[count], &bar[count]);

		if (!bar[count]) {
			/* This BAR is not implemented */
			debug("so we come here then, eh?, count = %d\n", count);
			continue;
		}
		//  tmp_bar = bar[count];

		debug("Bar %d wants %x\n", count, bar[count]);

		if (bar[count] & PCI_BASE_ADDRESS_SPACE_IO) {
			/* This is IO */
			len[count] = bar[count] & 0xFFFFFFFC;
			len[count] = ~len[count] + 1;

			debug("len[count] in IO = %x\n", len[count]);

			bus_io[count] = kzalloc(sizeof(struct resource_node), GFP_KERNEL);

			if (!bus_io[count]) {
				err("out of system memory\n");
				retval = -ENOMEM;
				goto error;
			}
			bus_io[count]->type = IO;
			bus_io[count]->busno = func->busno;
			bus_io[count]->devfunc = PCI_DEVFN(func->device,
							func->function);
			bus_io[count]->len = len[count];
			if (ibmphp_check_resource(bus_io[count], 0) == 0) {
				ibmphp_add_resource(bus_io[count]);
				func->io[count] = bus_io[count];
			} else {
				err("cannot allocate requested io for bus %x, device %x, len %x\n",
				     func->busno, func->device, len[count]);
				kfree(bus_io[count]);
				return -EIO;
			}

			pci_bus_write_config_dword(ibmphp_pci_bus, devfn, address[count], func->io[count]->start);

		} else {
			/* This is Memory */
			if (bar[count] & PCI_BASE_ADDRESS_MEM_PREFETCH) {
				/* pfmem */
				len[count] = bar[count] & 0xFFFFFFF0;
				len[count] = ~len[count] + 1;

				debug("len[count] in PFMEM = %x\n", len[count]);

				bus_pfmem[count] = kzalloc(sizeof(struct resource_node), GFP_KERNEL);
				if (!bus_pfmem[count]) {
					err("out of system memory\n");
					retval = -ENOMEM;
					goto error;
				}
				bus_pfmem[count]->type = PFMEM;
				bus_pfmem[count]->busno = func->busno;
				bus_pfmem[count]->devfunc = PCI_DEVFN(func->device,
							func->function);
				bus_pfmem[count]->len = len[count];
				bus_pfmem[count]->fromMem = 0;
				if (ibmphp_check_resource(bus_pfmem[count], 0) == 0) {
					ibmphp_add_resource(bus_pfmem[count]);
					func->pfmem[count] = bus_pfmem[count];
				} else {
					mem_tmp = kzalloc(sizeof(*mem_tmp), GFP_KERNEL);
					if (!mem_tmp) {
						err("out of system memory\n");
						retval = -ENOMEM;
						goto error;
					}
					mem_tmp->type = MEM;
					mem_tmp->busno = bus_pfmem[count]->busno;
					mem_tmp->devfunc = bus_pfmem[count]->devfunc;
					mem_tmp->len = bus_pfmem[count]->len;
					if (ibmphp_check_resource(mem_tmp, 0) == 0) {
						ibmphp_add_resource(mem_tmp);
						bus_pfmem[count]->fromMem = 1;
						bus_pfmem[count]->rangeno = mem_tmp->rangeno;
						ibmphp_add_pfmem_from_mem(bus_pfmem[count]);
						func->pfmem[count] = bus_pfmem[count];
					} else {
						err("cannot allocate requested pfmem for bus %x, device %x, len %x\n",
						     func->busno, func->device, len[count]);
						kfree(mem_tmp);
						kfree(bus_pfmem[count]);
						return -EIO;
					}
				}

				pci_bus_write_config_dword(ibmphp_pci_bus, devfn, address[count], func->pfmem[count]->start);

				if (bar[count] & PCI_BASE_ADDRESS_MEM_TYPE_64) {
					/* takes up another dword */
					count += 1;
					/* on the 2nd dword, write all 0s, since we can't handle them n.e.ways */
					pci_bus_write_config_dword(ibmphp_pci_bus, devfn, address[count], 0x00000000);

				}
			} else {
				/* regular memory */
				len[count] = bar[count] & 0xFFFFFFF0;
				len[count] = ~len[count] + 1;

				debug("len[count] in Memory is %x\n", len[count]);

				bus_mem[count] = kzalloc(sizeof(struct resource_node), GFP_KERNEL);
				if (!bus_mem[count]) {
					err("out of system memory\n");
					retval = -ENOMEM;
					goto error;
				}
				bus_mem[count]->type = MEM;
				bus_mem[count]->busno = func->busno;
				bus_mem[count]->devfunc = PCI_DEVFN(func->device,
							func->function);
				bus_mem[count]->len = len[count];
				if (ibmphp_check_resource(bus_mem[count], 0) == 0) {
					ibmphp_add_resource(bus_mem[count]);
					func->mem[count] = bus_mem[count];
				} else {
					err("cannot allocate requested mem for bus %x, device %x, len %x\n",
					     func->busno, func->device, len[count]);
					kfree(bus_mem[count]);
					return -EIO;
				}

				pci_bus_write_config_dword(ibmphp_pci_bus, devfn, address[count], func->mem[count]->start);

				if (bar[count] & PCI_BASE_ADDRESS_MEM_TYPE_64) {
					/* takes up another dword */
					count += 1;
					/* on the 2nd dword, write all 0s, since we can't handle them n.e.ways */
					pci_bus_write_config_dword(ibmphp_pci_bus, devfn, address[count], 0x00000000);

				}
			}
		}		/* end of mem */
	}			/* end of for  */

	/* Now need to see how much space the devices behind the bridge needed */
	amount_needed = scan_behind_bridge(func, sec_number);
	if (amount_needed == NULL)
		return -ENOMEM;

	ibmphp_pci_bus->number = func->busno;
	debug("after coming back from scan_behind_bridge\n");
	debug("amount_needed->not_correct = %x\n", amount_needed->not_correct);
	debug("amount_needed->io = %x\n", amount_needed->io);
	debug("amount_needed->mem = %x\n", amount_needed->mem);
	debug("amount_needed->pfmem =  %x\n", amount_needed->pfmem);

	if (amount_needed->not_correct) {
		debug("amount_needed is not correct\n");
		for (count = 0; address[count]; count++) {
			/* for 2 BARs */
			if (bus_io[count]) {
				ibmphp_remove_resource(bus_io[count]);
				func->io[count] = NULL;
			} else if (bus_pfmem[count]) {
				ibmphp_remove_resource(bus_pfmem[count]);
				func->pfmem[count] = NULL;
			} else if (bus_mem[count]) {
				ibmphp_remove_resource(bus_mem[count]);
				func->mem[count] = NULL;
			}
		}
		kfree(amount_needed);
		return -ENODEV;
	}

	if (!amount_needed->io) {
		debug("it doesn't want IO?\n");
		flag_io = 1;
	} else {
		debug("it wants %x IO behind the bridge\n", amount_needed->io);
		io = kzalloc(sizeof(*io), GFP_KERNEL);

		if (!io) {
			err("out of system memory\n");
			retval = -ENOMEM;
			goto error;
		}
		io->type = IO;
		io->busno = func->busno;
		io->devfunc = PCI_DEVFN(func->device, func->function);
		io->len = amount_needed->io;
		if (ibmphp_check_resource(io, 1) == 0) {
			debug("were we able to add io\n");
			ibmphp_add_resource(io);
			flag_io = 1;
		}
	}

	if (!amount_needed->mem) {
		debug("it doesn't want n.e.memory?\n");
		flag_mem = 1;
	} else {
		debug("it wants %x memory behind the bridge\n", amount_needed->mem);
		mem = kzalloc(sizeof(*mem), GFP_KERNEL);
		if (!mem) {
			err("out of system memory\n");
			retval = -ENOMEM;
			goto error;
		}
		mem->type = MEM;
		mem->busno = func->busno;
		mem->devfunc = PCI_DEVFN(func->device, func->function);
		mem->len = amount_needed->mem;
		if (ibmphp_check_resource(mem, 1) == 0) {
			ibmphp_add_resource(mem);
			flag_mem = 1;
			debug("were we able to add mem\n");
		}
	}

	if (!amount_needed->pfmem) {
		debug("it doesn't want n.e.pfmem mem?\n");
		flag_pfmem = 1;
	} else {
		debug("it wants %x pfmemory behind the bridge\n", amount_needed->pfmem);
		pfmem = kzalloc(sizeof(*pfmem), GFP_KERNEL);
		if (!pfmem) {
			err("out of system memory\n");
			retval = -ENOMEM;
			goto error;
		}
		pfmem->type = PFMEM;
		pfmem->busno = func->busno;
		pfmem->devfunc = PCI_DEVFN(func->device, func->function);
		pfmem->len = amount_needed->pfmem;
		pfmem->fromMem = 0;
		if (ibmphp_check_resource(pfmem, 1) == 0) {
			ibmphp_add_resource(pfmem);
			flag_pfmem = 1;
		} else {
			mem_tmp = kzalloc(sizeof(*mem_tmp), GFP_KERNEL);
			if (!mem_tmp) {
				err("out of system memory\n");
				retval = -ENOMEM;
				goto error;
			}
			mem_tmp->type = MEM;
			mem_tmp->busno = pfmem->busno;
			mem_tmp->devfunc = pfmem->devfunc;
			mem_tmp->len = pfmem->len;
			if (ibmphp_check_resource(mem_tmp, 1) == 0) {
				ibmphp_add_resource(mem_tmp);
				pfmem->fromMem = 1;
				pfmem->rangeno = mem_tmp->rangeno;
				ibmphp_add_pfmem_from_mem(pfmem);
				flag_pfmem = 1;
			}
		}
	}

	debug("b4 if (flag_io && flag_mem && flag_pfmem)\n");
	debug("flag_io = %x, flag_mem = %x, flag_pfmem = %x\n", flag_io, flag_mem, flag_pfmem);

	if (flag_io && flag_mem && flag_pfmem) {
		/* If on bootup, there was a bridged card in this slot,
		 * then card was removed and ibmphp got unloaded and loaded
		 * back again, there's no way for us to remove the bus
		 * struct, so no need to kmalloc, can use existing node
		 */
		bus = ibmphp_find_res_bus(sec_number);
		if (!bus) {
			bus = kzalloc(sizeof(*bus), GFP_KERNEL);
			if (!bus) {
				err("out of system memory\n");
				retval = -ENOMEM;
				goto error;
			}
			bus->busno = sec_number;
			debug("b4 adding new bus\n");
			rc = add_new_bus(bus, io, mem, pfmem, func->busno);
		} else if (!(bus->rangeIO) && !(bus->rangeMem) && !(bus->rangePFMem))
			rc = add_new_bus(bus, io, mem, pfmem, 0xFF);
		else {
			err("expected bus structure not empty?\n");
			retval = -EIO;
			goto error;
		}
		if (rc) {
			if (rc == -ENOMEM) {
				ibmphp_remove_bus(bus, func->busno);
				kfree(amount_needed);
				return rc;
			}
			retval = rc;
			goto error;
		}
		pci_bus_read_config_byte(ibmphp_pci_bus, devfn, PCI_IO_BASE, &io_base);
		pci_bus_read_config_word(ibmphp_pci_bus, devfn, PCI_PREF_MEMORY_BASE, &pfmem_base);

		if ((io_base & PCI_IO_RANGE_TYPE_MASK) == PCI_IO_RANGE_TYPE_32) {
			debug("io 32\n");
			need_io_upper = 1;
		}
		if ((pfmem_base & PCI_PREF_RANGE_TYPE_MASK) == PCI_PREF_RANGE_TYPE_64) {
			debug("pfmem 64\n");
			need_pfmem_upper = 1;
		}

		if (bus->noIORanges) {
			pci_bus_write_config_byte(ibmphp_pci_bus, devfn, PCI_IO_BASE, 0x00 | bus->rangeIO->start >> 8);
			pci_bus_write_config_byte(ibmphp_pci_bus, devfn, PCI_IO_LIMIT, 0x00 | bus->rangeIO->end >> 8);

			/* _______________This is for debugging purposes only ____________________
			pci_bus_read_config_byte(ibmphp_pci_bus, devfn, PCI_IO_BASE, &temp);
			debug("io_base = %x\n", (temp & PCI_IO_RANGE_TYPE_MASK) << 8);
			pci_bus_read_config_byte(ibmphp_pci_bus, devfn, PCI_IO_LIMIT, &temp);
			debug("io_limit = %x\n", (temp & PCI_IO_RANGE_TYPE_MASK) << 8);
			 ________________________________________________________________________*/

			if (need_io_upper) {	/* since can't support n.e.ways */
				pci_bus_write_config_word(ibmphp_pci_bus, devfn, PCI_IO_BASE_UPPER16, 0x0000);
				pci_bus_write_config_word(ibmphp_pci_bus, devfn, PCI_IO_LIMIT_UPPER16, 0x0000);
			}
		} else {
			pci_bus_write_config_byte(ibmphp_pci_bus, devfn, PCI_IO_BASE, 0x00);
			pci_bus_write_config_byte(ibmphp_pci_bus, devfn, PCI_IO_LIMIT, 0x00);
		}

		if (bus->noMemRanges) {
			pci_bus_write_config_word(ibmphp_pci_bus, devfn, PCI_MEMORY_BASE, 0x0000 | bus->rangeMem->start >> 16);
			pci_bus_write_config_word(ibmphp_pci_bus, devfn, PCI_MEMORY_LIMIT, 0x0000 | bus->rangeMem->end >> 16);

			/* ____________________This is for debugging purposes only ________________________
			pci_bus_read_config_word(ibmphp_pci_bus, devfn, PCI_MEMORY_BASE, &temp);
			debug("mem_base = %x\n", (temp & PCI_MEMORY_RANGE_TYPE_MASK) << 16);
			pci_bus_read_config_word(ibmphp_pci_bus, devfn, PCI_MEMORY_LIMIT, &temp);
			debug("mem_limit = %x\n", (temp & PCI_MEMORY_RANGE_TYPE_MASK) << 16);
			 __________________________________________________________________________________*/

		} else {
			pci_bus_write_config_word(ibmphp_pci_bus, devfn, PCI_MEMORY_BASE, 0xffff);
			pci_bus_write_config_word(ibmphp_pci_bus, devfn, PCI_MEMORY_LIMIT, 0x0000);
		}
		if (bus->noPFMemRanges) {
			pci_bus_write_config_word(ibmphp_pci_bus, devfn, PCI_PREF_MEMORY_BASE, 0x0000 | bus->rangePFMem->start >> 16);
			pci_bus_write_config_word(ibmphp_pci_bus, devfn, PCI_PREF_MEMORY_LIMIT, 0x0000 | bus->rangePFMem->end >> 16);

			/* __________________________This is for debugging purposes only _______________________
			pci_bus_read_config_word(ibmphp_pci_bus, devfn, PCI_PREF_MEMORY_BASE, &temp);
			debug("pfmem_base = %x", (temp & PCI_MEMORY_RANGE_TYPE_MASK) << 16);
			pci_bus_read_config_word(ibmphp_pci_bus, devfn, PCI_PREF_MEMORY_LIMIT, &temp);
			debug("pfmem_limit = %x\n", (temp & PCI_MEMORY_RANGE_TYPE_MASK) << 16);
			 ______________________________________________________________________________________*/

			if (need_pfmem_upper) {	/* since can't support n.e.ways */
				pci_bus_write_config_dword(ibmphp_pci_bus, devfn, PCI_PREF_BASE_UPPER32, 0x00000000);
				pci_bus_write_config_dword(ibmphp_pci_bus, devfn, PCI_PREF_LIMIT_UPPER32, 0x00000000);
			}
		} else {
			pci_bus_write_config_word(ibmphp_pci_bus, devfn, PCI_PREF_MEMORY_BASE, 0xffff);
			pci_bus_write_config_word(ibmphp_pci_bus, devfn, PCI_PREF_MEMORY_LIMIT, 0x0000);
		}

		debug("b4 writing control information\n");

		pci_bus_read_config_byte(ibmphp_pci_bus, devfn, PCI_INTERRUPT_PIN, &irq);
		if ((irq > 0x00) && (irq < 0x05))
			pci_bus_write_config_byte(ibmphp_pci_bus, devfn, PCI_INTERRUPT_LINE, func->irq[irq - 1]);
		/*
		pci_bus_write_config_byte(ibmphp_pci_bus, devfn, PCI_BRIDGE_CONTROL, ctrl);
		pci_bus_write_config_byte(ibmphp_pci_bus, devfn, PCI_BRIDGE_CONTROL, PCI_BRIDGE_CTL_PARITY);
		pci_bus_write_config_byte(ibmphp_pci_bus, devfn, PCI_BRIDGE_CONTROL, PCI_BRIDGE_CTL_SERR);
		 */

		pci_bus_write_config_word(ibmphp_pci_bus, devfn, PCI_COMMAND, DEVICEENABLE);
		pci_bus_write_config_word(ibmphp_pci_bus, devfn, PCI_BRIDGE_CONTROL, 0x07);
		for (i = 0; i < 32; i++) {
			if (amount_needed->devices[i]) {
				debug("device where devices[i] is 1 = %x\n", i);
				func->devices[i] = 1;
			}
		}
		func->bus = 1;	/* For unconfiguring, to indicate it's PPB */
		func_passed = &func;
		debug("func->busno b4 returning is %x\n", func->busno);
		debug("func->busno b4 returning in the other structure is %x\n", (*func_passed)->busno);
		kfree(amount_needed);
		return 0;
	} else {
		err("Configuring bridge was unsuccessful...\n");
		mem_tmp = NULL;
		retval = -EIO;
		goto error;
	}

error:
	kfree(amount_needed);
	if (pfmem)
		ibmphp_remove_resource(pfmem);
	if (io)
		ibmphp_remove_resource(io);
	if (mem)
		ibmphp_remove_resource(mem);
	for (i = 0; i < 2; i++) {	/* for 2 BARs */
		if (bus_io[i]) {
			ibmphp_remove_resource(bus_io[i]);
			func->io[i] = NULL;
		} else if (bus_pfmem[i]) {
			ibmphp_remove_resource(bus_pfmem[i]);
			func->pfmem[i] = NULL;
		} else if (bus_mem[i]) {
			ibmphp_remove_resource(bus_mem[i]);
			func->mem[i] = NULL;
		}
	}
	return retval;
}

/*****************************************************************************
 * This function adds up the amount of resources needed behind the PPB bridge
 * and passes it to the configure_bridge function
 * Input: bridge function
 * Output: amount of resources needed
 *****************************************************************************/
static struct res_needed *scan_behind_bridge(struct pci_func *func, u8 busno)
{
	int count, len[6];
	u16 vendor_id;
	u8 hdr_type;
	u8 device, function;
	unsigned int devfn;
	int howmany = 0;	/*this is to see if there are any devices behind the bridge */

	u32 bar[6], class;
	u32 address[] = {
		PCI_BASE_ADDRESS_0,
		PCI_BASE_ADDRESS_1,
		PCI_BASE_ADDRESS_2,
		PCI_BASE_ADDRESS_3,
		PCI_BASE_ADDRESS_4,
		PCI_BASE_ADDRESS_5,
		0
	};
	struct res_needed *amount;

	amount = kzalloc(sizeof(*amount), GFP_KERNEL);
	if (amount == NULL)
		return NULL;

	ibmphp_pci_bus->number = busno;

	debug("the bus_no behind the bridge is %x\n", busno);
	debug("scanning devices behind the bridge...\n");
	for (device = 0; device < 32; device++) {
		amount->devices[device] = 0;
		for (function = 0; function < 8; function++) {
			devfn = PCI_DEVFN(device, function);

			pci_bus_read_config_word(ibmphp_pci_bus, devfn, PCI_VENDOR_ID, &vendor_id);

			if (vendor_id != PCI_VENDOR_ID_NOTVALID) {
				/* found correct device!!! */
				howmany++;

				pci_bus_read_config_byte(ibmphp_pci_bus, devfn, PCI_HEADER_TYPE, &hdr_type);
				pci_bus_read_config_dword(ibmphp_pci_bus, devfn, PCI_CLASS_REVISION, &class);

				debug("hdr_type behind the bridge is %x\n", hdr_type);
				if ((hdr_type & 0x7f) == PCI_HEADER_TYPE_BRIDGE) {
					err("embedded bridges not supported for hot-plugging.\n");
					amount->not_correct = 1;
					return amount;
				}

				class >>= 8;	/* to take revision out, class = class.subclass.prog i/f */
				if (class == PCI_CLASS_NOT_DEFINED_VGA) {
					err("The device %x is VGA compatible and as is not supported for hot plugging.  Please choose another device.\n", device);
					amount->not_correct = 1;
					return amount;
				} else if (class == PCI_CLASS_DISPLAY_VGA) {
					err("The device %x is not supported for hot plugging.  Please choose another device.\n", device);
					amount->not_correct = 1;
					return amount;
				}

				amount->devices[device] = 1;

				for (count = 0; address[count]; count++) {
					/* for 6 BARs */
					/*
					pci_bus_read_config_byte(ibmphp_pci_bus, devfn, address[count], &tmp);
					if (tmp & 0x01) // IO
						pci_bus_write_config_dword(ibmphp_pci_bus, devfn, address[count], 0xFFFFFFFD);
					else // MEMORY
						pci_bus_write_config_dword(ibmphp_pci_bus, devfn, address[count], 0xFFFFFFFF);
					*/
					pci_bus_write_config_dword(ibmphp_pci_bus, devfn, address[count], 0xFFFFFFFF);
					pci_bus_read_config_dword(ibmphp_pci_bus, devfn, address[count], &bar[count]);

					debug("what is bar[count]? %x, count = %d\n", bar[count], count);

					if (!bar[count])	/* This BAR is not implemented */
						continue;

					//tmp_bar = bar[count];

					debug("count %d device %x function %x wants %x resources\n", count, device, function, bar[count]);

					if (bar[count] & PCI_BASE_ADDRESS_SPACE_IO) {
						/* This is IO */
						len[count] = bar[count] & 0xFFFFFFFC;
						len[count] = ~len[count] + 1;
						amount->io += len[count];
					} else {
						/* This is Memory */
						if (bar[count] & PCI_BASE_ADDRESS_MEM_PREFETCH) {
							/* pfmem */
							len[count] = bar[count] & 0xFFFFFFF0;
							len[count] = ~len[count] + 1;
							amount->pfmem += len[count];
							if (bar[count] & PCI_BASE_ADDRESS_MEM_TYPE_64)
								/* takes up another dword */
								count += 1;

						} else {
							/* regular memory */
							len[count] = bar[count] & 0xFFFFFFF0;
							len[count] = ~len[count] + 1;
							amount->mem += len[count];
							if (bar[count] & PCI_BASE_ADDRESS_MEM_TYPE_64) {
								/* takes up another dword */
								count += 1;
							}
						}
					}
				}	/* end for */
			}	/* end if (valid) */
		}	/* end for */
	}	/* end for */

	if (!howmany)
		amount->not_correct = 1;
	else
		amount->not_correct = 0;
	if ((amount->io) && (amount->io < IOBRIDGE))
		amount->io = IOBRIDGE;
	if ((amount->mem) && (amount->mem < MEMBRIDGE))
		amount->mem = MEMBRIDGE;
	if ((amount->pfmem) && (amount->pfmem < MEMBRIDGE))
		amount->pfmem = MEMBRIDGE;
	return amount;
}

/* The following 3 unconfigure_boot_ routines deal with the case when we had the card
 * upon bootup in the system, since we don't allocate func to such case, we need to read
 * the start addresses from pci config space and then find the corresponding entries in
 * our resource lists.  The functions return either 0, -ENODEV, or -1 (general failure)
 * Change: we also call these functions even if we configured the card ourselves (i.e., not
 * the bootup case), since it should work same way
 */
static int unconfigure_boot_device(u8 busno, u8 device, u8 function)
{
	u32 start_address;
	u32 address[] = {
		PCI_BASE_ADDRESS_0,
		PCI_BASE_ADDRESS_1,
		PCI_BASE_ADDRESS_2,
		PCI_BASE_ADDRESS_3,
		PCI_BASE_ADDRESS_4,
		PCI_BASE_ADDRESS_5,
		0
	};
	int count;
	struct resource_node *io;
	struct resource_node *mem;
	struct resource_node *pfmem;
	struct bus_node *bus;
	u32 end_address;
	u32 temp_end;
	u32 size;
	u32 tmp_address;
	unsigned int devfn;

	debug("%s - enter\n", __func__);

	bus = ibmphp_find_res_bus(busno);
	if (!bus) {
		debug("cannot find corresponding bus.\n");
		return -EINVAL;
	}

	devfn = PCI_DEVFN(device, function);
	ibmphp_pci_bus->number = busno;
	for (count = 0; address[count]; count++) {	/* for 6 BARs */
		pci_bus_read_config_dword(ibmphp_pci_bus, devfn, address[count], &start_address);

		/* We can do this here, b/c by that time the device driver of the card has been stopped */

		pci_bus_write_config_dword(ibmphp_pci_bus, devfn, address[count], 0xFFFFFFFF);
		pci_bus_read_config_dword(ibmphp_pci_bus, devfn, address[count], &size);
		pci_bus_write_config_dword(ibmphp_pci_bus, devfn, address[count], start_address);

		debug("start_address is %x\n", start_address);
		debug("busno, device, function %x %x %x\n", busno, device, function);
		if (!size) {
			/* This BAR is not implemented */
			debug("is this bar no implemented?, count = %d\n", count);
			continue;
		}
		tmp_address = start_address;
		if (start_address & PCI_BASE_ADDRESS_SPACE_IO) {
			/* This is IO */
			start_address &= PCI_BASE_ADDRESS_IO_MASK;
			size = size & 0xFFFFFFFC;
			size = ~size + 1;
			end_address = start_address + size - 1;
			if (ibmphp_find_resource(bus, start_address, &io, IO) < 0) {
				err("cannot find corresponding IO resource to remove\n");
				return -EIO;
			}
			debug("io->start = %x\n", io->start);
			temp_end = io->end;
			start_address = io->end + 1;
			ibmphp_remove_resource(io);
			/* This is needed b/c of the old I/O restrictions in the BIOS */
			while (temp_end < end_address) {
				if (ibmphp_find_resource(bus, start_address, &io, IO) < 0) {
					err("cannot find corresponding IO resource to remove\n");
					return -EIO;
				}
				debug("io->start = %x\n", io->start);
				temp_end = io->end;
				start_address = io->end + 1;
				ibmphp_remove_resource(io);
			}

			/* ????????? DO WE NEED TO WRITE ANYTHING INTO THE PCI CONFIG SPACE BACK ?????????? */
		} else {
			/* This is Memory */
			if (start_address & PCI_BASE_ADDRESS_MEM_PREFETCH) {
				/* pfmem */
				debug("start address of pfmem is %x\n", start_address);
				start_address &= PCI_BASE_ADDRESS_MEM_MASK;

				if (ibmphp_find_resource(bus, start_address, &pfmem, PFMEM) < 0) {
					err("cannot find corresponding PFMEM resource to remove\n");
					return -EIO;
				}
				if (pfmem) {
					debug("pfmem->start = %x\n", pfmem->start);

					ibmphp_remove_resource(pfmem);
				}
			} else {
				/* regular memory */
				debug("start address of mem is %x\n", start_address);
				start_address &= PCI_BASE_ADDRESS_MEM_MASK;

				if (ibmphp_find_resource(bus, start_address, &mem, MEM) < 0) {
					err("cannot find corresponding MEM resource to remove\n");
					return -EIO;
				}
				if (mem) {
					debug("mem->start = %x\n", mem->start);

					ibmphp_remove_resource(mem);
				}
			}
			if (tmp_address & PCI_BASE_ADDRESS_MEM_TYPE_64) {
				/* takes up another dword */
				count += 1;
			}
		}	/* end of mem */
	}	/* end of for */

	return 0;
}

static int unconfigure_boot_bridge(u8 busno, u8 device, u8 function)
{
	int count;
	int bus_no, pri_no, sub_no, sec_no = 0;
	u32 start_address, tmp_address;
	u8 sec_number, sub_number, pri_number;
	struct resource_node *io = NULL;
	struct resource_node *mem = NULL;
	struct resource_node *pfmem = NULL;
	struct bus_node *bus;
	u32 address[] = {
		PCI_BASE_ADDRESS_0,
		PCI_BASE_ADDRESS_1,
		0
	};
	unsigned int devfn;

	devfn = PCI_DEVFN(device, function);
	ibmphp_pci_bus->number = busno;
	bus_no = (int) busno;
	debug("busno is %x\n", busno);
	pci_bus_read_config_byte(ibmphp_pci_bus, devfn, PCI_PRIMARY_BUS, &pri_number);
	debug("%s - busno = %x, primary_number = %x\n", __func__, busno, pri_number);

	pci_bus_read_config_byte(ibmphp_pci_bus, devfn, PCI_SECONDARY_BUS, &sec_number);
	debug("sec_number is %x\n", sec_number);
	sec_no = (int) sec_number;
	pri_no = (int) pri_number;
	if (pri_no != bus_no) {
		err("primary numbers in our structures and pci config space don't match.\n");
		return -EINVAL;
	}

	pci_bus_read_config_byte(ibmphp_pci_bus, devfn, PCI_SUBORDINATE_BUS, &sub_number);
	sub_no = (int) sub_number;
	debug("sub_no is %d, sec_no is %d\n", sub_no, sec_no);
	if (sec_no != sub_number) {
		err("there're more buses behind this bridge.  Hot removal is not supported.  Please choose another card\n");
		return -ENODEV;
	}

	bus = ibmphp_find_res_bus(sec_number);
	if (!bus) {
		err("cannot find Bus structure for the bridged device\n");
		return -EINVAL;
	}
	debug("bus->busno is %x\n", bus->busno);
	debug("sec_number is %x\n", sec_number);

	ibmphp_remove_bus(bus, busno);

	for (count = 0; address[count]; count++) {
		/* for 2 BARs */
		pci_bus_read_config_dword(ibmphp_pci_bus, devfn, address[count], &start_address);

		if (!start_address) {
			/* This BAR is not implemented */
			continue;
		}

		tmp_address = start_address;

		if (start_address & PCI_BASE_ADDRESS_SPACE_IO) {
			/* This is IO */
			start_address &= PCI_BASE_ADDRESS_IO_MASK;
			if (ibmphp_find_resource(bus, start_address, &io, IO) < 0) {
				err("cannot find corresponding IO resource to remove\n");
				return -EIO;
			}
			if (io)
				debug("io->start = %x\n", io->start);

			ibmphp_remove_resource(io);

			/* ????????? DO WE NEED TO WRITE ANYTHING INTO THE PCI CONFIG SPACE BACK ?????????? */
		} else {
			/* This is Memory */
			if (start_address & PCI_BASE_ADDRESS_MEM_PREFETCH) {
				/* pfmem */
				start_address &= PCI_BASE_ADDRESS_MEM_MASK;
				if (ibmphp_find_resource(bus, start_address, &pfmem, PFMEM) < 0) {
					err("cannot find corresponding PFMEM resource to remove\n");
					return -EINVAL;
				}
				if (pfmem) {
					debug("pfmem->start = %x\n", pfmem->start);

					ibmphp_remove_resource(pfmem);
				}
			} else {
				/* regular memory */
				start_address &= PCI_BASE_ADDRESS_MEM_MASK;
				if (ibmphp_find_resource(bus, start_address, &mem, MEM) < 0) {
					err("cannot find corresponding MEM resource to remove\n");
					return -EINVAL;
				}
				if (mem) {
					debug("mem->start = %x\n", mem->start);

					ibmphp_remove_resource(mem);
				}
			}
			if (tmp_address & PCI_BASE_ADDRESS_MEM_TYPE_64) {
				/* takes up another dword */
				count += 1;
			}
		}	/* end of mem */
	}	/* end of for */
	debug("%s - exiting, returning success\n", __func__);
	return 0;
}

static int unconfigure_boot_card(struct slot *slot_cur)
{
	u16 vendor_id;
	u32 class;
	u8 hdr_type;
	u8 device;
	u8 busno;
	u8 function;
	int rc;
	unsigned int devfn;
	u8 valid_device = 0x00; /* To see if we are ever able to find valid device and read it */

	debug("%s - enter\n", __func__);

	device = slot_cur->device;
	busno = slot_cur->bus;

	debug("b4 for loop, device is %x\n", device);
	/* For every function on the card */
	for (function = 0x0; function < 0x08; function++) {
		devfn = PCI_DEVFN(device, function);
		ibmphp_pci_bus->number = busno;

		pci_bus_read_config_word(ibmphp_pci_bus, devfn, PCI_VENDOR_ID, &vendor_id);

		if (vendor_id != PCI_VENDOR_ID_NOTVALID) {
			/* found correct device!!! */
			++valid_device;

			debug("%s - found correct device\n", __func__);

			/* header: x x x x x x x x
			 *         | |___________|=> 1=PPB bridge, 0=normal device, 2=CardBus Bridge
			 *         |_=> 0 = single function device, 1 = multi-function device
			 */

			pci_bus_read_config_byte(ibmphp_pci_bus, devfn, PCI_HEADER_TYPE, &hdr_type);
			pci_bus_read_config_dword(ibmphp_pci_bus, devfn, PCI_CLASS_REVISION, &class);

			debug("hdr_type %x, class %x\n", hdr_type, class);
			class >>= 8;	/* to take revision out, class = class.subclass.prog i/f */
			if (class == PCI_CLASS_NOT_DEFINED_VGA) {
				err("The device %x function %x is VGA compatible and is not supported for hot removing.  Please choose another device.\n", device, function);
				return -ENODEV;
			} else if (class == PCI_CLASS_DISPLAY_VGA) {
				err("The device %x function %x is not supported for hot removing.  Please choose another device.\n", device, function);
				return -ENODEV;
			}

			switch (hdr_type) {
				case PCI_HEADER_TYPE_NORMAL:
					rc = unconfigure_boot_device(busno, device, function);
					if (rc) {
						err("was not able to unconfigure device %x func %x on bus %x. bailing out...\n",
						     device, function, busno);
						return rc;
					}
					function = 0x8;
					break;
				case PCI_HEADER_TYPE_MULTIDEVICE:
					rc = unconfigure_boot_device(busno, device, function);
					if (rc) {
						err("was not able to unconfigure device %x func %x on bus %x. bailing out...\n",
						     device, function, busno);
						return rc;
					}
					break;
				case PCI_HEADER_TYPE_BRIDGE:
					class >>= 8;
					if (class != PCI_CLASS_BRIDGE_PCI) {
						err("This device %x function %x is not PCI-to-PCI bridge, and is not supported for hot-removing.  Please try another card.\n", device, function);
						return -ENODEV;
					}
					rc = unconfigure_boot_bridge(busno, device, function);
					if (rc != 0) {
						err("was not able to hot-remove PPB properly.\n");
						return rc;
					}

					function = 0x8;
					break;
				case PCI_HEADER_TYPE_MULTIBRIDGE:
					class >>= 8;
					if (class != PCI_CLASS_BRIDGE_PCI) {
						err("This device %x function %x is not PCI-to-PCI bridge,  and is not supported for hot-removing.  Please try another card.\n", device, function);
						return -ENODEV;
					}
					rc = unconfigure_boot_bridge(busno, device, function);
					if (rc != 0) {
						err("was not able to hot-remove PPB properly.\n");
						return rc;
					}
					break;
				default:
					err("MAJOR PROBLEM!!!! Cannot read device's header\n");
					return -1;
					break;
			}	/* end of switch */
		}	/* end of valid device */
	}	/* end of for */

	if (!valid_device) {
		err("Could not find device to unconfigure.  Or could not read the card.\n");
		return -1;
	}
	return 0;
}

/*
 * free the resources of the card (multi, single, or bridged)
 * Parameters: slot, flag to say if this is for removing entire module or just
 * unconfiguring the device
 * TO DO:  will probably need to add some code in case there was some resource,
 * to remove it... this is from when we have errors in the configure_card...
 *			!!!!!!!!!!!!!!!!!!!!!!!!!FOR BUSES!!!!!!!!!!!!
 * Returns: 0, -1, -ENODEV
 */
int ibmphp_unconfigure_card(struct slot **slot_cur, int the_end)
{
	int i;
	int count;
	int rc;
	struct slot *sl = *slot_cur;
	struct pci_func *cur_func = NULL;
	struct pci_func *temp_func;

	debug("%s - enter\n", __func__);

	if (!the_end) {
		/* Need to unconfigure the card */
		rc = unconfigure_boot_card(sl);
		if ((rc == -ENODEV) || (rc == -EIO) || (rc == -EINVAL)) {
			/* In all other cases, will still need to get rid of func structure if it exists */
			return rc;
		}
	}

	if (sl->func) {
		cur_func = sl->func;
		while (cur_func) {
			/* TO DO: WILL MOST LIKELY NEED TO GET RID OF THE BUS STRUCTURE FROM RESOURCES AS WELL */
			if (cur_func->bus) {
				/* in other words, it's a PPB */
				count = 2;
			} else {
				count = 6;
			}

			for (i = 0; i < count; i++) {
				if (cur_func->io[i]) {
					debug("io[%d] exists\n", i);
					if (the_end > 0)
						ibmphp_remove_resource(cur_func->io[i]);
					cur_func->io[i] = NULL;
				}
				if (cur_func->mem[i]) {
					debug("mem[%d] exists\n", i);
					if (the_end > 0)
						ibmphp_remove_resource(cur_func->mem[i]);
					cur_func->mem[i] = NULL;
				}
				if (cur_func->pfmem[i]) {
					debug("pfmem[%d] exists\n", i);
					if (the_end > 0)
						ibmphp_remove_resource(cur_func->pfmem[i]);
					cur_func->pfmem[i] = NULL;
				}
			}

			temp_func = cur_func->next;
			kfree(cur_func);
			cur_func = temp_func;
		}
	}

	sl->func = NULL;
	*slot_cur = sl;
	debug("%s - exit\n", __func__);
	return 0;
}

/*
 * add a new bus resulting from hot-plugging a PPB bridge with devices
 *
 * Input: bus and the amount of resources needed (we know we can assign those,
 *        since they've been checked already
 * Output: bus added to the correct spot
 *         0, -1, error
 */
static int add_new_bus(struct bus_node *bus, struct resource_node *io, struct resource_node *mem, struct resource_node *pfmem, u8 parent_busno)
{
	struct range_node *io_range = NULL;
	struct range_node *mem_range = NULL;
	struct range_node *pfmem_range = NULL;
	struct bus_node *cur_bus = NULL;

	/* Trying to find the parent bus number */
	if (parent_busno != 0xFF) {
		cur_bus	= ibmphp_find_res_bus(parent_busno);
		if (!cur_bus) {
			err("strange, cannot find bus which is supposed to be at the system... something is terribly wrong...\n");
			return -ENODEV;
		}

		list_add(&bus->bus_list, &cur_bus->bus_list);
	}
	if (io) {
		io_range = kzalloc(sizeof(*io_range), GFP_KERNEL);
		if (!io_range) {
			err("out of system memory\n");
			return -ENOMEM;
		}
		io_range->start = io->start;
		io_range->end = io->end;
		io_range->rangeno = 1;
		bus->noIORanges = 1;
		bus->rangeIO = io_range;
	}
	if (mem) {
		mem_range = kzalloc(sizeof(*mem_range), GFP_KERNEL);
		if (!mem_range) {
			err("out of system memory\n");
			return -ENOMEM;
		}
		mem_range->start = mem->start;
		mem_range->end = mem->end;
		mem_range->rangeno = 1;
		bus->noMemRanges = 1;
		bus->rangeMem = mem_range;
	}
	if (pfmem) {
		pfmem_range = kzalloc(sizeof(*pfmem_range), GFP_KERNEL);
		if (!pfmem_range) {
			err("out of system memory\n");
			return -ENOMEM;
		}
		pfmem_range->start = pfmem->start;
		pfmem_range->end = pfmem->end;
		pfmem_range->rangeno = 1;
		bus->noPFMemRanges = 1;
		bus->rangePFMem = pfmem_range;
	}
	return 0;
}

/*
 * find the 1st available bus number for PPB to set as its secondary bus
 * Parameters: bus_number of the primary bus
 * Returns: bus_number of the secondary bus or 0xff in case of failure
 */
static u8 find_sec_number(u8 primary_busno, u8 slotno)
{
	int min, max;
	u8 busno;
	struct bus_info *bus;
	struct bus_node *bus_cur;

	bus = ibmphp_find_same_bus_num(primary_busno);
	if (!bus) {
		err("cannot get slot range of the bus from the BIOS\n");
		return 0xff;
	}
	max = bus->slot_max;
	min = bus->slot_min;
	if ((slotno > max) || (slotno < min)) {
		err("got the wrong range\n");
		return 0xff;
	}
	busno = (u8) (slotno - (u8) min);
	busno += primary_busno + 0x01;
	bus_cur = ibmphp_find_res_bus(busno);
	/* either there is no such bus number, or there are no ranges, which
	 * can only happen if we removed the bridged device in previous load
	 * of the driver, and now only have the skeleton bus struct
	 */
	if ((!bus_cur) || (!(bus_cur->rangeIO) && !(bus_cur->rangeMem) && !(bus_cur->rangePFMem)))
		return busno;
	return 0xff;
}
