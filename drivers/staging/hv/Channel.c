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

#include <linux/kernel.h>
#include <linux/mm.h>
#include "osd.h"
#include "include/logging.h"

#include "VmbusPrivate.h"

/* Internal routines */
static int VmbusChannelCreateGpadlHeader(
	void *					Kbuffer,	/* must be phys and virt contiguous */
	u32					Size,		/* page-size multiple */
	struct vmbus_channel_msginfo **msgInfo,
	u32					*MessageCount
	);
static void DumpVmbusChannel(struct vmbus_channel *channel);
static void VmbusChannelSetEvent(struct vmbus_channel *channel);


#if 0
static void
DumpMonitorPage(
	HV_MONITOR_PAGE *MonitorPage
	)
{
	int i=0;
	int j=0;

	DPRINT_DBG(VMBUS, "monitorPage - %p, trigger state - %d", MonitorPage, MonitorPage->TriggerState);

	for (i=0; i<4; i++)
	{
		DPRINT_DBG(VMBUS, "trigger group (%d) - %llx", i, MonitorPage->TriggerGroup[i].AsUINT64);
	}

	for (i=0; i<4; i++)
	{
		for (j=0; j<32; j++)
		{
			DPRINT_DBG(VMBUS, "latency (%d)(%d) - %llx", i, j, MonitorPage->Latency[i][j]);
		}
	}
	for (i=0; i<4; i++)
	{
		for (j=0; j<32; j++)
		{
			DPRINT_DBG(VMBUS, "param-conn id (%d)(%d) - %d", i, j, MonitorPage->Parameter[i][j].ConnectionId.Asu32);
			DPRINT_DBG(VMBUS, "param-flag (%d)(%d) - %d", i, j, MonitorPage->Parameter[i][j].FlagNumber);

		}
	}
}
#endif

/*++

Name:
	VmbusChannelSetEvent()

Description:
	Trigger an event notification on the specified channel.

--*/
static void VmbusChannelSetEvent(struct vmbus_channel *Channel)
{
	HV_MONITOR_PAGE *monitorPage;

	DPRINT_ENTER(VMBUS);

	if (Channel->OfferMsg.MonitorAllocated)
	{
		/* Each u32 represents 32 channels */
		set_bit(Channel->OfferMsg.ChildRelId & 31,
			(unsigned long *) gVmbusConnection.SendInterruptPage +
			(Channel->OfferMsg.ChildRelId >> 5) );

		monitorPage = (HV_MONITOR_PAGE*)gVmbusConnection.MonitorPages;
		monitorPage++; /* Get the child to parent monitor page */

		set_bit(Channel->MonitorBit,
			(unsigned long *) &monitorPage->TriggerGroup[Channel->MonitorGroup].Pending);

	}
	else
	{
		VmbusSetEvent(Channel->OfferMsg.ChildRelId);
	}

	DPRINT_EXIT(VMBUS);
}

#if 0
static void VmbusChannelClearEvent(struct vmbus_channel *channel)
{
	HV_MONITOR_PAGE *monitorPage;

	DPRINT_ENTER(VMBUS);

	if (Channel->OfferMsg.MonitorAllocated)
	{
		/* Each u32 represents 32 channels */
		clear_bit(Channel->OfferMsg.ChildRelId & 31,
			  (unsigned long *) gVmbusConnection.SendInterruptPage + (Channel->OfferMsg.ChildRelId >> 5));

		monitorPage = (HV_MONITOR_PAGE*)gVmbusConnection.MonitorPages;
		monitorPage++; /* Get the child to parent monitor page */

		clear_bit(Channel->MonitorBit,
			  (unsigned long *) &monitorPage->TriggerGroup[Channel->MonitorGroup].Pending);
	}

	DPRINT_EXIT(VMBUS);
}

#endif
/*++;

Name:
	VmbusChannelGetDebugInfo()

Description:
	Retrieve various channel debug info

--*/
void VmbusChannelGetDebugInfo(struct vmbus_channel *Channel,
			      struct vmbus_channel_debug_info *DebugInfo)
{
	HV_MONITOR_PAGE *monitorPage;
    u8 monitorGroup    = (u8)Channel->OfferMsg.MonitorId / 32;
    u8 monitorOffset   = (u8)Channel->OfferMsg.MonitorId % 32;
	/* u32 monitorBit	= 1 << monitorOffset; */

	DebugInfo->RelId = Channel->OfferMsg.ChildRelId;
	DebugInfo->State = Channel->State;
	memcpy(&DebugInfo->InterfaceType, &Channel->OfferMsg.Offer.InterfaceType, sizeof(struct hv_guid));
	memcpy(&DebugInfo->InterfaceInstance, &Channel->OfferMsg.Offer.InterfaceInstance, sizeof(struct hv_guid));

	monitorPage = (HV_MONITOR_PAGE*)gVmbusConnection.MonitorPages;

	DebugInfo->MonitorId = Channel->OfferMsg.MonitorId;

