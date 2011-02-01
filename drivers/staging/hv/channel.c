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
static int create_gpadl_header(
	void *kbuffer,	/* must be phys and virt contiguous */
	u32 size,	/* page-size multiple */
	struct vmbus_channel_msginfo **msginfo,
	u32 *messagecount);
static void dump_vmbus_channel(struct vmbus_channel *channel);
static void vmbus_setevent(struct vmbus_channel *channel);


#if 0
static void DumpMonitorPage(struct hv_monitor_page *MonitorPage)
{
	int i = 0;
	int j = 0;

	DPRINT_DBG(VMBUS, "monitorPage - %p, trigger state - %d",
		   MonitorPage, MonitorPage->trigger_state);

	for (i = 0; i < 4; i++)
		DPRINT_DBG(VMBUS, "trigger group (%d) - %llx", i,
			   MonitorPage->trigger_group[i].as_uint64);

	for (i = 0; i < 4; i++) {
		for (j = 0; j < 32; j++) {
			DPRINT_DBG(VMBUS, "latency (%d)(%d) - %llx", i, j,
				   MonitorPage->latency[i][j]);
		}
	}
	for (i = 0; i < 4; i++) {
		for (j = 0; j < 32; j++) {
			DPRINT_DBG(VMBUS, "param-conn id (%d)(%d) - %d", i, j,
			       MonitorPage->parameter[i][j].connectionid.asu32);
			DPRINT_DBG(VMBUS, "param-flag (%d)(%d) - %d", i, j,
				MonitorPage->parameter[i][j].flag_number);
		}
	}
}
#endif

/*
 * vmbus_setevent- Trigger an event notification on the specified
 * channel.
 */
static void vmbus_setevent(struct vmbus_channel *channel)
{
	struct hv_monitor_page *monitorpage;

	if (channel->offermsg.monitor_allocated) {
		/* Each u32 represents 32 channels */
		set_bit(channel->offermsg.child_relid & 31,
			(unsigned long *) gVmbusConnection.SendInterruptPage +
			(channel->offermsg.child_relid >> 5));

		monitorpage = gVmbusConnection.MonitorPages;
		monitorpage++; /* Get the child to parent monitor page */

		set_bit(channel->monitor_bit,
			(unsigned long *)&monitorpage->trigger_group
					[channel->monitor_grp].pending);

	} else {
		VmbusSetEvent(channel->offermsg.child_relid);
	}
}

#if 0
static void VmbusChannelClearEvent(struct vmbus_channel *channel)
{
	struct hv_monitor_page *monitorPage;

	if (Channel->offermsg.monitor_allocated) {
		/* Each u32 represents 32 channels */
		clear_bit(Channel->offermsg.child_relid & 31,
			  (unsigned long *)gVmbusConnection.SendInterruptPage +
			  (Channel->offermsg.child_relid >> 5));

		monitorPage =
			(struct hv_monitor_page *)gVmbusConnection.MonitorPages;
		monitorPage++; /* Get the child to parent monitor page */

		clear_bit(Channel->monitor_bit,
			  (unsigned long *)&monitorPage->trigger_group
					[Channel->monitor_grp].Pending);
	}
}

#endif
/*
 * vmbus_get_debug_info -Retrieve various channel debug info
 */
void vmbus_get_debug_info(struct vmbus_channel *channel,
			      struct vmbus_channel_debug_info *debuginfo)
{
	struct hv_monitor_page *monitorpage;
	u8 monitor_group = (u8)channel->offermsg.monitorid / 32;
	u8 monitor_offset = (u8)channel->offermsg.monitorid % 32;
	/* u32 monitorBit	= 1 << monitorOffset; */

	debuginfo->relid = channel->offermsg.child_relid;
	debuginfo->state = channel->state;
	memcpy(&debuginfo->interfacetype,
	       &channel->offermsg.offer.InterfaceType, sizeof(struct hv_guid));
	memcpy(&debuginfo->interface_instance,
	       &channel->offermsg.offer.InterfaceInstance,
	       sizeof(struct hv_guid));

	monitorpage = (struct hv_monitor_page *)gVmbusConnection.MonitorPages;

