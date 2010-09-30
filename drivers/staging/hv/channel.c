/*
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
 */
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/module.h>
#include "osd.h"
#include "logging.h"
#include "vmbus_private.h"

/* Internal routines */
static int VmbusChannelCreateGpadlHeader(
	void *kbuffer,	/* must be phys and virt contiguous */
	u32 size,	/* page-size multiple */
	struct vmbus_channel_msginfo **msginfo,
	u32 *messagecount);
static void DumpVmbusChannel(struct vmbus_channel *channel);
static void VmbusChannelSetEvent(struct vmbus_channel *channel);


#if 0
static void DumpMonitorPage(struct hv_monitor_page *MonitorPage)
{
	int i = 0;
	int j = 0;

	DPRINT_DBG(VMBUS, "monitorPage - %p, trigger state - %d",
		   MonitorPage, MonitorPage->TriggerState);

	for (i = 0; i < 4; i++)
		DPRINT_DBG(VMBUS, "trigger group (%d) - %llx", i,
			   MonitorPage->TriggerGroup[i].AsUINT64);

	for (i = 0; i < 4; i++) {
		for (j = 0; j < 32; j++) {
			DPRINT_DBG(VMBUS, "latency (%d)(%d) - %llx", i, j,
				   MonitorPage->Latency[i][j]);
		}
	}
	for (i = 0; i < 4; i++) {
		for (j = 0; j < 32; j++) {
			DPRINT_DBG(VMBUS, "param-conn id (%d)(%d) - %d", i, j,
			       MonitorPage->Parameter[i][j].ConnectionId.Asu32);
			DPRINT_DBG(VMBUS, "param-flag (%d)(%d) - %d", i, j,
				MonitorPage->Parameter[i][j].FlagNumber);
		}
	}
}
#endif

/*
 * VmbusChannelSetEvent - Trigger an event notification on the specified
 * channel.
 */
static void VmbusChannelSetEvent(struct vmbus_channel *channel)
{
	struct hv_monitor_page *monitorpage;

	if (channel->OfferMsg.MonitorAllocated) {
		/* Each u32 represents 32 channels */
		set_bit(channel->OfferMsg.ChildRelId & 31,
			(unsigned long *) gVmbusConnection.SendInterruptPage +
			(channel->OfferMsg.ChildRelId >> 5));

		monitorpage = gVmbusConnection.MonitorPages;
		monitorpage++; /* Get the child to parent monitor page */

		set_bit(channel->MonitorBit,
			(unsigned long *)&monitorpage->TriggerGroup
					[channel->MonitorGroup].Pending);

	} else {
		VmbusSetEvent(channel->OfferMsg.ChildRelId);
	}
}

#if 0
static void VmbusChannelClearEvent(struct vmbus_channel *channel)
{
	struct hv_monitor_page *monitorPage;

	if (Channel->OfferMsg.MonitorAllocated) {
		/* Each u32 represents 32 channels */
		clear_bit(Channel->OfferMsg.ChildRelId & 31,
			  (unsigned long *)gVmbusConnection.SendInterruptPage +
			  (Channel->OfferMsg.ChildRelId >> 5));

		monitorPage =
			(struct hv_monitor_page *)gVmbusConnection.MonitorPages;
		monitorPage++; /* Get the child to parent monitor page */

		clear_bit(Channel->MonitorBit,
			  (unsigned long *)&monitorPage->TriggerGroup
					[Channel->MonitorGroup].Pending);
	}
}

#endif
/*
 * VmbusChannelGetDebugInfo -Retrieve various channel debug info
 */
void VmbusChannelGetDebugInfo(struct vmbus_channel *channel,
			      struct vmbus_channel_debug_info *debuginfo)
{
	struct hv_monitor_page *monitorpage;
	u8 monitor_group = (u8)channel->OfferMsg.MonitorId / 32;
	u8 monitor_offset = (u8)channel->OfferMsg.MonitorId % 32;
	/* u32 monitorBit	= 1 << monitorOffset; */

	debuginfo->RelId = channel->OfferMsg.ChildRelId;
	debuginfo->State = channel->State;
	memcpy(&debuginfo->InterfaceType,
	       &channel->OfferMsg.Offer.InterfaceType, sizeof(struct hv_guid));
	memcpy(&debuginfo->InterfaceInstance,
	       &channel->OfferMsg.Offer.InterfaceInstance,
	       sizeof(struct hv_guid));

	monitorpage = (struct hv_monitor_page *)gVmbusConnection.MonitorPages;

	debuginfo->MonitorId = channel->OfferMsg.MonitorId;

	debuginfo->ServerMonitorPending =
			monitorpage->TriggerGroup[monitor_group].Pending;
	debuginfo->ServerMonitorLatency =
			monitorpage->Latency[monitor_group][monitor_offset];
	debuginfo->ServerMonitorConnectionId =
			monitorpage->Parameter[monitor_group]
					[monitor_offset].ConnectionId.u.Id;