	DebugInfo->ServerMonitorPending = monitorPage->TriggerGroup[monitorGroup].Pending;
	DebugInfo->ServerMonitorLatency = monitorPage->Latency[monitorGroup][ monitorOffset];
	DebugInfo->ServerMonitorConnectionId = monitorPage->Parameter[monitorGroup][ monitorOffset].ConnectionId.u.Id;

	monitorPage++;

	DebugInfo->ClientMonitorPending = monitorPage->TriggerGroup[monitorGroup].Pending;
	DebugInfo->ClientMonitorLatency = monitorPage->Latency[monitorGroup][ monitorOffset];
	DebugInfo->ClientMonitorConnectionId = monitorPage->Parameter[monitorGroup][ monitorOffset].ConnectionId.u.Id;

	RingBufferGetDebugInfo(&Channel->Inbound, &DebugInfo->Inbound);
	RingBufferGetDebugInfo(&Channel->Outbound, &DebugInfo->Outbound);
}


/*++;

Name:
	VmbusChannelOpen()

Description:
	Open the specified channel.

--*/
int VmbusChannelOpen(struct vmbus_channel *NewChannel,
	u32					SendRingBufferSize,
	u32					RecvRingBufferSize,
	void *					UserData,
	u32					UserDataLen,
	PFN_CHANNEL_CALLBACK	pfnOnChannelCallback,
	void *					Context
	)
{
	int ret=0;
	VMBUS_CHANNEL_OPEN_CHANNEL* openMsg;
	struct vmbus_channel_msginfo *openInfo;
	void *in, *out;
	unsigned long flags;

	DPRINT_ENTER(VMBUS);

	/* Aligned to page size */
	ASSERT(!(SendRingBufferSize & (PAGE_SIZE -1)));
	ASSERT(!(RecvRingBufferSize & (PAGE_SIZE -1)));

	NewChannel->OnChannelCallback = pfnOnChannelCallback;
	NewChannel->ChannelCallbackContext = Context;

	/* Allocate the ring buffer */
	out = osd_PageAlloc((SendRingBufferSize + RecvRingBufferSize) >> PAGE_SHIFT);
	/* out = kzalloc(sendRingBufferSize + recvRingBufferSize, GFP_KERNEL); */
	ASSERT(out);
	ASSERT(((unsigned long)out & (PAGE_SIZE-1)) == 0);

	in = (void*)((unsigned long)out + SendRingBufferSize);

	NewChannel->RingBufferPages = out;
	NewChannel->RingBufferPageCount = (SendRingBufferSize + RecvRingBufferSize) >> PAGE_SHIFT;

	RingBufferInit(&NewChannel->Outbound, out, SendRingBufferSize);

	RingBufferInit(&NewChannel->Inbound, in, RecvRingBufferSize);

	/* Establish the gpadl for the ring buffer */
	DPRINT_DBG(VMBUS, "Establishing ring buffer's gpadl for channel %p...", NewChannel);

	NewChannel->RingBufferGpadlHandle = 0;

	ret = VmbusChannelEstablishGpadl(NewChannel,
		NewChannel->Outbound.RingBuffer,
		SendRingBufferSize + RecvRingBufferSize,
		&NewChannel->RingBufferGpadlHandle);

	DPRINT_DBG(VMBUS, "channel %p <relid %d gpadl 0x%x send ring %p size %d recv ring %p size %d, downstreamoffset %d>",
		NewChannel,
		NewChannel->OfferMsg.ChildRelId,
		NewChannel->RingBufferGpadlHandle,
		NewChannel->Outbound.RingBuffer,
		NewChannel->Outbound.RingSize,
		NewChannel->Inbound.RingBuffer,
		NewChannel->Inbound.RingSize,
		SendRingBufferSize);

	/* Create and init the channel open message */
	openInfo = kmalloc(sizeof(*openInfo) + sizeof(VMBUS_CHANNEL_OPEN_CHANNEL), GFP_KERNEL);
	ASSERT(openInfo != NULL);

	openInfo->WaitEvent = osd_WaitEventCreate();

	openMsg = (VMBUS_CHANNEL_OPEN_CHANNEL*)openInfo->Msg;
	openMsg->Header.MessageType				= ChannelMessageOpenChannel;
	openMsg->OpenId							= NewChannel->OfferMsg.ChildRelId; /* FIXME */
    openMsg->ChildRelId						= NewChannel->OfferMsg.ChildRelId;
    openMsg->RingBufferGpadlHandle			= NewChannel->RingBufferGpadlHandle;
    ASSERT(openMsg->RingBufferGpadlHandle);
    openMsg->DownstreamRingBufferPageOffset		= SendRingBufferSize >> PAGE_SHIFT;
    openMsg->ServerContextAreaGpadlHandle	= 0; /* TODO */

	ASSERT(UserDataLen <= MAX_USER_DEFINED_BYTES);
	if (UserDataLen)
	{
		memcpy(openMsg->UserData, UserData, UserDataLen);
	}

	spin_lock_irqsave(&gVmbusConnection.channelmsg_lock, flags);
	INSERT_TAIL_LIST(&gVmbusConnection.ChannelMsgList, &openInfo->MsgListEntry);
	spin_unlock_irqrestore(&gVmbusConnection.channelmsg_lock, flags);

	DPRINT_DBG(VMBUS, "Sending channel open msg...");

	ret = VmbusPostMessage(openMsg, sizeof(VMBUS_CHANNEL_OPEN_CHANNEL));
	if (ret != 0)
	{
		DPRINT_ERR(VMBUS, "unable to open channel - %d", ret);
		goto Cleanup;
	}

	/* FIXME: Need to time-out here */
	osd_WaitEventWait(openInfo->WaitEvent);

	if (openInfo->Response.OpenResult.Status == 0)
	{
		DPRINT_INFO(VMBUS, "channel <%p> open success!!", NewChannel);
	}
	else
	{
		DPRINT_INFO(VMBUS, "channel <%p> open failed - %d!!", NewChannel, openInfo->Response.OpenResult.Status);
	}

Cleanup:
	spin_lock_irqsave(&gVmbusConnection.channelmsg_lock, flags);
	REMOVE_ENTRY_LIST(&openInfo->MsgListEntry);
	spin_unlock_irqrestore(&gVmbusConnection.channelmsg_lock, flags);

	kfree(openInfo->WaitEvent);
	kfree(openInfo);

	DPRINT_EXIT(VMBUS);

	return 0;
}

