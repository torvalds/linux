// SPDX-License-Identifier: GPL-2.0+
/*
 * IBM Hot Plug Controller Driver
 *
 * Written By: Tong Yu, IBM Corporation
 *
 * Copyright (C) 2001,2003 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2001-2003 IBM Corp.
 *
 * All rights reserved.
 *
 * Send feedback to <gregkh@us.ibm.com>
 *
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/list.h>
#include <linux/init.h>
#include "ibmphp.h"

/*
 * POST builds data blocks(in this data block definition, a char-1
 * byte, short(or word)-2 byte, long(dword)-4 byte) in the Extended
 * BIOS Data Area which describe the configuration of the hot-plug
 * controllers and resources used by the PCI Hot-Plug devices.
 *
 * This file walks EBDA, maps data block from physical addr,
 * reconstruct linked lists about all system resource(MEM, PFM, IO)
 * already assigned by POST, as well as linked lists about hot plug
 * controllers (ctlr#, slot#, bus&slot features...)
 */

/* Global lists */
LIST_HEAD(ibmphp_ebda_pci_rsrc_head);
LIST_HEAD(ibmphp_slot_head);

/* Local variables */
static struct ebda_hpc_list *hpc_list_ptr;
static struct ebda_rsrc_list *rsrc_list_ptr;
static struct rio_table_hdr *rio_table_ptr = NULL;
static LIST_HEAD(ebda_hpc_head);
static LIST_HEAD(bus_info_head);
static LIST_HEAD(rio_vg_head);
static LIST_HEAD(rio_lo_head);
static LIST_HEAD(opt_vg_head);
static LIST_HEAD(opt_lo_head);
static void __iomem *io_mem;

/* Local functions */
static int ebda_rsrc_controller(void);
static int ebda_rsrc_rsrc(void);
static int ebda_rio_table(void);

static struct ebda_hpc_list * __init alloc_ebda_hpc_list(void)
{
	return kzalloc(sizeof(struct ebda_hpc_list), GFP_KERNEL);
}

static struct controller *alloc_ebda_hpc(u32 slot_count, u32 bus_count)
{
	struct controller *controller;
	struct ebda_hpc_slot *slots;
	struct ebda_hpc_bus *buses;

	controller = kzalloc(sizeof(struct controller), GFP_KERNEL);
	if (!controller)
		goto error;

	slots = kcalloc(slot_count, sizeof(struct ebda_hpc_slot), GFP_KERNEL);
	if (!slots)
		goto error_contr;
	controller->slots = slots;

	buses = kcalloc(bus_count, sizeof(struct ebda_hpc_bus), GFP_KERNEL);
	if (!buses)
		goto error_slots;
	controller->buses = buses;

	return controller;
error_slots:
	kfree(controller->slots);
error_contr:
	kfree(controller);
error:
	return NULL;
}

static void free_ebda_hpc(struct controller *controller)
{
	kfree(controller->slots);
	kfree(controller->buses);
	kfree(controller);
}

static struct ebda_rsrc_list * __init alloc_ebda_rsrc_list(void)
{
	return kzalloc(sizeof(struct ebda_rsrc_list), GFP_KERNEL);
}

static struct ebda_pci_rsrc *alloc_ebda_pci_rsrc(void)
{
	return kzalloc(sizeof(struct ebda_pci_rsrc), GFP_KERNEL);
}

static void __init print_bus_info(void)
{
	struct bus_info *ptr;

	list_for_each_entry(ptr, &bus_info_head, bus_info_list) {
		debug("%s - slot_min = %x\n", __func__, ptr->slot_min);
		debug("%s - slot_max = %x\n", __func__, ptr->slot_max);
		debug("%s - slot_count = %x\n", __func__, ptr->slot_count);
		debug("%s - bus# = %x\n", __func__, ptr->busno);
		debug("%s - current_speed = %x\n", __func__, ptr->current_speed);
		debug("%s - controller_id = %x\n", __func__, ptr->controller_id);

		debug("%s - slots_at_33_conv = %x\n", __func__, ptr->slots_at_33_conv);
		debug("%s - slots_at_66_conv = %x\n", __func__, ptr->slots_at_66_conv);
		debug("%s - slots_at_66_pcix = %x\n", __func__, ptr->slots_at_66_pcix);
		debug("%s - slots_at_100_pcix = %x\n", __func__, ptr->slots_at_100_pcix);
		debug("%s - slots_at_133_pcix = %x\n", __func__, ptr->slots_at_133_pcix);

	}
}

static void print_lo_info(void)
{
	struct rio_detail *ptr;
	debug("print_lo_info ----\n");
	list_for_each_entry(ptr, &rio_lo_head, rio_detail_list) {
		debug("%s - rio_node_id = %x\n", __func__, ptr->rio_node_id);
		debug("%s - rio_type = %x\n", __func__, ptr->rio_type);
		debug("%s - owner_id = %x\n", __func__, ptr->owner_id);
		debug("%s - first_slot_num = %x\n", __func__, ptr->first_slot_num);
		debug("%s - wpindex = %x\n", __func__, ptr->wpindex);
		debug("%s - chassis_num = %x\n", __func__, ptr->chassis_num);

	}
}

static void print_vg_info(void)
{
	struct rio_detail *ptr;
	debug("%s ---\n", __func__);
	list_for_each_entry(ptr, &rio_vg_head, rio_detail_list) {
		debug("%s - rio_node_id = %x\n", __func__, ptr->rio_node_id);
		debug("%s - rio_type = %x\n", __func__, ptr->rio_type);
		debug("%s - owner_id = %x\n", __func__, ptr->owner_id);
		debug("%s - first_slot_num = %x\n", __func__, ptr->first_slot_num);
		debug("%s - wpindex = %x\n", __func__, ptr->wpindex);
		debug("%s - chassis_num = %x\n", __func__, ptr->chassis_num);

	}
}