	monitorpage++;

	debuginfo->ClientMonitorPending =
			monitorpage->TriggerGroup[monitor_group].Pending;
	debuginfo->ClientMonitorLatency =
			monitorpage->Latency[monitor_group][monitor_offset];
	debuginfo->ClientMonitorConnectionId =
			monitorpage->Parameter[monitor_group]
					[monitor_offset].ConnectionId.u.Id;

	RingBufferGetDebugInfo(&channel->Inbound, &debuginfo->Inbound);
	RingBufferGetDebugInfo(&channel->Outbound, &debuginfo->Outbound);
}

/*
 * VmbusChannelOpen - Open the specified channel.
 */
int VmbusChannelOpen(struct vmbus_channel *newchannel, u32 send_ringbuffer_size,
		     u32 recv_ringbuffer_size, void *userdata, u32 userdatalen,
		     void (*onchannelcallback)(void *context), void *context)
{
	struct vmbus_channel_open_channel *openMsg;
	struct vmbus_channel_msginfo *openInfo = NULL;
	void *in, *out;
	unsigned long flags;
	int ret, err = 0;

	/* Aligned to page size */
	/* ASSERT(!(SendRingBufferSize & (PAGE_SIZE - 1))); */
	/* ASSERT(!(RecvRingBufferSize & (PAGE_SIZE - 1))); */

	newchannel->OnChannelCallback = onchannelcallback;
	newchannel->ChannelCallbackContext = context;

	/* Allocate the ring buffer */
	out = osd_PageAlloc((send_ringbuffer_size + recv_ringbuffer_size)
			     >> PAGE_SHIFT);
	if (!out)
		return -ENOMEM;

	/* ASSERT(((unsigned long)out & (PAGE_SIZE-1)) == 0); */

	in = (void *)((unsigned long)out + send_ringbuffer_size);

	newchannel->RingBufferPages = out;
	newchannel->RingBufferPageCount = (send_ringbuffer_size +
					   recv_ringbuffer_size) >> PAGE_SHIFT;

	ret = RingBufferInit(&newchannel->Outbound, out, send_ringbuffer_size);
	if (ret != 0) {
		err = ret;
		goto errorout;
	}

	ret = RingBufferInit(&newchannel->Inbound, in, recv_ringbuffer_size);
	if (ret != 0) {
		err = ret;
		goto errorout;
	}


	/* Establish the gpadl for the ring buffer */
	DPRINT_DBG(VMBUS, "Establishing ring buffer's gpadl for channel %p...",
		   newchannel);

	newchannel->RingBufferGpadlHandle = 0;

	ret = VmbusChannelEstablishGpadl(newchannel,
					 newchannel->Outbound.RingBuffer,
					 send_ringbuffer_size +
					 recv_ringbuffer_size,
					 &newchannel->RingBufferGpadlHandle);

	if (ret != 0) {
		err = ret;
		goto errorout;
	}

	DPRINT_DBG(VMBUS, "channel %p <relid %d gpadl 0x%x send ring %p "
		   "size %d recv ring %p size %d, downstreamoffset %d>",
		   newchannel, newchannel->OfferMsg.ChildRelId,
		   newchannel->RingBufferGpadlHandle,
		   newchannel->Outbound.RingBuffer,
		   newchannel->Outbound.RingSize,
		   newchannel->Inbound.RingBuffer,
		   newchannel->Inbound.RingSize,
		   send_ringbuffer_size);

	/* Create and init the channel open message */
	openInfo = kmalloc(sizeof(*openInfo) +
			   sizeof(struct vmbus_channel_open_channel),
			   GFP_KERNEL);
	if (!openInfo) {
		err = -ENOMEM;
		goto errorout;
	}

	openInfo->WaitEvent = osd_WaitEventCreate();
	if (!openInfo->WaitEvent) {
		err = -ENOMEM;
		goto errorout;
	}

	openMsg = (struct vmbus_channel_open_channel *)openInfo->Msg;
	openMsg->Header.MessageType = ChannelMessageOpenChannel;
	openMsg->OpenId = newchannel->OfferMsg.ChildRelId; /* FIXME */
	openMsg->ChildRelId = newchannel->OfferMsg.ChildRelId;
	openMsg->RingBufferGpadlHandle = newchannel->RingBufferGpadlHandle;
	openMsg->DownstreamRingBufferPageOffset = send_ringbuffer_size >>
						  PAGE_SHIFT;
	openMsg->ServerContextAreaGpadlHandle = 0; /* TODO */

	if (userdatalen > MAX_USER_DEFINED_BYTES) {
		err = -EINVAL;
		goto errorout;
	}

	if (userdatalen)
		memcpy(openMsg->UserData, userdata, userdatalen);

	spin_lock_irqsave(&gVmbusConnection.channelmsg_lock, flags);
	list_add_tail(&openInfo->MsgListEntry,
		      &gVmbusConnection.ChannelMsgList);
	spin_unlock_irqrestore(&gVmbusConnection.channelmsg_lock, flags);

	DPRINT_DBG(VMBUS, "Sending channel open msg...");

	ret = VmbusPostMessage(openMsg,
			       sizeof(struct vmbus_channel_open_channel));
	if (ret != 0) {
		DPRINT_ERR(VMBUS, "unable to open channel - %d", ret);
		goto Cleanup;
	}

	/* FIXME: Need to time-out here */
	osd_WaitEventWait(openInfo->WaitEvent);

	if (openInfo->Response.OpenResult.Status == 0)
		DPRINT_INFO(VMBUS, "channel <%p> open success!!", newchannel);
	else
		DPRINT_INFO(VMBUS, "channel <%p> open failed - %d!!",
			    newchannel, openInfo->Response.OpenResult.Status);

Cleanup:
	spin_lock_irqsave(&gVmbusConnection.channelmsg_lock, flags);
	list_del(&openInfo->MsgListEntry);
	spin_unlock_irqrestore(&gVmbusConnection.channelmsg_lock, flags);

	kfree(openInfo->WaitEvent);
	kfree(openInfo);
	return 0;

errorout:
	RingBufferCleanup(&newchannel->Outbound);
	RingBufferCleanup(&newchannel->Inbound);
	osd_PageFree(out, (send_ringbuffer_size + recv_ringbuffer_size)
		     >> PAGE_SHIFT);
	kfree(openInfo);
	return err;
}

