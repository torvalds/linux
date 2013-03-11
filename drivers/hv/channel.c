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
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/hyperv.h>

#include "hyperv_vmbus.h"

#define NUM_PAGES_SPANNED(addr, len) \
((PAGE_ALIGN(addr + len) >> PAGE_SHIFT) - (addr >> PAGE_SHIFT))

/*
 * vmbus_setevent- Trigger an event notification on the specified
 * channel.
 */
static void vmbus_setevent(struct vmbus_channel *channel)
{
	struct hv_monitor_page *monitorpage;

	if (channel->offermsg.monitor_allocated) {
		/* Each u32 represents 32 channels */
		sync_set_bit(channel->offermsg.child_relid & 31,
			(unsigned long *) vmbus_connection.send_int_page +
			(channel->offermsg.child_relid >> 5));

		monitorpage = vmbus_connection.monitor_pages;
		monitorpage++; /* Get the child to parent monitor page */

		sync_set_bit(channel->monitor_bit,
			(unsigned long *)&monitorpage->trigger_group
					[channel->monitor_grp].pending);

	} else {
		vmbus_set_event(channel);
	}
}

/*
 * vmbus_get_debug_info -Retrieve various channel debug info
 */
void vmbus_get_debug_info(struct vmbus_channel *channel,
			      struct vmbus_channel_debug_info *debuginfo)
{
	struct hv_monitor_page *monitorpage;
	u8 monitor_group = (u8)channel->offermsg.monitorid / 32;
	u8 monitor_offset = (u8)channel->offermsg.monitorid % 32;

	debuginfo->relid = channel->offermsg.child_relid;
	debuginfo->state = channel->state;
	memcpy(&debuginfo->interfacetype,
	       &channel->offermsg.offer.if_type, sizeof(uuid_le));
	memcpy(&debuginfo->interface_instance,
	       &channel->offermsg.offer.if_instance,
	       sizeof(uuid_le));

	monitorpage = (struct hv_monitor_page *)vmbus_connection.monitor_pages;

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

	hv_ringbuffer_get_debuginfo(&channel->inbound, &debuginfo->inbound);
	hv_ringbuffer_get_debuginfo(&channel->outbound, &debuginfo->outbound);
}

/*
 * vmbus_open - Open the specified channel.
 */
int vmbus_open(struct vmbus_channel *newchannel, u32 send_ringbuffer_size,
		     u32 recv_ringbuffer_size, void *userdata, u32 userdatalen,
		     void (*onchannelcallback)(void *context), void *context)
{
	struct vmbus_channel_open_channel *open_msg;
	struct vmbus_channel_msginfo *open_info = NULL;
	void *in, *out;
	unsigned long flags;
	int ret, t, err = 0;

	newchannel->onchannel_callback = onchannelcallback;
	newchannel->channel_callback_context = context;

	/* Allocate the ring buffer */
	out = (void *)__get_free_pages(GFP_KERNEL|__GFP_ZERO,
		get_order(send_ringbuffer_size + recv_ringbuffer_size));

	if (!out)
		return -ENOMEM;


	in = (void *)((unsigned long)out + send_ringbuffer_size);

	newchannel->ringbuffer_pages = out;
	newchannel->ringbuffer_pagecount = (send_ringbuffer_size +
					   recv_ringbuffer_size) >> PAGE_SHIFT;

	ret = hv_ringbuffer_init(
		&newchannel->outbound, out, send_ringbuffer_size);

	if (ret != 0) {
		err = ret;
		goto error0;
	}

	ret = hv_ringbuffer_init(
		&newchannel->inbound, in, recv_ringbuffer_size);
	if (ret != 0) {
		err = ret;
		goto error0;
	}


	/* Establish the gpadl for the ring buffer */
	newchannel->ringbuffer_gpadlhandle = 0;

	ret = vmbus_establish_gpadl(newchannel,
					 newchannel->outbound.ring_buffer,
					 send_ringbuffer_size +
					 recv_ringbuffer_size,
					 &newchannel->ringbuffer_gpadlhandle);

	if (ret != 0) {
		err = ret;
		goto error0;
	}

	/* Create and init the channel open message */
	open_info = kmalloc(sizeof(*open_info) +
			   sizeof(struct vmbus_channel_open_channel),
			   GFP_KERNEL);
	if (!open_info) {
		err = -ENOMEM;
		goto error0;
	}

	init_completion(&open_info->waitevent);

	open_msg = (struct vmbus_channel_open_channel *)open_info->msg;
	open_msg->header.msgtype = CHANNELMSG_OPENCHANNEL;
	open_msg->openid = newchannel->offermsg.child_relid;
	open_msg->child_relid = newchannel->offermsg.child_relid;
	open_msg->ringbuffer_gpadlhandle = newchannel->ringbuffer_gpadlhandle;
	open_msg->downstream_ringbuffer_pageoffset = send_ringbuffer_size >>
						  PAGE_SHIFT;
	open_msg->target_vp = newchannel->target_vp;

	if (userdatalen > MAX_USER_DEFINED_BYTES) {
		err = -EINVAL;
		goto error0;
	}

	if (userdatalen)
		memcpy(open_msg->userdata, userdata, userdatalen);

	spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);
	list_add_tail(&open_info->msglistentry,
		      &vmbus_connection.chn_msg_list);
	spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock, flags);

	ret = vmbus_post_msg(open_msg,
			       sizeof(struct vmbus_channel_open_channel));

	if (ret != 0)
		goto error1;

	t = wait_for_completion_timeout(&open_info->waitevent, 5*HZ);
	if (t == 0) {
		err = -ETIMEDOUT;
		goto error1;
	}


	if (open_info->response.open_result.status)
		err = open_info->response.open_result.status;

	spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);
	list_del(&open_info->msglistentry);
	spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock, flags);

	kfree(open_info);
	return err;