static void __init print_ebda_pci_rsrc(void)
{
	struct ebda_pci_rsrc *ptr;

	list_for_each_entry(ptr, &ibmphp_ebda_pci_rsrc_head, ebda_pci_rsrc_list) {
		debug("%s - rsrc type: %x bus#: %x dev_func: %x start addr: %x end addr: %x\n",
			__func__, ptr->rsrc_type, ptr->bus_num, ptr->dev_fun, ptr->start_addr, ptr->end_addr);
	}
}

static void __init print_ibm_slot(void)
{
	struct slot *ptr;

	list_for_each_entry(ptr, &ibmphp_slot_head, ibm_slot_list) {
		debug("%s - slot_number: %x\n", __func__, ptr->number);
	}
}

static void __init print_opt_vg(void)
{
	struct opt_rio *ptr;
	debug("%s ---\n", __func__);
	list_for_each_entry(ptr, &opt_vg_head, opt_rio_list) {
		debug("%s - rio_type %x\n", __func__, ptr->rio_type);
		debug("%s - chassis_num: %x\n", __func__, ptr->chassis_num);
		debug("%s - first_slot_num: %x\n", __func__, ptr->first_slot_num);
		debug("%s - middle_num: %x\n", __func__, ptr->middle_num);
	}
}

static void __init print_ebda_hpc(void)
{
	struct controller *hpc_ptr;
	u16 index;

	list_for_each_entry(hpc_ptr, &ebda_hpc_head, ebda_hpc_list) {
		for (index = 0; index < hpc_ptr->slot_count; index++) {
			debug("%s - physical slot#: %x\n", __func__, hpc_ptr->slots[index].slot_num);
			debug("%s - pci bus# of the slot: %x\n", __func__, hpc_ptr->slots[index].slot_bus_num);
			debug("%s - index into ctlr addr: %x\n", __func__, hpc_ptr->slots[index].ctl_index);
			debug("%s - cap of the slot: %x\n", __func__, hpc_ptr->slots[index].slot_cap);
		}

		for (index = 0; index < hpc_ptr->bus_count; index++)
			debug("%s - bus# of each bus controlled by this ctlr: %x\n", __func__, hpc_ptr->buses[index].bus_num);

		debug("%s - type of hpc: %x\n", __func__, hpc_ptr->ctlr_type);
		switch (hpc_ptr->ctlr_type) {
		case 1:
			debug("%s - bus: %x\n", __func__, hpc_ptr->u.pci_ctlr.bus);
			debug("%s - dev_fun: %x\n", __func__, hpc_ptr->u.pci_ctlr.dev_fun);
			debug("%s - irq: %x\n", __func__, hpc_ptr->irq);
			break;

		case 0:
			debug("%s - io_start: %x\n", __func__, hpc_ptr->u.isa_ctlr.io_start);
			debug("%s - io_end: %x\n", __func__, hpc_ptr->u.isa_ctlr.io_end);
			debug("%s - irq: %x\n", __func__, hpc_ptr->irq);
			break;

		case 2:
		case 4:
			debug("%s - wpegbbar: %lx\n", __func__, hpc_ptr->u.wpeg_ctlr.wpegbbar);
			debug("%s - i2c_addr: %x\n", __func__, hpc_ptr->u.wpeg_ctlr.i2c_addr);
			debug("%s - irq: %x\n", __func__, hpc_ptr->irq);
			break;
		}
	}
}

