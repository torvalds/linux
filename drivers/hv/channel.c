// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2009, Microsoft Corporation.
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
#include <linux/uio.h>
#include <linux/interrupt.h>
#include <asm/page.h>

#include "hyperv_vmbus.h"

#define NUM_PAGES_SPANNED(addr, len) \
((PAGE_ALIGN(addr + len) >> PAGE_SHIFT) - (addr >> PAGE_SHIFT))

static unsigned long virt_to_hvpfn(void *addr)
{
	unsigned long paddr;

	if (is_vmalloc_addr(addr))
		paddr = page_to_phys(vmalloc_to_page(addr)) +
					 offset_in_page(addr);
	else
		paddr = __pa(addr);

	return  paddr >> PAGE_SHIFT;
}

/*
 * vmbus_setevent- Trigger an event notification on the specified
 * channel.
 */
void vmbus_setevent(struct vmbus_channel *channel)
{
	struct hv_monitor_page *monitorpage;

	trace_vmbus_setevent(channel);

	/*
	 * For channels marked as in "low latency" mode
	 * bypass the monitor page mechanism.
	 */
	if (channel->offermsg.monitor_allocated && !channel->low_latency) {
		vmbus_send_interrupt(channel->offermsg.child_relid);

		/* Get the child to parent monitor page */
		monitorpage = vmbus_connection.monitor_pages[1];

		sync_set_bit(channel->monitor_bit,
			(unsigned long *)&monitorpage->trigger_group
					[channel->monitor_grp].pending);

	} else {
		vmbus_set_event(channel);
	}
}
EXPORT_SYMBOL_GPL(vmbus_setevent);

/* vmbus_free_ring - drop mapping of ring buffer */
void vmbus_free_ring(struct vmbus_channel *channel)
{
	hv_ringbuffer_cleanup(&channel->outbound);
	hv_ringbuffer_cleanup(&channel->inbound);

	if (channel->ringbuffer_page) {
		__free_pages(channel->ringbuffer_page,
			     get_order(channel->ringbuffer_pagecount
				       << PAGE_SHIFT));
		channel->ringbuffer_page = NULL;
	}
}
EXPORT_SYMBOL_GPL(vmbus_free_ring);

/* vmbus_alloc_ring - allocate and map pages for ring buffer */
int vmbus_alloc_ring(struct vmbus_channel *newchannel,
		     u32 send_size, u32 recv_size)
{
	struct page *page;
	int order;

	if (send_size % PAGE_SIZE || recv_size % PAGE_SIZE)
		return -EINVAL;

	/* Allocate the ring buffer */
	order = get_order(send_size + recv_size);
	page = alloc_pages_node(cpu_to_node(newchannel->target_cpu),
				GFP_KERNEL|__GFP_ZERO, order);

	if (!page)
		page = alloc_pages(GFP_KERNEL|__GFP_ZERO, order);

	if (!page)
		return -ENOMEM;

	newchannel->ringbuffer_page = page;
	newchannel->ringbuffer_pagecount = (send_size + recv_size) >> PAGE_SHIFT;
	newchannel->ringbuffer_send_offset = send_size >> PAGE_SHIFT;

	return 0;
}
EXPORT_SYMBOL_GPL(vmbus_alloc_ring);

