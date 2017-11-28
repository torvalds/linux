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
#ifndef __DRV_TYPES_XP_H__
#define __DRV_TYPES_XP_H__

#include <drv_conf.h>
#include <osdep_service.h>



#define MAX_MCAST_LIST_NUM					32



/* for ioctl */
#define MAKE_DRIVER_VERSION(_MainVer, _MinorVer)	((((u32)(_MainVer))<<16)+_MinorVer)

#define NIC_HEADER_SIZE				14			/* !< can be moved to typedef.h */
#define NIC_MAX_PACKET_SIZE			1514		/* !< can be moved to typedef.h */
#define NIC_MAX_SEND_PACKETS			10		/* max number of send packets the MiniportSendPackets function can accept, can be moved to typedef.h */
#define NIC_VENDOR_DRIVER_VERSION       MAKE_DRIVER_VERSION(0, 001)	/* !< can be moved to typedef.h */
#define NIC_MAX_PACKET_SIZE			1514		/* !< can be moved to typedef.h */


#undef ON_VISTA
/* added by Jackson */
#ifndef ON_VISTA
	/*
	* Bus driver versions
	*   */

	#define SDBUS_DRIVER_VERSION_1          0x100
	#define SDBUS_DRIVER_VERSION_2          0x200

	#define    SDP_FUNCTION_TYPE	4
	#define    SDP_BUS_DRIVER_VERSION 5
	#define    SDP_BUS_WIDTH 6
	#define    SDP_BUS_CLOCK 7
	#define    SDP_BUS_INTERFACE_CONTROL 8
	#define    SDP_HOST_BLOCK_LENGTH 9
	#define    SDP_FUNCTION_BLOCK_LENGTH 10
	#define    SDP_FN0_BLOCK_LENGTH 11
	#define    SDP_FUNCTION_INT_ENABLE 12
#endif


typedef struct _MP_REG_ENTRY {

	NDIS_STRING		RegName;	/* variable name text */
	BOOLEAN			bRequired;	/* 1->required, 0->optional */

	u8			Type;		/* NdisParameterInteger/NdisParameterHexInteger/NdisParameterStringle/NdisParameterMultiString */
	uint			FieldOffset;	/* offset to MP_ADAPTER field */
	uint			FieldSize;	/* size (in bytes) of the field */

#ifdef UNDER_AMD64
	u64			Default;
#else
	u32			Default;		/* default value to use */
#endif

	u32			Min;			/* minimum value allowed */
	u32			Max;		/* maximum value allowed */
} MP_REG_ENTRY, *PMP_REG_ENTRY;


typedef struct _OCTET_STRING {
	u8      *Octet;
	u16      Length;
} OCTET_STRING, *POCTET_STRING;





#endif