int __init ibmphp_access_ebda(void)
{
	u8 format, num_ctlrs, rio_complete, hs_complete, ebda_sz;
	u16 ebda_seg, num_entries, next_offset, offset, blk_id, sub_addr, re, rc_id, re_id, base;
	int rc = 0;


	rio_complete = 0;
	hs_complete = 0;

	io_mem = ioremap((0x40 << 4) + 0x0e, 2);
	if (!io_mem)
		return -ENOMEM;
	ebda_seg = readw(io_mem);
	iounmap(io_mem);
	debug("returned ebda segment: %x\n", ebda_seg);

	io_mem = ioremap(ebda_seg<<4, 1);
	if (!io_mem)
		return -ENOMEM;
	ebda_sz = readb(io_mem);
	iounmap(io_mem);
	debug("ebda size: %d(KiB)\n", ebda_sz);
	if (ebda_sz == 0)
		return -ENOMEM;

	io_mem = ioremap(ebda_seg<<4, (ebda_sz * 1024));
	if (!io_mem)
		return -ENOMEM;
	next_offset = 0x180;

	for (;;) {
		offset = next_offset;

		/* Make sure what we read is still in the mapped section */
		if (WARN(offset > (ebda_sz * 1024 - 4),
			 "ibmphp_ebda: next read is beyond ebda_sz\n"))
			break;

		next_offset = readw(io_mem + offset);	/* offset of next blk */

		offset += 2;
		if (next_offset == 0)	/* 0 indicate it's last blk */
			break;
		blk_id = readw(io_mem + offset);	/* this blk id */

		offset += 2;
		/* check if it is hot swap block or rio block */
		if (blk_id != 0x4853 && blk_id != 0x4752)
			continue;
		/* found hs table */
		if (blk_id == 0x4853) {
			debug("now enter hot swap block---\n");
			debug("hot blk id: %x\n", blk_id);
			format = readb(io_mem + offset);

			offset += 1;
			if (format != 4)
				goto error_nodev;
			debug("hot blk format: %x\n", format);
			/* hot swap sub blk */
			base = offset;

			sub_addr = base;
			re = readw(io_mem + sub_addr);	/* next sub blk */

			sub_addr += 2;
			rc_id = readw(io_mem + sub_addr);	/* sub blk id */

			sub_addr += 2;
			if (rc_id != 0x5243)
				goto error_nodev;
			/* rc sub blk signature  */
			num_ctlrs = readb(io_mem + sub_addr);

			sub_addr += 1;
			hpc_list_ptr = alloc_ebda_hpc_list();
			if (!hpc_list_ptr) {
				rc = -ENOMEM;
				goto out;
			}
			hpc_list_ptr->format = format;
			hpc_list_ptr->num_ctlrs = num_ctlrs;
			hpc_list_ptr->phys_addr = sub_addr;	/*  offset of RSRC_CONTROLLER blk */
			debug("info about hpc descriptor---\n");
			debug("hot blk format: %x\n", format);
			debug("num of controller: %x\n", num_ctlrs);
			debug("offset of hpc data structure entries: %x\n ", sub_addr);

			sub_addr = base + re;	/* re sub blk */
			/* FIXME: rc is never used/checked */
			rc = readw(io_mem + sub_addr);	/* next sub blk */

			sub_addr += 2;
			re_id = readw(io_mem + sub_addr);	/* sub blk id */

			sub_addr += 2;
			if (re_id != 0x5245)
				goto error_nodev;

			/* signature of re */
			num_entries = readw(io_mem + sub_addr);

			sub_addr += 2;	/* offset of RSRC_ENTRIES blk */
			rsrc_list_ptr = alloc_ebda_rsrc_list();
			if (!rsrc_list_ptr) {
				rc = -ENOMEM;
				goto out;
			}
			rsrc_list_ptr->format = format;
			rsrc_list_ptr->num_entries = num_entries;
			rsrc_list_ptr->phys_addr = sub_addr;

			debug("info about rsrc descriptor---\n");
			debug("format: %x\n", format);
			debug("num of rsrc: %x\n", num_entries);
			debug("offset of rsrc data structure entries: %x\n ", sub_addr);

			hs_complete = 1;
		} else {
		/* found rio table, blk_id == 0x4752 */
			debug("now enter io table ---\n");
			debug("rio blk id: %x\n", blk_id);

			rio_table_ptr = kzalloc(sizeof(struct rio_table_hdr), GFP_KERNEL);
			if (!rio_table_ptr) {
				rc = -ENOMEM;
				goto out;
			}
			rio_table_ptr->ver_num = readb(io_mem + offset);
			rio_table_ptr->scal_count = readb(io_mem + offset + 1);
			rio_table_ptr->riodev_count = readb(io_mem + offset + 2);
			rio_table_ptr->offset = offset + 3 ;

			debug("info about rio table hdr ---\n");
			debug("ver_num: %x\nscal_count: %x\nriodev_count: %x\noffset of rio table: %x\n ",
				rio_table_ptr->ver_num, rio_table_ptr->scal_count,
				rio_table_ptr->riodev_count, rio_table_ptr->offset);

			rio_complete = 1;
		}
	}

	if (!hs_complete && !rio_complete)
		goto error_nodev;

	if (rio_table_ptr) {
		if (rio_complete && rio_table_ptr->ver_num == 3) {
			rc = ebda_rio_table();
			if (rc)
				goto out;
		}
	}
	rc = ebda_rsrc_controller();
	if (rc)
		goto out;

	rc = ebda_rsrc_rsrc();
	goto out;
error_nodev:
	rc = -ENODEV;
out:
	iounmap(io_mem);
	return rc;
}

/*
 * map info of scalability details and rio details from physical address
 */
static int __init ebda_rio_table(void)
{
	u16 offset;
	u8 i;
	struct rio_detail *rio_detail_ptr;

	offset = rio_table_ptr->offset;
	offset += 12 * rio_table_ptr->scal_count;

	// we do concern about rio details
	for (i = 0; i < rio_table_ptr->riodev_count; i++) {
		rio_detail_ptr = kzalloc(sizeof(struct rio_detail), GFP_KERNEL);
		if (!rio_detail_ptr)
			return -ENOMEM;
		rio_detail_ptr->rio_node_id = readb(io_mem + offset);
		rio_detail_ptr->bbar = readl(io_mem + offset + 1);
		rio_detail_ptr->rio_type = readb(io_mem + offset + 5);
		rio_detail_ptr->owner_id = readb(io_mem + offset + 6);
		rio_detail_ptr->port0_node_connect = readb(io_mem + offset + 7);
		rio_detail_ptr->port0_port_connect = readb(io_mem + offset + 8);
		rio_detail_ptr->port1_node_connect = readb(io_mem + offset + 9);
		rio_detail_ptr->port1_port_connect = readb(io_mem + offset + 10);
		rio_detail_ptr->first_slot_num = readb(io_mem + offset + 11);
		rio_detail_ptr->status = readb(io_mem + offset + 12);
		rio_detail_ptr->wpindex = readb(io_mem + offset + 13);
		rio_detail_ptr->chassis_num = readb(io_mem + offset + 14);
//		debug("rio_node_id: %x\nbbar: %x\nrio_type: %x\nowner_id: %x\nport0_node: %x\nport0_port: %x\nport1_node: %x\nport1_port: %x\nfirst_slot_num: %x\nstatus: %x\n", rio_detail_ptr->rio_node_id, rio_detail_ptr->bbar, rio_detail_ptr->rio_type, rio_detail_ptr->owner_id, rio_detail_ptr->port0_node_connect, rio_detail_ptr->port0_port_connect, rio_detail_ptr->port1_node_connect, rio_detail_ptr->port1_port_connect, rio_detail_ptr->first_slot_num, rio_detail_ptr->status);
		//create linked list of chassis
		if (rio_detail_ptr->rio_type == 4 || rio_detail_ptr->rio_type == 5)
			list_add(&rio_detail_ptr->rio_detail_list, &rio_vg_head);
		//create linked list of expansion box
		else if (rio_detail_ptr->rio_type == 6 || rio_detail_ptr->rio_type == 7)
			list_add(&rio_detail_ptr->rio_detail_list, &rio_lo_head);
		else
			// not in my concern
			kfree(rio_detail_ptr);
		offset += 15;
	}
	print_lo_info();
	print_vg_info();
	return 0;
}

