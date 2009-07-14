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
#include "include/osd.h"
#include "include/logging.h"

#include "VmbusPrivate.h"

//
// Internal routines
//
static int
VmbusChannelCreateGpadlHeader(
	void *					Kbuffer,	// must be phys and virt contiguous
	UINT32					Size,		// page-size multiple
	VMBUS_CHANNEL_MSGINFO	**msgInfo,
	UINT32					*MessageCount
	);

static void
DumpVmbusChannel(
	VMBUS_CHANNEL			*Channel
	);


static void
VmbusChannelSetEvent(
	VMBUS_CHANNEL			*Channel
	);


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
			DPRINT_DBG(VMBUS, "param-conn id (%d)(%d) - %d", i, j, MonitorPage->Parameter[i][j].ConnectionId.AsUINT32);
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
static void
VmbusChannelSetEvent(
	VMBUS_CHANNEL			*Channel
	)
{
	HV_MONITOR_PAGE *monitorPage;

	DPRINT_ENTER(VMBUS);

	if (Channel->OfferMsg.MonitorAllocated)
	{
		// Each UINT32 represents 32 channels
		BitSet((UINT32*)gVmbusConnection.SendInterruptPage + (Channel->OfferMsg.ChildRelId >> 5), Channel->OfferMsg.ChildRelId & 31);

		monitorPage = (HV_MONITOR_PAGE*)gVmbusConnection.MonitorPages;
		monitorPage++; // Get the child to parent monitor page

		BitSet((UINT32*) &monitorPage->TriggerGroup[Channel->MonitorGroup].Pending, Channel->MonitorBit);
	}
	else
	{
		VmbusSetEvent(Channel->OfferMsg.ChildRelId);
	}

	DPRINT_EXIT(VMBUS);
}

