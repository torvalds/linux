/*
 * Compaq Hot Plug Controller Driver
 *
 * Copyright (C) 1995,2001 Compaq Computer Corporation
 * Copyright (C) 2001 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2001 IBM Corp.
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
 * Send feedback to <greg@kroah.com>
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/pci.h>
#include <linux/pci_hotplug.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include "cpqphp.h"
#include "cpqphp_nvram.h"


#define ROM_INT15_PHY_ADDR		0x0FF859
#define READ_EV				0xD8A4
#define WRITE_EV			0xD8A5

struct register_foo {
	union {
		unsigned long lword;		/* eax */
		unsigned short word;		/* ax */

		struct {
			unsigned char low;	/* al */
			unsigned char high;	/* ah */
		} byte;
	} data;

	unsigned char opcode;	/* see below */
	unsigned long length;	/* if the reg. is a pointer, how much data */
} __attribute__ ((packed));

struct all_reg {
	struct register_foo eax_reg;
	struct register_foo ebx_reg;
	struct register_foo ecx_reg;
	struct register_foo edx_reg;
	struct register_foo edi_reg;
	struct register_foo esi_reg;
	struct register_foo eflags_reg;
} __attribute__ ((packed));


struct ev_hrt_header {
	u8 Version;
	u8 num_of_ctrl;
	u8 next;
};

struct ev_hrt_ctrl {
	u8 bus;
	u8 device;
	u8 function;
	u8 mem_avail;
	u8 p_mem_avail;
	u8 io_avail;
	u8 bus_avail;
	u8 next;
};


static u8 evbuffer_init;
static u8 evbuffer_length;
static u8 evbuffer[1024];

static void __iomem *compaq_int15_entry_point;

static spinlock_t int15_lock;		/* lock for ordering int15_bios_call() */


/* This is a series of function that deals with
   setting & getting the hotplug resource table in some environment variable.
*/

/*
 * We really shouldn't be doing this unless there is a _very_ good reason to!!!
 * greg k-h
 */


static u32 add_byte( u32 **p_buffer, u8 value, u32 *used, u32 *avail)
{
	u8 **tByte;

	if ((*used + 1) > *avail)
		return(1);
	
	*((u8*)*p_buffer) = value;
	tByte = (u8**)p_buffer;
	(*tByte)++;
	*used+=1;
	return(0);
}


static u32 add_dword( u32 **p_buffer, u32 value, u32 *used, u32 *avail)
{
	if ((*used + 4) > *avail)
		return(1);

	**p_buffer = value;
	(*p_buffer)++;
	*used+=4;
	return(0);
}


/*
 * check_for_compaq_ROM
 *
 * this routine verifies that the ROM OEM string is 'COMPAQ'
 *
 * returns 0 for non-Compaq ROM, 1 for Compaq ROM
 */
static int check_for_compaq_ROM (void __iomem *rom_start)
{
	u8 temp1, temp2, temp3, temp4, temp5, temp6;
	int result = 0;

	temp1 = readb(rom_start + 0xffea + 0);
	temp2 = readb(rom_start + 0xffea + 1);
	temp3 = readb(rom_start + 0xffea + 2);
	temp4 = readb(rom_start + 0xffea + 3);
	temp5 = readb(rom_start + 0xffea + 4);
	temp6 = readb(rom_start + 0xffea + 5);
	if ((temp1 == 'C') &&
	    (temp2 == 'O') &&
	    (temp3 == 'M') &&
	    (temp4 == 'P') &&
	    (temp5 == 'A') &&
	    (temp6 == 'Q')) {
		result = 1;
	}
	dbg ("%s - returned %d\n", __FUNCTION__, result);
	return result;
}


static u32 access_EV (u16 operation, u8 *ev_name, u8 *buffer, u32 *buf_size)
{
	unsigned long flags;
	int op = operation;
	int ret_val;
	
	if (!compaq_int15_entry_point)
		return -ENODEV;
	
	spin_lock_irqsave(&int15_lock, flags);
	__asm__ (
		"xorl   %%ebx,%%ebx\n" \
		"xorl    %%edx,%%edx\n" \
		"pushf\n" \
		"push %%cs\n" \
		"cli\n" \
		"call *%6\n"
		: "=c" (*buf_size), "=a" (ret_val)
		: "a" (op), "c" (*buf_size), "S" (ev_name),
		"D" (buffer), "m" (compaq_int15_entry_point)
		: "%ebx", "%edx");
	spin_unlock_irqrestore(&int15_lock, flags);
	
	return((ret_val & 0xFF00) >> 8);
}


