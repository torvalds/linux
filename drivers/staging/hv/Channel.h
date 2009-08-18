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

/* The format must be the same as VMDATA_GPA_DIRECT */
struct VMBUS_CHANNEL_PACKET_PAGE_BUFFER {
	u16 Type;
	u16 DataOffset8;
	u16 Length8;
	u16 Flags;
	u64 TransactionId;
	u32 Reserved;
	u32 RangeCount;
	PAGE_BUFFER Range[MAX_PAGE_BUFFER_COUNT];
} __attribute__((packed));

/* The format must be the same as VMDATA_GPA_DIRECT */
struct VMBUS_CHANNEL_PACKET_MULITPAGE_BUFFER {
	u16 Type;
	u16 DataOffset8;
	u16 Length8;
	u16 Flags;
	u64 TransactionId;
	u32 Reserved;
	u32 RangeCount;		/* Always 1 in this case */
	MULTIPAGE_BUFFER Range;
} __attribute__((packed));


extern int VmbusChannelOpen(VMBUS_CHANNEL *Channel,
			    u32 SendRingBufferSize,
			    u32 RecvRingBufferSize,
			    void *UserData,
			    u32 UserDataLen,
			    PFN_CHANNEL_CALLBACK pfnOnChannelCallback,
			    void *Context);

extern void VmbusChannelClose(VMBUS_CHANNEL *Channel);

extern int VmbusChannelSendPacket(VMBUS_CHANNEL *Channel,
				  const void *Buffer,
				  u32 BufferLen,
				  u64 RequestId,
				  VMBUS_PACKET_TYPE Type,
				  u32 Flags);

extern int VmbusChannelSendPacketPageBuffer(VMBUS_CHANNEL *Channel,
					    PAGE_BUFFER PageBuffers[],
					    u32 PageCount,
					    void *Buffer,
					    u32 BufferLen,
					    u64 RequestId);

extern int VmbusChannelSendPacketMultiPageBuffer(VMBUS_CHANNEL *Channel,
					MULTIPAGE_BUFFER *MultiPageBuffer,
					void *Buffer,
					u32 BufferLen,
					u64 RequestId);

extern int VmbusChannelEstablishGpadl(VMBUS_CHANNEL *Channel,
				      void *Kbuffer,
				      u32 Size,
				      u32 *GpadlHandle);

extern int VmbusChannelTeardownGpadl(VMBUS_CHANNEL *Channel,
				     u32 GpadlHandle);

extern int VmbusChannelRecvPacket(VMBUS_CHANNEL *Channel,
				  void *Buffer,
				  u32 BufferLen,
				  u32 *BufferActualLen,
				  u64 *RequestId);

extern int VmbusChannelRecvPacketRaw(VMBUS_CHANNEL *Channel,
				     void *Buffer,
				     u32 BufferLen,
				     u32 *BufferActualLen,
				     u64 *RequestId);

extern void VmbusChannelOnChannelEvent(VMBUS_CHANNEL *Channel);

extern void VmbusChannelGetDebugInfo(VMBUS_CHANNEL *Channel,
				     VMBUS_CHANNEL_DEBUG_INFO *DebugInfo);

extern void VmbusChannelOnTimer(unsigned long data);

#endif /* _CHANNEL_H_ */