/*
 * DumpGpadlBody - Dump the gpadl body message to the console for
 * debugging purposes.
 */
static void DumpGpadlBody(struct vmbus_channel_gpadl_body *gpadl, u32 len)
{
	int i;
	int pfncount;

	pfncount = (len - sizeof(struct vmbus_channel_gpadl_body)) /
		   sizeof(u64);
	DPRINT_DBG(VMBUS, "gpadl body - len %d pfn count %d", len, pfncount);

	for (i = 0; i < pfncount; i++)
		DPRINT_DBG(VMBUS, "gpadl body  - %d) pfn %llu",
			   i, gpadl->Pfn[i]);
}

/*
 * DumpGpadlHeader - Dump the gpadl header message to the console for
 * debugging purposes.
 */
static void DumpGpadlHeader(struct vmbus_channel_gpadl_header *gpadl)
{
	int i, j;
	int pagecount;

	DPRINT_DBG(VMBUS,
		   "gpadl header - relid %d, range count %d, range buflen %d",
		   gpadl->ChildRelId, gpadl->RangeCount, gpadl->RangeBufLen);
	for (i = 0; i < gpadl->RangeCount; i++) {
		pagecount = gpadl->Range[i].ByteCount >> PAGE_SHIFT;
		pagecount = (pagecount > 26) ? 26 : pagecount;

		DPRINT_DBG(VMBUS, "gpadl range %d - len %d offset %d "
			   "page count %d", i, gpadl->Range[i].ByteCount,
			   gpadl->Range[i].ByteOffset, pagecount);

		for (j = 0; j < pagecount; j++)
			DPRINT_DBG(VMBUS, "%d) pfn %llu", j,
				   gpadl->Range[i].PfnArray[j]);
	}
}

/*
 * VmbusChannelCreateGpadlHeader - Creates a gpadl for the specified buffer
 */
