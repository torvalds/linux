/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
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
 *************************************************************************/


#ifndef __RTMP_USB_H__
#define __RTMP_USB_H__

#include "rtusb_io.h"

extern UCHAR EpToQueue[6];

#define RXBULKAGGRE_SIZE			12
#define MAX_TXBULK_LIMIT			(LOCAL_TXBUF_SIZE*(BULKAGGRE_SIZE-1))
#define MAX_TXBULK_SIZE			(LOCAL_TXBUF_SIZE*BULKAGGRE_SIZE)
#define MAX_RXBULK_SIZE			(LOCAL_TXBUF_SIZE*RXBULKAGGRE_SIZE)
#define MAX_MLME_HANDLER_MEMORY 20

/*Power saving */
#define PowerWakeCID		3
#define CID0MASK		0x000000ff
#define CID1MASK		0x0000ff00
#define CID2MASK		0x00ff0000
#define CID3MASK		0xff000000


/* Flags for Bulkflags control for bulk out data */
/* */
#define	fRTUSB_BULK_OUT_DATA_NULL				0x00000001
#define	fRTUSB_BULK_OUT_RTS						0x00000002
#define	fRTUSB_BULK_OUT_MLME					0x00000004

#define	fRTUSB_BULK_OUT_PSPOLL					0x00000010
#define	fRTUSB_BULK_OUT_DATA_FRAG				0x00000020
#define	fRTUSB_BULK_OUT_DATA_FRAG_2				0x00000040
#define	fRTUSB_BULK_OUT_DATA_FRAG_3				0x00000080
#define	fRTUSB_BULK_OUT_DATA_FRAG_4				0x00000100

#define	fRTUSB_BULK_OUT_DATA_NORMAL				0x00010000
#define	fRTUSB_BULK_OUT_DATA_NORMAL_2			0x00020000
#define	fRTUSB_BULK_OUT_DATA_NORMAL_3			0x00040000
#define	fRTUSB_BULK_OUT_DATA_NORMAL_4			0x00080000

/* TODO:move to ./ate/include/iface/ate_usb.h */
#ifdef RALINK_ATE
#define	fRTUSB_BULK_OUT_DATA_ATE				0x00100000
#endif /* RALINK_ATE */


#define FREE_HTTX_RING(_pCookie, _pipeId, _txContext)			\
{										\
	if ((_txContext)->ENextBulkOutPosition == (_txContext)->CurWritePosition)	\
	{																	\
		(_txContext)->bRingEmpty = TRUE;			\
	}																	\
	/*NdisInterlockedDecrement(&(_p)->TxCount); */\
}

#define NT_SUCCESS(status)			(((status) >=0) ? (TRUE):(FALSE))




#define PIRP		PVOID
/*#define NDIS_OID	UINT */
#ifndef USB_ST_NOERROR
#define USB_ST_NOERROR     0
#endif


/* vendor-specific control operations */
#define CONTROL_TIMEOUT_JIFFIES ( (300 * OS_HZ) / 1000)
/*#define UNLINK_TIMEOUT_MS		3 // os abl move */


#define DEVICE_VENDOR_REQUEST_OUT       0x40
#define DEVICE_VENDOR_REQUEST_IN        0xc0
/*#define INTERFACE_VENDOR_REQUEST_OUT    0x41 */
/*#define INTERFACE_VENDOR_REQUEST_IN     0xc1 */
#define BULKOUT_MGMT_RESET_FLAG		0x80

#define RTUSB_SET_BULK_FLAG(_M, _F)	((_M)->BulkFlags |= (_F))
#define RTUSB_CLEAR_BULK_FLAG(_M, _F)	((_M)->BulkFlags &= ~(_F))
#define RTUSB_TEST_BULK_FLAG(_M, _F)	(((_M)->BulkFlags & (_F)) != 0)

#endif /* __RTMP_USB_H__ */
