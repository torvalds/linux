/*
 * IBM Summit-Specific Code
 *
 * Written By: Matthew Dobson, IBM Corporation
 *
 * Copyright (c) 2003 IBM Corp.
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
 * Send feedback to <colpatch@us.ibm.com>
 *
 */

#include <linux/mm.h>
#include <linux/init.h>
#include <asm/io.h>
#include <asm/bios_ebda.h>
#include <asm/summit/mpparse.h>

static struct rio_table_hdr *rio_table_hdr __initdata;
static struct scal_detail   *scal_devs[MAX_NUMNODES] __initdata;
static struct rio_detail    *rio_devs[MAX_NUMNODES*4] __initdata;

#ifndef CONFIG_X86_NUMAQ
static int mp_bus_id_to_node[MAX_MP_BUSSES] __initdata;
#endif

static int __init setup_pci_node_map_for_wpeg(int wpeg_num, int last_bus)
{
	int twister = 0, node = 0;
	int i, bus, num_buses;

	for (i = 0; i < rio_table_hdr->num_rio_dev; i++) {
		if (rio_devs[i]->node_id == rio_devs[wpeg_num]->owner_id) {
			twister = rio_devs[i]->owner_id;
			break;
		}
	}
	if (i == rio_table_hdr->num_rio_dev) {
		printk(KERN_ERR "%s: Couldn't find owner Cyclone for Winnipeg!\n", __func__);
		return last_bus;
	}

	for (i = 0; i < rio_table_hdr->num_scal_dev; i++) {
		if (scal_devs[i]->node_id == twister) {
			node = scal_devs[i]->node_id;
			break;
		}
	}
	if (i == rio_table_hdr->num_scal_dev) {
		printk(KERN_ERR "%s: Couldn't find owner Twister for Cyclone!\n", __func__);
		return last_bus;
	}

	switch (rio_devs[wpeg_num]->type) {
	case CompatWPEG:
		/*
		 * The Compatibility Winnipeg controls the 2 legacy buses,
		 * the 66MHz PCI bus [2 slots] and the 2 "extra" buses in case
		 * a PCI-PCI bridge card is used in either slot: total 5 buses.
		 */
		num_buses = 5;
		break;
	case AltWPEG:
		/*
		 * The Alternate Winnipeg controls the 2 133MHz buses [1 slot
		 * each], their 2 "extra" buses, the 100MHz bus [2 slots] and
		 * the "extra" buses for each of those slots: total 7 buses.
		 */
		num_buses = 7;
		break;
	case LookOutAWPEG:
	case LookOutBWPEG:
		/*
		 * A Lookout Winnipeg controls 3 100MHz buses [2 slots each]
		 * & the "extra" buses for each of those slots: total 9 buses.
		 */
		num_buses = 9;
		break;
	default:
		printk(KERN_INFO "%s: Unsupported Winnipeg type!\n", __func__);
		return last_bus;
	}

	for (bus = last_bus; bus < last_bus + num_buses; bus++)
		mp_bus_id_to_node[bus] = node;
	return bus;
}

static int __init build_detail_arrays(void)
{
	unsigned long ptr;
	int i, scal_detail_size, rio_detail_size;

	if (rio_table_hdr->num_scal_dev > MAX_NUMNODES) {
		printk(KERN_WARNING "%s: MAX_NUMNODES too low!  Defined as %d, but system has %d nodes.\n", __func__, MAX_NUMNODES, rio_table_hdr->num_scal_dev);
		return 0;
	}

	switch (rio_table_hdr->version) {
	default:
		printk(KERN_WARNING "%s: Invalid Rio Grande Table Version: %d\n", __func__, rio_table_hdr->version);
		return 0;
	case 2:
		scal_detail_size = 11;
		rio_detail_size = 13;
		break;
	case 3:
		scal_detail_size = 12;
		rio_detail_size = 15;
		break;
	}

	ptr = (unsigned long)rio_table_hdr + 3;
	for (i = 0; i < rio_table_hdr->num_scal_dev; i++, ptr += scal_detail_size)
		scal_devs[i] = (struct scal_detail *)ptr;

	for (i = 0; i < rio_table_hdr->num_rio_dev; i++, ptr += rio_detail_size)
		rio_devs[i] = (struct rio_detail *)ptr;

	return 1;
}

void __init setup_summit(void)
{
	unsigned long		ptr;
	unsigned short		offset;
	int			i, next_wpeg, next_bus = 0;

	/* The pointer to the EBDA is stored in the word @ phys 0x40E(40:0E) */
	ptr = get_bios_ebda();
	ptr = (unsigned long)phys_to_virt(ptr);

	rio_table_hdr = NULL;
	offset = 0x180;
	while (offset) {
		/* The block id is stored in the 2nd word */
		if (*((unsigned short *)(ptr + offset + 2)) == 0x4752) {
			/* set the pointer past the offset & block id */
			rio_table_hdr = (struct rio_table_hdr *)(ptr + offset + 4);
			break;
		}
		/* The next offset is stored in the 1st word.  0 means no more */
		offset = *((unsigned short *)(ptr + offset));
	}
	if (!rio_table_hdr) {
		printk(KERN_ERR "%s: Unable to locate Rio Grande Table in EBDA - bailing!\n", __func__);
		return;
	}

	if (!build_detail_arrays())
		return;

	/* The first Winnipeg we're looking for has an index of 0 */
	next_wpeg = 0;
	do {
		for (i = 0; i < rio_table_hdr->num_rio_dev; i++) {
			if (is_WPEG(rio_devs[i]) && rio_devs[i]->WP_index == next_wpeg) {
				/* It's the Winnipeg we're looking for! */
				next_bus = setup_pci_node_map_for_wpeg(i, next_bus);
				next_wpeg++;
				break;
			}
		}
		/*
		 * If we go through all Rio devices and don't find one with
		 * the next index, it means we've found all the Winnipegs,
		 * and thus all the PCI buses.
		 */
		if (i == rio_table_hdr->num_rio_dev)
			next_wpeg = 0;
	} while (next_wpeg != 0);
}