/*++;

Name:
	DumpGpadlBody()

Description:
	Dump the gpadl body message to the console for debugging purposes.

--*/
static void DumpGpadlBody(
	VMBUS_CHANNEL_GPADL_BODY	*Gpadl,
	u32						Len)
{
	int i=0;
	int pfnCount=0;

	pfnCount = (Len - sizeof(VMBUS_CHANNEL_GPADL_BODY))/ sizeof(u64);
	DPRINT_DBG(VMBUS, "gpadl body - len %d pfn count %d", Len, pfnCount);

	for (i=0; i< pfnCount; i++)
	{
		DPRINT_DBG(VMBUS, "gpadl body  - %d) pfn %llu", i, Gpadl->Pfn[i]);
	}
}


/*++;

Name:
	DumpGpadlHeader()

Description:
	Dump the gpadl header message to the console for debugging purposes.

--*/
static void DumpGpadlHeader(
	VMBUS_CHANNEL_GPADL_HEADER	*Gpadl
	)
{
	int i=0,j=0;
	int pageCount=0;


	DPRINT_DBG(VMBUS, "gpadl header - relid %d, range count %d, range buflen %d",
				Gpadl->ChildRelId,
				Gpadl->RangeCount,
				Gpadl->RangeBufLen);
	for (i=0; i< Gpadl->RangeCount; i++)
	{
		pageCount = Gpadl->Range[i].ByteCount >> PAGE_SHIFT;
		pageCount = (pageCount > 26)? 26 : pageCount;

		DPRINT_DBG(VMBUS, "gpadl range %d - len %d offset %d page count %d",
			i, Gpadl->Range[i].ByteCount, Gpadl->Range[i].ByteOffset, pageCount);

		for (j=0; j< pageCount; j++)
		{
			DPRINT_DBG(VMBUS, "%d) pfn %llu", j, Gpadl->Range[i].PfnArray[j]);
		}
	}
}

