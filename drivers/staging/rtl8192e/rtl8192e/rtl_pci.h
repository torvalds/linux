/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Based on the r8180 driver, which is:
 * Copyright 2004-2005 Andrea Merello <andrea.merello@gmail.com>, et al.
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 ******************************************************************************/
#ifndef _RTL_PCI_H
#define _RTL_PCI_H

#include <linux/types.h>
#include <linux/pci.h>

struct mp_adapter {
	u8		LinkCtrlReg;

	u8		BusNumber;
	u8		DevNumber;
	u8		FuncNumber;

	u8		PciBridgeBusNum;
	u8		PciBridgeDevNum;
	u8		PciBridgeFuncNum;
	u8		PciBridgeVendor;
	u16		PciBridgeVendorId;
	u16		PciBridgeDeviceId;
	u8		PciBridgePCIeHdrOffset;
	u8		PciBridgeLinkCtrlReg;
};

struct net_device;
bool rtl8192_pci_findadapter(struct pci_dev *pdev, struct net_device *dev);

#endif