error1:
	spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);
	list_del(&open_info->msglistentry);
	spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock, flags);

error0:
	free_pages((unsigned long)out,
		get_order(send_ringbuffer_size + recv_ringbuffer_size));
	kfree(open_info);
	return err;
}
EXPORT_SYMBOL_GPL(vmbus_open);

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
		gpadl_header->range[0].byte_offset = 0;
		gpadl_header->range[0].byte_count = size;
		for (i = 0; i < pfncount; i++)
			gpadl_header->range[0].pfn_array[i] = pfn+i;
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

			if (!msgbody) {
				struct vmbus_channel_msginfo *pos = NULL;
				struct vmbus_channel_msginfo *tmp = NULL;
				/*
				 * Free up all the allocated messages.
				 */
				list_for_each_entry_safe(pos, tmp,
					&msgheader->submsglist,
					msglistentry) {

					list_del(&pos->msglistentry);
					kfree(pos);
				}

				goto nomem;
			}

			msgbody->msgsize = msgsize;
			(*messagecount)++;
			gpadl_body =
				(struct vmbus_channel_gpadl_body *)msgbody->msg;

			/*
			 * Gpadl is u32 and we are using a pointer which could
			 * be 64-bit
			 * This is governed by the guest/host protocol and
			 * so the hypervisor gurantees that this is ok.
			 */
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
		gpadl_header->range[0].byte_offset = 0;
		gpadl_header->range[0].byte_count = size;
		for (i = 0; i < pagecount; i++)
			gpadl_header->range[0].pfn_array[i] = pfn+i;

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
	struct vmbus_channel_msginfo *msginfo = NULL;
	struct vmbus_channel_msginfo *submsginfo;
	u32 msgcount;
	struct list_head *curr;
	u32 next_gpadl_handle;
	unsigned long flags;
	int ret = 0;
	int t;

	next_gpadl_handle = atomic_read(&vmbus_connection.next_gpadl_handle);
	atomic_inc(&vmbus_connection.next_gpadl_handle);

	ret = create_gpadl_header(kbuffer, size, &msginfo, &msgcount);
	if (ret)
		return ret;

	init_completion(&msginfo->waitevent);

	gpadlmsg = (struct vmbus_channel_gpadl_header *)msginfo->msg;
	gpadlmsg->header.msgtype = CHANNELMSG_GPADL_HEADER;
	gpadlmsg->child_relid = channel->offermsg.child_relid;
	gpadlmsg->gpadl = next_gpadl_handle;


	spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);
	list_add_tail(&msginfo->msglistentry,
		      &vmbus_connection.chn_msg_list);

	spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock, flags);

	ret = vmbus_post_msg(gpadlmsg, msginfo->msgsize -
			       sizeof(*msginfo));
	if (ret != 0)
		goto cleanup;

	if (msgcount > 1) {
		list_for_each(curr, &msginfo->submsglist) {

			submsginfo = (struct vmbus_channel_msginfo *)curr;
			gpadl_body =
			     (struct vmbus_channel_gpadl_body *)submsginfo->msg;

			gpadl_body->header.msgtype =
				CHANNELMSG_GPADL_BODY;
			gpadl_body->gpadl = next_gpadl_handle;

			ret = vmbus_post_msg(gpadl_body,
					       submsginfo->msgsize -
					       sizeof(*submsginfo));
			if (ret != 0)
				goto cleanup;

		}
	}
	t = wait_for_completion_timeout(&msginfo->waitevent, 5*HZ);
	BUG_ON(t == 0);


	/* At this point, we received the gpadl created msg */
	*gpadl_handle = gpadlmsg->gpadl;