static int __vmbus_open(struct vmbus_channel *newchannel,
		       void *userdata, u32 userdatalen,
		       void (*onchannelcallback)(void *context), void *context)
{
	struct vmbus_channel_open_channel *open_msg;
	struct vmbus_channel_msginfo *open_info = NULL;
	struct page *page = newchannel->ringbuffer_page;
	u32 send_pages, recv_pages;
	unsigned long flags;
	int err;

	if (userdatalen > MAX_USER_DEFINED_BYTES)
		return -EINVAL;

	send_pages = newchannel->ringbuffer_send_offset;
	recv_pages = newchannel->ringbuffer_pagecount - send_pages;

	spin_lock_irqsave(&newchannel->lock, flags);
	if (newchannel->state != CHANNEL_OPEN_STATE) {
		spin_unlock_irqrestore(&newchannel->lock, flags);
		return -EINVAL;
	}
	spin_unlock_irqrestore(&newchannel->lock, flags);

	newchannel->state = CHANNEL_OPENING_STATE;
	newchannel->onchannel_callback = onchannelcallback;
	newchannel->channel_callback_context = context;

	err = hv_ringbuffer_init(&newchannel->outbound, page, send_pages);
	if (err)
		goto error_clean_ring;

	err = hv_ringbuffer_init(&newchannel->inbound,
				 &page[send_pages], recv_pages);
	if (err)
		goto error_clean_ring;

	/* Establish the gpadl for the ring buffer */
	newchannel->ringbuffer_gpadlhandle = 0;

	err = vmbus_establish_gpadl(newchannel,
				    page_address(newchannel->ringbuffer_page),
				    (send_pages + recv_pages) << PAGE_SHIFT,
				    &newchannel->ringbuffer_gpadlhandle);
	if (err)
		goto error_clean_ring;

	/* Create and init the channel open message */
	open_info = kmalloc(sizeof(*open_info) +
			   sizeof(struct vmbus_channel_open_channel),
			   GFP_KERNEL);
	if (!open_info) {
		err = -ENOMEM;
		goto error_free_gpadl;
	}

	init_completion(&open_info->waitevent);
	open_info->waiting_channel = newchannel;

	open_msg = (struct vmbus_channel_open_channel *)open_info->msg;
	open_msg->header.msgtype = CHANNELMSG_OPENCHANNEL;
	open_msg->openid = newchannel->offermsg.child_relid;
	open_msg->child_relid = newchannel->offermsg.child_relid;
	open_msg->ringbuffer_gpadlhandle = newchannel->ringbuffer_gpadlhandle;
	open_msg->downstream_ringbuffer_pageoffset = newchannel->ringbuffer_send_offset;
	open_msg->target_vp = newchannel->target_vp;

	if (userdatalen)
		memcpy(open_msg->userdata, userdata, userdatalen);

	spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);
	list_add_tail(&open_info->msglistentry,
		      &vmbus_connection.chn_msg_list);
	spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock, flags);

	if (newchannel->rescind) {
		err = -ENODEV;
		goto error_free_info;
	}

	err = vmbus_post_msg(open_msg,
			     sizeof(struct vmbus_channel_open_channel), true);

	trace_vmbus_open(open_msg, err);

	if (err != 0)
		goto error_clean_msglist;

	wait_for_completion(&open_info->waitevent);

	spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);
	list_del(&open_info->msglistentry);
	spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock, flags);

	if (newchannel->rescind) {
		err = -ENODEV;
		goto error_free_info;
	}

	if (open_info->response.open_result.status) {
		err = -EAGAIN;
		goto error_free_info;
	}

	newchannel->state = CHANNEL_OPENED_STATE;
	kfree(open_info);
	return 0;

error_clean_msglist:
	spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);
	list_del(&open_info->msglistentry);
	spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock, flags);
error_free_info:
	kfree(open_info);
error_free_gpadl:
	vmbus_teardown_gpadl(newchannel, newchannel->ringbuffer_gpadlhandle);
	newchannel->ringbuffer_gpadlhandle = 0;
error_clean_ring:
	hv_ringbuffer_cleanup(&newchannel->outbound);
	hv_ringbuffer_cleanup(&newchannel->inbound);
	newchannel->state = CHANNEL_OPEN_STATE;
	return err;
}

/*
 * vmbus_connect_ring - Open the channel but reuse ring buffer
 */
int vmbus_connect_ring(struct vmbus_channel *newchannel,
		       void (*onchannelcallback)(void *context), void *context)
{
	return  __vmbus_open(newchannel, NULL, 0, onchannelcallback, context);
}
EXPORT_SYMBOL_GPL(vmbus_connect_ring);