static int VmbusChannelCreateGpadlHeader(void *kbuffer, u32 size,
					 struct vmbus_channel_msginfo **msginfo,
					 u32 *messagecount)
{
	int i;
	int pagecount;
	unsigned long long pfn;
	struct vmbus_channel_gpadl_header *gpadl_header;
	struct vmbus_channel_gpadl_body *gpadl_body;
	struct vmbus_channel_msginfo *msgheader;
	struct vmbus_channel_msginfo *msgbody = NULL;
	u32 msgsize;

	int pfnsum, pfncount, pfnleft, pfncurr, pfnsize;

	/* ASSERT((kbuffer & (PAGE_SIZE-1)) == 0); */
	/* ASSERT((Size & (PAGE_SIZE-1)) == 0); */

	pagecount = size >> PAGE_SHIFT;
	pfn = virt_to_phys(kbuffer) >> PAGE_SHIFT;

	/* do we need a gpadl body msg */
	pfnsize = MAX_SIZE_CHANNEL_MESSAGE -
		  sizeof(struct vmbus_channel_gpadl_header) -
		  sizeof(struct gpa_range);
	pfncount = pfnsize / sizeof(u64);

	if (pagecount > pfncount) {
		/* we need a gpadl body */
		/* fill in the header */
		msgsize = sizeof(struct vmbus_channel_msginfo) +
			  sizeof(struct vmbus_channel_gpadl_header) +
			  sizeof(struct gpa_range) + pfncount * sizeof(u64);
		msgheader =  kzalloc(msgsize, GFP_KERNEL);
		if (!msgheader)
			goto nomem;

		INIT_LIST_HEAD(&msgheader->SubMsgList);
		msgheader->MessageSize = msgsize;

		gpadl_header = (struct vmbus_channel_gpadl_header *)
			msgheader->Msg;
		gpadl_header->RangeCount = 1;
		gpadl_header->RangeBufLen = sizeof(struct gpa_range) +
					 pagecount * sizeof(u64);
		gpadl_header->Range[0].ByteOffset = 0;
		gpadl_header->Range[0].ByteCount = size;
		for (i = 0; i < pfncount; i++)
			gpadl_header->Range[0].PfnArray[i] = pfn+i;
		*msginfo = msgheader;
		*messagecount = 1;

		pfnsum = pfncount;
		pfnleft = pagecount - pfncount;

		/* how many pfns can we fit */
		pfnsize = MAX_SIZE_CHANNEL_MESSAGE -
			  sizeof(struct vmbus_channel_gpadl_body);
		pfncount = pfnsize / sizeof(u64);

		/* fill in the body */
		while (pfnleft) {
			if (pfnleft > pfncount)
				pfncurr = pfncount;
			else
				pfncurr = pfnleft;

			msgsize = sizeof(struct vmbus_channel_msginfo) +
				  sizeof(struct vmbus_channel_gpadl_body) +
				  pfncurr * sizeof(u64);
			msgbody = kzalloc(msgsize, GFP_KERNEL);
			/* FIXME: we probably need to more if this fails */
			if (!msgbody)
				goto nomem;
			msgbody->MessageSize = msgsize;
			(*messagecount)++;
			gpadl_body =
				(struct vmbus_channel_gpadl_body *)msgbody->Msg;

			/*
			 * FIXME:
			 * Gpadl is u32 and we are using a pointer which could
			 * be 64-bit
			 */
			/* gpadl_body->Gpadl = kbuffer; */
			for (i = 0; i < pfncurr; i++)
				gpadl_body->Pfn[i] = pfn + pfnsum + i;

			/* add to msg header */
			list_add_tail(&msgbody->MsgListEntry,
				      &msgheader->SubMsgList);
			pfnsum += pfncurr;
			pfnleft -= pfncurr;
		}
	} else {
		/* everything fits in a header */
		msgsize = sizeof(struct vmbus_channel_msginfo) +
			  sizeof(struct vmbus_channel_gpadl_header) +
			  sizeof(struct gpa_range) + pagecount * sizeof(u64);
		msgheader = kzalloc(msgsize, GFP_KERNEL);
		if (msgheader == NULL)
			goto nomem;
		msgheader->MessageSize = msgsize;

		gpadl_header = (struct vmbus_channel_gpadl_header *)
			msgheader->Msg;
		gpadl_header->RangeCount = 1;
		gpadl_header->RangeBufLen = sizeof(struct gpa_range) +
					 pagecount * sizeof(u64);
		gpadl_header->Range[0].ByteOffset = 0;
		gpadl_header->Range[0].ByteCount = size;
		for (i = 0; i < pagecount; i++)
			gpadl_header->Range[0].PfnArray[i] = pfn+i;

		*msginfo = msgheader;
		*messagecount = 1;
	}

	return 0;
nomem:
	kfree(msgheader);
	kfree(msgbody);
	return -ENOMEM;
}

/*
 * VmbusChannelEstablishGpadl - Estabish a GPADL for the specified buffer
 *
 * @channel: a channel
 * @kbuffer: from kmalloc
 * @size: page-size multiple
 * @gpadl_handle: some funky thing
 */