cleanup:
	spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);
	list_del(&msginfo->msglistentry);
	spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock, flags);

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
	int ret, t;

	info = kmalloc(sizeof(*info) +
		       sizeof(struct vmbus_channel_gpadl_teardown), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	init_completion(&info->waitevent);

	msg = (struct vmbus_channel_gpadl_teardown *)info->msg;

	msg->header.msgtype = CHANNELMSG_GPADL_TEARDOWN;
	msg->child_relid = channel->offermsg.child_relid;
	msg->gpadl = gpadl_handle;

	spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);
	list_add_tail(&info->msglistentry,
		      &vmbus_connection.chn_msg_list);
	spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock, flags);
	ret = vmbus_post_msg(msg,
			       sizeof(struct vmbus_channel_gpadl_teardown));

	BUG_ON(ret != 0);
	t = wait_for_completion_timeout(&info->waitevent, 5*HZ);
	BUG_ON(t == 0);

	/* Received a torndown response */
	spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);
	list_del(&info->msglistentry);
	spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock, flags);

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
	int ret;
	unsigned long flags;

	/* Stop callback and cancel the timer asap */
	spin_lock_irqsave(&channel->inbound_lock, flags);
	channel->onchannel_callback = NULL;
	spin_unlock_irqrestore(&channel->inbound_lock, flags);

	/* Send a closing message */

	msg = &channel->close_msg.msg;

	msg->header.msgtype = CHANNELMSG_CLOSECHANNEL;
	msg->child_relid = channel->offermsg.child_relid;

	ret = vmbus_post_msg(msg, sizeof(struct vmbus_channel_close_channel));

	BUG_ON(ret != 0);
	/* Tear down the gpadl for the channel's ring buffer */
	if (channel->ringbuffer_gpadlhandle)
		vmbus_teardown_gpadl(channel,
					  channel->ringbuffer_gpadlhandle);

	/* Cleanup the ring buffers for this channel */
	hv_ringbuffer_cleanup(&channel->outbound);
	hv_ringbuffer_cleanup(&channel->inbound);

	free_pages((unsigned long)channel->ringbuffer_pages,
		get_order(channel->ringbuffer_pagecount * PAGE_SIZE));


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
	u32 packetlen_aligned = ALIGN(packetlen, sizeof(u64));
	struct scatterlist bufferlist[3];
	u64 aligned_data = 0;
	int ret;
	bool signal = false;


	/* Setup the descriptor */
	desc.type = type; /* VmbusPacketTypeDataInBand; */
	desc.flags = flags; /* VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED; */
	/* in 8-bytes granularity */
	desc.offset8 = sizeof(struct vmpacket_descriptor) >> 3;
	desc.len8 = (u16)(packetlen_aligned >> 3);
	desc.trans_id = requestid;

	sg_init_table(bufferlist, 3);
	sg_set_buf(&bufferlist[0], &desc, sizeof(struct vmpacket_descriptor));
	sg_set_buf(&bufferlist[1], buffer, bufferlen);
	sg_set_buf(&bufferlist[2], &aligned_data,
		   packetlen_aligned - packetlen);

	ret = hv_ringbuffer_write(&channel->outbound, bufferlist, 3, &signal);

	if (ret == 0 && signal)
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
	bool signal = false;

	if (pagecount > MAX_PAGE_BUFFER_COUNT)
		return -EINVAL;


	/*
	 * Adjust the size down since vmbus_channel_packet_page_buffer is the
	 * largest size we support
	 */
	descsize = sizeof(struct vmbus_channel_packet_page_buffer) -
			  ((MAX_PAGE_BUFFER_COUNT - pagecount) *
			  sizeof(struct hv_page_buffer));
	packetlen = descsize + bufferlen;
	packetlen_aligned = ALIGN(packetlen, sizeof(u64));

	/* Setup the descriptor */
	desc.type = VM_PKT_DATA_USING_GPA_DIRECT;
	desc.flags = VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED;
	desc.dataoffset8 = descsize >> 3; /* in 8-bytes grandularity */
	desc.length8 = (u16)(packetlen_aligned >> 3);
	desc.transactionid = requestid;
	desc.rangecount = pagecount;

	for (i = 0; i < pagecount; i++) {
		desc.range[i].len = pagebuffers[i].len;
		desc.range[i].offset = pagebuffers[i].offset;
		desc.range[i].pfn	 = pagebuffers[i].pfn;
	}

	sg_init_table(bufferlist, 3);
	sg_set_buf(&bufferlist[0], &desc, descsize);
	sg_set_buf(&bufferlist[1], buffer, bufferlen);
	sg_set_buf(&bufferlist[2], &aligned_data,
		packetlen_aligned - packetlen);

	ret = hv_ringbuffer_write(&channel->outbound, bufferlist, 3, &signal);

	if (ret == 0 && signal)
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
	bool signal = false;
	u32 pfncount = NUM_PAGES_SPANNED(multi_pagebuffer->offset,
					 multi_pagebuffer->len);


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
	packetlen_aligned = ALIGN(packetlen, sizeof(u64));


	/* Setup the descriptor */
	desc.type = VM_PKT_DATA_USING_GPA_DIRECT;
	desc.flags = VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED;
	desc.dataoffset8 = descsize >> 3; /* in 8-bytes grandularity */
	desc.length8 = (u16)(packetlen_aligned >> 3);
	desc.transactionid = requestid;
	desc.rangecount = 1;

	desc.range.len = multi_pagebuffer->len;
	desc.range.offset = multi_pagebuffer->offset;

	memcpy(desc.range.pfn_array, multi_pagebuffer->pfn_array,
	       pfncount * sizeof(u64));

	sg_init_table(bufferlist, 3);
	sg_set_buf(&bufferlist[0], &desc, descsize);
	sg_set_buf(&bufferlist[1], buffer, bufferlen);
	sg_set_buf(&bufferlist[2], &aligned_data,
		packetlen_aligned - packetlen);

	ret = hv_ringbuffer_write(&channel->outbound, bufferlist, 3, &signal);

	if (ret == 0 && signal)
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
	bool signal = false;

	*buffer_actual_len = 0;
	*requestid = 0;


	ret = hv_ringbuffer_peek(&channel->inbound, &desc,
			     sizeof(struct vmpacket_descriptor));
	if (ret != 0)
		return 0;

	packetlen = desc.len8 << 3;
	userlen = packetlen - (desc.offset8 << 3);

	*buffer_actual_len = userlen;

	if (userlen > bufferlen) {

		pr_err("Buffer too small - got %d needs %d\n",
			   bufferlen, userlen);
		return -ETOOSMALL;
	}

	*requestid = desc.trans_id;

	/* Copy over the packet to the user buffer */
	ret = hv_ringbuffer_read(&channel->inbound, buffer, userlen,
			     (desc.offset8 << 3), &signal);

	if (signal)
		vmbus_setevent(channel);

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
	int ret;
	bool signal = false;

	*buffer_actual_len = 0;
	*requestid = 0;


	ret = hv_ringbuffer_peek(&channel->inbound, &desc,
			     sizeof(struct vmpacket_descriptor));
	if (ret != 0)
		return 0;


	packetlen = desc.len8 << 3;

	*buffer_actual_len = packetlen;

	if (packetlen > bufferlen) {
		pr_err("Buffer too small - needed %d bytes but "
			"got space for only %d bytes\n",
			packetlen, bufferlen);
		return -ENOBUFS;
	}

	*requestid = desc.trans_id;

	/* Copy over the entire packet to the user buffer */
	ret = hv_ringbuffer_read(&channel->inbound, buffer, packetlen, 0,
				 &signal);

	if (signal)
		vmbus_setevent(channel);

	return 0;
}
EXPORT_SYMBOL_GPL(vmbus_recvpacket_raw);
