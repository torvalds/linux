/*
 * This code gets the card location of the hardware
 * Copyright (C) 2001  <Allan H Trautman> <IBM Corp>
 * Copyright (C) 2005  Stephen Rothwel, IBM Corp
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the:
 * Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330,
 * Boston, MA  02111-1307  USA
 *
 * Change Activity:
 *   Created, Feb 2, 2001
 *   Ported to ppc64, August 20, 2001
 * End Change Activity
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>

#include <asm/types.h>
#include <asm/resource.h>
#include <asm/abs_addr.h>
#include <asm/iseries/hv_types.h>

#include "pci.h"
#include "call_pci.h"

/*
 * Size of Bus VPD data
 */
#define BUS_VPDSIZE      1024

/*
 * Bus Vpd Tags
 */
#define VPD_END_OF_AREA		0x79
#define VPD_ID_STRING		0x82
#define VPD_VENDOR_AREA		0x84

/*
 * Mfg Area Tags
 */
#define VPD_FRU_FRAME_ID	0x4649	/* "FI" */
#define VPD_SLOT_MAP_FORMAT	0x4D46	/* "MF" */
#define VPD_SLOT_MAP		0x534D	/* "SM" */

/*
 * Structures of the areas
 */
struct mfg_vpd_area {
	u16	tag;
	u8	length;
	u8	data1;
	u8	data2;
};
#define MFG_ENTRY_SIZE   3

struct slot_map {
	u8	agent;
	u8	secondary_agent;
	u8	phb;
	char	card_location[3];
	char	parms[8];
	char	reserved[2];
};
#define SLOT_ENTRY_SIZE   16

/*
 * Parse the Slot Area
 */
static void __init iseries_parse_slot_area(struct slot_map *map, int len,
		HvAgentId agent, u8 *phb, char card[4])
{
	int slot_map_len = len;
	struct slot_map *slot_map = map;

	/*
	 * Parse Slot label until we find the one requested
	 */
	while (slot_map_len > 0) {
		if (slot_map->agent == agent) {
			/*
			 * If Phb wasn't found, grab the entry first one found.
			 */
			if (*phb == 0xff)
				*phb = slot_map->phb;
			/* Found it, extract the data. */
			if (slot_map->phb == *phb) {
				memcpy(card, &slot_map->card_location, 3);
				card[3]  = 0;
				break;
			}
		}
		/* Point to the next Slot */
		slot_map = (struct slot_map *)((char *)slot_map + SLOT_ENTRY_SIZE);
		slot_map_len -= SLOT_ENTRY_SIZE;
	}
}

/*
 * Parse the Mfg Area
 */
static void __init iseries_parse_mfg_area(u8 *area, int len,
		HvAgentId agent, u8 *phb,
		u8 *frame, char card[4])
{
	struct mfg_vpd_area *mfg_area = (struct mfg_vpd_area *)area;
	int mfg_area_len = len;
	u16 slot_map_fmt = 0;

	/* Parse Mfg Data */
	while (mfg_area_len > 0) {
		int mfg_tag_len = mfg_area->length;
		/* Frame ID         (FI 4649020310 ) */
		if (mfg_area->tag == VPD_FRU_FRAME_ID)
			*frame = mfg_area->data1;
		/* Slot Map Format  (MF 4D46020004 ) */
		else if (mfg_area->tag == VPD_SLOT_MAP_FORMAT)
			slot_map_fmt = (mfg_area->data1 * 256)
				+ mfg_area->data2;
		/* Slot Map         (SM 534D90 */
		else if (mfg_area->tag == VPD_SLOT_MAP) {
			struct slot_map *slot_map;

			if (slot_map_fmt == 0x1004)
				slot_map = (struct slot_map *)((char *)mfg_area
						+ MFG_ENTRY_SIZE + 1);
			else
				slot_map = (struct slot_map *)((char *)mfg_area
						+ MFG_ENTRY_SIZE);
			iseries_parse_slot_area(slot_map, mfg_tag_len,
					agent, phb, card);
		}
		/*
		 * Point to the next Mfg Area
		 * Use defined size, sizeof give wrong answer
		 */
		mfg_area = (struct mfg_vpd_area *)((char *)mfg_area + mfg_tag_len
				+ MFG_ENTRY_SIZE);
		mfg_area_len -= (mfg_tag_len + MFG_ENTRY_SIZE);
	}
}