/*++;

Name:
	VmbusChannelCreateGpadlHeader()

Description:
	Creates a gpadl for the specified buffer

--*/
static int
VmbusChannelCreateGpadlHeader(
	void *					Kbuffer,	/* from kmalloc() */
	u32					Size,		/* page-size multiple */
	struct vmbus_channel_msginfo **MsgInfo,
	u32					*MessageCount)
{
	int i;
	int pageCount;
    unsigned long long pfn;
	VMBUS_CHANNEL_GPADL_HEADER* gpaHeader;
	VMBUS_CHANNEL_GPADL_BODY* gpadlBody;
	struct vmbus_channel_msginfo *msgHeader;
	struct vmbus_channel_msginfo *msgBody;
	u32				msgSize;

	int pfnSum, pfnCount, pfnLeft, pfnCurr, pfnSize;

	/* ASSERT( (kbuffer & (PAGE_SIZE-1)) == 0); */
	ASSERT( (Size & (PAGE_SIZE-1)) == 0);

	pageCount = Size >> PAGE_SHIFT;
	pfn = virt_to_phys(Kbuffer) >> PAGE_SHIFT;

	/* do we need a gpadl body msg */
	pfnSize = MAX_SIZE_CHANNEL_MESSAGE - sizeof(VMBUS_CHANNEL_GPADL_HEADER) - sizeof(GPA_RANGE);
	pfnCount = pfnSize / sizeof(u64);

	if (pageCount > pfnCount) /* we need a gpadl body */
	{
		/* fill in the header */
		msgSize = sizeof(struct vmbus_channel_msginfo) + sizeof(VMBUS_CHANNEL_GPADL_HEADER) + sizeof(GPA_RANGE) + pfnCount*sizeof(u64);
		msgHeader =  kzalloc(msgSize, GFP_KERNEL);

		INITIALIZE_LIST_HEAD(&msgHeader->SubMsgList);
		msgHeader->MessageSize=msgSize;

		gpaHeader = (VMBUS_CHANNEL_GPADL_HEADER*)msgHeader->Msg;
		gpaHeader->RangeCount = 1;
		gpaHeader->RangeBufLen = sizeof(GPA_RANGE) + pageCount*sizeof(u64);
		gpaHeader->Range[0].ByteOffset = 0;
		gpaHeader->Range[0].ByteCount = Size;
		for (i=0; i<pfnCount; i++)
		{
			gpaHeader->Range[0].PfnArray[i] = pfn+i;
		}
		*MsgInfo = msgHeader;
		*MessageCount = 1;

		pfnSum = pfnCount;
		pfnLeft = pageCount - pfnCount;

		/* how many pfns can we fit */
		pfnSize = MAX_SIZE_CHANNEL_MESSAGE - sizeof(VMBUS_CHANNEL_GPADL_BODY);
		pfnCount = pfnSize / sizeof(u64);

		/* fill in the body */
		while (pfnLeft)
		{
			if (pfnLeft > pfnCount)
			{
				pfnCurr = pfnCount;
			}
			else
			{
				pfnCurr = pfnLeft;
			}

			msgSize = sizeof(struct vmbus_channel_msginfo) + sizeof(VMBUS_CHANNEL_GPADL_BODY) + pfnCurr*sizeof(u64);
			msgBody = kzalloc(msgSize, GFP_KERNEL);
			ASSERT(msgBody);
			msgBody->MessageSize = msgSize;
			(*MessageCount)++;
			gpadlBody = (VMBUS_CHANNEL_GPADL_BODY*)msgBody->Msg;

			/* FIXME: Gpadl is u32 and we are using a pointer which could be 64-bit */
			/* gpadlBody->Gpadl = kbuffer; */
			for (i=0; i<pfnCurr; i++)
			{
				gpadlBody->Pfn[i] = pfn + pfnSum + i;
			}

			/* add to msg header */
			INSERT_TAIL_LIST(&msgHeader->SubMsgList, &msgBody->MsgListEntry);
			pfnSum += pfnCurr;
			pfnLeft -= pfnCurr;
		}
	}
	else
	{
		/* everything fits in a header */
		msgSize = sizeof(struct vmbus_channel_msginfo) + sizeof(VMBUS_CHANNEL_GPADL_HEADER) + sizeof(GPA_RANGE) + pageCount*sizeof(u64);
		msgHeader = kzalloc(msgSize, GFP_KERNEL);
		msgHeader->MessageSize=msgSize;

		gpaHeader = (VMBUS_CHANNEL_GPADL_HEADER*)msgHeader->Msg;
		gpaHeader->RangeCount = 1;
		gpaHeader->RangeBufLen = sizeof(GPA_RANGE) + pageCount*sizeof(u64);
		gpaHeader->Range[0].ByteOffset = 0;
		gpaHeader->Range[0].ByteCount = Size;
		for (i=0; i<pageCount; i++)
		{
			gpaHeader->Range[0].PfnArray[i] = pfn+i;
		}

		*MsgInfo = msgHeader;
		*MessageCount = 1;
	}

	return 0;
}