/*
 * reorganizing linked list of chassis
 */
static struct opt_rio *search_opt_vg(u8 chassis_num)
{
	struct opt_rio *ptr;
	list_for_each_entry(ptr, &opt_vg_head, opt_rio_list) {
		if (ptr->chassis_num == chassis_num)
			return ptr;
	}
	return NULL;
}

static int __init combine_wpg_for_chassis(void)
{
	struct opt_rio *opt_rio_ptr = NULL;
	struct rio_detail *rio_detail_ptr = NULL;

	list_for_each_entry(rio_detail_ptr, &rio_vg_head, rio_detail_list) {
		opt_rio_ptr = search_opt_vg(rio_detail_ptr->chassis_num);
		if (!opt_rio_ptr) {
			opt_rio_ptr = kzalloc(sizeof(struct opt_rio), GFP_KERNEL);
			if (!opt_rio_ptr)
				return -ENOMEM;
			opt_rio_ptr->rio_type = rio_detail_ptr->rio_type;
			opt_rio_ptr->chassis_num = rio_detail_ptr->chassis_num;
			opt_rio_ptr->first_slot_num = rio_detail_ptr->first_slot_num;
			opt_rio_ptr->middle_num = rio_detail_ptr->first_slot_num;
			list_add(&opt_rio_ptr->opt_rio_list, &opt_vg_head);
		} else {
			opt_rio_ptr->first_slot_num = min(opt_rio_ptr->first_slot_num, rio_detail_ptr->first_slot_num);
			opt_rio_ptr->middle_num = max(opt_rio_ptr->middle_num, rio_detail_ptr->first_slot_num);
		}
	}
	print_opt_vg();
	return 0;
}

/*
 * reorganizing linked list of expansion box
 */
static struct opt_rio_lo *search_opt_lo(u8 chassis_num)
{
	struct opt_rio_lo *ptr;
	list_for_each_entry(ptr, &opt_lo_head, opt_rio_lo_list) {
		if (ptr->chassis_num == chassis_num)
			return ptr;
	}
	return NULL;
}

static int combine_wpg_for_expansion(void)
{
	struct opt_rio_lo *opt_rio_lo_ptr = NULL;
	struct rio_detail *rio_detail_ptr = NULL;

	list_for_each_entry(rio_detail_ptr, &rio_lo_head, rio_detail_list) {
		opt_rio_lo_ptr = search_opt_lo(rio_detail_ptr->chassis_num);
		if (!opt_rio_lo_ptr) {
			opt_rio_lo_ptr = kzalloc(sizeof(struct opt_rio_lo), GFP_KERNEL);
			if (!opt_rio_lo_ptr)
				return -ENOMEM;
			opt_rio_lo_ptr->rio_type = rio_detail_ptr->rio_type;
			opt_rio_lo_ptr->chassis_num = rio_detail_ptr->chassis_num;
			opt_rio_lo_ptr->first_slot_num = rio_detail_ptr->first_slot_num;
			opt_rio_lo_ptr->middle_num = rio_detail_ptr->first_slot_num;
			opt_rio_lo_ptr->pack_count = 1;

			list_add(&opt_rio_lo_ptr->opt_rio_lo_list, &opt_lo_head);
		} else {
			opt_rio_lo_ptr->first_slot_num = min(opt_rio_lo_ptr->first_slot_num, rio_detail_ptr->first_slot_num);
			opt_rio_lo_ptr->middle_num = max(opt_rio_lo_ptr->middle_num, rio_detail_ptr->first_slot_num);
			opt_rio_lo_ptr->pack_count = 2;
		}
	}
	return 0;
}


/* Since we don't know the max slot number per each chassis, hence go
 * through the list of all chassis to find out the range
 * Arguments: slot_num, 1st slot number of the chassis we think we are on,
 * var (0 = chassis, 1 = expansion box)
 */
static int first_slot_num(u8 slot_num, u8 first_slot, u8 var)
{
	struct opt_rio *opt_vg_ptr = NULL;
	struct opt_rio_lo *opt_lo_ptr = NULL;
	int rc = 0;

	if (!var) {
		list_for_each_entry(opt_vg_ptr, &opt_vg_head, opt_rio_list) {
			if ((first_slot < opt_vg_ptr->first_slot_num) && (slot_num >= opt_vg_ptr->first_slot_num)) {
				rc = -ENODEV;
				break;
			}
		}
	} else {
		list_for_each_entry(opt_lo_ptr, &opt_lo_head, opt_rio_lo_list) {
			if ((first_slot < opt_lo_ptr->first_slot_num) && (slot_num >= opt_lo_ptr->first_slot_num)) {
				rc = -ENODEV;
				break;
			}
		}
	}
	return rc;
}

static struct opt_rio_lo *find_rxe_num(u8 slot_num)
{
	struct opt_rio_lo *opt_lo_ptr;

	list_for_each_entry(opt_lo_ptr, &opt_lo_head, opt_rio_lo_list) {
		//check to see if this slot_num belongs to expansion box
		if ((slot_num >= opt_lo_ptr->first_slot_num) && (!first_slot_num(slot_num, opt_lo_ptr->first_slot_num, 1)))
			return opt_lo_ptr;
	}
	return NULL;
}

static struct opt_rio *find_chassis_num(u8 slot_num)
{
	struct opt_rio *opt_vg_ptr;

	list_for_each_entry(opt_vg_ptr, &opt_vg_head, opt_rio_list) {
		//check to see if this slot_num belongs to chassis
		if ((slot_num >= opt_vg_ptr->first_slot_num) && (!first_slot_num(slot_num, opt_vg_ptr->first_slot_num, 0)))
			return opt_vg_ptr;
	}
	return NULL;
}

