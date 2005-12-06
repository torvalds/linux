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
#include <asm/pci-bridge.h>
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
#define  VpdEndOfAreaTag   0x79
#define  VpdIdStringTag    0x82
#define  VpdVendorAreaTag  0x84

/*
 * Mfg Area Tags
 */
#define  VpdFruFrameId    0x4649     // "FI"
#define  VpdSlotMapFormat 0x4D46     // "MF"
#define  VpdSlotMap       0x534D     // "SM"

/*
 * Structures of the areas
 */
struct MfgVpdAreaStruct {
	u16 Tag;
	u8  TagLength;
	u8  AreaData1;
	u8  AreaData2;
};
typedef struct MfgVpdAreaStruct MfgArea;
#define MFG_ENTRY_SIZE   3

struct SlotMapStruct {
	u8   AgentId;
	u8   SecondaryAgentId;
	u8   PhbId;
	char CardLocation[3];
	char Parms[8];
	char Reserved[2];
};
typedef struct SlotMapStruct SlotMap;
#define SLOT_ENTRY_SIZE   16

/*
 * Parse the Slot Area
 */
static void __init iSeries_Parse_SlotArea(SlotMap *MapPtr, int MapLen,
		HvAgentId agent, u8 *PhbId, char card[4])
{
	int SlotMapLen = MapLen;
	SlotMap *SlotMapPtr = MapPtr;

	/*
	 * Parse Slot label until we find the one requested
	 */
	while (SlotMapLen > 0) {
		if (SlotMapPtr->AgentId == agent) {
			/*
			 * If Phb wasn't found, grab the entry first one found.
			 */
			if (*PhbId == 0xff)
				*PhbId = SlotMapPtr->PhbId;
			/* Found it, extract the data. */
			if (SlotMapPtr->PhbId == *PhbId) {
				memcpy(card, &SlotMapPtr->CardLocation, 3);
				card[3]  = 0;
				break;
			}
		}
		/* Point to the next Slot */
		SlotMapPtr = (SlotMap *)((char *)SlotMapPtr + SLOT_ENTRY_SIZE);
		SlotMapLen -= SLOT_ENTRY_SIZE;
	}
}

/*
 * Parse the Mfg Area
 */
static void __init iSeries_Parse_MfgArea(u8 *AreaData, int AreaLen,
		HvAgentId agent, u8 *PhbId,
		u8 *frame, char card[4])
{
	MfgArea *MfgAreaPtr = (MfgArea *)AreaData;
	int MfgAreaLen = AreaLen;
	u16 SlotMapFmt = 0;

	/* Parse Mfg Data */
	while (MfgAreaLen > 0) {
		int MfgTagLen = MfgAreaPtr->TagLength;
		/* Frame ID         (FI 4649020310 ) */
		if (MfgAreaPtr->Tag == VpdFruFrameId)		/* FI  */
			*frame = MfgAreaPtr->AreaData1;
		/* Slot Map Format  (MF 4D46020004 ) */
		else if (MfgAreaPtr->Tag == VpdSlotMapFormat)	/* MF  */
			SlotMapFmt = (MfgAreaPtr->AreaData1 * 256)
				+ MfgAreaPtr->AreaData2;
		/* Slot Map         (SM 534D90 */
		else if (MfgAreaPtr->Tag == VpdSlotMap)	{	/* SM  */
			SlotMap *SlotMapPtr;

			if (SlotMapFmt == 0x1004)
				SlotMapPtr = (SlotMap *)((char *)MfgAreaPtr
						+ MFG_ENTRY_SIZE + 1);
			else
				SlotMapPtr = (SlotMap *)((char *)MfgAreaPtr
						+ MFG_ENTRY_SIZE);
			iSeries_Parse_SlotArea(SlotMapPtr, MfgTagLen,
					agent, PhbId, card);
		}
		/*
		 * Point to the next Mfg Area
		 * Use defined size, sizeof give wrong answer
		 */
		MfgAreaPtr = (MfgArea *)((char *)MfgAreaPtr + MfgTagLen
				+ MFG_ENTRY_SIZE);
		MfgAreaLen -= (MfgTagLen + MFG_ENTRY_SIZE);
	}
}