	debuginfo->monitorid = channel->offermsg.monitorid;

	debuginfo->servermonitor_pending =
			monitorpage->trigger_group[monitor_group].pending;
	debuginfo->servermonitor_latency =
			monitorpage->latency[monitor_group][monitor_offset];
	debuginfo->servermonitor_connectionid =
			monitorpage->parameter[monitor_group]
					[monitor_offset].connectionid.u.id;

	monitorpage++;

	debuginfo->clientmonitor_pending =
			monitorpage->trigger_group[monitor_group].pending;
	debuginfo->clientmonitor_latency =
			monitorpage->latency[monitor_group][monitor_offset];
	debuginfo->clientmonitor_connectionid =
			monitorpage->parameter[monitor_group]
					[monitor_offset].connectionid.u.id;

	ringbuffer_get_debuginfo(&channel->inbound, &debuginfo->inbound);
	ringbuffer_get_debuginfo(&channel->outbound, &debuginfo->outbound);
}

/*
 * vmbus_open - Open the specified channel.
 */
int vmbus_open(struct vmbus_channel *newchannel, u32 send_ringbuffer_size,
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

	newchannel->onchannel_callback = onchannelcallback;
	newchannel->channel_callback_context = context;

	/* Allocate the ring buffer */
	out = osd_page_alloc((send_ringbuffer_size + recv_ringbuffer_size)
			     >> PAGE_SHIFT);
	if (!out)
		return -ENOMEM;

	/* ASSERT(((unsigned long)out & (PAGE_SIZE-1)) == 0); */

	in = (void *)((unsigned long)out + send_ringbuffer_size);

	newchannel->ringbuffer_pages = out;
	newchannel->ringbuffer_pagecount = (send_ringbuffer_size +
					   recv_ringbuffer_size) >> PAGE_SHIFT;

	ret = ringbuffer_init(&newchannel->outbound, out, send_ringbuffer_size);
	if (ret != 0) {
		err = ret;
		goto errorout;
	}

	ret = ringbuffer_init(&newchannel->inbound, in, recv_ringbuffer_size);
	if (ret != 0) {
		err = ret;
		goto errorout;
	}


	/* Establish the gpadl for the ring buffer */
	DPRINT_DBG(VMBUS, "Establishing ring buffer's gpadl for channel %p...",
		   newchannel);

	newchannel->ringbuffer_gpadlhandle = 0;

	ret = vmbus_establish_gpadl(newchannel,
					 newchannel->outbound.ring_buffer,
					 send_ringbuffer_size +
					 recv_ringbuffer_size,
					 &newchannel->ringbuffer_gpadlhandle);

	if (ret != 0) {
		err = ret;
		goto errorout;
	}

	DPRINT_DBG(VMBUS, "channel %p <relid %d gpadl 0x%x send ring %p "
		   "size %d recv ring %p size %d, downstreamoffset %d>",
		   newchannel, newchannel->offermsg.child_relid,
		   newchannel->ringbuffer_gpadlhandle,
		   newchannel->outbound.ring_buffer,
		   newchannel->outbound.ring_size,
		   newchannel->inbound.ring_buffer,
		   newchannel->inbound.ring_size,
		   send_ringbuffer_size);

	/* Create and init the channel open message */
	openInfo = kmalloc(sizeof(*openInfo) +
			   sizeof(struct vmbus_channel_open_channel),
			   GFP_KERNEL);
	if (!openInfo) {
		err = -ENOMEM;
		goto errorout;
	}

	openInfo->waitevent = osd_waitevent_create();
	if (!openInfo->waitevent) {
		err = -ENOMEM;
		goto errorout;
	}

	openMsg = (struct vmbus_channel_open_channel *)openInfo->msg;
	openMsg->header.msgtype = CHANNELMSG_OPENCHANNEL;
	openMsg->openid = newchannel->offermsg.child_relid; /* FIXME */
	openMsg->child_relid = newchannel->offermsg.child_relid;
	openMsg->ringbuffer_gpadlhandle = newchannel->ringbuffer_gpadlhandle;
	openMsg->downstream_ringbuffer_pageoffset = send_ringbuffer_size >>
						  PAGE_SHIFT;
	openMsg->server_contextarea_gpadlhandle = 0; /* TODO */

	if (userdatalen > MAX_USER_DEFINED_BYTES) {
		err = -EINVAL;
		goto errorout;
	}

	if (userdatalen)
		memcpy(openMsg->userdata, userdata, userdatalen);

	spin_lock_irqsave(&gVmbusConnection.channelmsg_lock, flags);
	list_add_tail(&openInfo->msglistentry,
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
	osd_waitevent_wait(openInfo->waitevent);

	if (openInfo->response.open_result.status == 0)
		DPRINT_INFO(VMBUS, "channel <%p> open success!!", newchannel);
	else
		DPRINT_INFO(VMBUS, "channel <%p> open failed - %d!!",
			    newchannel, openInfo->response.open_result.status);

Cleanup:
	spin_lock_irqsave(&gVmbusConnection.channelmsg_lock, flags);
	list_del(&openInfo->msglistentry);
	spin_unlock_irqrestore(&gVmbusConnection.channelmsg_lock, flags);

	kfree(openInfo->waitevent);
	kfree(openInfo);
	return 0;

errorout:
	ringbuffer_cleanup(&newchannel->outbound);
	ringbuffer_cleanup(&newchannel->inbound);
	osd_page_free(out, (send_ringbuffer_size + recv_ringbuffer_size)
		     >> PAGE_SHIFT);
	kfree(openInfo);
	return err;
}
EXPORT_SYMBOL_GPL(vmbus_open);

/*
 * dump_gpadl_body - Dump the gpadl body message to the console for
 * debugging purposes.
 */
static void dump_gpadl_body(struct vmbus_channel_gpadl_body *gpadl, u32 len)
{
	int i;
	int pfncount;

	pfncount = (len - sizeof(struct vmbus_channel_gpadl_body)) /
		   sizeof(u64);
	DPRINT_DBG(VMBUS, "gpadl body - len %d pfn count %d", len, pfncount);

	for (i = 0; i < pfncount; i++)
		DPRINT_DBG(VMBUS, "gpadl body  - %d) pfn %llu",
			   i, gpadl->pfn[i]);
}

/*
 * dump_gpadl_header - Dump the gpadl header message to the console for
 * debugging purposes.
 */
static void dump_gpadl_header(struct vmbus_channel_gpadl_header *gpadl)
{
	int i, j;
	int pagecount;

	DPRINT_DBG(VMBUS,
		   "gpadl header - relid %d, range count %d, range buflen %d",
		   gpadl->child_relid, gpadl->rangecount, gpadl->range_buflen);
	for (i = 0; i < gpadl->rangecount; i++) {
		pagecount = gpadl->range[i].ByteCount >> PAGE_SHIFT;
		pagecount = (pagecount > 26) ? 26 : pagecount;

		DPRINT_DBG(VMBUS, "gpadl range %d - len %d offset %d "
			   "page count %d", i, gpadl->range[i].ByteCount,
			   gpadl->range[i].ByteOffset, pagecount);

		for (j = 0; j < pagecount; j++)
			DPRINT_DBG(VMBUS, "%d) pfn %llu", j,
				   gpadl->range[i].PfnArray[j]);
	}
}

/*
 * create_gpadl_header - Creates a gpadl for the specified buffer
 */
static int create_gpadl_header(void *kbuffer, u32 size,
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

		INIT_LIST_HEAD(&msgheader->submsglist);
		msgheader->msgsize = msgsize;

		gpadl_header = (struct vmbus_channel_gpadl_header *)
			msgheader->msg;
		gpadl_header->rangecount = 1;
		gpadl_header->range_buflen = sizeof(struct gpa_range) +
					 pagecount * sizeof(u64);
		gpadl_header->range[0].ByteOffset = 0;
		gpadl_header->range[0].ByteCount = size;
		for (i = 0; i < pfncount; i++)
			gpadl_header->range[0].PfnArray[i] = pfn+i;
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
			msgbody->msgsize = msgsize;
			(*messagecount)++;
			gpadl_body =
				(struct vmbus_channel_gpadl_body *)msgbody->msg;

			/*
			 * FIXME:
			 * Gpadl is u32 and we are using a pointer which could
			 * be 64-bit
			 */
			/* gpadl_body->Gpadl = kbuffer; */
			for (i = 0; i < pfncurr; i++)
				gpadl_body->pfn[i] = pfn + pfnsum + i;

			/* add to msg header */
			list_add_tail(&msgbody->msglistentry,
				      &msgheader->submsglist);
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
		msgheader->msgsize = msgsize;

		gpadl_header = (struct vmbus_channel_gpadl_header *)
			msgheader->msg;
		gpadl_header->rangecount = 1;
		gpadl_header->range_buflen = sizeof(struct gpa_range) +
					 pagecount * sizeof(u64);
		gpadl_header->range[0].ByteOffset = 0;
		gpadl_header->range[0].ByteCount = size;
		for (i = 0; i < pagecount; i++)
			gpadl_header->range[0].PfnArray[i] = pfn+i;

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
 * vmbus_establish_gpadl - Estabish a GPADL for the specified buffer
 *
 * @channel: a channel
 * @kbuffer: from kmalloc
 * @size: page-size multiple
 * @gpadl_handle: some funky thing
 */
int vmbus_establish_gpadl(struct vmbus_channel *channel, void *kbuffer,
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

	ret = create_gpadl_header(kbuffer, size, &msginfo, &msgcount);
	if (ret)
		return ret;

	msginfo->waitevent = osd_waitevent_create();
	if (!msginfo->waitevent) {
		ret = -ENOMEM;
		goto Cleanup;
	}

	gpadlmsg = (struct vmbus_channel_gpadl_header *)msginfo->msg;
	gpadlmsg->header.msgtype = CHANNELMSG_GPADL_HEADER;
	gpadlmsg->child_relid = channel->offermsg.child_relid;
	gpadlmsg->gpadl = next_gpadl_handle;

	dump_gpadl_header(gpadlmsg);

	spin_lock_irqsave(&gVmbusConnection.channelmsg_lock, flags);
	list_add_tail(&msginfo->msglistentry,
		      &gVmbusConnection.ChannelMsgList);

	spin_unlock_irqrestore(&gVmbusConnection.channelmsg_lock, flags);
	DPRINT_DBG(VMBUS, "buffer %p, size %d msg cnt %d",
		   kbuffer, size, msgcount);

	DPRINT_DBG(VMBUS, "Sending GPADL Header - len %zd",
		   msginfo->msgsize - sizeof(*msginfo));

	ret = VmbusPostMessage(gpadlmsg, msginfo->msgsize -
			       sizeof(*msginfo));
	if (ret != 0) {
		DPRINT_ERR(VMBUS, "Unable to open channel - %d", ret);
		goto Cleanup;
	}

	if (msgcount > 1) {
		list_for_each(curr, &msginfo->submsglist) {

			/* FIXME: should this use list_entry() instead ? */
			submsginfo = (struct vmbus_channel_msginfo *)curr;
			gpadl_body =
			     (struct vmbus_channel_gpadl_body *)submsginfo->msg;

			gpadl_body->header.msgtype =
				CHANNELMSG_GPADL_BODY;
			gpadl_body->gpadl = next_gpadl_handle;

			DPRINT_DBG(VMBUS, "Sending GPADL Body - len %zd",
				   submsginfo->msgsize -
				   sizeof(*submsginfo));

			dump_gpadl_body(gpadl_body, submsginfo->msgsize -
				      sizeof(*submsginfo));
			ret = VmbusPostMessage(gpadl_body,
					       submsginfo->msgsize -
					       sizeof(*submsginfo));
			if (ret != 0)
				goto Cleanup;

		}
	}
	osd_waitevent_wait(msginfo->waitevent);

	/* At this point, we received the gpadl created msg */
	DPRINT_DBG(VMBUS, "Received GPADL created "
		   "(relid %d, status %d handle %x)",
		   channel->offermsg.child_relid,
		   msginfo->response.gpadl_created.creation_status,
		   gpadlmsg->gpadl);

	*gpadl_handle = gpadlmsg->gpadl;

Cleanup:
	spin_lock_irqsave(&gVmbusConnection.channelmsg_lock, flags);
	list_del(&msginfo->msglistentry);
	spin_unlock_irqrestore(&gVmbusConnection.channelmsg_lock, flags);

	kfree(msginfo->waitevent);
	kfree(msginfo);
	return ret;
}
EXPORT_SYMBOL_GPL(vmbus_establish_gpadl);

/*
 * vmbus_teardown_gpadl -Teardown the specified GPADL handle
 */
int vmbus_teardown_gpadl(struct vmbus_channel *channel, u32 gpadl_handle)
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

	info->waitevent = osd_waitevent_create();
	if (!info->waitevent) {
		kfree(info);
		return -ENOMEM;
	}

	msg = (struct vmbus_channel_gpadl_teardown *)info->msg;

	msg->header.msgtype = CHANNELMSG_GPADL_TEARDOWN;
	msg->child_relid = channel->offermsg.child_relid;
	msg->gpadl = gpadl_handle;

	spin_lock_irqsave(&gVmbusConnection.channelmsg_lock, flags);
	list_add_tail(&info->msglistentry,
		      &gVmbusConnection.ChannelMsgList);
	spin_unlock_irqrestore(&gVmbusConnection.channelmsg_lock, flags);

	ret = VmbusPostMessage(msg,
			       sizeof(struct vmbus_channel_gpadl_teardown));
	if (ret != 0) {
		/* TODO: */
		/* something... */
	}

	osd_waitevent_wait(info->waitevent);

	/* Received a torndown response */
	spin_lock_irqsave(&gVmbusConnection.channelmsg_lock, flags);
	list_del(&info->msglistentry);
	spin_unlock_irqrestore(&gVmbusConnection.channelmsg_lock, flags);

	kfree(info->waitevent);
	kfree(info);
	return ret;
}
EXPORT_SYMBOL_GPL(vmbus_teardown_gpadl);

/*
 * vmbus_close - Close the specified channel
 */
void vmbus_close(struct vmbus_channel *channel)
{
	struct vmbus_channel_close_channel *msg;
	struct vmbus_channel_msginfo *info;
	unsigned long flags;
	int ret;

	/* Stop callback and cancel the timer asap */
	channel->onchannel_callback = NULL;
	del_timer_sync(&channel->poll_timer);

	/* Send a closing message */
	info = kmalloc(sizeof(*info) +
		       sizeof(struct vmbus_channel_close_channel), GFP_KERNEL);
        /* FIXME: can't do anything other than return here because the
	 *        function is void */
	if (!info)
		return;

	/* info->waitEvent = osd_waitevent_create(); */

	msg = (struct vmbus_channel_close_channel *)info->msg;
	msg->header.msgtype = CHANNELMSG_CLOSECHANNEL;
	msg->child_relid = channel->offermsg.child_relid;

	ret = VmbusPostMessage(msg, sizeof(struct vmbus_channel_close_channel));
	if (ret != 0) {
		/* TODO: */
		/* something... */
	}

	/* Tear down the gpadl for the channel's ring buffer */
	if (channel->ringbuffer_gpadlhandle)
		vmbus_teardown_gpadl(channel,
					  channel->ringbuffer_gpadlhandle);

	/* TODO: Send a msg to release the childRelId */

	/* Cleanup the ring buffers for this channel */
	ringbuffer_cleanup(&channel->outbound);
	ringbuffer_cleanup(&channel->inbound);

	osd_page_free(channel->ringbuffer_pages, channel->ringbuffer_pagecount);

	kfree(info);

	/*
	 * If we are closing the channel during an error path in
	 * opening the channel, don't free the channel since the
	 * caller will free the channel
	 */

	if (channel->state == CHANNEL_OPEN_STATE) {
		spin_lock_irqsave(&gVmbusConnection.channel_lock, flags);
		list_del(&channel->listentry);
		spin_unlock_irqrestore(&gVmbusConnection.channel_lock, flags);

		free_channel(channel);
	}
}
EXPORT_SYMBOL_GPL(vmbus_close);

/**
 * vmbus_sendpacket() - Send the specified buffer on the given channel
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
int vmbus_sendpacket(struct vmbus_channel *channel, const void *buffer,
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

	dump_vmbus_channel(channel);

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

	ret = ringbuffer_write(&channel->outbound, bufferlist, 3);

	/* TODO: We should determine if this is optional */
	if (ret == 0 && !get_ringbuffer_interrupt_mask(&channel->outbound))
		vmbus_setevent(channel);

	return ret;
}
EXPORT_SYMBOL(vmbus_sendpacket);