/* This routine will find out how many slots are in the chassis, so that
 * the slot numbers for rxe100 would start from 1, and not from 7, or 6 etc
 */
static u8 calculate_first_slot(u8 slot_num)
{
	u8 first_slot = 1;
	struct slot *slot_cur;

	list_for_each_entry(slot_cur, &ibmphp_slot_head, ibm_slot_list) {
		if (slot_cur->ctrl) {
			if ((slot_cur->ctrl->ctlr_type != 4) && (slot_cur->ctrl->ending_slot_num > first_slot) && (slot_num > slot_cur->ctrl->ending_slot_num))
				first_slot = slot_cur->ctrl->ending_slot_num;
		}
	}
	return first_slot + 1;

}

#define SLOT_NAME_SIZE 30

static char *create_file_name(struct slot *slot_cur)
{
	struct opt_rio *opt_vg_ptr = NULL;
	struct opt_rio_lo *opt_lo_ptr = NULL;
	static char str[SLOT_NAME_SIZE];
	int which = 0; /* rxe = 1, chassis = 0 */
	u8 number = 1; /* either chassis or rxe # */
	u8 first_slot = 1;
	u8 slot_num;
	u8 flag = 0;

	if (!slot_cur) {
		err("Structure passed is empty\n");
		return NULL;
	}

	slot_num = slot_cur->number;

	memset(str, 0, sizeof(str));

	if (rio_table_ptr) {
		if (rio_table_ptr->ver_num == 3) {
			opt_vg_ptr = find_chassis_num(slot_num);
			opt_lo_ptr = find_rxe_num(slot_num);
		}
	}
	if (opt_vg_ptr) {
		if (opt_lo_ptr) {
			if ((slot_num - opt_vg_ptr->first_slot_num) > (slot_num - opt_lo_ptr->first_slot_num)) {
				number = opt_lo_ptr->chassis_num;
				first_slot = opt_lo_ptr->first_slot_num;
				which = 1; /* it is RXE */
			} else {
				first_slot = opt_vg_ptr->first_slot_num;
				number = opt_vg_ptr->chassis_num;
				which = 0;
			}
		} else {
			first_slot = opt_vg_ptr->first_slot_num;
			number = opt_vg_ptr->chassis_num;
			which = 0;
		}
		++flag;
	} else if (opt_lo_ptr) {
		number = opt_lo_ptr->chassis_num;
		first_slot = opt_lo_ptr->first_slot_num;
		which = 1;
		++flag;
	} else if (rio_table_ptr) {
		if (rio_table_ptr->ver_num == 3) {
			/* if both NULL and we DO have correct RIO table in BIOS */
			return NULL;
		}
	}
	if (!flag) {
		if (slot_cur->ctrl->ctlr_type == 4) {
			first_slot = calculate_first_slot(slot_num);
			which = 1;
		} else {
			which = 0;
		}
	}

	sprintf(str, "%s%dslot%d",
		which == 0 ? "chassis" : "rxe",
		number, slot_num - first_slot + 1);
	return str;
}

static int fillslotinfo(struct hotplug_slot *hotplug_slot)
{
	struct slot *slot;
	int rc = 0;

	if (!hotplug_slot || !hotplug_slot->private)
		return -EINVAL;

	slot = hotplug_slot->private;
	rc = ibmphp_hpc_readslot(slot, READ_ALLSTAT, NULL);
	if (rc)
		return rc;

	// power - enabled:1  not:0
	hotplug_slot->info->power_status = SLOT_POWER(slot->status);

	// attention - off:0, on:1, blinking:2
	hotplug_slot->info->attention_status = SLOT_ATTN(slot->status, slot->ext_status);

	// latch - open:1 closed:0
	hotplug_slot->info->latch_status = SLOT_LATCH(slot->status);

	// pci board - present:1 not:0
	if (SLOT_PRESENT(slot->status))
		hotplug_slot->info->adapter_status = 1;
	else
		hotplug_slot->info->adapter_status = 0;
/*
	if (slot->bus_on->supported_bus_mode
		&& (slot->bus_on->supported_speed == BUS_SPEED_66))
		hotplug_slot->info->max_bus_speed_status = BUS_SPEED_66PCIX;
	else
		hotplug_slot->info->max_bus_speed_status = slot->bus_on->supported_speed;
*/

	return rc;
}

static struct pci_driver ibmphp_driver;

/*
 * map info (ctlr-id, slot count, slot#.. bus count, bus#, ctlr type...) of
 * each hpc from physical address to a list of hot plug controllers based on
 * hpc descriptors.
 */