/*++;

Name:
	VmbusChannelEstablishGpadl()

Description:
	Estabish a GPADL for the specified buffer

--*/
int VmbusChannelEstablishGpadl(struct vmbus_channel *Channel,
	void *			Kbuffer,	/* from kmalloc() */
	u32			Size,		/* page-size multiple */
	u32			*GpadlHandle
	)
{
	int ret=0;
	VMBUS_CHANNEL_GPADL_HEADER* gpadlMsg;
	VMBUS_CHANNEL_GPADL_BODY* gpadlBody;
	/* VMBUS_CHANNEL_GPADL_CREATED* gpadlCreated; */

	struct vmbus_channel_msginfo *msgInfo;
	struct vmbus_channel_msginfo *subMsgInfo;

	u32 msgCount;
	LIST_ENTRY* anchor;
	LIST_ENTRY* curr;
	u32 nextGpadlHandle;
	unsigned long flags;

	DPRINT_ENTER(VMBUS);

	nextGpadlHandle = atomic_read(&gVmbusConnection.NextGpadlHandle);
	atomic_inc(&gVmbusConnection.NextGpadlHandle);

	VmbusChannelCreateGpadlHeader(Kbuffer, Size, &msgInfo, &msgCount);
	ASSERT(msgInfo != NULL);
	ASSERT(msgCount >0);

	msgInfo->WaitEvent = osd_WaitEventCreate();
	gpadlMsg = (VMBUS_CHANNEL_GPADL_HEADER*)msgInfo->Msg;
	gpadlMsg->Header.MessageType = ChannelMessageGpadlHeader;
	gpadlMsg->ChildRelId = Channel->OfferMsg.ChildRelId;
	gpadlMsg->Gpadl = nextGpadlHandle;

	DumpGpadlHeader(gpadlMsg);

	spin_lock_irqsave(&gVmbusConnection.channelmsg_lock, flags);
	INSERT_TAIL_LIST(&gVmbusConnection.ChannelMsgList, &msgInfo->MsgListEntry);
	spin_unlock_irqrestore(&gVmbusConnection.channelmsg_lock, flags);

	DPRINT_DBG(VMBUS, "buffer %p, size %d msg cnt %d", Kbuffer, Size, msgCount);

	DPRINT_DBG(VMBUS, "Sending GPADL Header - len %zd", msgInfo->MessageSize - sizeof(*msgInfo));

	ret = VmbusPostMessage(gpadlMsg, msgInfo->MessageSize - sizeof(*msgInfo));
	if (ret != 0)
	{
		DPRINT_ERR(VMBUS, "Unable to open channel - %d", ret);
		goto Cleanup;
	}

	if (msgCount>1)
	{
		ITERATE_LIST_ENTRIES(anchor, curr, &msgInfo->SubMsgList)
		{
			subMsgInfo = (struct vmbus_channel_msginfo*)curr;
			gpadlBody = (VMBUS_CHANNEL_GPADL_BODY*)subMsgInfo->Msg;

			gpadlBody->Header.MessageType = ChannelMessageGpadlBody;
			gpadlBody->Gpadl = nextGpadlHandle;

			DPRINT_DBG(VMBUS, "Sending GPADL Body - len %zd", subMsgInfo->MessageSize - sizeof(*subMsgInfo));

			DumpGpadlBody(gpadlBody, subMsgInfo->MessageSize - sizeof(*subMsgInfo));
			ret = VmbusPostMessage(gpadlBody, subMsgInfo->MessageSize - sizeof(*subMsgInfo));
			ASSERT(ret == 0);
		}
	}
	osd_WaitEventWait(msgInfo->WaitEvent);

	/* At this point, we received the gpadl created msg */
	DPRINT_DBG(VMBUS, "Received GPADL created (relid %d, status %d handle %x)",
		Channel->OfferMsg.ChildRelId,
		msgInfo->Response.GpadlCreated.CreationStatus,
		gpadlMsg->Gpadl);

	*GpadlHandle = gpadlMsg->Gpadl;

Cleanup:
	spin_lock_irqsave(&gVmbusConnection.channelmsg_lock, flags);
	REMOVE_ENTRY_LIST(&msgInfo->MsgListEntry);
	spin_unlock_irqrestore(&gVmbusConnection.channelmsg_lock, flags);

	kfree(msgInfo->WaitEvent);
	kfree(msgInfo);

	DPRINT_EXIT(VMBUS);

	return ret;
}



/*++;

Name:
	VmbusChannelTeardownGpadl()

Description:
	Teardown the specified GPADL handle

--*/
int VmbusChannelTeardownGpadl(struct vmbus_channel *Channel, u32 GpadlHandle)
{
	int ret=0;
	VMBUS_CHANNEL_GPADL_TEARDOWN *msg;
	struct vmbus_channel_msginfo *info;
	unsigned long flags;

	DPRINT_ENTER(VMBUS);

	ASSERT(GpadlHandle != 0);

	info = kmalloc(sizeof(*info) + sizeof(VMBUS_CHANNEL_GPADL_TEARDOWN), GFP_KERNEL);
	ASSERT(info != NULL);

	info->WaitEvent = osd_WaitEventCreate();

	msg = (VMBUS_CHANNEL_GPADL_TEARDOWN*)info->Msg;

	msg->Header.MessageType = ChannelMessageGpadlTeardown;
    msg->ChildRelId  = Channel->OfferMsg.ChildRelId;
    msg->Gpadl       = GpadlHandle;

	spin_lock_irqsave(&gVmbusConnection.channelmsg_lock, flags);
	INSERT_TAIL_LIST(&gVmbusConnection.ChannelMsgList, &info->MsgListEntry);
	spin_unlock_irqrestore(&gVmbusConnection.channelmsg_lock, flags);

	ret = VmbusPostMessage(msg, sizeof(VMBUS_CHANNEL_GPADL_TEARDOWN));
	if (ret != 0)
	{
		/* TODO: */
	}

	osd_WaitEventWait(info->WaitEvent);

	/* Received a torndown response */
	spin_lock_irqsave(&gVmbusConnection.channelmsg_lock, flags);
	REMOVE_ENTRY_LIST(&info->MsgListEntry);
	spin_unlock_irqrestore(&gVmbusConnection.channelmsg_lock, flags);

	kfree(info->WaitEvent);
	kfree(info);

	DPRINT_EXIT(VMBUS);

	return ret;
}