/*
 * vmbus_sendpacket_pagebuffer - Send a range of single-page buffer
 * packets using a GPADL Direct packet type.
 */
int vmbus_sendpacket_pagebuffer(struct vmbus_channel *channel,
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

	dump_vmbus_channel(channel);

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

	ret = ringbuffer_write(&channel->outbound, bufferlist, 3);

	/* TODO: We should determine if this is optional */
	if (ret == 0 && !get_ringbuffer_interrupt_mask(&channel->outbound))
		vmbus_setevent(channel);

	return ret;
}
EXPORT_SYMBOL_GPL(vmbus_sendpacket_pagebuffer);

/*
 * vmbus_sendpacket_multipagebuffer - Send a multi-page buffer packet
 * using a GPADL Direct packet type.
 */
int vmbus_sendpacket_multipagebuffer(struct vmbus_channel *channel,
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

	dump_vmbus_channel(channel);

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

	ret = ringbuffer_write(&channel->outbound, bufferlist, 3);

	/* TODO: We should determine if this is optional */
	if (ret == 0 && !get_ringbuffer_interrupt_mask(&channel->outbound))
		vmbus_setevent(channel);

	return ret;
}
EXPORT_SYMBOL_GPL(vmbus_sendpacket_multipagebuffer);

/**
 * vmbus_recvpacket() - Retrieve the user packet on the specified channel
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
int vmbus_recvpacket(struct vmbus_channel *channel, void *buffer,
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

	ret = ringbuffer_peek(&channel->inbound, &desc,
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
		   channel, channel->offermsg.child_relid, desc.Type,
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
	ret = ringbuffer_read(&channel->inbound, buffer, userlen,
			     (desc.DataOffset8 << 3));

	spin_unlock_irqrestore(&channel->inbound_lock, flags);

	return 0;
}
EXPORT_SYMBOL(vmbus_recvpacket);

/*
 * vmbus_recvpacket_raw - Retrieve the raw packet on the specified channel
 */