static int __init ebda_rsrc_controller(void)
{
	u16 addr, addr_slot, addr_bus;
	u8 ctlr_id, temp, bus_index;
	u16 ctlr, slot, bus;
	u16 slot_num, bus_num, index;
	struct hotplug_slot *hp_slot_ptr;
	struct controller *hpc_ptr;
	struct ebda_hpc_bus *bus_ptr;
	struct ebda_hpc_slot *slot_ptr;
	struct bus_info *bus_info_ptr1, *bus_info_ptr2;
	int rc;
	struct slot *tmp_slot;
	char name[SLOT_NAME_SIZE];

	addr = hpc_list_ptr->phys_addr;
	for (ctlr = 0; ctlr < hpc_list_ptr->num_ctlrs; ctlr++) {
		bus_index = 1;
		ctlr_id = readb(io_mem + addr);
		addr += 1;
		slot_num = readb(io_mem + addr);

		addr += 1;
		addr_slot = addr;	/* offset of slot structure */
		addr += (slot_num * 4);

		bus_num = readb(io_mem + addr);

		addr += 1;
		addr_bus = addr;	/* offset of bus */
		addr += (bus_num * 9);	/* offset of ctlr_type */
		temp = readb(io_mem + addr);

		addr += 1;
		/* init hpc structure */
		hpc_ptr = alloc_ebda_hpc(slot_num, bus_num);
		if (!hpc_ptr) {
			rc = -ENOMEM;
			goto error_no_hpc;
		}
		hpc_ptr->ctlr_id = ctlr_id;
		hpc_ptr->ctlr_relative_id = ctlr;
		hpc_ptr->slot_count = slot_num;
		hpc_ptr->bus_count = bus_num;
		debug("now enter ctlr data structure ---\n");
		debug("ctlr id: %x\n", ctlr_id);
		debug("ctlr_relative_id: %x\n", hpc_ptr->ctlr_relative_id);
		debug("count of slots controlled by this ctlr: %x\n", slot_num);
		debug("count of buses controlled by this ctlr: %x\n", bus_num);

		/* init slot structure, fetch slot, bus, cap... */
		slot_ptr = hpc_ptr->slots;
		for (slot = 0; slot < slot_num; slot++) {
			slot_ptr->slot_num = readb(io_mem + addr_slot);
			slot_ptr->slot_bus_num = readb(io_mem + addr_slot + slot_num);
			slot_ptr->ctl_index = readb(io_mem + addr_slot + 2*slot_num);
			slot_ptr->slot_cap = readb(io_mem + addr_slot + 3*slot_num);

			// create bus_info lined list --- if only one slot per bus: slot_min = slot_max

			bus_info_ptr2 = ibmphp_find_same_bus_num(slot_ptr->slot_bus_num);
			if (!bus_info_ptr2) {
				bus_info_ptr1 = kzalloc(sizeof(struct bus_info), GFP_KERNEL);
				if (!bus_info_ptr1) {
					rc = -ENOMEM;
					goto error_no_hp_slot;
				}
				bus_info_ptr1->slot_min = slot_ptr->slot_num;
				bus_info_ptr1->slot_max = slot_ptr->slot_num;
				bus_info_ptr1->slot_count += 1;
				bus_info_ptr1->busno = slot_ptr->slot_bus_num;
				bus_info_ptr1->index = bus_index++;
				bus_info_ptr1->current_speed = 0xff;
				bus_info_ptr1->current_bus_mode = 0xff;

				bus_info_ptr1->controller_id = hpc_ptr->ctlr_id;

				list_add_tail(&bus_info_ptr1->bus_info_list, &bus_info_head);

			} else {
				bus_info_ptr2->slot_min = min(bus_info_ptr2->slot_min, slot_ptr->slot_num);
				bus_info_ptr2->slot_max = max(bus_info_ptr2->slot_max, slot_ptr->slot_num);
				bus_info_ptr2->slot_count += 1;

			}

			// end of creating the bus_info linked list

			slot_ptr++;
			addr_slot += 1;
		}

		/* init bus structure */
		bus_ptr = hpc_ptr->buses;
		for (bus = 0; bus < bus_num; bus++) {
			bus_ptr->bus_num = readb(io_mem + addr_bus + bus);
			bus_ptr->slots_at_33_conv = readb(io_mem + addr_bus + bus_num + 8 * bus);
			bus_ptr->slots_at_66_conv = readb(io_mem + addr_bus + bus_num + 8 * bus + 1);

			bus_ptr->slots_at_66_pcix = readb(io_mem + addr_bus + bus_num + 8 * bus + 2);

			bus_ptr->slots_at_100_pcix = readb(io_mem + addr_bus + bus_num + 8 * bus + 3);

			bus_ptr->slots_at_133_pcix = readb(io_mem + addr_bus + bus_num + 8 * bus + 4);

			bus_info_ptr2 = ibmphp_find_same_bus_num(bus_ptr->bus_num);
			if (bus_info_ptr2) {
				bus_info_ptr2->slots_at_33_conv = bus_ptr->slots_at_33_conv;
				bus_info_ptr2->slots_at_66_conv = bus_ptr->slots_at_66_conv;
				bus_info_ptr2->slots_at_66_pcix = bus_ptr->slots_at_66_pcix;
				bus_info_ptr2->slots_at_100_pcix = bus_ptr->slots_at_100_pcix;
				bus_info_ptr2->slots_at_133_pcix = bus_ptr->slots_at_133_pcix;
			}
			bus_ptr++;
		}

		hpc_ptr->ctlr_type = temp;

		switch (hpc_ptr->ctlr_type) {
			case 1:
				hpc_ptr->u.pci_ctlr.bus = readb(io_mem + addr);
				hpc_ptr->u.pci_ctlr.dev_fun = readb(io_mem + addr + 1);
				hpc_ptr->irq = readb(io_mem + addr + 2);
				addr += 3;
				debug("ctrl bus = %x, ctlr devfun = %x, irq = %x\n",
					hpc_ptr->u.pci_ctlr.bus,
					hpc_ptr->u.pci_ctlr.dev_fun, hpc_ptr->irq);
				break;

			case 0:
				hpc_ptr->u.isa_ctlr.io_start = readw(io_mem + addr);
				hpc_ptr->u.isa_ctlr.io_end = readw(io_mem + addr + 2);
				if (!request_region(hpc_ptr->u.isa_ctlr.io_start,
						     (hpc_ptr->u.isa_ctlr.io_end - hpc_ptr->u.isa_ctlr.io_start + 1),
						     "ibmphp")) {
					rc = -ENODEV;
					goto error_no_hp_slot;
				}
				hpc_ptr->irq = readb(io_mem + addr + 4);
				addr += 5;
				break;

			case 2:
			case 4:
				hpc_ptr->u.wpeg_ctlr.wpegbbar = readl(io_mem + addr);
				hpc_ptr->u.wpeg_ctlr.i2c_addr = readb(io_mem + addr + 4);
				hpc_ptr->irq = readb(io_mem + addr + 5);
				addr += 6;
				break;
			default:
				rc = -ENODEV;
				goto error_no_hp_slot;
		}

		//reorganize chassis' linked list
		combine_wpg_for_chassis();
		combine_wpg_for_expansion();
		hpc_ptr->revision = 0xff;
		hpc_ptr->options = 0xff;
		hpc_ptr->starting_slot_num = hpc_ptr->slots[0].slot_num;
		hpc_ptr->ending_slot_num = hpc_ptr->slots[slot_num-1].slot_num;

		// register slots with hpc core as well as create linked list of ibm slot
		for (index = 0; index < hpc_ptr->slot_count; index++) {

			hp_slot_ptr = kzalloc(sizeof(*hp_slot_ptr), GFP_KERNEL);
			if (!hp_slot_ptr) {
				rc = -ENOMEM;
				goto error_no_hp_slot;
			}

			hp_slot_ptr->info = kzalloc(sizeof(struct hotplug_slot_info), GFP_KERNEL);
			if (!hp_slot_ptr->info) {
				rc = -ENOMEM;
				goto error_no_hp_info;
			}

			tmp_slot = kzalloc(sizeof(*tmp_slot), GFP_KERNEL);
			if (!tmp_slot) {
				rc = -ENOMEM;
				goto error_no_slot;
			}

			tmp_slot->flag = 1;

			tmp_slot->capabilities = hpc_ptr->slots[index].slot_cap;
			if ((hpc_ptr->slots[index].slot_cap & EBDA_SLOT_133_MAX) == EBDA_SLOT_133_MAX)
				tmp_slot->supported_speed =  3;
			else if ((hpc_ptr->slots[index].slot_cap & EBDA_SLOT_100_MAX) == EBDA_SLOT_100_MAX)
				tmp_slot->supported_speed =  2;
			else if ((hpc_ptr->slots[index].slot_cap & EBDA_SLOT_66_MAX) == EBDA_SLOT_66_MAX)
				tmp_slot->supported_speed =  1;

			if ((hpc_ptr->slots[index].slot_cap & EBDA_SLOT_PCIX_CAP) == EBDA_SLOT_PCIX_CAP)
				tmp_slot->supported_bus_mode = 1;
			else
				tmp_slot->supported_bus_mode = 0;


			tmp_slot->bus = hpc_ptr->slots[index].slot_bus_num;

			bus_info_ptr1 = ibmphp_find_same_bus_num(hpc_ptr->slots[index].slot_bus_num);
			if (!bus_info_ptr1) {
				kfree(tmp_slot);
				rc = -ENODEV;
				goto error;
			}
			tmp_slot->bus_on = bus_info_ptr1;
			bus_info_ptr1 = NULL;
			tmp_slot->ctrl = hpc_ptr;

			tmp_slot->ctlr_index = hpc_ptr->slots[index].ctl_index;
			tmp_slot->number = hpc_ptr->slots[index].slot_num;
			tmp_slot->hotplug_slot = hp_slot_ptr;

			hp_slot_ptr->private = tmp_slot;

			rc = fillslotinfo(hp_slot_ptr);
			if (rc)
				goto error;

			rc = ibmphp_init_devno((struct slot **) &hp_slot_ptr->private);
			if (rc)
				goto error;
			hp_slot_ptr->ops = &ibmphp_hotplug_slot_ops;

			// end of registering ibm slot with hotplug core

			list_add(&((struct slot *)(hp_slot_ptr->private))->ibm_slot_list, &ibmphp_slot_head);
		}

		print_bus_info();
		list_add(&hpc_ptr->ebda_hpc_list, &ebda_hpc_head);

	}			/* each hpc  */

	list_for_each_entry(tmp_slot, &ibmphp_slot_head, ibm_slot_list) {
		snprintf(name, SLOT_NAME_SIZE, "%s", create_file_name(tmp_slot));
		pci_hp_register(tmp_slot->hotplug_slot,
			pci_find_bus(0, tmp_slot->bus), tmp_slot->device, name);
	}

	print_ebda_hpc();
	print_ibm_slot();
	return 0;

error:
	kfree(hp_slot_ptr->private);
error_no_slot:
	kfree(hp_slot_ptr->info);
error_no_hp_info:
	kfree(hp_slot_ptr);
error_no_hp_slot:
	free_ebda_hpc(hpc_ptr);
error_no_hpc:
	iounmap(io_mem);
	return rc;
}