/*
 * load_HRT
 *
 * Read the hot plug Resource Table from NVRAM
 */
static int load_HRT (void __iomem *rom_start)
{
	u32 available;
	u32 temp_dword;
	u8 temp_byte = 0xFF;
	u32 rc;

	if (!check_for_compaq_ROM(rom_start)) {
		return -ENODEV;
	}

	available = 1024;

	// Now load the EV
	temp_dword = available;

	rc = access_EV(READ_EV, "CQTHPS", evbuffer, &temp_dword);

	evbuffer_length = temp_dword;

	// We're maintaining the resource lists so write FF to invalidate old info
	temp_dword = 1;

	rc = access_EV(WRITE_EV, "CQTHPS", &temp_byte, &temp_dword);

	return rc;
}


/*
 * store_HRT
 *
 * Save the hot plug Resource Table in NVRAM
 */
static u32 store_HRT (void __iomem *rom_start)
{
	u32 *buffer;
	u32 *pFill;
	u32 usedbytes;
	u32 available;
	u32 temp_dword;
	u32 rc;
	u8 loop;
	u8 numCtrl = 0;
	struct controller *ctrl;
	struct pci_resource *resNode;
	struct ev_hrt_header *p_EV_header;
	struct ev_hrt_ctrl *p_ev_ctrl;

	available = 1024;

	if (!check_for_compaq_ROM(rom_start)) {
		return(1);
	}

	buffer = (u32*) evbuffer;

	if (!buffer)
		return(1);

	pFill = buffer;
	usedbytes = 0;

	p_EV_header = (struct ev_hrt_header *) pFill;

	ctrl = cpqhp_ctrl_list;
	
	// The revision of this structure
	rc = add_byte( &pFill, 1 + ctrl->push_flag, &usedbytes, &available);
	if (rc)
		return(rc);

	// The number of controllers
	rc = add_byte( &pFill, 1, &usedbytes, &available);
	if (rc)
		return(rc);

	while (ctrl) {
		p_ev_ctrl = (struct ev_hrt_ctrl *) pFill;

		numCtrl++;

		// The bus number
		rc = add_byte( &pFill, ctrl->bus, &usedbytes, &available);
		if (rc)
			return(rc);

		// The device Number
		rc = add_byte( &pFill, PCI_SLOT(ctrl->pci_dev->devfn), &usedbytes, &available);
		if (rc)
			return(rc);

		// The function Number
		rc = add_byte( &pFill, PCI_FUNC(ctrl->pci_dev->devfn), &usedbytes, &available);
		if (rc)
			return(rc);

		// Skip the number of available entries
		rc = add_dword( &pFill, 0, &usedbytes, &available);
		if (rc)
			return(rc);

		// Figure out memory Available

		resNode = ctrl->mem_head;

		loop = 0;

		while (resNode) {
			loop ++;

			// base
			rc = add_dword( &pFill, resNode->base, &usedbytes, &available);
			if (rc)
				return(rc);

			// length
			rc = add_dword( &pFill, resNode->length, &usedbytes, &available);
			if (rc)
				return(rc);

			resNode = resNode->next;
		}

		// Fill in the number of entries
		p_ev_ctrl->mem_avail = loop;

		// Figure out prefetchable memory Available

		resNode = ctrl->p_mem_head;

		loop = 0;

		while (resNode) {
			loop ++;

			// base
			rc = add_dword( &pFill, resNode->base, &usedbytes, &available);
			if (rc)
				return(rc);

			// length
			rc = add_dword( &pFill, resNode->length, &usedbytes, &available);
			if (rc)
				return(rc);

			resNode = resNode->next;
		}

		// Fill in the number of entries
		p_ev_ctrl->p_mem_avail = loop;

		// Figure out IO Available

		resNode = ctrl->io_head;

		loop = 0;

		while (resNode) {
			loop ++;

			// base
			rc = add_dword( &pFill, resNode->base, &usedbytes, &available);
			if (rc)
				return(rc);

			// length
			rc = add_dword( &pFill, resNode->length, &usedbytes, &available);
			if (rc)
				return(rc);

			resNode = resNode->next;
		}

		// Fill in the number of entries
		p_ev_ctrl->io_avail = loop;

		// Figure out bus Available

		resNode = ctrl->bus_head;

		loop = 0;

		while (resNode) {
			loop ++;

			// base
			rc = add_dword( &pFill, resNode->base, &usedbytes, &available);
			if (rc)
				return(rc);

			// length
			rc = add_dword( &pFill, resNode->length, &usedbytes, &available);
			if (rc)
				return(rc);

			resNode = resNode->next;
		}

		// Fill in the number of entries
		p_ev_ctrl->bus_avail = loop;

		ctrl = ctrl->next;
	}
	
	p_EV_header->num_of_ctrl = numCtrl;

	// Now store the EV

	temp_dword = usedbytes;

	rc = access_EV(WRITE_EV, "CQTHPS", (u8*) buffer, &temp_dword);

	dbg("usedbytes = 0x%x, length = 0x%x\n", usedbytes, temp_dword);

	evbuffer_length = temp_dword;

	if (rc) {
		err(msg_unable_to_save);
		return(1);
	}

	return(0);
}