int vmbus_recvpacket_raw(struct vmbus_channel *channel, void *buffer,
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

	ret = ringbuffer_peek(&channel->inbound, &desc,
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
		   channel, channel->offermsg.child_relid, desc.Type,
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
	ret = ringbuffer_read(&channel->inbound, buffer, packetlen, 0);

	spin_unlock_irqrestore(&channel->inbound_lock, flags);
	return 0;
}
EXPORT_SYMBOL_GPL(vmbus_recvpacket_raw);

/*
 * vmbus_onchannel_event - Channel event callback
 */
void vmbus_onchannel_event(struct vmbus_channel *channel)
{
	dump_vmbus_channel(channel);
	/* ASSERT(Channel->OnChannelCallback); */

	channel->onchannel_callback(channel->channel_callback_context);

	mod_timer(&channel->poll_timer, jiffies + usecs_to_jiffies(100));
}

/*
 * vmbus_ontimer - Timer event callback
 */
void vmbus_ontimer(unsigned long data)
{
	struct vmbus_channel *channel = (struct vmbus_channel *)data;

	if (channel->onchannel_callback)
		channel->onchannel_callback(channel->channel_callback_context);
}

/*
 * dump_vmbus_channel- Dump vmbus channel info to the console
 */
static void dump_vmbus_channel(struct vmbus_channel *channel)
{
	DPRINT_DBG(VMBUS, "Channel (%d)", channel->offermsg.child_relid);
	dump_ring_info(&channel->outbound, "Outbound ");
	dump_ring_info(&channel->inbound, "Inbound ");
}