/*++

Name:
	VmbusChannelClose()

Description:
	Close the specified channel

--*/
void VmbusChannelClose(struct vmbus_channel *Channel)
{
	int ret=0;
	VMBUS_CHANNEL_CLOSE_CHANNEL* msg;
	struct vmbus_channel_msginfo *info;
	unsigned long flags;

	DPRINT_ENTER(VMBUS);

	/* Stop callback and cancel the timer asap */
	Channel->OnChannelCallback = NULL;
	del_timer(&Channel->poll_timer);

	/* Send a closing message */
	info = kmalloc(sizeof(*info) + sizeof(VMBUS_CHANNEL_CLOSE_CHANNEL), GFP_KERNEL);
	ASSERT(info != NULL);

	/* info->waitEvent = osd_WaitEventCreate(); */

	msg = (VMBUS_CHANNEL_CLOSE_CHANNEL*)info->Msg;
	msg->Header.MessageType				= ChannelMessageCloseChannel;
    msg->ChildRelId						= Channel->OfferMsg.ChildRelId;

	ret = VmbusPostMessage(msg, sizeof(VMBUS_CHANNEL_CLOSE_CHANNEL));
	if (ret != 0)
	{
		/* TODO: */
	}

	/* Tear down the gpadl for the channel's ring buffer */
	if (Channel->RingBufferGpadlHandle)
	{
		VmbusChannelTeardownGpadl(Channel, Channel->RingBufferGpadlHandle);
	}

	/* TODO: Send a msg to release the childRelId */

	/* Cleanup the ring buffers for this channel */
	RingBufferCleanup(&Channel->Outbound);
	RingBufferCleanup(&Channel->Inbound);

	osd_PageFree(Channel->RingBufferPages, Channel->RingBufferPageCount);

	kfree(info);


	/*
	 * If we are closing the channel during an error path in
	 * opening the channel, don't free the channel since the
	 * caller will free the channel
	 */

	if (Channel->State == CHANNEL_OPEN_STATE)
	{
		spin_lock_irqsave(&gVmbusConnection.channel_lock, flags);
		REMOVE_ENTRY_LIST(&Channel->ListEntry);
		spin_unlock_irqrestore(&gVmbusConnection.channel_lock, flags);

		FreeVmbusChannel(Channel);
	}

	DPRINT_EXIT(VMBUS);
}


/*++

Name:
	VmbusChannelSendPacket()

Description:
	Send the specified buffer on the given channel

--*/
int VmbusChannelSendPacket(struct vmbus_channel *Channel,
	const void *			Buffer,
	u32				BufferLen,
	u64				RequestId,
	VMBUS_PACKET_TYPE	Type,
	u32				Flags
)
{
	int ret=0;
	VMPACKET_DESCRIPTOR desc;
	u32 packetLen = sizeof(VMPACKET_DESCRIPTOR) + BufferLen;
	u32 packetLenAligned = ALIGN_UP(packetLen, sizeof(u64));
	struct scatterlist bufferList[3];
	u64 alignedData=0;

	DPRINT_ENTER(VMBUS);
	DPRINT_DBG(VMBUS, "channel %p buffer %p len %d", Channel, Buffer, BufferLen);

	DumpVmbusChannel(Channel);

	ASSERT((packetLenAligned - packetLen) < sizeof(u64));

	/* Setup the descriptor */
	desc.Type = Type; /* VmbusPacketTypeDataInBand; */
	desc.Flags = Flags; /* VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED; */
	desc.DataOffset8 = sizeof(VMPACKET_DESCRIPTOR) >> 3; /* in 8-bytes granularity */
	desc.Length8 = (u16)(packetLenAligned >> 3);
	desc.TransactionId = RequestId;

	sg_init_table(bufferList,3);
	sg_set_buf(&bufferList[0], &desc, sizeof(VMPACKET_DESCRIPTOR));
	sg_set_buf(&bufferList[1], Buffer, BufferLen);
	sg_set_buf(&bufferList[2], &alignedData, packetLenAligned - packetLen);

	ret = RingBufferWrite(
		&Channel->Outbound,
		bufferList,
		3);

	/* TODO: We should determine if this is optional */
	if (ret == 0 && !GetRingBufferInterruptMask(&Channel->Outbound))
	{
		VmbusChannelSetEvent(Channel);
	}

	DPRINT_EXIT(VMBUS);

	return ret;
}


/*++

Name:
	VmbusChannelSendPacketPageBuffer()

Description:
	Send a range of single-page buffer packets using a GPADL Direct packet type.

--*/
int VmbusChannelSendPacketPageBuffer(struct vmbus_channel *Channel,
	PAGE_BUFFER			PageBuffers[],
	u32				PageCount,
	void *				Buffer,
	u32				BufferLen,
	u64				RequestId
)
{
	int ret=0;
	int i=0;
	struct VMBUS_CHANNEL_PACKET_PAGE_BUFFER desc;
	u32 descSize;
	u32 packetLen;
	u32 packetLenAligned;
	struct scatterlist bufferList[3];
	u64 alignedData=0;

	DPRINT_ENTER(VMBUS);

	ASSERT(PageCount <= MAX_PAGE_BUFFER_COUNT);

	DumpVmbusChannel(Channel);

	/* Adjust the size down since VMBUS_CHANNEL_PACKET_PAGE_BUFFER is the largest size we support */
	descSize = sizeof(struct VMBUS_CHANNEL_PACKET_PAGE_BUFFER) - ((MAX_PAGE_BUFFER_COUNT - PageCount)*sizeof(PAGE_BUFFER));
	packetLen = descSize + BufferLen;
	packetLenAligned = ALIGN_UP(packetLen, sizeof(u64));

	ASSERT((packetLenAligned - packetLen) < sizeof(u64));

	/* Setup the descriptor */
	desc.Type = VmbusPacketTypeDataUsingGpaDirect;
	desc.Flags = VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED;
	desc.DataOffset8 = descSize >> 3; /* in 8-bytes grandularity */
	desc.Length8 = (u16)(packetLenAligned >> 3);
	desc.TransactionId = RequestId;
	desc.RangeCount = PageCount;

	for (i=0; i<PageCount; i++)
	{
		desc.Range[i].Length = PageBuffers[i].Length;
		desc.Range[i].Offset = PageBuffers[i].Offset;
		desc.Range[i].Pfn	 = PageBuffers[i].Pfn;
	}

	sg_init_table(bufferList,3);
	sg_set_buf(&bufferList[0], &desc, descSize);
	sg_set_buf(&bufferList[1], Buffer, BufferLen);
	sg_set_buf(&bufferList[2], &alignedData, packetLenAligned - packetLen);

	ret = RingBufferWrite(
		&Channel->Outbound,
		bufferList,
		3);

	/* TODO: We should determine if this is optional */
	if (ret == 0 && !GetRingBufferInterruptMask(&Channel->Outbound))
	{
		VmbusChannelSetEvent(Channel);
	}

	DPRINT_EXIT(VMBUS);

	return ret;
}



