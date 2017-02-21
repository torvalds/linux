/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *                                        
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef __DRV_TYPES_CE_H__
#define __DRV_TYPES_CE_H__

#include <drv_conf.h>
#include <osdep_service.h>

#include <Sdcardddk.h>

#define MAX_ACTIVE_REG_PATH 256

#define MAX_MCAST_LIST_NUM					32



//for ioctl
#define MAKE_DRIVER_VERSION(_MainVer,_MinorVer)	((((u32)(_MainVer))<<16)+_MinorVer)

#define NIC_HEADER_SIZE				14			//!< can be moved to typedef.h
#define NIC_MAX_PACKET_SIZE			1514		//!< can be moved to typedef.h
#define NIC_MAX_SEND_PACKETS			10		// max number of send packets the MiniportSendPackets function can accept, can be moved to typedef.h
#define NIC_VENDOR_DRIVER_VERSION       MAKE_DRIVER_VERSION(0,001)	//!< can be moved to typedef.h
#define NIC_MAX_PACKET_SIZE			1514		//!< can be moved to typedef.h

typedef struct _MP_REG_ENTRY
{

	NDIS_STRING		RegName;	// variable name text
	BOOLEAN			bRequired;	// 1 -> required, 0 -> optional

	u8			Type;		// NdisParameterInteger/NdisParameterHexInteger/NdisParameterStringle/NdisParameterMultiString
	uint			FieldOffset;	// offset to MP_ADAPTER field
	uint			FieldSize;	// size (in bytes) of the field
	
#ifdef UNDER_AMD64
	u64			Default;
#else
	u32			Default;		// default value to use
#endif

	u32			Min;			// minimum value allowed
	u32			Max;		// maximum value allowed
} MP_REG_ENTRY, *PMP_REG_ENTRY;

#ifdef CONFIG_USB_HCI
typedef struct _USB_EXTENSION {
    LPCUSB_FUNCS    _lpUsbFuncs;
	USB_HANDLE	    _hDevice;
    PVOID 		    pAdapter;

#if 0
	USB_ENDPOINT_DESCRIPTOR		_endpACLIn;
    USB_ENDPOINT_DESCRIPTOR		_endpACLOutHigh;
	USB_ENDPOINT_DESCRIPTOR		_endpACLOutNormal;

	USB_PIPE        pPipeIn;
    USB_PIPE        pPipeOutNormal;
    USB_PIPE        pPipeOutHigh;
#endif

} USB_EXTENSION, *PUSB_EXTENSION;
#endif


typedef struct _OCTET_STRING{
	u8      *Octet;
	u16      Length;
} OCTET_STRING, *POCTET_STRING;





#endif