/*
 * map info (bus, devfun, start addr, end addr..) of i/o, memory,
 * pfm from the physical addr to a list of resource.
 */
static int __init ebda_rsrc_rsrc(void)
{
	u16 addr;
	short rsrc;
	u8 type, rsrc_type;
	struct ebda_pci_rsrc *rsrc_ptr;

	addr = rsrc_list_ptr->phys_addr;
	debug("now entering rsrc land\n");
	debug("offset of rsrc: %x\n", rsrc_list_ptr->phys_addr);

	for (rsrc = 0; rsrc < rsrc_list_ptr->num_entries; rsrc++) {
		type = readb(io_mem + addr);

		addr += 1;
		rsrc_type = type & EBDA_RSRC_TYPE_MASK;

		if (rsrc_type == EBDA_IO_RSRC_TYPE) {
			rsrc_ptr = alloc_ebda_pci_rsrc();
			if (!rsrc_ptr) {
				iounmap(io_mem);
				return -ENOMEM;
			}
			rsrc_ptr->rsrc_type = type;

			rsrc_ptr->bus_num = readb(io_mem + addr);
			rsrc_ptr->dev_fun = readb(io_mem + addr + 1);
			rsrc_ptr->start_addr = readw(io_mem + addr + 2);
			rsrc_ptr->end_addr = readw(io_mem + addr + 4);
			addr += 6;

			debug("rsrc from io type ----\n");
			debug("rsrc type: %x bus#: %x dev_func: %x start addr: %x end addr: %x\n",
				rsrc_ptr->rsrc_type, rsrc_ptr->bus_num, rsrc_ptr->dev_fun, rsrc_ptr->start_addr, rsrc_ptr->end_addr);

			list_add(&rsrc_ptr->ebda_pci_rsrc_list, &ibmphp_ebda_pci_rsrc_head);
		}

		if (rsrc_type == EBDA_MEM_RSRC_TYPE || rsrc_type == EBDA_PFM_RSRC_TYPE) {
			rsrc_ptr = alloc_ebda_pci_rsrc();
			if (!rsrc_ptr) {
				iounmap(io_mem);
				return -ENOMEM;
			}
			rsrc_ptr->rsrc_type = type;

			rsrc_ptr->bus_num = readb(io_mem + addr);
			rsrc_ptr->dev_fun = readb(io_mem + addr + 1);
			rsrc_ptr->start_addr = readl(io_mem + addr + 2);
			rsrc_ptr->end_addr = readl(io_mem + addr + 6);
			addr += 10;

			debug("rsrc from mem or pfm ---\n");
			debug("rsrc type: %x bus#: %x dev_func: %x start addr: %x end addr: %x\n",
				rsrc_ptr->rsrc_type, rsrc_ptr->bus_num, rsrc_ptr->dev_fun, rsrc_ptr->start_addr, rsrc_ptr->end_addr);

			list_add(&rsrc_ptr->ebda_pci_rsrc_list, &ibmphp_ebda_pci_rsrc_head);
		}
	}
	kfree(rsrc_list_ptr);
	rsrc_list_ptr = NULL;
	print_ebda_pci_rsrc();
	return 0;
}