/*
 * Look for "BUS".. Data is not Null terminated.
 * PHBID of 0xFF indicates PHB was not found in VPD Data.
 */
static int __init iSeries_Parse_PhbId(u8 *AreaPtr, int AreaLength)
{
	u8 *PhbPtr = AreaPtr;
	int DataLen = AreaLength;
	char PhbId = 0xFF;

	while (DataLen > 0) {
		if ((*PhbPtr == 'B') && (*(PhbPtr + 1) == 'U')
				&& (*(PhbPtr + 2) == 'S')) {
			PhbPtr += 3;
			while (*PhbPtr == ' ')
				++PhbPtr;
			PhbId = (*PhbPtr & 0x0F);
			break;
		}
		++PhbPtr;
		--DataLen;
	}
	return PhbId;
}

/*
 * Parse out the VPD Areas
 */
static void __init iSeries_Parse_Vpd(u8 *VpdData, int VpdDataLen,
		HvAgentId agent, u8 *frame, char card[4])
{
	u8 *TagPtr = VpdData;
	int DataLen = VpdDataLen - 3;
	u8 PhbId;

	while ((*TagPtr != VpdEndOfAreaTag) && (DataLen > 0)) {
		int AreaLen = *(TagPtr + 1) + (*(TagPtr + 2) * 256);
		u8 *AreaData  = TagPtr + 3;

		if (*TagPtr == VpdIdStringTag)
			PhbId = iSeries_Parse_PhbId(AreaData, AreaLen);
		else if (*TagPtr == VpdVendorAreaTag)
			iSeries_Parse_MfgArea(AreaData, AreaLen,
					agent, &PhbId, frame, card);
		/* Point to next Area. */
		TagPtr  = AreaData + AreaLen;
		DataLen -= AreaLen;
	}
}

static void __init iSeries_Get_Location_Code(u16 bus, HvAgentId agent,
		u8 *frame, char card[4])
{
	int BusVpdLen = 0;
	u8 *BusVpdPtr = kmalloc(BUS_VPDSIZE, GFP_KERNEL);

	if (BusVpdPtr == NULL) {
		printk("PCI: Bus VPD Buffer allocation failure.\n");
		return;
	}
	BusVpdLen = HvCallPci_getBusVpd(bus, iseries_hv_addr(BusVpdPtr),
					BUS_VPDSIZE);
	if (BusVpdLen == 0) {
		printk("PCI: Bus VPD Buffer zero length.\n");
		goto out_free;
	}
	/* printk("PCI: BusVpdPtr: %p, %d\n",BusVpdPtr, BusVpdLen); */
	/* Make sure this is what I think it is */
	if (*BusVpdPtr != VpdIdStringTag) {	/* 0x82 */
		printk("PCI: Bus VPD Buffer missing starting tag.\n");
		goto out_free;
	}
	iSeries_Parse_Vpd(BusVpdPtr, BusVpdLen, agent, frame, card);
out_free:
	kfree(BusVpdPtr);
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
void __init iSeries_Device_Information(struct pci_dev *PciDev, int count)
{
	struct device_node *DevNode = PciDev->sysdata;
	struct pci_dn *pdn;
	u16 bus;
	u8 frame;
	char card[4];
	HvSubBusNumber subbus;
	HvAgentId agent;

	if (DevNode == NULL) {
		printk("%d. PCI: iSeries_Device_Information DevNode is NULL\n",
				count);
		return;
	}

	pdn = PCI_DN(DevNode);
	bus = pdn->busno;
	subbus = pdn->bussubno;
	agent = ISERIES_PCI_AGENTID(ISERIES_GET_DEVICE_FROM_SUBBUS(subbus),
			ISERIES_GET_FUNCTION_FROM_SUBBUS(subbus));
	iSeries_Get_Location_Code(bus, agent, &frame, card);

	printk("%d. PCI: Bus%3d, Device%3d, Vendor %04X Frame%3d, Card %4s  ",
			count, bus, PCI_SLOT(PciDev->devfn), PciDev->vendor,
			frame, card);
	printk("0x%04X\n", (int)(PciDev->class >> 8));
}