int VmbusChannelEstablishGpadl(struct vmbus_channel *channel, void *kbuffer,
			       u32 size, u32 *gpadl_handle)
{
	struct vmbus_channel_gpadl_header *gpadlmsg;
	struct vmbus_channel_gpadl_body *gpadl_body;
	/* struct vmbus_channel_gpadl_created *gpadlCreated; */
	struct vmbus_channel_msginfo *msginfo = NULL;
	struct vmbus_channel_msginfo *submsginfo;
	u32 msgcount;
	struct list_head *curr;
	u32 next_gpadl_handle;
	unsigned long flags;
	int ret = 0;

	next_gpadl_handle = atomic_read(&gVmbusConnection.NextGpadlHandle);
	atomic_inc(&gVmbusConnection.NextGpadlHandle);

	ret = VmbusChannelCreateGpadlHeader(kbuffer, size, &msginfo, &msgcount);
	if (ret)
		return ret;

	msginfo->WaitEvent = osd_WaitEventCreate();
	if (!msginfo->WaitEvent) {
		ret = -ENOMEM;
		goto Cleanup;
	}

	gpadlmsg = (struct vmbus_channel_gpadl_header *)msginfo->Msg;
	gpadlmsg->Header.MessageType = ChannelMessageGpadlHeader;
	gpadlmsg->ChildRelId = channel->OfferMsg.ChildRelId;
	gpadlmsg->Gpadl = next_gpadl_handle;

	DumpGpadlHeader(gpadlmsg);

	spin_lock_irqsave(&gVmbusConnection.channelmsg_lock, flags);
	list_add_tail(&msginfo->MsgListEntry,
		      &gVmbusConnection.ChannelMsgList);

	spin_unlock_irqrestore(&gVmbusConnection.channelmsg_lock, flags);
	DPRINT_DBG(VMBUS, "buffer %p, size %d msg cnt %d",
		   kbuffer, size, msgcount);

	DPRINT_DBG(VMBUS, "Sending GPADL Header - len %zd",
		   msginfo->MessageSize - sizeof(*msginfo));

	ret = VmbusPostMessage(gpadlmsg, msginfo->MessageSize -
			       sizeof(*msginfo));
	if (ret != 0) {
		DPRINT_ERR(VMBUS, "Unable to open channel - %d", ret);
		goto Cleanup;
	}

	if (msgcount > 1) {
		list_for_each(curr, &msginfo->SubMsgList) {

			/* FIXME: should this use list_entry() instead ? */
			submsginfo = (struct vmbus_channel_msginfo *)curr;
			gpadl_body =
			     (struct vmbus_channel_gpadl_body *)submsginfo->Msg;

			gpadl_body->Header.MessageType =
				ChannelMessageGpadlBody;
			gpadl_body->Gpadl = next_gpadl_handle;

			DPRINT_DBG(VMBUS, "Sending GPADL Body - len %zd",
				   submsginfo->MessageSize -
				   sizeof(*submsginfo));

			DumpGpadlBody(gpadl_body, submsginfo->MessageSize -
				      sizeof(*submsginfo));
			ret = VmbusPostMessage(gpadl_body,
					       submsginfo->MessageSize -
					       sizeof(*submsginfo));
			if (ret != 0)
				goto Cleanup;

		}
	}
	osd_WaitEventWait(msginfo->WaitEvent);

	/* At this point, we received the gpadl created msg */
	DPRINT_DBG(VMBUS, "Received GPADL created "
		   "(relid %d, status %d handle %x)",
		   channel->OfferMsg.ChildRelId,
		   msginfo->Response.GpadlCreated.CreationStatus,
		   gpadlmsg->Gpadl);

	*gpadl_handle = gpadlmsg->Gpadl;

Cleanup:
	spin_lock_irqsave(&gVmbusConnection.channelmsg_lock, flags);
	list_del(&msginfo->MsgListEntry);
	spin_unlock_irqrestore(&gVmbusConnection.channelmsg_lock, flags);

	kfree(msginfo->WaitEvent);
	kfree(msginfo);
	return ret;
}

/*
 * VmbusChannelTeardownGpadl -Teardown the specified GPADL handle
 */
