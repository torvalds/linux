/*
 *
 * Copyright (c) 2009, Microsoft Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * Authors:
 *   Haiyang Zhang <haiyangz@microsoft.com>
 *   Hank Janssen  <hjanssen@microsoft.com>
 *
 */


#ifndef _CHANNEL_H_
#define _CHANNEL_H_

#include "include/osd.h"
#include "ChannelMgmt.h"

#pragma pack(push,1)


// The format must be the same as VMDATA_GPA_DIRECT
typedef struct _VMBUS_CHANNEL_PACKET_PAGE_BUFFER {
    UINT16				Type;
    UINT16				DataOffset8;
    UINT16				Length8;
    UINT16				Flags;
    UINT64				TransactionId;
	UINT32				Reserved;
	UINT32				RangeCount;
    PAGE_BUFFER			Range[MAX_PAGE_BUFFER_COUNT];
} VMBUS_CHANNEL_PACKET_PAGE_BUFFER;


// The format must be the same as VMDATA_GPA_DIRECT
typedef struct _VMBUS_CHANNEL_PACKET_MULITPAGE_BUFFER {
    UINT16				Type;
    UINT16				DataOffset8;
    UINT16				Length8;
    UINT16				Flags;
    UINT64				TransactionId;
	UINT32				Reserved;
	UINT32				RangeCount;		// Always 1 in this case
	MULTIPAGE_BUFFER	Range;
} VMBUS_CHANNEL_PACKET_MULITPAGE_BUFFER;

#pragma pack(pop)

//
// Routines
//

static int
VmbusChannelOpen(
	VMBUS_CHANNEL			*Channel,
	UINT32					SendRingBufferSize,
	UINT32					RecvRingBufferSize,
	PVOID					UserData,
	UINT32					UserDataLen,
	PFN_CHANNEL_CALLBACK	pfnOnChannelCallback,
	PVOID					Context
	);

static void
VmbusChannelClose(
	VMBUS_CHANNEL		*Channel
	);

static int
VmbusChannelSendPacket(
	VMBUS_CHANNEL		*Channel,
	const PVOID			Buffer,
	UINT32				BufferLen,
	UINT64				RequestId,
	VMBUS_PACKET_TYPE	Type,
	UINT32				Flags
);

static int
VmbusChannelSendPacketPageBuffer(
	VMBUS_CHANNEL		*Channel,
	PAGE_BUFFER			PageBuffers[],
	UINT32				PageCount,
	PVOID				Buffer,
	UINT32				BufferLen,
	UINT64				RequestId
	);

static int
VmbusChannelSendPacketMultiPageBuffer(
	VMBUS_CHANNEL		*Channel,
	MULTIPAGE_BUFFER	*MultiPageBuffer,
	PVOID				Buffer,
	UINT32				BufferLen,
	UINT64				RequestId
);

static int
VmbusChannelEstablishGpadl(
	VMBUS_CHANNEL		*Channel,
	PVOID				Kbuffer,	// from kmalloc()
	UINT32				Size,		// page-size multiple
	UINT32				*GpadlHandle
	);

static int
VmbusChannelTeardownGpadl(
	VMBUS_CHANNEL	*Channel,
	UINT32			GpadlHandle
	);

static int
VmbusChannelRecvPacket(
	VMBUS_CHANNEL		*Channel,
	PVOID				Buffer,
	UINT32				BufferLen,
	UINT32*				BufferActualLen,
	UINT64*				RequestId
	);

static int
VmbusChannelRecvPacketRaw(
	VMBUS_CHANNEL		*Channel,
	PVOID				Buffer,
	UINT32				BufferLen,
	UINT32*				BufferActualLen,
	UINT64*				RequestId
	);

static void
VmbusChannelOnChannelEvent(
	VMBUS_CHANNEL		*Channel
	);

static void
VmbusChannelGetDebugInfo(
	VMBUS_CHANNEL				*Channel,
	VMBUS_CHANNEL_DEBUG_INFO	*DebugInfo
	);

static void
VmbusChannelOnTimer(
	void		*Context
	);
#endif //_CHANNEL_H_
