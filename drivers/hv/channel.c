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
#include <linux/set_memory.h>
#include <asm/page.h>
#include <asm/mem_encrypt.h>
#include <asm/mshyperv.h>

#include "hyperv_vmbus.h"

/*
 * hv_gpadl_size - Return the real size of a gpadl, the size that Hyper-V uses
 *
 * For BUFFER gpadl, Hyper-V uses the exact same size as the guest does.
 *
 * For RING gpadl, in each ring, the guest uses one PAGE_SIZE as the header
 * (because of the alignment requirement), however, the hypervisor only
 * uses the first HV_HYP_PAGE_SIZE as the header, therefore leaving a
 * (PAGE_SIZE - HV_HYP_PAGE_SIZE) gap. And since there are two rings in a
 * ringbuffer, the total size for a RING gpadl that Hyper-V uses is the
 * total size that the guest uses minus twice of the gap size.
 */
static inline u32 hv_gpadl_size(enum hv_gpadl_type type, u32 size)
{
	switch (type) {
	case HV_GPADL_BUFFER:
		return size;
	case HV_GPADL_RING:
		/* The size of a ringbuffer must be page-aligned */
		BUG_ON(size % PAGE_SIZE);
		/*
		 * Two things to notice here:
		 * 1) We're processing two ring buffers as a unit
		 * 2) We're skipping any space larger than HV_HYP_PAGE_SIZE in
		 * the first guest-size page of each of the two ring buffers.
		 * So we effectively subtract out two guest-size pages, and add
		 * back two Hyper-V size pages.
		 */
		return size - 2 * (PAGE_SIZE - HV_HYP_PAGE_SIZE);
	}
	BUG();
	return 0;
}

/*
 * hv_ring_gpadl_send_hvpgoffset - Calculate the send offset (in unit of
 *                                 HV_HYP_PAGE) in a ring gpadl based on the
 *                                 offset in the guest
 *
 * @offset: the offset (in bytes) where the send ringbuffer starts in the
 *               virtual address space of the guest
 */
static inline u32 hv_ring_gpadl_send_hvpgoffset(u32 offset)
{

	/*
	 * For RING gpadl, in each ring, the guest uses one PAGE_SIZE as the
	 * header (because of the alignment requirement), however, the
	 * hypervisor only uses the first HV_HYP_PAGE_SIZE as the header,
	 * therefore leaving a (PAGE_SIZE - HV_HYP_PAGE_SIZE) gap.
	 *
	 * And to calculate the effective send offset in gpadl, we need to
	 * substract this gap.
	 */
	return (offset - (PAGE_SIZE - HV_HYP_PAGE_SIZE)) >> HV_HYP_PAGE_SHIFT;
}

/*
 * hv_gpadl_hvpfn - Return the Hyper-V page PFN of the @i th Hyper-V page in
 *                  the gpadl
 *
 * @type: the type of the gpadl
 * @kbuffer: the pointer to the gpadl in the guest
 * @size: the total size (in bytes) of the gpadl
 * @send_offset: the offset (in bytes) where the send ringbuffer starts in the
 *               virtual address space of the guest
 * @i: the index
 */