u16 ibmphp_get_total_controllers(void)
{
	return hpc_list_ptr->num_ctlrs;
}

struct slot *ibmphp_get_slot_from_physical_num(u8 physical_num)
{
	struct slot *slot;

	list_for_each_entry(slot, &ibmphp_slot_head, ibm_slot_list) {
		if (slot->number == physical_num)
			return slot;
	}
	return NULL;
}

/* To find:
 *	- the smallest slot number
 *	- the largest slot number
 *	- the total number of the slots based on each bus
 *	  (if only one slot per bus slot_min = slot_max )
 */
struct bus_info *ibmphp_find_same_bus_num(u32 num)
{
	struct bus_info *ptr;

	list_for_each_entry(ptr, &bus_info_head, bus_info_list) {
		if (ptr->busno == num)
			 return ptr;
	}
	return NULL;
}

/*  Finding relative bus number, in order to map corresponding
 *  bus register
 */
int ibmphp_get_bus_index(u8 num)
{
	struct bus_info *ptr;

	list_for_each_entry(ptr, &bus_info_head, bus_info_list) {
		if (ptr->busno == num)
			return ptr->index;
	}
	return -ENODEV;
}

void ibmphp_free_bus_info_queue(void)
{
	struct bus_info *bus_info, *next;

	list_for_each_entry_safe(bus_info, next, &bus_info_head,
				 bus_info_list) {
		kfree (bus_info);
	}
}

void ibmphp_free_ebda_hpc_queue(void)
{
	struct controller *controller = NULL, *next;
	int pci_flag = 0;

	list_for_each_entry_safe(controller, next, &ebda_hpc_head,
				 ebda_hpc_list) {
		if (controller->ctlr_type == 0)
			release_region(controller->u.isa_ctlr.io_start, (controller->u.isa_ctlr.io_end - controller->u.isa_ctlr.io_start + 1));
		else if ((controller->ctlr_type == 1) && (!pci_flag)) {
			++pci_flag;
			pci_unregister_driver(&ibmphp_driver);
		}
		free_ebda_hpc(controller);
	}
}

void ibmphp_free_ebda_pci_rsrc_queue(void)
{
	struct ebda_pci_rsrc *resource, *next;

	list_for_each_entry_safe(resource, next, &ibmphp_ebda_pci_rsrc_head,
				 ebda_pci_rsrc_list) {
		kfree (resource);
		resource = NULL;
	}
}

static const struct pci_device_id id_table[] = {
	{
		.vendor		= PCI_VENDOR_ID_IBM,
		.device		= HPC_DEVICE_ID,
		.subvendor	= PCI_VENDOR_ID_IBM,
		.subdevice	= HPC_SUBSYSTEM_ID,
		.class		= ((PCI_CLASS_SYSTEM_PCI_HOTPLUG << 8) | 0x00),
	}, {}
};

MODULE_DEVICE_TABLE(pci, id_table);

static int ibmphp_probe(struct pci_dev *, const struct pci_device_id *);
static struct pci_driver ibmphp_driver = {
	.name		= "ibmphp",
	.id_table	= id_table,
	.probe		= ibmphp_probe,
};

int ibmphp_register_pci(void)
{
	struct controller *ctrl;
	int rc = 0;

	list_for_each_entry(ctrl, &ebda_hpc_head, ebda_hpc_list) {
		if (ctrl->ctlr_type == 1) {
			rc = pci_register_driver(&ibmphp_driver);
			break;
		}
	}
	return rc;
}
static int ibmphp_probe(struct pci_dev *dev, const struct pci_device_id *ids)
{
	struct controller *ctrl;

	debug("inside ibmphp_probe\n");

	list_for_each_entry(ctrl, &ebda_hpc_head, ebda_hpc_list) {
		if (ctrl->ctlr_type == 1) {
			if ((dev->devfn == ctrl->u.pci_ctlr.dev_fun) && (dev->bus->number == ctrl->u.pci_ctlr.bus)) {
				ctrl->ctrl_dev = dev;
				debug("found device!!!\n");
				debug("dev->device = %x, dev->subsystem_device = %x\n", dev->device, dev->subsystem_device);
				return 0;
			}
		}
	}
	return -ENODEV;
}