/*
 * Look for "BUS".. Data is not Null terminated.
 * PHBID of 0xFF indicates PHB was not found in VPD Data.
 */
static int __init iseries_parse_phbid(u8 *area, int len)
{
	u8 *phb_ptr = area;
	int data_len = len;
	char phb = 0xFF;

	while (data_len > 0) {
		if ((*phb_ptr == 'B') && (*(phb_ptr + 1) == 'U')
				&& (*(phb_ptr + 2) == 'S')) {
			phb_ptr += 3;
			while (*phb_ptr == ' ')
				++phb_ptr;
			phb = (*phb_ptr & 0x0F);
			break;
		}
		++phb_ptr;
		--data_len;
	}
	return phb;
}

/*
 * Parse out the VPD Areas
 */
static void __init iseries_parse_vpd(u8 *data, int vpd_data_len,
		HvAgentId agent, u8 *frame, char card[4])
{
	u8 *tag_ptr = data;
	int data_len = vpd_data_len - 3;
	u8 phb = 0xff;

	while ((*tag_ptr != VPD_END_OF_AREA) && (data_len > 0)) {
		int len = *(tag_ptr + 1) + (*(tag_ptr + 2) * 256);
		u8 *area  = tag_ptr + 3;

		if (*tag_ptr == VPD_ID_STRING)
			phb = iseries_parse_phbid(area, len);
		else if (*tag_ptr == VPD_VENDOR_AREA)
			iseries_parse_mfg_area(area, len,
					agent, &phb, frame, card);
		/* Point to next Area. */
		tag_ptr  = area + len;
		data_len -= len;
	}
}

static int __init iseries_get_location_code(u16 bus, HvAgentId agent,
		u8 *frame, char card[4])
{
	int status = 0;
	int bus_vpd_len = 0;
	u8 *bus_vpd = kmalloc(BUS_VPDSIZE, GFP_KERNEL);

	if (bus_vpd == NULL) {
		printk("PCI: Bus VPD Buffer allocation failure.\n");
		return 0;
	}
	bus_vpd_len = HvCallPci_getBusVpd(bus, iseries_hv_addr(bus_vpd),
					BUS_VPDSIZE);
	if (bus_vpd_len == 0) {
		printk("PCI: Bus VPD Buffer zero length.\n");
		goto out_free;
	}
	/* printk("PCI: bus_vpd: %p, %d\n",bus_vpd, bus_vpd_len); */
	/* Make sure this is what I think it is */
	if (*bus_vpd != VPD_ID_STRING) {
		printk("PCI: Bus VPD Buffer missing starting tag.\n");
		goto out_free;
	}
	iseries_parse_vpd(bus_vpd, bus_vpd_len, agent, frame, card);
	status = 1;
out_free:
	kfree(bus_vpd);
	return status;
}

/*
 * Prints the device information.
 * - Pass in pci_dev* pointer to the device.
 * - Pass in the device count
 *
 * Format:
 * PCI: Bus  0, Device 26, Vendor 0x12AE  Frame  1, Card  C10  Ethernet
 * controller
 */
void __init iseries_device_information(struct pci_dev *pdev, int count,
		u16 bus, HvSubBusNumber subbus)
{
	u8 frame = 0;
	char card[4];
	HvAgentId agent;

	agent = ISERIES_PCI_AGENTID(ISERIES_GET_DEVICE_FROM_SUBBUS(subbus),
			ISERIES_GET_FUNCTION_FROM_SUBBUS(subbus));

	if (iseries_get_location_code(bus, agent, &frame, card)) {
		printk("%d. PCI: Bus%3d, Device%3d, Vendor %04X Frame%3d, "
			"Card %4s  0x%04X\n", count, bus,
			PCI_SLOT(pdev->devfn), pdev->vendor, frame,
			card, (int)(pdev->class >> 8));
	}
}