void compaq_nvram_init (void __iomem *rom_start)
{
	if (rom_start) {
		compaq_int15_entry_point = (rom_start + ROM_INT15_PHY_ADDR - ROM_PHY_ADDR);
	}
	dbg("int15 entry  = %p\n", compaq_int15_entry_point);

	/* initialize our int15 lock */
	spin_lock_init(&int15_lock);
}


int compaq_nvram_load (void __iomem *rom_start, struct controller *ctrl)
{
	u8 bus, device, function;
	u8 nummem, numpmem, numio, numbus;
	u32 rc;
	u8 *p_byte;
	struct pci_resource *mem_node;
	struct pci_resource *p_mem_node;
	struct pci_resource *io_node;
	struct pci_resource *bus_node;
	struct ev_hrt_ctrl *p_ev_ctrl;
	struct ev_hrt_header *p_EV_header;

	if (!evbuffer_init) {
		// Read the resource list information in from NVRAM
		if (load_HRT(rom_start))
			memset (evbuffer, 0, 1024);

		evbuffer_init = 1;
	}

	// If we saved information in NVRAM, use it now
	p_EV_header = (struct ev_hrt_header *) evbuffer;

	// The following code is for systems where version 1.0 of this
	// driver has been loaded, but doesn't support the hardware.
	// In that case, the driver would incorrectly store something
	// in NVRAM.
	if ((p_EV_header->Version == 2) ||
	    ((p_EV_header->Version == 1) && !ctrl->push_flag)) {
		p_byte = &(p_EV_header->next);

		p_ev_ctrl = (struct ev_hrt_ctrl *) &(p_EV_header->next);

		p_byte += 3;

		if (p_byte > ((u8*)p_EV_header + evbuffer_length))
			return 2;

		bus = p_ev_ctrl->bus;
		device = p_ev_ctrl->device;
		function = p_ev_ctrl->function;

		while ((bus != ctrl->bus) ||
		       (device != PCI_SLOT(ctrl->pci_dev->devfn)) || 
		       (function != PCI_FUNC(ctrl->pci_dev->devfn))) {
			nummem = p_ev_ctrl->mem_avail;
			numpmem = p_ev_ctrl->p_mem_avail;
			numio = p_ev_ctrl->io_avail;
			numbus = p_ev_ctrl->bus_avail;

			p_byte += 4;

			if (p_byte > ((u8*)p_EV_header + evbuffer_length))
				return 2;

			// Skip forward to the next entry
			p_byte += (nummem + numpmem + numio + numbus) * 8;

			if (p_byte > ((u8*)p_EV_header + evbuffer_length))
				return 2;

			p_ev_ctrl = (struct ev_hrt_ctrl *) p_byte;

			p_byte += 3;

			if (p_byte > ((u8*)p_EV_header + evbuffer_length))
				return 2;

			bus = p_ev_ctrl->bus;
			device = p_ev_ctrl->device;
			function = p_ev_ctrl->function;
		}

		nummem = p_ev_ctrl->mem_avail;
		numpmem = p_ev_ctrl->p_mem_avail;
		numio = p_ev_ctrl->io_avail;
		numbus = p_ev_ctrl->bus_avail;

		p_byte += 4;

		if (p_byte > ((u8*)p_EV_header + evbuffer_length))
			return 2;

		while (nummem--) {
			mem_node = kmalloc(sizeof(struct pci_resource), GFP_KERNEL);

			if (!mem_node)
				break;

			mem_node->base = *(u32*)p_byte;
			dbg("mem base = %8.8x\n",mem_node->base);
			p_byte += 4;

			if (p_byte > ((u8*)p_EV_header + evbuffer_length)) {
				kfree(mem_node);
				return 2;
			}

			mem_node->length = *(u32*)p_byte;
			dbg("mem length = %8.8x\n",mem_node->length);
			p_byte += 4;

			if (p_byte > ((u8*)p_EV_header + evbuffer_length)) {
				kfree(mem_node);
				return 2;
			}

			mem_node->next = ctrl->mem_head;
			ctrl->mem_head = mem_node;
		}

		while (numpmem--) {
			p_mem_node = kmalloc(sizeof(struct pci_resource), GFP_KERNEL);

			if (!p_mem_node)
				break;

			p_mem_node->base = *(u32*)p_byte;
			dbg("pre-mem base = %8.8x\n",p_mem_node->base);
			p_byte += 4;

			if (p_byte > ((u8*)p_EV_header + evbuffer_length)) {
				kfree(p_mem_node);
				return 2;
			}

			p_mem_node->length = *(u32*)p_byte;
			dbg("pre-mem length = %8.8x\n",p_mem_node->length);
			p_byte += 4;

			if (p_byte > ((u8*)p_EV_header + evbuffer_length)) {
				kfree(p_mem_node);
				return 2;
			}

			p_mem_node->next = ctrl->p_mem_head;
			ctrl->p_mem_head = p_mem_node;
		}

		while (numio--) {
			io_node = kmalloc(sizeof(struct pci_resource), GFP_KERNEL);

			if (!io_node)
				break;

			io_node->base = *(u32*)p_byte;
			dbg("io base = %8.8x\n",io_node->base);
			p_byte += 4;

			if (p_byte > ((u8*)p_EV_header + evbuffer_length)) {
				kfree(io_node);
				return 2;
			}

			io_node->length = *(u32*)p_byte;
			dbg("io length = %8.8x\n",io_node->length);
			p_byte += 4;

			if (p_byte > ((u8*)p_EV_header + evbuffer_length)) {
				kfree(io_node);
				return 2;
			}

			io_node->next = ctrl->io_head;
			ctrl->io_head = io_node;
		}

		while (numbus--) {
			bus_node = kmalloc(sizeof(struct pci_resource), GFP_KERNEL);

			if (!bus_node)
				break;

			bus_node->base = *(u32*)p_byte;
			p_byte += 4;

			if (p_byte > ((u8*)p_EV_header + evbuffer_length)) {
				kfree(bus_node);
				return 2;
			}

			bus_node->length = *(u32*)p_byte;
			p_byte += 4;

			if (p_byte > ((u8*)p_EV_header + evbuffer_length)) {
				kfree(bus_node);
				return 2;
			}

			bus_node->next = ctrl->bus_head;
			ctrl->bus_head = bus_node;
		}

		// If all of the following fail, we don't have any resources for
		// hot plug add
		rc = 1;
		rc &= cpqhp_resource_sort_and_combine(&(ctrl->mem_head));
		rc &= cpqhp_resource_sort_and_combine(&(ctrl->p_mem_head));
		rc &= cpqhp_resource_sort_and_combine(&(ctrl->io_head));
		rc &= cpqhp_resource_sort_and_combine(&(ctrl->bus_head));

		if (rc)
			return(rc);
	} else {
		if ((evbuffer[0] != 0) && (!ctrl->push_flag)) 
			return 1;
	}

	return 0;
}

	
int compaq_nvram_store (void __iomem *rom_start)
{
	int rc = 1;

	if (rom_start == NULL)
		return -ENODEV;

	if (evbuffer_init) {
		rc = store_HRT(rom_start);
		if (rc) {
			err(msg_unable_to_save);
		}
	}
	return rc;
}