/*
 * vmbus_open - Open the specified channel.
 */
int vmbus_open(struct vmbus_channel *newchannel,
	       u32 send_ringbuffer_size, u32 recv_ringbuffer_size,
	       void *userdata, u32 userdatalen,
	       void (*onchannelcallback)(void *context), void *context)
{
	int err;

	err = vmbus_alloc_ring(newchannel, send_ringbuffer_size,
			       recv_ringbuffer_size);
	if (err)
		return err;

	err = __vmbus_open(newchannel, userdata, userdatalen,
			   onchannelcallback, context);
	if (err)
		vmbus_free_ring(newchannel);

	return err;
}
EXPORT_SYMBOL_GPL(vmbus_open);

/* Used for Hyper-V Socket: a guest client's connect() to the host */
int vmbus_send_tl_connect_request(const guid_t *shv_guest_servie_id,
				  const guid_t *shv_host_servie_id)
{
	struct vmbus_channel_tl_connect_request conn_msg;
	int ret;

	memset(&conn_msg, 0, sizeof(conn_msg));
	conn_msg.header.msgtype = CHANNELMSG_TL_CONNECT_REQUEST;
	conn_msg.guest_endpoint_id = *shv_guest_servie_id;
	conn_msg.host_service_id = *shv_host_servie_id;

	ret = vmbus_post_msg(&conn_msg, sizeof(conn_msg), true);

	trace_vmbus_send_tl_connect_request(&conn_msg, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(vmbus_send_tl_connect_request);

/*
 * create_gpadl_header - Creates a gpadl for the specified buffer
 */
static int create_gpadl_header(void *kbuffer, u32 size,
			       struct vmbus_channel_msginfo **msginfo)
{
	int i;
	int pagecount;
	struct vmbus_channel_gpadl_header *gpadl_header;
	struct vmbus_channel_gpadl_body *gpadl_body;
	struct vmbus_channel_msginfo *msgheader;
	struct vmbus_channel_msginfo *msgbody = NULL;
	u32 msgsize;

	int pfnsum, pfncount, pfnleft, pfncurr, pfnsize;

	pagecount = size >> PAGE_SHIFT;

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
			gpadl_header->range[0].pfn_array[i] = virt_to_hvpfn(
				kbuffer + PAGE_SIZE * i);
		*msginfo = msgheader;

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
			gpadl_body =
				(struct vmbus_channel_gpadl_body *)msgbody->msg;

			/*
			 * Gpadl is u32 and we are using a pointer which could
			 * be 64-bit
			 * This is governed by the guest/host protocol and
			 * so the hypervisor guarantees that this is ok.
			 */
			for (i = 0; i < pfncurr; i++)
				gpadl_body->pfn[i] = virt_to_hvpfn(
					kbuffer + PAGE_SIZE * (pfnsum + i));

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

		INIT_LIST_HEAD(&msgheader->submsglist);
		msgheader->msgsize = msgsize;

		gpadl_header = (struct vmbus_channel_gpadl_header *)
			msgheader->msg;
		gpadl_header->rangecount = 1;
		gpadl_header->range_buflen = sizeof(struct gpa_range) +
					 pagecount * sizeof(u64);
		gpadl_header->range[0].byte_offset = 0;
		gpadl_header->range[0].byte_count = size;
		for (i = 0; i < pagecount; i++)
			gpadl_header->range[0].pfn_array[i] = virt_to_hvpfn(
				kbuffer + PAGE_SIZE * i);

		*msginfo = msgheader;
	}

	return 0;
nomem:
	kfree(msgheader);
	kfree(msgbody);
	return -ENOMEM;
}

/*
 * vmbus_establish_gpadl - Establish a GPADL for the specified buffer
 *
 * @channel: a channel
 * @kbuffer: from kmalloc or vmalloc
 * @size: page-size multiple
 * @gpadl_handle: some funky thing
 */
int vmbus_establish_gpadl(struct vmbus_channel *channel, void *kbuffer,
			       u32 size, u32 *gpadl_handle)
{
	struct vmbus_channel_gpadl_header *gpadlmsg;
	struct vmbus_channel_gpadl_body *gpadl_body;
	struct vmbus_channel_msginfo *msginfo = NULL;
	struct vmbus_channel_msginfo *submsginfo, *tmp;
	struct list_head *curr;
	u32 next_gpadl_handle;
	unsigned long flags;
	int ret = 0;

	next_gpadl_handle =
		(atomic_inc_return(&vmbus_connection.next_gpadl_handle) - 1);

	ret = create_gpadl_header(kbuffer, size, &msginfo);
	if (ret)
		return ret;

	init_completion(&msginfo->waitevent);
	msginfo->waiting_channel = channel;

	gpadlmsg = (struct vmbus_channel_gpadl_header *)msginfo->msg;
	gpadlmsg->header.msgtype = CHANNELMSG_GPADL_HEADER;
	gpadlmsg->child_relid = channel->offermsg.child_relid;
	gpadlmsg->gpadl = next_gpadl_handle;


	spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);
	list_add_tail(&msginfo->msglistentry,
		      &vmbus_connection.chn_msg_list);

	spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock, flags);

	if (channel->rescind) {
		ret = -ENODEV;
		goto cleanup;
	}

	ret = vmbus_post_msg(gpadlmsg, msginfo->msgsize -
			     sizeof(*msginfo), true);

	trace_vmbus_establish_gpadl_header(gpadlmsg, ret);

	if (ret != 0)
		goto cleanup;

	list_for_each(curr, &msginfo->submsglist) {
		submsginfo = (struct vmbus_channel_msginfo *)curr;
		gpadl_body =
			(struct vmbus_channel_gpadl_body *)submsginfo->msg;

		gpadl_body->header.msgtype =
			CHANNELMSG_GPADL_BODY;
		gpadl_body->gpadl = next_gpadl_handle;

		ret = vmbus_post_msg(gpadl_body,
				     submsginfo->msgsize - sizeof(*submsginfo),
				     true);

		trace_vmbus_establish_gpadl_body(gpadl_body, ret);

		if (ret != 0)
			goto cleanup;

	}
	wait_for_completion(&msginfo->waitevent);

	if (msginfo->response.gpadl_created.creation_status != 0) {
		pr_err("Failed to establish GPADL: err = 0x%x\n",
		       msginfo->response.gpadl_created.creation_status);

		ret = -EDQUOT;
		goto cleanup;
	}

	if (channel->rescind) {
		ret = -ENODEV;
		goto cleanup;
	}

	/* At this point, we received the gpadl created msg */
	*gpadl_handle = gpadlmsg->gpadl;