int VmbusChannelTeardownGpadl(struct vmbus_channel *channel, u32 gpadl_handle)
{
	struct vmbus_channel_gpadl_teardown *msg;
	struct vmbus_channel_msginfo *info;
	unsigned long flags;
	int ret;

	/* ASSERT(gpadl_handle != 0); */

	info = kmalloc(sizeof(*info) +
		       sizeof(struct vmbus_channel_gpadl_teardown), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->WaitEvent = osd_WaitEventCreate();
	if (!info->WaitEvent) {
		kfree(info);
		return -ENOMEM;
	}

	msg = (struct vmbus_channel_gpadl_teardown *)info->Msg;

	msg->Header.MessageType = ChannelMessageGpadlTeardown;
	msg->ChildRelId = channel->OfferMsg.ChildRelId;
	msg->Gpadl = gpadl_handle;

	spin_lock_irqsave(&gVmbusConnection.channelmsg_lock, flags);
	list_add_tail(&info->MsgListEntry,
		      &gVmbusConnection.ChannelMsgList);
	spin_unlock_irqrestore(&gVmbusConnection.channelmsg_lock, flags);

	ret = VmbusPostMessage(msg,
			       sizeof(struct vmbus_channel_gpadl_teardown));
	if (ret != 0) {
		/* TODO: */
		/* something... */
	}

	osd_WaitEventWait(info->WaitEvent);

	/* Received a torndown response */
	spin_lock_irqsave(&gVmbusConnection.channelmsg_lock, flags);
	list_del(&info->MsgListEntry);
	spin_unlock_irqrestore(&gVmbusConnection.channelmsg_lock, flags);

	kfree(info->WaitEvent);
	kfree(info);
	return ret;
}

/*
 * VmbusChannelClose - Close the specified channel
 */
void VmbusChannelClose(struct vmbus_channel *channel)
{
	struct vmbus_channel_close_channel *msg;
	struct vmbus_channel_msginfo *info;
	unsigned long flags;
	int ret;

	/* Stop callback and cancel the timer asap */
	channel->OnChannelCallback = NULL;
	del_timer_sync(&channel->poll_timer);

	/* Send a closing message */
	info = kmalloc(sizeof(*info) +
		       sizeof(struct vmbus_channel_close_channel), GFP_KERNEL);
        /* FIXME: can't do anything other than return here because the
	 *        function is void */
	if (!info)
		return;

	/* info->waitEvent = osd_WaitEventCreate(); */

	msg = (struct vmbus_channel_close_channel *)info->Msg;
	msg->Header.MessageType = ChannelMessageCloseChannel;
	msg->ChildRelId = channel->OfferMsg.ChildRelId;

	ret = VmbusPostMessage(msg, sizeof(struct vmbus_channel_close_channel));
	if (ret != 0) {
		/* TODO: */
		/* something... */
	}

	/* Tear down the gpadl for the channel's ring buffer */
	if (channel->RingBufferGpadlHandle)
		VmbusChannelTeardownGpadl(channel,
					  channel->RingBufferGpadlHandle);

	/* TODO: Send a msg to release the childRelId */

	/* Cleanup the ring buffers for this channel */
	RingBufferCleanup(&channel->Outbound);
	RingBufferCleanup(&channel->Inbound);

	osd_PageFree(channel->RingBufferPages, channel->RingBufferPageCount);

	kfree(info);

	/*
	 * If we are closing the channel during an error path in
	 * opening the channel, don't free the channel since the
	 * caller will free the channel
	 */

	if (channel->State == CHANNEL_OPEN_STATE) {
		spin_lock_irqsave(&gVmbusConnection.channel_lock, flags);
		list_del(&channel->ListEntry);
		spin_unlock_irqrestore(&gVmbusConnection.channel_lock, flags);

		FreeVmbusChannel(channel);
	}
}

/**
 * VmbusChannelSendPacket() - Send the specified buffer on the given channel
 * @channel: Pointer to vmbus_channel structure.
 * @buffer: Pointer to the buffer you want to receive the data into.
 * @bufferlen: Maximum size of what the the buffer will hold
 * @requestid: Identifier of the request
 * @type: Type of packet that is being send e.g. negotiate, time
 * packet etc.
 *
 * Sends data in @buffer directly to hyper-v via the vmbus
 * This will send the data unparsed to hyper-v.
 *
 * Mainly used by Hyper-V drivers.
 */
int VmbusChannelSendPacket(struct vmbus_channel *channel, const void *buffer,
			   u32 bufferlen, u64 requestid,
			   enum vmbus_packet_type type, u32 flags)
{
	struct vmpacket_descriptor desc;
	u32 packetlen = sizeof(struct vmpacket_descriptor) + bufferlen;
	u32 packetlen_aligned = ALIGN_UP(packetlen, sizeof(u64));
	struct scatterlist bufferlist[3];
	u64 aligned_data = 0;
	int ret;

	DPRINT_DBG(VMBUS, "channel %p buffer %p len %d",
		   channel, buffer, bufferlen);

	DumpVmbusChannel(channel);

	/* ASSERT((packetLenAligned - packetLen) < sizeof(u64)); */

	/* Setup the descriptor */
	desc.Type = type; /* VmbusPacketTypeDataInBand; */
	desc.Flags = flags; /* VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED; */
	/* in 8-bytes granularity */
	desc.DataOffset8 = sizeof(struct vmpacket_descriptor) >> 3;
	desc.Length8 = (u16)(packetlen_aligned >> 3);
	desc.TransactionId = requestid;

	sg_init_table(bufferlist, 3);
	sg_set_buf(&bufferlist[0], &desc, sizeof(struct vmpacket_descriptor));
	sg_set_buf(&bufferlist[1], buffer, bufferlen);
	sg_set_buf(&bufferlist[2], &aligned_data,
		   packetlen_aligned - packetlen);

	ret = RingBufferWrite(&channel->Outbound, bufferlist, 3);

	/* TODO: We should determine if this is optional */
	if (ret == 0 && !GetRingBufferInterruptMask(&channel->Outbound))
		VmbusChannelSetEvent(channel);

	return ret;
}
EXPORT_SYMBOL(VmbusChannelSendPacket);

/*
 * VmbusChannelSendPacketPageBuffer - Send a range of single-page buffer
 * packets using a GPADL Direct packet type.
 */
int VmbusChannelSendPacketPageBuffer(struct vmbus_channel *channel,
				     struct hv_page_buffer pagebuffers[],
				     u32 pagecount, void *buffer, u32 bufferlen,
				     u64 requestid)
{
	int ret;
	int i;
	struct vmbus_channel_packet_page_buffer desc;
	u32 descsize;
	u32 packetlen;
	u32 packetlen_aligned;
	struct scatterlist bufferlist[3];
	u64 aligned_data = 0;

	if (pagecount > MAX_PAGE_BUFFER_COUNT)
		return -EINVAL;

	DumpVmbusChannel(channel);

	/*
	 * Adjust the size down since vmbus_channel_packet_page_buffer is the
	 * largest size we support
	 */
	descsize = sizeof(struct vmbus_channel_packet_page_buffer) -
			  ((MAX_PAGE_BUFFER_COUNT - pagecount) *
			  sizeof(struct hv_page_buffer));
	packetlen = descsize + bufferlen;
	packetlen_aligned = ALIGN_UP(packetlen, sizeof(u64));

	/* ASSERT((packetLenAligned - packetLen) < sizeof(u64)); */

	/* Setup the descriptor */
	desc.type = VmbusPacketTypeDataUsingGpaDirect;
	desc.flags = VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED;
	desc.dataoffset8 = descsize >> 3; /* in 8-bytes grandularity */
	desc.length8 = (u16)(packetlen_aligned >> 3);
	desc.transactionid = requestid;
	desc.rangecount = pagecount;

	for (i = 0; i < pagecount; i++) {
		desc.range[i].Length = pagebuffers[i].Length;
		desc.range[i].Offset = pagebuffers[i].Offset;
		desc.range[i].Pfn	 = pagebuffers[i].Pfn;
	}

	sg_init_table(bufferlist, 3);
	sg_set_buf(&bufferlist[0], &desc, descsize);
	sg_set_buf(&bufferlist[1], buffer, bufferlen);
	sg_set_buf(&bufferlist[2], &aligned_data,
		packetlen_aligned - packetlen);

	ret = RingBufferWrite(&channel->Outbound, bufferlist, 3);

	/* TODO: We should determine if this is optional */
	if (ret == 0 && !GetRingBufferInterruptMask(&channel->Outbound))
		VmbusChannelSetEvent(channel);

	return ret;
}

/*
 * VmbusChannelSendPacketMultiPageBuffer - Send a multi-page buffer packet
 * using a GPADL Direct packet type.
 */
int VmbusChannelSendPacketMultiPageBuffer(struct vmbus_channel *channel,
				struct hv_multipage_buffer *multi_pagebuffer,
				void *buffer, u32 bufferlen, u64 requestid)
{
	int ret;
	struct vmbus_channel_packet_multipage_buffer desc;
	u32 descsize;
	u32 packetlen;
	u32 packetlen_aligned;
	struct scatterlist bufferlist[3];
	u64 aligned_data = 0;
	u32 pfncount = NUM_PAGES_SPANNED(multi_pagebuffer->Offset,
					 multi_pagebuffer->Length);

	DumpVmbusChannel(channel);

	DPRINT_DBG(VMBUS, "data buffer - offset %u len %u pfn count %u",
		multi_pagebuffer->Offset,
		multi_pagebuffer->Length, pfncount);

	if ((pfncount < 0) || (pfncount > MAX_MULTIPAGE_BUFFER_COUNT))
		return -EINVAL;

	/*
	 * Adjust the size down since vmbus_channel_packet_multipage_buffer is
	 * the largest size we support
	 */
	descsize = sizeof(struct vmbus_channel_packet_multipage_buffer) -
			  ((MAX_MULTIPAGE_BUFFER_COUNT - pfncount) *
			  sizeof(u64));
	packetlen = descsize + bufferlen;
	packetlen_aligned = ALIGN_UP(packetlen, sizeof(u64));

	/* ASSERT((packetLenAligned - packetLen) < sizeof(u64)); */

	/* Setup the descriptor */
	desc.type = VmbusPacketTypeDataUsingGpaDirect;
	desc.flags = VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED;
	desc.dataoffset8 = descsize >> 3; /* in 8-bytes grandularity */
	desc.length8 = (u16)(packetlen_aligned >> 3);
	desc.transactionid = requestid;
	desc.rangecount = 1;

	desc.range.Length = multi_pagebuffer->Length;
	desc.range.Offset = multi_pagebuffer->Offset;

	memcpy(desc.range.PfnArray, multi_pagebuffer->PfnArray,
	       pfncount * sizeof(u64));

	sg_init_table(bufferlist, 3);
	sg_set_buf(&bufferlist[0], &desc, descsize);
	sg_set_buf(&bufferlist[1], buffer, bufferlen);
	sg_set_buf(&bufferlist[2], &aligned_data,
		packetlen_aligned - packetlen);

	ret = RingBufferWrite(&channel->Outbound, bufferlist, 3);

	/* TODO: We should determine if this is optional */
	if (ret == 0 && !GetRingBufferInterruptMask(&channel->Outbound))
		VmbusChannelSetEvent(channel);

	return ret;
}


/**
 * VmbusChannelRecvPacket() - Retrieve the user packet on the specified channel
 * @channel: Pointer to vmbus_channel structure.
 * @buffer: Pointer to the buffer you want to receive the data into.
 * @bufferlen: Maximum size of what the the buffer will hold
 * @buffer_actual_len: The actual size of the data after it was received
 * @requestid: Identifier of the request
 *
 * Receives directly from the hyper-v vmbus and puts the data it received
 * into Buffer. This will receive the data unparsed from hyper-v.
 *
 * Mainly used by Hyper-V drivers.
 */
int VmbusChannelRecvPacket(struct vmbus_channel *channel, void *buffer,
			u32 bufferlen, u32 *buffer_actual_len, u64 *requestid)
{
	struct vmpacket_descriptor desc;
	u32 packetlen;
	u32 userlen;
	int ret;
	unsigned long flags;

	*buffer_actual_len = 0;
	*requestid = 0;

	spin_lock_irqsave(&channel->inbound_lock, flags);

	ret = RingBufferPeek(&channel->Inbound, &desc,
			     sizeof(struct vmpacket_descriptor));
	if (ret != 0) {
		spin_unlock_irqrestore(&channel->inbound_lock, flags);

		/* DPRINT_DBG(VMBUS, "nothing to read!!"); */
		return 0;
	}

	/* VmbusChannelClearEvent(Channel); */

	packetlen = desc.Length8 << 3;
	userlen = packetlen - (desc.DataOffset8 << 3);
	/* ASSERT(userLen > 0); */

	DPRINT_DBG(VMBUS, "packet received on channel %p relid %d <type %d "
		   "flag %d tid %llx pktlen %d datalen %d> ",
		   channel, channel->OfferMsg.ChildRelId, desc.Type,
		   desc.Flags, desc.TransactionId, packetlen, userlen);

	*buffer_actual_len = userlen;

	if (userlen > bufferlen) {
		spin_unlock_irqrestore(&channel->inbound_lock, flags);

		DPRINT_ERR(VMBUS, "buffer too small - got %d needs %d",
			   bufferlen, userlen);
		return -1;
	}

	*requestid = desc.TransactionId;

	/* Copy over the packet to the user buffer */
	ret = RingBufferRead(&channel->Inbound, buffer, userlen,
			     (desc.DataOffset8 << 3));

	spin_unlock_irqrestore(&channel->inbound_lock, flags);

	return 0;
}
EXPORT_SYMBOL(VmbusChannelRecvPacket);

/*
 * VmbusChannelRecvPacketRaw - Retrieve the raw packet on the specified channel
 */
int VmbusChannelRecvPacketRaw(struct vmbus_channel *channel, void *buffer,
			      u32 bufferlen, u32 *buffer_actual_len,
			      u64 *requestid)
{
	struct vmpacket_descriptor desc;
	u32 packetlen;
	u32 userlen;
	int ret;
	unsigned long flags;

	*buffer_actual_len = 0;
	*requestid = 0;

	spin_lock_irqsave(&channel->inbound_lock, flags);

	ret = RingBufferPeek(&channel->Inbound, &desc,
			     sizeof(struct vmpacket_descriptor));
	if (ret != 0) {
		spin_unlock_irqrestore(&channel->inbound_lock, flags);

		/* DPRINT_DBG(VMBUS, "nothing to read!!"); */
		return 0;
	}

	/* VmbusChannelClearEvent(Channel); */

	packetlen = desc.Length8 << 3;
	userlen = packetlen - (desc.DataOffset8 << 3);

	DPRINT_DBG(VMBUS, "packet received on channel %p relid %d <type %d "
		   "flag %d tid %llx pktlen %d datalen %d> ",
		   channel, channel->OfferMsg.ChildRelId, desc.Type,
		   desc.Flags, desc.TransactionId, packetlen, userlen);

	*buffer_actual_len = packetlen;

	if (packetlen > bufferlen) {
		spin_unlock_irqrestore(&channel->inbound_lock, flags);

		DPRINT_ERR(VMBUS, "buffer too small - needed %d bytes but "
			   "got space for only %d bytes", packetlen, bufferlen);
		return -2;
	}

	*requestid = desc.TransactionId;

	/* Copy over the entire packet to the user buffer */
	ret = RingBufferRead(&channel->Inbound, buffer, packetlen, 0);

	spin_unlock_irqrestore(&channel->inbound_lock, flags);
	return 0;
}

/*
 * VmbusChannelOnChannelEvent - Channel event callback
 */
void VmbusChannelOnChannelEvent(struct vmbus_channel *channel)
{
	DumpVmbusChannel(channel);
	/* ASSERT(Channel->OnChannelCallback); */

	channel->OnChannelCallback(channel->ChannelCallbackContext);

	mod_timer(&channel->poll_timer, jiffies + usecs_to_jiffies(100));
}

/*
 * VmbusChannelOnTimer - Timer event callback
 */
void VmbusChannelOnTimer(unsigned long data)
{
	struct vmbus_channel *channel = (struct vmbus_channel *)data;

	if (channel->OnChannelCallback)
		channel->OnChannelCallback(channel->ChannelCallbackContext);
}

/*
 * DumpVmbusChannel - Dump vmbus channel info to the console
 */
static void DumpVmbusChannel(struct vmbus_channel *channel)
{
	DPRINT_DBG(VMBUS, "Channel (%d)", channel->OfferMsg.ChildRelId);
	DumpRingInfo(&channel->Outbound, "Outbound ");
	DumpRingInfo(&channel->Inbound, "Inbound ");
}