/*++

Name:
	VmbusChannelSendPacketMultiPageBuffer()

Description:
	Send a multi-page buffer packet using a GPADL Direct packet type.

--*/
int VmbusChannelSendPacketMultiPageBuffer(struct vmbus_channel *Channel,
	MULTIPAGE_BUFFER	*MultiPageBuffer,
	void *				Buffer,
	u32				BufferLen,
	u64				RequestId
)
{
	int ret=0;
	struct VMBUS_CHANNEL_PACKET_MULITPAGE_BUFFER desc;
	u32 descSize;
	u32 packetLen;
	u32 packetLenAligned;
	struct scatterlist bufferList[3];
	u64 alignedData=0;
	u32 PfnCount = NUM_PAGES_SPANNED(MultiPageBuffer->Offset, MultiPageBuffer->Length);

	DPRINT_ENTER(VMBUS);

	DumpVmbusChannel(Channel);

	DPRINT_DBG(VMBUS, "data buffer - offset %u len %u pfn count %u", MultiPageBuffer->Offset, MultiPageBuffer->Length, PfnCount);

	ASSERT(PfnCount > 0);
	ASSERT(PfnCount <= MAX_MULTIPAGE_BUFFER_COUNT);

	/* Adjust the size down since VMBUS_CHANNEL_PACKET_MULITPAGE_BUFFER is the largest size we support */
	descSize = sizeof(struct VMBUS_CHANNEL_PACKET_MULITPAGE_BUFFER) - ((MAX_MULTIPAGE_BUFFER_COUNT - PfnCount)*sizeof(u64));
	packetLen = descSize + BufferLen;
	packetLenAligned = ALIGN_UP(packetLen, sizeof(u64));

	ASSERT((packetLenAligned - packetLen) < sizeof(u64));

	/* Setup the descriptor */
	desc.Type = VmbusPacketTypeDataUsingGpaDirect;
	desc.Flags = VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED;
	desc.DataOffset8 = descSize >> 3; /* in 8-bytes grandularity */
	desc.Length8 = (u16)(packetLenAligned >> 3);
	desc.TransactionId = RequestId;
	desc.RangeCount = 1;

	desc.Range.Length = MultiPageBuffer->Length;
	desc.Range.Offset = MultiPageBuffer->Offset;

	memcpy(desc.Range.PfnArray, MultiPageBuffer->PfnArray, PfnCount*sizeof(u64));

	sg_init_table(bufferList,3);
	sg_set_buf(&bufferList[0], &desc, descSize);
	sg_set_buf(&bufferList[1], Buffer, BufferLen);
	sg_set_buf(&bufferList[2], &alignedData, packetLenAligned - packetLen);

	ret = RingBufferWrite(
		&Channel->Outbound,
		bufferList,
		3);

	/* TODO: We should determine if this is optional */
	if (ret == 0 && !GetRingBufferInterruptMask(&Channel->Outbound))
	{
		VmbusChannelSetEvent(Channel);
	}

	DPRINT_EXIT(VMBUS);

	return ret;
}


