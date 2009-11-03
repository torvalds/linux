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

#include "ChannelMgmt.h"

/* The format must be the same as struct vmdata_gpa_direct */
struct VMBUS_CHANNEL_PACKET_PAGE_BUFFER {
	u16 Type;
	u16 DataOffset8;
	u16 Length8;
	u16 Flags;
	u64 TransactionId;
	u32 Reserved;
	u32 RangeCount;
	struct hv_page_buffer Range[MAX_PAGE_BUFFER_COUNT];
} __attribute__((packed));

/* The format must be the same as struct vmdata_gpa_direct */
struct VMBUS_CHANNEL_PACKET_MULITPAGE_BUFFER {
	u16 Type;
	u16 DataOffset8;
	u16 Length8;
	u16 Flags;
	u64 TransactionId;
	u32 Reserved;
	u32 RangeCount;		/* Always 1 in this case */
	struct hv_multipage_buffer Range;
} __attribute__((packed));


extern int VmbusChannelOpen(struct vmbus_channel *channel,
			    u32 SendRingBufferSize,
			    u32 RecvRingBufferSize,
			    void *UserData,
			    u32 UserDataLen,
			    void(*OnChannelCallback)(void *context),
			    void *Context);

extern void VmbusChannelClose(struct vmbus_channel *channel);

extern int VmbusChannelSendPacket(struct vmbus_channel *channel,
				  const void *Buffer,
				  u32 BufferLen,
				  u64 RequestId,
				  enum vmbus_packet_type Type,
				  u32 Flags);

extern int VmbusChannelSendPacketPageBuffer(struct vmbus_channel *channel,
					    struct hv_page_buffer PageBuffers[],
					    u32 PageCount,
					    void *Buffer,
					    u32 BufferLen,
					    u64 RequestId);

extern int VmbusChannelSendPacketMultiPageBuffer(struct vmbus_channel *channel,
					struct hv_multipage_buffer *mpb,
					void *Buffer,
					u32 BufferLen,
					u64 RequestId);

extern int VmbusChannelEstablishGpadl(struct vmbus_channel *channel,
				      void *Kbuffer,
				      u32 Size,
				      u32 *GpadlHandle);

extern int VmbusChannelTeardownGpadl(struct vmbus_channel *channel,
				     u32 GpadlHandle);

extern int VmbusChannelRecvPacket(struct vmbus_channel *channel,
				  void *Buffer,
				  u32 BufferLen,
				  u32 *BufferActualLen,
				  u64 *RequestId);

extern int VmbusChannelRecvPacketRaw(struct vmbus_channel *channel,
				     void *Buffer,
				     u32 BufferLen,
				     u32 *BufferActualLen,
				     u64 *RequestId);

extern void VmbusChannelOnChannelEvent(struct vmbus_channel *channel);

extern void VmbusChannelGetDebugInfo(struct vmbus_channel *channel,
				     struct vmbus_channel_debug_info *debug);

extern void VmbusChannelOnTimer(unsigned long data);

#endif /* _CHANNEL_H_ */
