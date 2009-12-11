/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2007, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************
 */

#ifndef __RT2860_H__
#define __RT2860_H__

#include "mac_pci.h"

#ifndef RTMP_PCI_SUPPORT
#error "For RT2860, you should define the compile flag -DRTMP_PCI_SUPPORT"
#endif

#ifndef RTMP_MAC_PCI
#error "For RT2880, you should define the compile flag -DRTMP_MAC_PCI"
#endif

/* */
/* Device ID & Vendor ID, these values should match EEPROM value */
/* */
#define NIC2860_PCI_DEVICE_ID	0x0601
#define NIC2860_PCIe_DEVICE_ID	0x0681
#define NIC2760_PCI_DEVICE_ID	0x0701	/* 1T/2R Cardbus ??? */
#define NIC2790_PCIe_DEVICE_ID  0x0781	/* 1T/2R miniCard */

#define VEN_AWT_PCIe_DEVICE_ID	0x1059
#define VEN_AWT_PCI_VENDOR_ID		0x1A3B

#define EDIMAX_PCI_VENDOR_ID		0x1432

#endif /*__RT2860_H__ // */
