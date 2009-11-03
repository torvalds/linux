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

    Module Name:
	rt_iface.h

    Abstract:

    Revision History:
    Who          When          What
    ---------    ----------    ----------------------------------------------
 */

#ifndef __RTMP_IFACE_H__
#define __RTMP_IFACE_H__

#ifdef RTMP_PCI_SUPPORT
#include "rtmp_pci.h"
#endif // RTMP_PCI_SUPPORT //


typedef struct _INF_PCI_CONFIG_
{
	unsigned long	CSRBaseAddress;     // PCI MMIO Base Address, all access will use
	unsigned int	irq_num;
}INF_PCI_CONFIG;


typedef struct _INF_USB_CONFIG_
{
	UINT8                BulkInEpAddr;		// bulk-in endpoint address
	UINT8                BulkOutEpAddr[6];	// bulk-out endpoint address
}INF_USB_CONFIG;


typedef struct _INF_RBUS_CONFIG_
{
	unsigned long		csr_addr;
	unsigned int		irq;
}INF_RBUS_CONFIG;


typedef enum _RTMP_INF_TYPE_
{
	RTMP_DEV_INF_UNKNOWN = 0,
	RTMP_DEV_INF_PCI = 1,
	RTMP_DEV_INF_USB = 2,
	RTMP_DEV_INF_RBUS = 4,
}RTMP_INF_TYPE;


typedef union _RTMP_INF_CONFIG_{
	struct _INF_PCI_CONFIG_			pciConfig;
	struct _INF_USB_CONFIG_			usbConfig;
	struct _INF_RBUS_CONFIG_		rbusConfig;
}RTMP_INF_CONFIG;

#endif // __RTMP_IFACE_H__ //
