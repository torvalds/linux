#define PCIFR(...)
/************************************************************************/
/* File iSeries_pci_reset.c created by Allan Trautman on Mar 21 2001.   */
/************************************************************************/
/* This code supports the pci interface on the IBM iSeries systems.     */
/* Copyright (C) 20yy  <Allan H Trautman> <IBM Corp>                    */
/*                                                                      */
/* This program is free software; you can redistribute it and/or modify */
/* it under the terms of the GNU General Public License as published by */
/* the Free Software Foundation; either version 2 of the License, or    */
/* (at your option) any later version.                                  */
/*                                                                      */
/* This program is distributed in the hope that it will be useful,      */ 
/* but WITHOUT ANY WARRANTY; without even the implied warranty of       */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the        */
/* GNU General Public License for more details.                         */
/*                                                                      */
/* You should have received a copy of the GNU General Public License    */ 
/* along with this program; if not, write to the:                       */
/* Free Software Foundation, Inc.,                                      */ 
/* 59 Temple Place, Suite 330,                                          */ 
/* Boston, MA  02111-1307  USA                                          */
/************************************************************************/
/* Change Activity:                                                     */
/*   Created, March 20, 2001                                            */
/*   April 30, 2001, Added return codes on functions.                   */
/*   September 10, 2001, Ported to ppc64.                               */
/* End Change Activity                                                  */
/************************************************************************/
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/irq.h>
#include <linux/delay.h>

#include <asm/io.h>
#include <asm/iSeries/HvCallPci.h>
#include <asm/iSeries/HvTypes.h>
#include <asm/iSeries/mf.h>
#include <asm/pci.h>

#include <asm/iSeries/iSeries_pci.h>
#include "pci.h"

/*
 * Interface to toggle the reset line
 * Time is in .1 seconds, need for seconds.
 */
int iSeries_Device_ToggleReset(struct pci_dev *PciDev, int AssertTime,
		int DelayTime)
{
	unsigned int AssertDelay, WaitDelay;
	struct iSeries_Device_Node *DeviceNode =
		(struct iSeries_Device_Node *)PciDev->sysdata;

 	if (DeviceNode == NULL) { 
		printk("PCI: Pci Reset Failed, Device Node not found for pci_dev %p\n",
				PciDev);
		return -1;
	}
	/*
	 * Set defaults, Assert is .5 second, Wait is 3 seconds.
	 */
	if (AssertTime == 0)
		AssertDelay = 500;
	else
		AssertDelay = AssertTime * 100;

	if (DelayTime == 0)
		WaitDelay = 3000;
	else
		WaitDelay = DelayTime * 100;

	/*
	 * Assert reset
	 */
	DeviceNode->ReturnCode = HvCallPci_setSlotReset(ISERIES_BUS(DeviceNode),
			0x00, DeviceNode->AgentId, 1);
	if (DeviceNode->ReturnCode == 0) {
		msleep(AssertDelay);			/* Sleep for the time */
		DeviceNode->ReturnCode =
			HvCallPci_setSlotReset(ISERIES_BUS(DeviceNode),
					0x00, DeviceNode->AgentId, 0);

		/*
   		 * Wait for device to reset
		 */
		msleep(WaitDelay);
	}
	if (DeviceNode->ReturnCode == 0)
		PCIFR("Slot 0x%04X.%02 Reset\n", ISERIES_BUS(DeviceNode),
				DeviceNode->AgentId);
	else {
		printk("PCI: Slot 0x%04X.%02X Reset Failed, RCode: %04X\n",
				ISERIES_BUS(DeviceNode), DeviceNode->AgentId,
				DeviceNode->ReturnCode);
		PCIFR("Slot 0x%04X.%02X Reset Failed, RCode: %04X\n",
				ISERIES_BUS(DeviceNode), DeviceNode->AgentId,
				DeviceNode->ReturnCode);
	}
	return DeviceNode->ReturnCode;
}
EXPORT_SYMBOL(iSeries_Device_ToggleReset);