static inline u64 hv_gpadl_hvpfn(enum hv_gpadl_type type, void *kbuffer,
				 u32 size, u32 send_offset, int i)
{
	int send_idx = hv_ring_gpadl_send_hvpgoffset(send_offset);
	unsigned long delta = 0UL;

	switch (type) {
	case HV_GPADL_BUFFER:
		break;
	case HV_GPADL_RING:
		if (i == 0)
			delta = 0;
		else if (i <= send_idx)
			delta = PAGE_SIZE - HV_HYP_PAGE_SIZE;
		else
			delta = 2 * (PAGE_SIZE - HV_HYP_PAGE_SIZE);
		break;
	default:
		BUG();
		break;
	}

	return virt_to_hvpfn(kbuffer + delta + (HV_HYP_PAGE_SIZE * i));
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
		/* In a CoCo VM leak the memory if it didn't get re-encrypted */
		if (!channel->ringbuffer_gpadlhandle.decrypted)
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

static int send_modifychannel_without_ack(struct vmbus_channel *channel, u32 target_vp)
{
	struct vmbus_channel_modifychannel msg;
	int ret;

	memset(&msg, 0, sizeof(msg));
	msg.header.msgtype = CHANNELMSG_MODIFYCHANNEL;
	msg.child_relid = channel->offermsg.child_relid;
	msg.target_vp = target_vp;

	ret = vmbus_post_msg(&msg, sizeof(msg), true);
	trace_vmbus_send_modifychannel(&msg, ret);

	return ret;
}

static int send_modifychannel_with_ack(struct vmbus_channel *channel, u32 target_vp)
{
	struct vmbus_channel_modifychannel *msg;
	struct vmbus_channel_msginfo *info;
	unsigned long flags;
	int ret;

	info = kzalloc(sizeof(struct vmbus_channel_msginfo) +
				sizeof(struct vmbus_channel_modifychannel),
		       GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	init_completion(&info->waitevent);
	info->waiting_channel = channel;

	msg = (struct vmbus_channel_modifychannel *)info->msg;
	msg->header.msgtype = CHANNELMSG_MODIFYCHANNEL;
	msg->child_relid = channel->offermsg.child_relid;
	msg->target_vp = target_vp;

	spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);
	list_add_tail(&info->msglistentry, &vmbus_connection.chn_msg_list);
	spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock, flags);

	ret = vmbus_post_msg(msg, sizeof(*msg), true);
	trace_vmbus_send_modifychannel(msg, ret);
	if (ret != 0) {
		spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);
		list_del(&info->msglistentry);
		spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock, flags);
		goto free_info;
	}

	/*
	 * Release channel_mutex; otherwise, vmbus_onoffer_rescind() could block on
	 * the mutex and be unable to signal the completion.
	 *
	 * See the caller target_cpu_store() for information about the usage of the
	 * mutex.
	 */
	mutex_unlock(&vmbus_connection.channel_mutex);
	wait_for_completion(&info->waitevent);
	mutex_lock(&vmbus_connection.channel_mutex);

	spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);
	list_del(&info->msglistentry);
	spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock, flags);

	if (info->response.modify_response.status)
		ret = -EAGAIN;

free_info:
	kfree(info);
	return ret;
}

/*
 * Set/change the vCPU (@target_vp) the channel (@child_relid) will interrupt.
 *
 * CHANNELMSG_MODIFYCHANNEL messages are aynchronous.  When VMbus version 5.3
 * or later is negotiated, Hyper-V always sends an ACK in response to such a
 * message.  For VMbus version 5.2 and earlier, it never sends an ACK.  With-
 * out an ACK, we can not know when the host will stop interrupting the "old"
 * vCPU and start interrupting the "new" vCPU for the given channel.
 *
 * The CHANNELMSG_MODIFYCHANNEL message type is supported since VMBus version
 * VERSION_WIN10_V4_1.
 */
int vmbus_send_modifychannel(struct vmbus_channel *channel, u32 target_vp)
{
	if (vmbus_proto_version >= VERSION_WIN10_V5_3)
		return send_modifychannel_with_ack(channel, target_vp);
	return send_modifychannel_without_ack(channel, target_vp);
}
EXPORT_SYMBOL_GPL(vmbus_send_modifychannel);

/*
 * create_gpadl_header - Creates a gpadl for the specified buffer
 */
static int create_gpadl_header(enum hv_gpadl_type type, void *kbuffer,
			       u32 size, u32 send_offset,
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

	pagecount = hv_gpadl_size(type, size) >> HV_HYP_PAGE_SHIFT;

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
		gpadl_header->range[0].byte_count = hv_gpadl_size(type, size);
		for (i = 0; i < pfncount; i++)
			gpadl_header->range[0].pfn_array[i] = hv_gpadl_hvpfn(
				type, kbuffer, size, send_offset, i);
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
				gpadl_body->pfn[i] = hv_gpadl_hvpfn(type,
					kbuffer, size, send_offset, pfnsum + i);

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
		gpadl_header->range[0].byte_count = hv_gpadl_size(type, size);
		for (i = 0; i < pagecount; i++)
			gpadl_header->range[0].pfn_array[i] = hv_gpadl_hvpfn(
				type, kbuffer, size, send_offset, i);

		*msginfo = msgheader;
	}

	return 0;