cleanup:
	spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);
	list_del(&msginfo->msglistentry);
	spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock, flags);
	list_for_each_entry_safe(submsginfo, tmp, &msginfo->submsglist,
				 msglistentry) {
		kfree(submsginfo);
	}

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

	info = kmalloc(sizeof(*info) +
		       sizeof(struct vmbus_channel_gpadl_teardown), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	init_completion(&info->waitevent);
	info->waiting_channel = channel;

	msg = (struct vmbus_channel_gpadl_teardown *)info->msg;

	msg->header.msgtype = CHANNELMSG_GPADL_TEARDOWN;
	msg->child_relid = channel->offermsg.child_relid;
	msg->gpadl = gpadl_handle;

	spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);
	list_add_tail(&info->msglistentry,
		      &vmbus_connection.chn_msg_list);
	spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock, flags);

	if (channel->rescind)
		goto post_msg_err;

	ret = vmbus_post_msg(msg, sizeof(struct vmbus_channel_gpadl_teardown),
			     true);

	trace_vmbus_teardown_gpadl(msg, ret);

	if (ret)
		goto post_msg_err;

	wait_for_completion(&info->waitevent);

post_msg_err:
	/*
	 * If the channel has been rescinded;
	 * we will be awakened by the rescind
	 * handler; set the error code to zero so we don't leak memory.
	 */
	if (channel->rescind)
		ret = 0;

	spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);
	list_del(&info->msglistentry);
	spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock, flags);

	kfree(info);
	return ret;
}
EXPORT_SYMBOL_GPL(vmbus_teardown_gpadl);