/*++

Name:
	VmbusChannelRecvPacket()

Description:
	Retrieve the user packet on the specified channel

--*/
/* TODO: Do we ever receive a gpa direct packet other than the ones we send ? */
int VmbusChannelRecvPacket(struct vmbus_channel *Channel,
			   void *Buffer,
			   u32 BufferLen,
			   u32 *BufferActualLen,
			   u64 *RequestId)
{
	VMPACKET_DESCRIPTOR desc;
	u32 packetLen;
	u32 userLen;
	int ret;
	unsigned long flags;

	DPRINT_ENTER(VMBUS);

	*BufferActualLen = 0;
	*RequestId = 0;

	spin_lock_irqsave(&Channel->inbound_lock, flags);

	ret = RingBufferPeek(&Channel->Inbound, &desc, sizeof(VMPACKET_DESCRIPTOR));
	if (ret != 0)
	{
		spin_unlock_irqrestore(&Channel->inbound_lock, flags);

		/* DPRINT_DBG(VMBUS, "nothing to read!!"); */
		DPRINT_EXIT(VMBUS);
		return 0;
	}

	/* VmbusChannelClearEvent(Channel); */

	packetLen = desc.Length8 << 3;
	userLen = packetLen - (desc.DataOffset8 << 3);
	/* ASSERT(userLen > 0); */

	DPRINT_DBG(VMBUS, "packet received on channel %p relid %d <type %d flag %d tid %llx pktlen %d datalen %d> ",
		Channel,
		Channel->OfferMsg.ChildRelId,
		desc.Type,
		desc.Flags,
		desc.TransactionId, packetLen, userLen);

	*BufferActualLen = userLen;

	if (userLen > BufferLen)
	{
		spin_unlock_irqrestore(&Channel->inbound_lock, flags);

		DPRINT_ERR(VMBUS, "buffer too small - got %d needs %d", BufferLen, userLen);
		DPRINT_EXIT(VMBUS);

		return -1;
	}

	*RequestId = desc.TransactionId;

	/* Copy over the packet to the user buffer */
	ret = RingBufferRead(&Channel->Inbound, Buffer, userLen, (desc.DataOffset8 << 3));

	spin_unlock_irqrestore(&Channel->inbound_lock, flags);

	DPRINT_EXIT(VMBUS);

	return 0;
}

/*++

Name:
	VmbusChannelRecvPacketRaw()

Description:
	Retrieve the raw packet on the specified channel

--*/
int VmbusChannelRecvPacketRaw(struct vmbus_channel *Channel,
	void *				Buffer,
	u32				BufferLen,
	u32*				BufferActualLen,
	u64*				RequestId
	)
{
	VMPACKET_DESCRIPTOR desc;
	u32 packetLen;
	u32 userLen;
	int ret;
	unsigned long flags;

	DPRINT_ENTER(VMBUS);

	*BufferActualLen = 0;
	*RequestId = 0;

	spin_lock_irqsave(&Channel->inbound_lock, flags);

	ret = RingBufferPeek(&Channel->Inbound, &desc, sizeof(VMPACKET_DESCRIPTOR));
	if (ret != 0)
	{
		spin_unlock_irqrestore(&Channel->inbound_lock, flags);

		/* DPRINT_DBG(VMBUS, "nothing to read!!"); */
		DPRINT_EXIT(VMBUS);
		return 0;
	}

	/* VmbusChannelClearEvent(Channel); */

	packetLen = desc.Length8 << 3;
	userLen = packetLen - (desc.DataOffset8 << 3);

	DPRINT_DBG(VMBUS, "packet received on channel %p relid %d <type %d flag %d tid %llx pktlen %d datalen %d> ",
		Channel,
		Channel->OfferMsg.ChildRelId,
		desc.Type,
		desc.Flags,
		desc.TransactionId, packetLen, userLen);

	*BufferActualLen = packetLen;

	if (packetLen > BufferLen)
	{
		spin_unlock_irqrestore(&Channel->inbound_lock, flags);

		DPRINT_ERR(VMBUS, "buffer too small - needed %d bytes but got space for only %d bytes", packetLen, BufferLen);
		DPRINT_EXIT(VMBUS);
		return -2;
	}

	*RequestId = desc.TransactionId;

	/* Copy over the entire packet to the user buffer */
	ret = RingBufferRead(&Channel->Inbound, Buffer, packetLen, 0);

	spin_unlock_irqrestore(&Channel->inbound_lock, flags);

	DPRINT_EXIT(VMBUS);

	return 0;
}


/*++

Name:
	VmbusChannelOnChannelEvent()

Description:
	Channel event callback

--*/
void VmbusChannelOnChannelEvent(struct vmbus_channel *Channel)
{
	DumpVmbusChannel(Channel);
	ASSERT(Channel->OnChannelCallback);
#ifdef ENABLE_POLLING
	del_timer(&Channel->poll_timer);
	Channel->OnChannelCallback(Channel->ChannelCallbackContext);
	channel->poll_timer.expires(jiffies + usecs_to_jiffies(100);
	add_timer(&channel->poll_timer);
#else
	Channel->OnChannelCallback(Channel->ChannelCallbackContext);
#endif
}

/*++

Name:
	VmbusChannelOnTimer()

Description:
	Timer event callback

--*/
void VmbusChannelOnTimer(unsigned long data)
{
	struct vmbus_channel *channel = (struct vmbus_channel *)data;

	if (channel->OnChannelCallback)
	{
		channel->OnChannelCallback(channel->ChannelCallbackContext);
#ifdef ENABLE_POLLING
		channel->poll_timer.expires(jiffies + usecs_to_jiffies(100);
		add_timer(&channel->poll_timer);
#endif
	}
}


/*++

Name:
	DumpVmbusChannel()

Description:
	Dump vmbus channel info to the console

--*/
static void DumpVmbusChannel(struct vmbus_channel *Channel)
{
	DPRINT_DBG(VMBUS, "Channel (%d)", Channel->OfferMsg.ChildRelId);
	DumpRingInfo(&Channel->Outbound, "Outbound ");
	DumpRingInfo(&Channel->Inbound, "Inbound ");
}


/* eof */