nomem:
	kfree(msgheader);
	kfree(msgbody);
	return -ENOMEM;
}

/*
 * __vmbus_establish_gpadl - Establish a GPADL for a buffer or ringbuffer
 *
 * @channel: a channel
 * @type: the type of the corresponding GPADL, only meaningful for the guest.
 * @kbuffer: from kmalloc or vmalloc
 * @size: page-size multiple
 * @send_offset: the offset (in bytes) where the send ring buffer starts,
 *              should be 0 for BUFFER type gpadl
 * @gpadl_handle: some funky thing
 */
static int __vmbus_establish_gpadl(struct vmbus_channel *channel,
				   enum hv_gpadl_type type, void *kbuffer,
				   u32 size, u32 send_offset,
				   struct vmbus_gpadl *gpadl)
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

	ret = create_gpadl_header(type, kbuffer, size, send_offset, &msginfo);
	if (ret) {
		gpadl->decrypted = false;
		return ret;
	}

	/*
	 * Set the "decrypted" flag to true for the set_memory_decrypted()
	 * success case. In the failure case, the encryption state of the
	 * memory is unknown. Leave "decrypted" as true to ensure the
	 * memory will be leaked instead of going back on the free list.
	 */
	gpadl->decrypted = true;
	ret = set_memory_decrypted((unsigned long)kbuffer,
				   PFN_UP(size));
	if (ret) {
		dev_warn(&channel->device_obj->device,
			 "Failed to set host visibility for new GPADL %d.\n",
			 ret);
		return ret;
	}

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
	gpadl->gpadl_handle = gpadlmsg->gpadl;
	gpadl->buffer = kbuffer;
	gpadl->size = size;


cleanup:
	spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);
	list_del(&msginfo->msglistentry);
	spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock, flags);
	list_for_each_entry_safe(submsginfo, tmp, &msginfo->submsglist,
				 msglistentry) {
		kfree(submsginfo);
	}

	kfree(msginfo);

	if (ret) {
		/*
		 * If set_memory_encrypted() fails, the decrypted flag is
		 * left as true so the memory is leaked instead of being
		 * put back on the free list.
		 */
		if (!set_memory_encrypted((unsigned long)kbuffer, PFN_UP(size)))
			gpadl->decrypted = false;
	}

	return ret;
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
			  u32 size, struct vmbus_gpadl *gpadl)
{
	return __vmbus_establish_gpadl(channel, HV_GPADL_BUFFER, kbuffer, size,
				       0U, gpadl);
}
EXPORT_SYMBOL_GPL(vmbus_establish_gpadl);

/**
 * request_arr_init - Allocates memory for the requestor array. Each slot
 * keeps track of the next available slot in the array. Initially, each
 * slot points to the next one (as in a Linked List). The last slot
 * does not point to anything, so its value is U64_MAX by default.
 * @size The size of the array
 */
static u64 *request_arr_init(u32 size)
{
	int i;
	u64 *req_arr;

	req_arr = kcalloc(size, sizeof(u64), GFP_KERNEL);
	if (!req_arr)
		return NULL;

	for (i = 0; i < size - 1; i++)
		req_arr[i] = i + 1;

	/* Last slot (no more available slots) */
	req_arr[i] = U64_MAX;

	return req_arr;
}

/*
 * vmbus_alloc_requestor - Initializes @rqstor's fields.
 * Index 0 is the first free slot
 * @size: Size of the requestor array
 */
static int vmbus_alloc_requestor(struct vmbus_requestor *rqstor, u32 size)
{
	u64 *rqst_arr;
	unsigned long *bitmap;

	rqst_arr = request_arr_init(size);
	if (!rqst_arr)
		return -ENOMEM;

	bitmap = bitmap_zalloc(size, GFP_KERNEL);
	if (!bitmap) {
		kfree(rqst_arr);
		return -ENOMEM;
	}

	rqstor->req_arr = rqst_arr;
	rqstor->req_bitmap = bitmap;
	rqstor->size = size;
	rqstor->next_request_id = 0;
	spin_lock_init(&rqstor->req_lock);

	return 0;
}

/*
 * vmbus_free_requestor - Frees memory allocated for @rqstor
 * @rqstor: Pointer to the requestor struct
 */