static void reset_channel_cb(void *arg)
{
	struct vmbus_channel *channel = arg;

	channel->onchannel_callback = NULL;
}

void vmbus_reset_channel_cb(struct vmbus_channel *channel)
{
	/*
	 * vmbus_on_event(), running in the per-channel tasklet, can race
	 * with vmbus_close_internal() in the case of SMP guest, e.g., when
	 * the former is accessing channel->inbound.ring_buffer, the latter
	 * could be freeing the ring_buffer pages, so here we must stop it
	 * first.
	 */
	tasklet_disable(&channel->callback_event);

	channel->sc_creation_callback = NULL;

	/* Stop the callback asap */
	if (channel->target_cpu != get_cpu()) {
		put_cpu();
		smp_call_function_single(channel->target_cpu, reset_channel_cb,
					 channel, true);
	} else {
		reset_channel_cb(channel);
		put_cpu();
	}

	/* Re-enable tasklet for use on re-open */
	tasklet_enable(&channel->callback_event);
}

static int vmbus_close_internal(struct vmbus_channel *channel)
{
	struct vmbus_channel_close_channel *msg;
	int ret;

	vmbus_reset_channel_cb(channel);

	/*
	 * In case a device driver's probe() fails (e.g.,
	 * util_probe() -> vmbus_open() returns -ENOMEM) and the device is
	 * rescinded later (e.g., we dynamically disable an Integrated Service
	 * in Hyper-V Manager), the driver's remove() invokes vmbus_close():
	 * here we should skip most of the below cleanup work.
	 */
	if (channel->state != CHANNEL_OPENED_STATE)
		return -EINVAL;

	channel->state = CHANNEL_OPEN_STATE;

	/* Send a closing message */

	msg = &channel->close_msg.msg;

	msg->header.msgtype = CHANNELMSG_CLOSECHANNEL;
	msg->child_relid = channel->offermsg.child_relid;

	ret = vmbus_post_msg(msg, sizeof(struct vmbus_channel_close_channel),
			     true);

	trace_vmbus_close_internal(msg, ret);

	if (ret) {
		pr_err("Close failed: close post msg return is %d\n", ret);
		/*
		 * If we failed to post the close msg,
		 * it is perhaps better to leak memory.
		 */
	}

	/* Tear down the gpadl for the channel's ring buffer */
	else if (channel->ringbuffer_gpadlhandle) {
		ret = vmbus_teardown_gpadl(channel,
					   channel->ringbuffer_gpadlhandle);
		if (ret) {
			pr_err("Close failed: teardown gpadl return %d\n", ret);
			/*
			 * If we failed to teardown gpadl,
			 * it is perhaps better to leak memory.
			 */
		}

		channel->ringbuffer_gpadlhandle = 0;
	}

	return ret;
}