#if 0
static void
VmbusChannelClearEvent(
	VMBUS_CHANNEL			*Channel
	)
{
	HV_MONITOR_PAGE *monitorPage;

	DPRINT_ENTER(VMBUS);

	if (Channel->OfferMsg.MonitorAllocated)
	{
		// Each UINT32 represents 32 channels
		BitClear((UINT32*)gVmbusConnection.SendInterruptPage + (Channel->OfferMsg.ChildRelId >> 5), Channel->OfferMsg.ChildRelId & 31);

		monitorPage = (HV_MONITOR_PAGE*)gVmbusConnection.MonitorPages;
		monitorPage++; // Get the child to parent monitor page

		BitClear((UINT32*) &monitorPage->TriggerGroup[Channel->MonitorGroup].Pending, Channel->MonitorBit);
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
void
VmbusChannelGetDebugInfo(
	VMBUS_CHANNEL				*Channel,
	VMBUS_CHANNEL_DEBUG_INFO	*DebugInfo
	)
{
	HV_MONITOR_PAGE *monitorPage;
    u8 monitorGroup    = (u8)Channel->OfferMsg.MonitorId / 32;
    u8 monitorOffset   = (u8)Channel->OfferMsg.MonitorId % 32;
	//UINT32 monitorBit	= 1 << monitorOffset;

	DebugInfo->RelId = Channel->OfferMsg.ChildRelId;
	DebugInfo->State = Channel->State;
	memcpy(&DebugInfo->InterfaceType, &Channel->OfferMsg.Offer.InterfaceType, sizeof(GUID));
	memcpy(&DebugInfo->InterfaceInstance, &Channel->OfferMsg.Offer.InterfaceInstance, sizeof(GUID));

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
int
VmbusChannelOpen(
	VMBUS_CHANNEL			*NewChannel,
	UINT32					SendRingBufferSize,
	UINT32					RecvRingBufferSize,
	void *					UserData,
	UINT32					UserDataLen,
	PFN_CHANNEL_CALLBACK	pfnOnChannelCallback,
	void *					Context
	)
{
	int ret=0;
	VMBUS_CHANNEL_OPEN_CHANNEL* openMsg;
	VMBUS_CHANNEL_MSGINFO* openInfo;
	void *in, *out;

	DPRINT_ENTER(VMBUS);

	// Aligned to page size
	ASSERT(!(SendRingBufferSize & (PAGE_SIZE -1)));
	ASSERT(!(RecvRingBufferSize & (PAGE_SIZE -1)));

	NewChannel->OnChannelCallback = pfnOnChannelCallback;
	NewChannel->ChannelCallbackContext = Context;

	// Allocate the ring buffer
	out = PageAlloc((SendRingBufferSize + RecvRingBufferSize) >> PAGE_SHIFT);
	//out = MemAllocZeroed(sendRingBufferSize + recvRingBufferSize);
	ASSERT(out);
	ASSERT(((ULONG_PTR)out & (PAGE_SIZE-1)) == 0);

	in = (void*)((ULONG_PTR)out + SendRingBufferSize);

	NewChannel->RingBufferPages = out;
	NewChannel->RingBufferPageCount = (SendRingBufferSize + RecvRingBufferSize) >> PAGE_SHIFT;

	RingBufferInit(&NewChannel->Outbound, out, SendRingBufferSize);

	RingBufferInit(&NewChannel->Inbound, in, RecvRingBufferSize);

	// Establish the gpadl for the ring buffer
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

	// Create and init the channel open message
	openInfo =
		(VMBUS_CHANNEL_MSGINFO*)MemAlloc(sizeof(VMBUS_CHANNEL_MSGINFO) + sizeof(VMBUS_CHANNEL_OPEN_CHANNEL));
	ASSERT(openInfo != NULL);

	openInfo->WaitEvent = WaitEventCreate();

	openMsg = (VMBUS_CHANNEL_OPEN_CHANNEL*)openInfo->Msg;
	openMsg->Header.MessageType				= ChannelMessageOpenChannel;
	openMsg->OpenId							= NewChannel->OfferMsg.ChildRelId; // FIXME
    openMsg->ChildRelId						= NewChannel->OfferMsg.ChildRelId;
    openMsg->RingBufferGpadlHandle			= NewChannel->RingBufferGpadlHandle;
    ASSERT(openMsg->RingBufferGpadlHandle);
    openMsg->DownstreamRingBufferPageOffset		= SendRingBufferSize >> PAGE_SHIFT;
    openMsg->ServerContextAreaGpadlHandle	= 0; // TODO

	ASSERT(UserDataLen <= MAX_USER_DEFINED_BYTES);
	if (UserDataLen)
	{
		memcpy(openMsg->UserData, UserData, UserDataLen);
	}

	SpinlockAcquire(gVmbusConnection.ChannelMsgLock);
	INSERT_TAIL_LIST(&gVmbusConnection.ChannelMsgList, &openInfo->MsgListEntry);
	SpinlockRelease(gVmbusConnection.ChannelMsgLock);

	DPRINT_DBG(VMBUS, "Sending channel open msg...");

	ret = VmbusPostMessage(openMsg, sizeof(VMBUS_CHANNEL_OPEN_CHANNEL));
	if (ret != 0)
	{
		DPRINT_ERR(VMBUS, "unable to open channel - %d", ret);
		goto Cleanup;
	}

	// FIXME: Need to time-out here
	WaitEventWait(openInfo->WaitEvent);

	if (openInfo->Response.OpenResult.Status == 0)
	{
		DPRINT_INFO(VMBUS, "channel <%p> open success!!", NewChannel);
	}
	else
	{
		DPRINT_INFO(VMBUS, "channel <%p> open failed - %d!!", NewChannel, openInfo->Response.OpenResult.Status);
	}

Cleanup:
	SpinlockAcquire(gVmbusConnection.ChannelMsgLock);
	REMOVE_ENTRY_LIST(&openInfo->MsgListEntry);
	SpinlockRelease(gVmbusConnection.ChannelMsgLock);

	WaitEventClose(openInfo->WaitEvent);
	MemFree(openInfo);

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
	UINT32						Len)
{
	int i=0;
	int pfnCount=0;

	pfnCount = (Len - sizeof(VMBUS_CHANNEL_GPADL_BODY))/ sizeof(UINT64);
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
	void *					Kbuffer,	// from kmalloc()
	UINT32					Size,		// page-size multiple
	VMBUS_CHANNEL_MSGINFO	**MsgInfo,
	UINT32					*MessageCount)
{
	int i;
	int pageCount;
    unsigned long long pfn;
	VMBUS_CHANNEL_GPADL_HEADER* gpaHeader;
	VMBUS_CHANNEL_GPADL_BODY* gpadlBody;
	VMBUS_CHANNEL_MSGINFO* msgHeader;
	VMBUS_CHANNEL_MSGINFO* msgBody;
	UINT32				msgSize;

	int pfnSum, pfnCount, pfnLeft, pfnCurr, pfnSize;

	//ASSERT( (kbuffer & (PAGE_SIZE-1)) == 0);
	ASSERT( (Size & (PAGE_SIZE-1)) == 0);

	pageCount = Size >> PAGE_SHIFT;
	pfn = GetPhysicalAddress(Kbuffer) >> PAGE_SHIFT;

	// do we need a gpadl body msg
	pfnSize = MAX_SIZE_CHANNEL_MESSAGE - sizeof(VMBUS_CHANNEL_GPADL_HEADER) - sizeof(GPA_RANGE);
	pfnCount = pfnSize / sizeof(UINT64);

	if (pageCount > pfnCount) // we need a gpadl body
	{
		// fill in the header
		msgSize = sizeof(VMBUS_CHANNEL_MSGINFO) + sizeof(VMBUS_CHANNEL_GPADL_HEADER) + sizeof(GPA_RANGE) + pfnCount*sizeof(UINT64);
		msgHeader =  MemAllocZeroed(msgSize);

		INITIALIZE_LIST_HEAD(&msgHeader->SubMsgList);
		msgHeader->MessageSize=msgSize;

		gpaHeader = (VMBUS_CHANNEL_GPADL_HEADER*)msgHeader->Msg;
		gpaHeader->RangeCount = 1;
		gpaHeader->RangeBufLen = sizeof(GPA_RANGE) + pageCount*sizeof(UINT64);
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

		// how many pfns can we fit
		pfnSize = MAX_SIZE_CHANNEL_MESSAGE - sizeof(VMBUS_CHANNEL_GPADL_BODY);
		pfnCount = pfnSize / sizeof(UINT64);

		// fill in the body
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

			msgSize = sizeof(VMBUS_CHANNEL_MSGINFO) + sizeof(VMBUS_CHANNEL_GPADL_BODY) + pfnCurr*sizeof(UINT64);
			msgBody =  MemAllocZeroed(msgSize);
			ASSERT(msgBody);
			msgBody->MessageSize = msgSize;
			(*MessageCount)++;
			gpadlBody = (VMBUS_CHANNEL_GPADL_BODY*)msgBody->Msg;

			// FIXME: Gpadl is UINT32 and we are using a pointer which could be 64-bit
			//gpadlBody->Gpadl = kbuffer;
			for (i=0; i<pfnCurr; i++)
			{
				gpadlBody->Pfn[i] = pfn + pfnSum + i;
			}

			// add to msg header
			INSERT_TAIL_LIST(&msgHeader->SubMsgList, &msgBody->MsgListEntry);
			pfnSum += pfnCurr;
			pfnLeft -= pfnCurr;
		}
	}
	else
	{
		// everything fits in a header
		msgSize = sizeof(VMBUS_CHANNEL_MSGINFO) + sizeof(VMBUS_CHANNEL_GPADL_HEADER) + sizeof(GPA_RANGE) + pageCount*sizeof(UINT64);
		msgHeader =  MemAllocZeroed(msgSize);
		msgHeader->MessageSize=msgSize;

		gpaHeader = (VMBUS_CHANNEL_GPADL_HEADER*)msgHeader->Msg;
		gpaHeader->RangeCount = 1;
		gpaHeader->RangeBufLen = sizeof(GPA_RANGE) + pageCount*sizeof(UINT64);
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
int
VmbusChannelEstablishGpadl(
	VMBUS_CHANNEL	*Channel,
	void *			Kbuffer,	// from kmalloc()
	UINT32			Size,		// page-size multiple
	UINT32			*GpadlHandle
	)
{
	int ret=0;
	VMBUS_CHANNEL_GPADL_HEADER* gpadlMsg;
	VMBUS_CHANNEL_GPADL_BODY* gpadlBody;
	//VMBUS_CHANNEL_GPADL_CREATED* gpadlCreated;

	VMBUS_CHANNEL_MSGINFO *msgInfo;
	VMBUS_CHANNEL_MSGINFO *subMsgInfo;

	UINT32 msgCount;
	LIST_ENTRY* anchor;
	LIST_ENTRY* curr;
	UINT32 nextGpadlHandle;

	DPRINT_ENTER(VMBUS);

	nextGpadlHandle = gVmbusConnection.NextGpadlHandle;
	InterlockedIncrement((int*)&gVmbusConnection.NextGpadlHandle);

	VmbusChannelCreateGpadlHeader(Kbuffer, Size, &msgInfo, &msgCount);
	ASSERT(msgInfo != NULL);
	ASSERT(msgCount >0);

	msgInfo->WaitEvent = WaitEventCreate();
	gpadlMsg = (VMBUS_CHANNEL_GPADL_HEADER*)msgInfo->Msg;
	gpadlMsg->Header.MessageType = ChannelMessageGpadlHeader;
	gpadlMsg->ChildRelId = Channel->OfferMsg.ChildRelId;
	gpadlMsg->Gpadl = nextGpadlHandle;

	DumpGpadlHeader(gpadlMsg);

	SpinlockAcquire(gVmbusConnection.ChannelMsgLock);
	INSERT_TAIL_LIST(&gVmbusConnection.ChannelMsgList, &msgInfo->MsgListEntry);
	SpinlockRelease(gVmbusConnection.ChannelMsgLock);

	DPRINT_DBG(VMBUS, "buffer %p, size %d msg cnt %d", Kbuffer, Size, msgCount);

	DPRINT_DBG(VMBUS, "Sending GPADL Header - len %d", msgInfo->MessageSize - sizeof(VMBUS_CHANNEL_MSGINFO));

	ret = VmbusPostMessage(gpadlMsg, msgInfo->MessageSize - sizeof(VMBUS_CHANNEL_MSGINFO));
	if (ret != 0)
	{
		DPRINT_ERR(VMBUS, "Unable to open channel - %d", ret);
		goto Cleanup;
	}

	if (msgCount>1)
	{
		ITERATE_LIST_ENTRIES(anchor, curr, &msgInfo->SubMsgList)
		{
			subMsgInfo = (VMBUS_CHANNEL_MSGINFO*) curr;
			gpadlBody = (VMBUS_CHANNEL_GPADL_BODY*)subMsgInfo->Msg;

			gpadlBody->Header.MessageType = ChannelMessageGpadlBody;
			gpadlBody->Gpadl = nextGpadlHandle;

			DPRINT_DBG(VMBUS, "Sending GPADL Body - len %d", subMsgInfo->MessageSize - sizeof(VMBUS_CHANNEL_MSGINFO));

			DumpGpadlBody(gpadlBody, subMsgInfo->MessageSize - sizeof(VMBUS_CHANNEL_MSGINFO));
			ret = VmbusPostMessage(gpadlBody, subMsgInfo->MessageSize - sizeof(VMBUS_CHANNEL_MSGINFO));
			ASSERT(ret == 0);
		}
	}
	WaitEventWait(msgInfo->WaitEvent);

	// At this point, we received the gpadl created msg
	DPRINT_DBG(VMBUS, "Received GPADL created (relid %d, status %d handle %x)",
		Channel->OfferMsg.ChildRelId,
		msgInfo->Response.GpadlCreated.CreationStatus,
		gpadlMsg->Gpadl);

	*GpadlHandle = gpadlMsg->Gpadl;

Cleanup:
	SpinlockAcquire(gVmbusConnection.ChannelMsgLock);
	REMOVE_ENTRY_LIST(&msgInfo->MsgListEntry);
	SpinlockRelease(gVmbusConnection.ChannelMsgLock);

	WaitEventClose(msgInfo->WaitEvent);
	MemFree(msgInfo);

	DPRINT_EXIT(VMBUS);

	return ret;
}



/*++;

Name:
	VmbusChannelTeardownGpadl()

Description:
	Teardown the specified GPADL handle

--*/
int
VmbusChannelTeardownGpadl(
	VMBUS_CHANNEL	*Channel,
	UINT32			GpadlHandle
	)
{
	int ret=0;
	VMBUS_CHANNEL_GPADL_TEARDOWN *msg;
	VMBUS_CHANNEL_MSGINFO* info;

	DPRINT_ENTER(VMBUS);

	ASSERT(GpadlHandle != 0);

	info =
		(VMBUS_CHANNEL_MSGINFO*)MemAlloc(sizeof(VMBUS_CHANNEL_MSGINFO) + sizeof(VMBUS_CHANNEL_GPADL_TEARDOWN));
	ASSERT(info != NULL);

	info->WaitEvent = WaitEventCreate();

	msg = (VMBUS_CHANNEL_GPADL_TEARDOWN*)info->Msg;

	msg->Header.MessageType = ChannelMessageGpadlTeardown;
    msg->ChildRelId  = Channel->OfferMsg.ChildRelId;
    msg->Gpadl       = GpadlHandle;

	SpinlockAcquire(gVmbusConnection.ChannelMsgLock);
	INSERT_TAIL_LIST(&gVmbusConnection.ChannelMsgList, &info->MsgListEntry);
	SpinlockRelease(gVmbusConnection.ChannelMsgLock);

	ret = VmbusPostMessage(msg, sizeof(VMBUS_CHANNEL_GPADL_TEARDOWN));
	if (ret != 0)
	{
		// TODO:
	}

	WaitEventWait(info->WaitEvent);

	// Received a torndown response
	SpinlockAcquire(gVmbusConnection.ChannelMsgLock);
	REMOVE_ENTRY_LIST(&info->MsgListEntry);
	SpinlockRelease(gVmbusConnection.ChannelMsgLock);

	WaitEventClose(info->WaitEvent);
	MemFree(info);

	DPRINT_EXIT(VMBUS);

	return ret;
}


/*++

Name:
	VmbusChannelClose()

Description:
	Close the specified channel

--*/
void
VmbusChannelClose(
	VMBUS_CHANNEL	*Channel
	)
{
	int ret=0;
	VMBUS_CHANNEL_CLOSE_CHANNEL* msg;
	VMBUS_CHANNEL_MSGINFO* info;

	DPRINT_ENTER(VMBUS);

	// Stop callback and cancel the timer asap
	Channel->OnChannelCallback = NULL;
	TimerStop(Channel->PollTimer);

	// Send a closing message
	info =
		(VMBUS_CHANNEL_MSGINFO*)MemAlloc(sizeof(VMBUS_CHANNEL_MSGINFO) + sizeof(VMBUS_CHANNEL_CLOSE_CHANNEL));
	ASSERT(info != NULL);

	//info->waitEvent = WaitEventCreate();

	msg = (VMBUS_CHANNEL_CLOSE_CHANNEL*)info->Msg;
	msg->Header.MessageType				= ChannelMessageCloseChannel;
    msg->ChildRelId						= Channel->OfferMsg.ChildRelId;

	ret = VmbusPostMessage(msg, sizeof(VMBUS_CHANNEL_CLOSE_CHANNEL));
	if (ret != 0)
	{
		// TODO:
	}

	// Tear down the gpadl for the channel's ring buffer
	if (Channel->RingBufferGpadlHandle)
	{
		VmbusChannelTeardownGpadl(Channel, Channel->RingBufferGpadlHandle);
	}

	// TODO: Send a msg to release the childRelId

	// Cleanup the ring buffers for this channel
	RingBufferCleanup(&Channel->Outbound);
	RingBufferCleanup(&Channel->Inbound);

	PageFree(Channel->RingBufferPages, Channel->RingBufferPageCount);

	MemFree(info);

	// If we are closing the channel during an error path in opening the channel, don't free the channel
	// since the caller will free the channel
	if (Channel->State == CHANNEL_OPEN_STATE)
	{
		SpinlockAcquire(gVmbusConnection.ChannelLock);
		REMOVE_ENTRY_LIST(&Channel->ListEntry);
		SpinlockRelease(gVmbusConnection.ChannelLock);

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
int
VmbusChannelSendPacket(
	VMBUS_CHANNEL		*Channel,
	const void *			Buffer,
	UINT32				BufferLen,
	UINT64				RequestId,
	VMBUS_PACKET_TYPE	Type,
	UINT32				Flags
)
{
	int ret=0;
	VMPACKET_DESCRIPTOR desc;
	UINT32 packetLen = sizeof(VMPACKET_DESCRIPTOR) + BufferLen;
	UINT32 packetLenAligned = ALIGN_UP(packetLen, sizeof(UINT64));
	SG_BUFFER_LIST bufferList[3];
	UINT64 alignedData=0;

	DPRINT_ENTER(VMBUS);
	DPRINT_DBG(VMBUS, "channel %p buffer %p len %d", Channel, Buffer, BufferLen);

	DumpVmbusChannel(Channel);

	ASSERT((packetLenAligned - packetLen) < sizeof(UINT64));

	// Setup the descriptor
	desc.Type = Type;//VmbusPacketTypeDataInBand;
	desc.Flags = Flags;//VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED;
    desc.DataOffset8 = sizeof(VMPACKET_DESCRIPTOR) >> 3; // in 8-bytes granularity
    desc.Length8 = (u16)(packetLenAligned >> 3);
    desc.TransactionId = RequestId;

	bufferList[0].Data = &desc;
	bufferList[0].Length = sizeof(VMPACKET_DESCRIPTOR);

	bufferList[1].Data = Buffer;
	bufferList[1].Length = BufferLen;

	bufferList[2].Data = &alignedData;
	bufferList[2].Length = packetLenAligned - packetLen;

	ret = RingBufferWrite(
		&Channel->Outbound,
		bufferList,
		3);

	// TODO: We should determine if this is optional
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
int
VmbusChannelSendPacketPageBuffer(
	VMBUS_CHANNEL		*Channel,
	PAGE_BUFFER			PageBuffers[],
	UINT32				PageCount,
	void *				Buffer,
	UINT32				BufferLen,
	UINT64				RequestId
)
{
	int ret=0;
	int i=0;
	VMBUS_CHANNEL_PACKET_PAGE_BUFFER desc;
	UINT32 descSize;
	UINT32 packetLen;
	UINT32 packetLenAligned;
	SG_BUFFER_LIST bufferList[3];
	UINT64 alignedData=0;

	DPRINT_ENTER(VMBUS);

	ASSERT(PageCount <= MAX_PAGE_BUFFER_COUNT);

	DumpVmbusChannel(Channel);

	// Adjust the size down since VMBUS_CHANNEL_PACKET_PAGE_BUFFER is the largest size we support
	descSize = sizeof(VMBUS_CHANNEL_PACKET_PAGE_BUFFER) - ((MAX_PAGE_BUFFER_COUNT - PageCount)*sizeof(PAGE_BUFFER));
	packetLen = descSize + BufferLen;
	packetLenAligned = ALIGN_UP(packetLen, sizeof(UINT64));

	ASSERT((packetLenAligned - packetLen) < sizeof(UINT64));

	// Setup the descriptor
	desc.Type = VmbusPacketTypeDataUsingGpaDirect;
	desc.Flags = VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED;
    desc.DataOffset8 = descSize >> 3; // in 8-bytes grandularity
    desc.Length8 = (u16)(packetLenAligned >> 3);
    desc.TransactionId = RequestId;
	desc.RangeCount = PageCount;

	for (i=0; i<PageCount; i++)
	{
		desc.Range[i].Length = PageBuffers[i].Length;
		desc.Range[i].Offset = PageBuffers[i].Offset;
		desc.Range[i].Pfn	 = PageBuffers[i].Pfn;
	}

	bufferList[0].Data = &desc;
	bufferList[0].Length = descSize;

	bufferList[1].Data = Buffer;
	bufferList[1].Length = BufferLen;

	bufferList[2].Data = &alignedData;
	bufferList[2].Length = packetLenAligned - packetLen;

	ret = RingBufferWrite(
		&Channel->Outbound,
		bufferList,
		3);

	// TODO: We should determine if this is optional
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
int
VmbusChannelSendPacketMultiPageBuffer(
	VMBUS_CHANNEL		*Channel,
	MULTIPAGE_BUFFER	*MultiPageBuffer,
	void *				Buffer,
	UINT32				BufferLen,
	UINT64				RequestId
)
{
	int ret=0;
	VMBUS_CHANNEL_PACKET_MULITPAGE_BUFFER desc;
	UINT32 descSize;
	UINT32 packetLen;
	UINT32 packetLenAligned;
	SG_BUFFER_LIST bufferList[3];
	UINT64 alignedData=0;
	UINT32 PfnCount = NUM_PAGES_SPANNED(MultiPageBuffer->Offset, MultiPageBuffer->Length);

	DPRINT_ENTER(VMBUS);

	DumpVmbusChannel(Channel);

	DPRINT_DBG(VMBUS, "data buffer - offset %u len %u pfn count %u", MultiPageBuffer->Offset, MultiPageBuffer->Length, PfnCount);

	ASSERT(PfnCount > 0);
	ASSERT(PfnCount <= MAX_MULTIPAGE_BUFFER_COUNT);

	// Adjust the size down since VMBUS_CHANNEL_PACKET_MULITPAGE_BUFFER is the largest size we support
	descSize = sizeof(VMBUS_CHANNEL_PACKET_MULITPAGE_BUFFER) - ((MAX_MULTIPAGE_BUFFER_COUNT - PfnCount)*sizeof(UINT64));
	packetLen = descSize + BufferLen;
	packetLenAligned = ALIGN_UP(packetLen, sizeof(UINT64));

	ASSERT((packetLenAligned - packetLen) < sizeof(UINT64));

	// Setup the descriptor
	desc.Type = VmbusPacketTypeDataUsingGpaDirect;
	desc.Flags = VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED;
    desc.DataOffset8 = descSize >> 3; // in 8-bytes grandularity
    desc.Length8 = (u16)(packetLenAligned >> 3);
    desc.TransactionId = RequestId;
	desc.RangeCount = 1;

	desc.Range.Length = MultiPageBuffer->Length;
	desc.Range.Offset = MultiPageBuffer->Offset;

	memcpy(desc.Range.PfnArray, MultiPageBuffer->PfnArray, PfnCount*sizeof(UINT64));

	bufferList[0].Data = &desc;
	bufferList[0].Length = descSize;

	bufferList[1].Data = Buffer;
	bufferList[1].Length = BufferLen;

	bufferList[2].Data = &alignedData;
	bufferList[2].Length = packetLenAligned - packetLen;

	ret = RingBufferWrite(
		&Channel->Outbound,
		bufferList,
		3);

	// TODO: We should determine if this is optional
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
// TODO: Do we ever receive a gpa direct packet other than the ones we send ?
int
VmbusChannelRecvPacket(
	VMBUS_CHANNEL		*Channel,
	void *				Buffer,
	UINT32				BufferLen,
	UINT32*				BufferActualLen,
	UINT64*				RequestId
	)
{
	VMPACKET_DESCRIPTOR desc;
	UINT32 packetLen;
	UINT32 userLen;
	int ret;

	DPRINT_ENTER(VMBUS);

	*BufferActualLen = 0;
	*RequestId = 0;

	SpinlockAcquire(Channel->InboundLock);

	ret = RingBufferPeek(&Channel->Inbound, &desc, sizeof(VMPACKET_DESCRIPTOR));
	if (ret != 0)
	{
		SpinlockRelease(Channel->InboundLock);

		//DPRINT_DBG(VMBUS, "nothing to read!!");
		DPRINT_EXIT(VMBUS);
		return 0;
	}

	//VmbusChannelClearEvent(Channel);

	packetLen = desc.Length8 << 3;
	userLen = packetLen - (desc.DataOffset8 << 3);
	//ASSERT(userLen > 0);

	DPRINT_DBG(VMBUS, "packet received on channel %p relid %d <type %d flag %d tid %llx pktlen %d datalen %d> ",
		Channel,
		Channel->OfferMsg.ChildRelId,
		desc.Type,
		desc.Flags,
		desc.TransactionId, packetLen, userLen);

	*BufferActualLen = userLen;

	if (userLen > BufferLen)
	{
		SpinlockRelease(Channel->InboundLock);

		DPRINT_ERR(VMBUS, "buffer too small - got %d needs %d", BufferLen, userLen);
		DPRINT_EXIT(VMBUS);

		return -1;
	}

	*RequestId = desc.TransactionId;

	// Copy over the packet to the user buffer
	ret = RingBufferRead(&Channel->Inbound, Buffer, userLen, (desc.DataOffset8 << 3));

	SpinlockRelease(Channel->InboundLock);

	DPRINT_EXIT(VMBUS);

	return 0;
}

/*++

Name:
	VmbusChannelRecvPacketRaw()

Description:
	Retrieve the raw packet on the specified channel

--*/
int
VmbusChannelRecvPacketRaw(
	VMBUS_CHANNEL		*Channel,
	void *				Buffer,
	UINT32				BufferLen,
	UINT32*				BufferActualLen,
	UINT64*				RequestId
	)
{
	VMPACKET_DESCRIPTOR desc;
	UINT32 packetLen;
	UINT32 userLen;
	int ret;

	DPRINT_ENTER(VMBUS);

	*BufferActualLen = 0;
	*RequestId = 0;

	SpinlockAcquire(Channel->InboundLock);

	ret = RingBufferPeek(&Channel->Inbound, &desc, sizeof(VMPACKET_DESCRIPTOR));
	if (ret != 0)
	{
		SpinlockRelease(Channel->InboundLock);

		//DPRINT_DBG(VMBUS, "nothing to read!!");
		DPRINT_EXIT(VMBUS);
		return 0;
	}

	//VmbusChannelClearEvent(Channel);

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
		SpinlockRelease(Channel->InboundLock);

		DPRINT_ERR(VMBUS, "buffer too small - needed %d bytes but got space for only %d bytes", packetLen, BufferLen);
		DPRINT_EXIT(VMBUS);
		return -2;
	}

	*RequestId = desc.TransactionId;

	// Copy over the entire packet to the user buffer
	ret = RingBufferRead(&Channel->Inbound, Buffer, packetLen, 0);

	SpinlockRelease(Channel->InboundLock);

	DPRINT_EXIT(VMBUS);

	return 0;
}


/*++

Name:
	VmbusChannelOnChannelEvent()

Description:
	Channel event callback

--*/
void
VmbusChannelOnChannelEvent(
	VMBUS_CHANNEL		*Channel
	)
{
	DumpVmbusChannel(Channel);
	ASSERT(Channel->OnChannelCallback);
#ifdef ENABLE_POLLING
	TimerStop(Channel->PollTimer);
	Channel->OnChannelCallback(Channel->ChannelCallbackContext);
	TimerStart(Channel->PollTimer, 100 /* 100us */);
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
void
VmbusChannelOnTimer(
	void		*Context
	)
{
	VMBUS_CHANNEL *channel = (VMBUS_CHANNEL*)Context;

	if (channel->OnChannelCallback)
	{
		channel->OnChannelCallback(channel->ChannelCallbackContext);
#ifdef ENABLE_POLLING
		TimerStart(channel->PollTimer, 100 /* 100us */);
#endif
	}
}


/*++

Name:
	DumpVmbusChannel()

Description:
	Dump vmbus channel info to the console

--*/
static void
DumpVmbusChannel(
	VMBUS_CHANNEL		*Channel
	)
{
	DPRINT_DBG(VMBUS, "Channel (%d)", Channel->OfferMsg.ChildRelId);
	DumpRingInfo(&Channel->Outbound, "Outbound ");
	DumpRingInfo(&Channel->Inbound, "Inbound ");
}


// eof