static void vmbus_free_requestor(struct vmbus_requestor *rqstor)
{
	kfree(rqstor->req_arr);
	bitmap_free(rqstor->req_bitmap);
}

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

	if (newchannel->state != CHANNEL_OPEN_STATE)
		return -EINVAL;

	/* Create and init requestor */
	if (newchannel->rqstor_size) {
		if (vmbus_alloc_requestor(&newchannel->requestor, newchannel->rqstor_size))
			return -ENOMEM;
	}

	newchannel->state = CHANNEL_OPENING_STATE;
	newchannel->onchannel_callback = onchannelcallback;
	newchannel->channel_callback_context = context;

	if (!newchannel->max_pkt_size)
		newchannel->max_pkt_size = VMBUS_DEFAULT_MAX_PKT_SIZE;

	/* Establish the gpadl for the ring buffer */
	newchannel->ringbuffer_gpadlhandle.gpadl_handle = 0;

	err = __vmbus_establish_gpadl(newchannel, HV_GPADL_RING,
				      page_address(newchannel->ringbuffer_page),
				      (send_pages + recv_pages) << PAGE_SHIFT,
				      newchannel->ringbuffer_send_offset << PAGE_SHIFT,
				      &newchannel->ringbuffer_gpadlhandle);
	if (err)
		goto error_clean_ring;

	err = hv_ringbuffer_init(&newchannel->outbound,
				 page, send_pages, 0);
	if (err)
		goto error_free_gpadl;

	err = hv_ringbuffer_init(&newchannel->inbound, &page[send_pages],
				 recv_pages, newchannel->max_pkt_size);
	if (err)
		goto error_free_gpadl;

	/* Create and init the channel open message */
	open_info = kzalloc(sizeof(*open_info) +
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
	open_msg->ringbuffer_gpadlhandle
		= newchannel->ringbuffer_gpadlhandle.gpadl_handle;
	/*
	 * The unit of ->downstream_ringbuffer_pageoffset is HV_HYP_PAGE and
	 * the unit of ->ringbuffer_send_offset (i.e. send_pages) is PAGE, so
	 * here we calculate it into HV_HYP_PAGE.
	 */
	open_msg->downstream_ringbuffer_pageoffset =
		hv_ring_gpadl_send_hvpgoffset(send_pages << PAGE_SHIFT);
	open_msg->target_vp = hv_cpu_number_to_vp_number(newchannel->target_cpu);

	if (userdatalen)
		memcpy(open_msg->userdata, userdata, userdatalen);

	spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);
	list_add_tail(&open_info->msglistentry,
		      &vmbus_connection.chn_msg_list);
	spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock, flags);

	if (newchannel->rescind) {
		err = -ENODEV;
		goto error_clean_msglist;
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
	vmbus_teardown_gpadl(newchannel, &newchannel->ringbuffer_gpadlhandle);
error_clean_ring:
	hv_ringbuffer_cleanup(&newchannel->outbound);
	hv_ringbuffer_cleanup(&newchannel->inbound);
	vmbus_free_requestor(&newchannel->requestor);
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

/*
 * vmbus_teardown_gpadl -Teardown the specified GPADL handle
 */
int vmbus_teardown_gpadl(struct vmbus_channel *channel, struct vmbus_gpadl *gpadl)
{
	struct vmbus_channel_gpadl_teardown *msg;
	struct vmbus_channel_msginfo *info;
	unsigned long flags;
	int ret;

	info = kzalloc(sizeof(*info) +
		       sizeof(struct vmbus_channel_gpadl_teardown), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	init_completion(&info->waitevent);
	info->waiting_channel = channel;

	msg = (struct vmbus_channel_gpadl_teardown *)info->msg;

	msg->header.msgtype = CHANNELMSG_GPADL_TEARDOWN;
	msg->child_relid = channel->offermsg.child_relid;
	msg->gpadl = gpadl->gpadl_handle;

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

	gpadl->gpadl_handle = 0;

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

	ret = set_memory_encrypted((unsigned long)gpadl->buffer,
				   PFN_UP(gpadl->size));
	if (ret)
		pr_warn("Fail to set mem host visibility in GPADL teardown %d.\n", ret);

	gpadl->decrypted = ret;

	return ret;
}
EXPORT_SYMBOL_GPL(vmbus_teardown_gpadl);

void vmbus_reset_channel_cb(struct vmbus_channel *channel)
{
	unsigned long flags;

	/*
	 * vmbus_on_event(), running in the per-channel tasklet, can race
	 * with vmbus_close_internal() in the case of SMP guest, e.g., when
	 * the former is accessing channel->inbound.ring_buffer, the latter
	 * could be freeing the ring_buffer pages, so here we must stop it
	 * first.
	 *
	 * vmbus_chan_sched() might call the netvsc driver callback function
	 * that ends up scheduling NAPI work that accesses the ring buffer.
	 * At this point, we have to ensure that any such work is completed
	 * and that the channel ring buffer is no longer being accessed, cf.
	 * the calls to napi_disable() in netvsc_device_remove().
	 */
	tasklet_disable(&channel->callback_event);

	/* See the inline comments in vmbus_chan_sched(). */
	spin_lock_irqsave(&channel->sched_lock, flags);
	channel->onchannel_callback = NULL;
	spin_unlock_irqrestore(&channel->sched_lock, flags);

	channel->sc_creation_callback = NULL;

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
	else if (channel->ringbuffer_gpadlhandle.gpadl_handle) {
		ret = vmbus_teardown_gpadl(channel, &channel->ringbuffer_gpadlhandle);
		if (ret) {
			pr_err("Close failed: teardown gpadl return %d\n", ret);
			/*
			 * If we failed to teardown gpadl,
			 * it is perhaps better to leak memory.
			 */
		}
	}

	if (!ret)
		vmbus_free_requestor(&channel->requestor);

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
 * vmbus_sendpacket_getid() - Send the specified buffer on the given channel
 * @channel: Pointer to vmbus_channel structure
 * @buffer: Pointer to the buffer you want to send the data from.
 * @bufferlen: Maximum size of what the buffer holds.
 * @requestid: Identifier of the request
 * @trans_id: Identifier of the transaction associated to this request, if
 *            the send is successful; undefined, otherwise.
 * @type: Type of packet that is being sent e.g. negotiate, time
 *	  packet etc.
 * @flags: 0 or VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED
 *
 * Sends data in @buffer directly to Hyper-V via the vmbus.
 * This will send the data unparsed to Hyper-V.
 *
 * Mainly used by Hyper-V drivers.
 */
int vmbus_sendpacket_getid(struct vmbus_channel *channel, void *buffer,
			   u32 bufferlen, u64 requestid, u64 *trans_id,
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
	desc.trans_id = VMBUS_RQST_ERROR; /* will be updated in hv_ringbuffer_write() */

	bufferlist[0].iov_base = &desc;
	bufferlist[0].iov_len = sizeof(struct vmpacket_descriptor);
	bufferlist[1].iov_base = buffer;
	bufferlist[1].iov_len = bufferlen;
	bufferlist[2].iov_base = &aligned_data;
	bufferlist[2].iov_len = (packetlen_aligned - packetlen);

	return hv_ringbuffer_write(channel, bufferlist, num_vecs, requestid, trans_id);
}
EXPORT_SYMBOL(vmbus_sendpacket_getid);

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
	return vmbus_sendpacket_getid(channel, buffer, bufferlen,
				      requestid, NULL, type, flags);
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
	desc.transactionid = VMBUS_RQST_ERROR; /* will be updated in hv_ringbuffer_write() */
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

	return hv_ringbuffer_write(channel, bufferlist, 3, requestid, NULL);
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
	desc->transactionid = VMBUS_RQST_ERROR; /* will be updated in hv_ringbuffer_write() */
	desc->reserved = 0;
	desc->rangecount = 1;

	bufferlist[0].iov_base = desc;
	bufferlist[0].iov_len = desc_size;
	bufferlist[1].iov_base = buffer;
	bufferlist[1].iov_len = bufferlen;
	bufferlist[2].iov_base = &aligned_data;
	bufferlist[2].iov_len = (packetlen_aligned - packetlen);

	return hv_ringbuffer_write(channel, bufferlist, 3, requestid, NULL);
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

/*
 * vmbus_next_request_id - Returns a new request id. It is also
 * the index at which the guest memory address is stored.
 * Uses a spin lock to avoid race conditions.
 * @channel: Pointer to the VMbus channel struct
 * @rqst_add: Guest memory address to be stored in the array
 */
u64 vmbus_next_request_id(struct vmbus_channel *channel, u64 rqst_addr)
{
	struct vmbus_requestor *rqstor = &channel->requestor;
	unsigned long flags;
	u64 current_id;

	/* Check rqstor has been initialized */
	if (!channel->rqstor_size)
		return VMBUS_NO_RQSTOR;

	lock_requestor(channel, flags);
	current_id = rqstor->next_request_id;

	/* Requestor array is full */
	if (current_id >= rqstor->size) {
		unlock_requestor(channel, flags);
		return VMBUS_RQST_ERROR;
	}

	rqstor->next_request_id = rqstor->req_arr[current_id];
	rqstor->req_arr[current_id] = rqst_addr;

	/* The already held spin lock provides atomicity */
	bitmap_set(rqstor->req_bitmap, current_id, 1);

	unlock_requestor(channel, flags);

	/*
	 * Cannot return an ID of 0, which is reserved for an unsolicited
	 * message from Hyper-V; Hyper-V does not acknowledge (respond to)
	 * VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED requests with ID of
	 * 0 sent by the guest.
	 */
	return current_id + 1;
}
EXPORT_SYMBOL_GPL(vmbus_next_request_id);

/* As in vmbus_request_addr_match() but without the requestor lock */
u64 __vmbus_request_addr_match(struct vmbus_channel *channel, u64 trans_id,
			       u64 rqst_addr)
{
	struct vmbus_requestor *rqstor = &channel->requestor;
	u64 req_addr;

	/* Check rqstor has been initialized */
	if (!channel->rqstor_size)
		return VMBUS_NO_RQSTOR;

	/* Hyper-V can send an unsolicited message with ID of 0 */
	if (!trans_id)
		return VMBUS_RQST_ERROR;

	/* Data corresponding to trans_id is stored at trans_id - 1 */
	trans_id--;

	/* Invalid trans_id */
	if (trans_id >= rqstor->size || !test_bit(trans_id, rqstor->req_bitmap))
		return VMBUS_RQST_ERROR;

	req_addr = rqstor->req_arr[trans_id];
	if (rqst_addr == VMBUS_RQST_ADDR_ANY || req_addr == rqst_addr) {
		rqstor->req_arr[trans_id] = rqstor->next_request_id;
		rqstor->next_request_id = trans_id;

		/* The already held spin lock provides atomicity */
		bitmap_clear(rqstor->req_bitmap, trans_id, 1);
	}

	return req_addr;
}
EXPORT_SYMBOL_GPL(__vmbus_request_addr_match);

/*
 * vmbus_request_addr_match - Clears/removes @trans_id from the @channel's
 * requestor, provided the memory address stored at @trans_id equals @rqst_addr
 * (or provided @rqst_addr matches the sentinel value VMBUS_RQST_ADDR_ANY).
 *
 * Returns the memory address stored at @trans_id, or VMBUS_RQST_ERROR if
 * @trans_id is not contained in the requestor.
 *
 * Acquires and releases the requestor spin lock.
 */
u64 vmbus_request_addr_match(struct vmbus_channel *channel, u64 trans_id,
			     u64 rqst_addr)
{
	unsigned long flags;
	u64 req_addr;

	lock_requestor(channel, flags);
	req_addr = __vmbus_request_addr_match(channel, trans_id, rqst_addr);
	unlock_requestor(channel, flags);

	return req_addr;
}
EXPORT_SYMBOL_GPL(vmbus_request_addr_match);

/*
 * vmbus_request_addr - Returns the memory address stored at @trans_id
 * in @rqstor. Uses a spin lock to avoid race conditions.
 * @channel: Pointer to the VMbus channel struct
 * @trans_id: Request id sent back from Hyper-V. Becomes the requestor's
 * next request id.
 */
u64 vmbus_request_addr(struct vmbus_channel *channel, u64 trans_id)
{
	return vmbus_request_addr_match(channel, trans_id, VMBUS_RQST_ADDR_ANY);
}
EXPORT_SYMBOL_GPL(vmbus_request_addr);