/* disconnect ring - close all channels */
int vmbus_disconnect_ring(struct vmbus_channel *channel)
{
	struct vmbus_channel *cur_channel, *tmp;
	int ret;

	if (channel->primary_channel != NULL)
		return -EINVAL;

	list_for_each_entry_safe(cur_channel, tmp, &channel->sc_list, sc_list) {
		if (cur_channel->rescind)
			wait_for_completion(&cur_channel->rescind_event);

		mutex_lock(&vmbus_connection.channel_mutex);
		if (vmbus_close_internal(cur_channel) == 0) {
			vmbus_free_ring(cur_channel);

			if (cur_channel->rescind)
				hv_process_channel_removal(cur_channel);
		}
		mutex_unlock(&vmbus_connection.channel_mutex);
	}

	/*
	 * Now close the primary.
	 */
	mutex_lock(&vmbus_connection.channel_mutex);
	ret = vmbus_close_internal(channel);
	mutex_unlock(&vmbus_connection.channel_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(vmbus_disconnect_ring);

/*
 * vmbus_close - Close the specified channel
 */
void vmbus_close(struct vmbus_channel *channel)
{
	if (vmbus_disconnect_ring(channel) == 0)
		vmbus_free_ring(channel);
}
EXPORT_SYMBOL_GPL(vmbus_close);

/**
 * vmbus_sendpacket() - Send the specified buffer on the given channel
 * @channel: Pointer to vmbus_channel structure
 * @buffer: Pointer to the buffer you want to send the data from.
 * @bufferlen: Maximum size of what the buffer holds.
 * @requestid: Identifier of the request
 * @type: Type of packet that is being sent e.g. negotiate, time
 *	  packet etc.
 * @flags: 0 or VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED
 *
 * Sends data in @buffer directly to Hyper-V via the vmbus.
 * This will send the data unparsed to Hyper-V.
 *
 * Mainly used by Hyper-V drivers.
 */
int vmbus_sendpacket(struct vmbus_channel *channel, void *buffer,
			   u32 bufferlen, u64 requestid,
			   enum vmbus_packet_type type, u32 flags)
{
	struct vmpacket_descriptor desc;
	u32 packetlen = sizeof(struct vmpacket_descriptor) + bufferlen;
	u32 packetlen_aligned = ALIGN(packetlen, sizeof(u64));
	struct kvec bufferlist[3];
	u64 aligned_data = 0;
	int num_vecs = ((bufferlen != 0) ? 3 : 1);


	/* Setup the descriptor */
	desc.type = type; /* VmbusPacketTypeDataInBand; */
	desc.flags = flags; /* VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED; */
	/* in 8-bytes granularity */
	desc.offset8 = sizeof(struct vmpacket_descriptor) >> 3;
	desc.len8 = (u16)(packetlen_aligned >> 3);
	desc.trans_id = requestid;

	bufferlist[0].iov_base = &desc;
	bufferlist[0].iov_len = sizeof(struct vmpacket_descriptor);
	bufferlist[1].iov_base = buffer;
	bufferlist[1].iov_len = bufferlen;
	bufferlist[2].iov_base = &aligned_data;
	bufferlist[2].iov_len = (packetlen_aligned - packetlen);

	return hv_ringbuffer_write(channel, bufferlist, num_vecs);
}
EXPORT_SYMBOL(vmbus_sendpacket);

/*
 * vmbus_sendpacket_pagebuffer - Send a range of single-page buffer
 * packets using a GPADL Direct packet type. This interface allows you
 * to control notifying the host. This will be useful for sending
 * batched data. Also the sender can control the send flags
 * explicitly.
 */
int vmbus_sendpacket_pagebuffer(struct vmbus_channel *channel,
				struct hv_page_buffer pagebuffers[],
				u32 pagecount, void *buffer, u32 bufferlen,
				u64 requestid)
{
	int i;
	struct vmbus_channel_packet_page_buffer desc;
	u32 descsize;
	u32 packetlen;
	u32 packetlen_aligned;
	struct kvec bufferlist[3];
	u64 aligned_data = 0;

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
	desc.dataoffset8 = descsize >> 3; /* in 8-bytes granularity */
	desc.length8 = (u16)(packetlen_aligned >> 3);
	desc.transactionid = requestid;
	desc.reserved = 0;
	desc.rangecount = pagecount;

	for (i = 0; i < pagecount; i++) {
		desc.range[i].len = pagebuffers[i].len;
		desc.range[i].offset = pagebuffers[i].offset;
		desc.range[i].pfn	 = pagebuffers[i].pfn;
	}

	bufferlist[0].iov_base = &desc;
	bufferlist[0].iov_len = descsize;
	bufferlist[1].iov_base = buffer;
	bufferlist[1].iov_len = bufferlen;
	bufferlist[2].iov_base = &aligned_data;
	bufferlist[2].iov_len = (packetlen_aligned - packetlen);

	return hv_ringbuffer_write(channel, bufferlist, 3);
}
EXPORT_SYMBOL_GPL(vmbus_sendpacket_pagebuffer);

/*
 * vmbus_sendpacket_multipagebuffer - Send a multi-page buffer packet
 * using a GPADL Direct packet type.
 * The buffer includes the vmbus descriptor.
 */
int vmbus_sendpacket_mpb_desc(struct vmbus_channel *channel,
			      struct vmbus_packet_mpb_array *desc,
			      u32 desc_size,
			      void *buffer, u32 bufferlen, u64 requestid)
{
	u32 packetlen;
	u32 packetlen_aligned;
	struct kvec bufferlist[3];
	u64 aligned_data = 0;

	packetlen = desc_size + bufferlen;
	packetlen_aligned = ALIGN(packetlen, sizeof(u64));

	/* Setup the descriptor */
	desc->type = VM_PKT_DATA_USING_GPA_DIRECT;
	desc->flags = VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED;
	desc->dataoffset8 = desc_size >> 3; /* in 8-bytes granularity */
	desc->length8 = (u16)(packetlen_aligned >> 3);
	desc->transactionid = requestid;
	desc->reserved = 0;
	desc->rangecount = 1;

	bufferlist[0].iov_base = desc;
	bufferlist[0].iov_len = desc_size;
	bufferlist[1].iov_base = buffer;
	bufferlist[1].iov_len = bufferlen;
	bufferlist[2].iov_base = &aligned_data;
	bufferlist[2].iov_len = (packetlen_aligned - packetlen);

	return hv_ringbuffer_write(channel, bufferlist, 3);
}
EXPORT_SYMBOL_GPL(vmbus_sendpacket_mpb_desc);

/**
 * __vmbus_recvpacket() - Retrieve the user packet on the specified channel
 * @channel: Pointer to vmbus_channel structure
 * @buffer: Pointer to the buffer you want to receive the data into.
 * @bufferlen: Maximum size of what the buffer can hold.
 * @buffer_actual_len: The actual size of the data after it was received.
 * @requestid: Identifier of the request
 * @raw: true means keep the vmpacket_descriptor header in the received data.
 *
 * Receives directly from the hyper-v vmbus and puts the data it received
 * into Buffer. This will receive the data unparsed from hyper-v.
 *
 * Mainly used by Hyper-V drivers.
 */
static inline int
__vmbus_recvpacket(struct vmbus_channel *channel, void *buffer,
		   u32 bufferlen, u32 *buffer_actual_len, u64 *requestid,
		   bool raw)
{
	return hv_ringbuffer_read(channel, buffer, bufferlen,
				  buffer_actual_len, requestid, raw);

}

int vmbus_recvpacket(struct vmbus_channel *channel, void *buffer,
		     u32 bufferlen, u32 *buffer_actual_len,
		     u64 *requestid)
{
	return __vmbus_recvpacket(channel, buffer, bufferlen,
				  buffer_actual_len, requestid, false);
}
EXPORT_SYMBOL(vmbus_recvpacket);

/*
 * vmbus_recvpacket_raw - Retrieve the raw packet on the specified channel
 */
int vmbus_recvpacket_raw(struct vmbus_channel *channel, void *buffer,
			      u32 bufferlen, u32 *buffer_actual_len,
			      u64 *requestid)
{
	return __vmbus_recvpacket(channel, buffer, bufferlen,
				  buffer_actual_len, requestid, true);
}
EXPORT_SYMBOL_GPL(vmbus_recvpacket_raw);
