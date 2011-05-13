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
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/slab.h>

#include "hyperv.h"
#include "hyperv_net.h"


/* Globals */
static const char *driver_name = "netvsc";

/* {F8615163-DF3E-46c5-913F-F2D2F965ED0E} */
static const struct hv_guid netvsc_device_type = {
	.data = {
		0x63, 0x51, 0x61, 0xF8, 0x3E, 0xDF, 0xc5, 0x46,
		0x91, 0x3F, 0xF2, 0xD2, 0xF9, 0x65, 0xED, 0x0E
	}
};


static struct netvsc_device *alloc_net_device(struct hv_device *device)
{
	struct netvsc_device *net_device;

	net_device = kzalloc(sizeof(struct netvsc_device), GFP_KERNEL);
	if (!net_device)
		return NULL;

	/* Set to 2 to allow both inbound and outbound traffic */
	atomic_cmpxchg(&net_device->refcnt, 0, 2);

	net_device->dev = device;
	device->ext = net_device;

	return net_device;
}

static void free_net_device(struct netvsc_device *device)
{
	WARN_ON(atomic_read(&device->refcnt) != 0);
	device->dev->ext = NULL;
	kfree(device);
}


/* Get the net device object iff exists and its refcount > 1 */
static struct netvsc_device *get_outbound_net_device(struct hv_device *device)
{
	struct netvsc_device *net_device;

	net_device = device->ext;
	if (net_device && atomic_read(&net_device->refcnt) > 1)
		atomic_inc(&net_device->refcnt);
	else
		net_device = NULL;

	return net_device;
}

/* Get the net device object iff exists and its refcount > 0 */
static struct netvsc_device *get_inbound_net_device(struct hv_device *device)
{
	struct netvsc_device *net_device;

	net_device = device->ext;
	if (net_device && atomic_read(&net_device->refcnt))
		atomic_inc(&net_device->refcnt);
	else
		net_device = NULL;

	return net_device;
}

static void put_net_device(struct hv_device *device)
{
	struct netvsc_device *net_device;

	net_device = device->ext;

	atomic_dec(&net_device->refcnt);
}

static struct netvsc_device *release_outbound_net_device(
		struct hv_device *device)
{
	struct netvsc_device *net_device;

	net_device = device->ext;
	if (net_device == NULL)
		return NULL;

	/* Busy wait until the ref drop to 2, then set it to 1 */
	while (atomic_cmpxchg(&net_device->refcnt, 2, 1) != 2)
		udelay(100);

	return net_device;
}

static struct netvsc_device *release_inbound_net_device(
		struct hv_device *device)
{
	struct netvsc_device *net_device;

	net_device = device->ext;
	if (net_device == NULL)
		return NULL;

	/* Busy wait until the ref drop to 1, then set it to 0 */
	while (atomic_cmpxchg(&net_device->refcnt, 1, 0) != 1)
		udelay(100);

	device->ext = NULL;
	return net_device;
}

static int netvsc_destroy_recv_buf(struct netvsc_device *net_device)
{
	struct nvsp_message *revoke_packet;
	int ret = 0;

	/*
	 * If we got a section count, it means we received a
	 * SendReceiveBufferComplete msg (ie sent
	 * NvspMessage1TypeSendReceiveBuffer msg) therefore, we need
	 * to send a revoke msg here
	 */
	if (net_device->recv_section_cnt) {
		/* Send the revoke receive buffer */
		revoke_packet = &net_device->revoke_packet;
		memset(revoke_packet, 0, sizeof(struct nvsp_message));

		revoke_packet->hdr.msg_type =
			NVSP_MSG1_TYPE_REVOKE_RECV_BUF;
		revoke_packet->msg.v1_msg.
		revoke_recv_buf.id = NETVSC_RECEIVE_BUFFER_ID;

		ret = vmbus_sendpacket(net_device->dev->channel,
				       revoke_packet,
				       sizeof(struct nvsp_message),
				       (unsigned long)revoke_packet,
				       VM_PKT_DATA_INBAND, 0);
		/*
		 * If we failed here, we might as well return and
		 * have a leak rather than continue and a bugchk
		 */
		if (ret != 0) {
			dev_err(&net_device->dev->device, "unable to send "
				"revoke receive buffer to netvsp");
			return -1;
		}
	}

	/* Teardown the gpadl on the vsp end */
	if (net_device->recv_buf_gpadl_handle) {
		ret = vmbus_teardown_gpadl(net_device->dev->channel,
			   net_device->recv_buf_gpadl_handle);

		/* If we failed here, we might as well return and have a leak
		 * rather than continue and a bugchk
		 */
		if (ret != 0) {
			dev_err(&net_device->dev->device,
				   "unable to teardown receive buffer's gpadl");
			return -1;
		}
		net_device->recv_buf_gpadl_handle = 0;
	}

	if (net_device->recv_buf) {
		/* Free up the receive buffer */
		free_pages((unsigned long)net_device->recv_buf,
			get_order(net_device->recv_buf_size));
		net_device->recv_buf = NULL;
	}

	if (net_device->recv_section) {
		net_device->recv_section_cnt = 0;
		kfree(net_device->recv_section);
		net_device->recv_section = NULL;
	}

	return ret;
}

static int netvsc_init_recv_buf(struct hv_device *device)
{
	int ret = 0;
	int t;
	struct netvsc_device *net_device;
	struct nvsp_message *init_packet;

	net_device = get_outbound_net_device(device);
	if (!net_device) {
		dev_err(&device->device, "unable to get net device..."
			   "device being destroyed?");
		return -1;
	}

	net_device->recv_buf =
		(void *)__get_free_pages(GFP_KERNEL|__GFP_ZERO,
				get_order(net_device->recv_buf_size));
	if (!net_device->recv_buf) {
		dev_err(&device->device, "unable to allocate receive "
			"buffer of size %d", net_device->recv_buf_size);
		ret = -1;
		goto cleanup;
	}

	/*
	 * Establish the gpadl handle for this buffer on this
	 * channel.  Note: This call uses the vmbus connection rather
	 * than the channel to establish the gpadl handle.
	 */
	ret = vmbus_establish_gpadl(device->channel, net_device->recv_buf,
				    net_device->recv_buf_size,
				    &net_device->recv_buf_gpadl_handle);
	if (ret != 0) {
		dev_err(&device->device,
			"unable to establish receive buffer's gpadl");
		goto cleanup;
	}


	/* Notify the NetVsp of the gpadl handle */
	init_packet = &net_device->channel_init_pkt;

	memset(init_packet, 0, sizeof(struct nvsp_message));

	init_packet->hdr.msg_type = NVSP_MSG1_TYPE_SEND_RECV_BUF;
	init_packet->msg.v1_msg.send_recv_buf.
		gpadl_handle = net_device->recv_buf_gpadl_handle;
	init_packet->msg.v1_msg.
		send_recv_buf.id = NETVSC_RECEIVE_BUFFER_ID;

	/* Send the gpadl notification request */
	ret = vmbus_sendpacket(device->channel, init_packet,
			       sizeof(struct nvsp_message),
			       (unsigned long)init_packet,
			       VM_PKT_DATA_INBAND,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	if (ret != 0) {
		dev_err(&device->device,
			"unable to send receive buffer's gpadl to netvsp");
		goto cleanup;
	}

	t = wait_for_completion_timeout(&net_device->channel_init_wait, HZ);
	BUG_ON(t == 0);


	/* Check the response */
	if (init_packet->msg.v1_msg.
	    send_recv_buf_complete.status != NVSP_STAT_SUCCESS) {
		dev_err(&device->device, "Unable to complete receive buffer "
			   "initialzation with NetVsp - status %d",
			   init_packet->msg.v1_msg.
			   send_recv_buf_complete.status);
		ret = -1;
		goto cleanup;
	}

	/* Parse the response */

	net_device->recv_section_cnt = init_packet->msg.
		v1_msg.send_recv_buf_complete.num_sections;

	net_device->recv_section = kmalloc(net_device->recv_section_cnt
		* sizeof(struct nvsp_1_receive_buffer_section), GFP_KERNEL);
	if (net_device->recv_section == NULL) {
		ret = -1;
		goto cleanup;
	}

	memcpy(net_device->recv_section,
		init_packet->msg.v1_msg.
	       send_recv_buf_complete.sections,
		net_device->recv_section_cnt *
	       sizeof(struct nvsp_1_receive_buffer_section));

	/*
	 * For 1st release, there should only be 1 section that represents the
	 * entire receive buffer
	 */
	if (net_device->recv_section_cnt != 1 ||
	    net_device->recv_section->offset != 0) {
		ret = -1;
		goto cleanup;
	}

	goto exit;

cleanup:
	netvsc_destroy_recv_buf(net_device);

exit:
	put_net_device(device);
	return ret;
}

static int netvsc_destroy_send_buf(struct netvsc_device *net_device)
{
	struct nvsp_message *revoke_packet;
	int ret = 0;

	/*
	 * If we got a section count, it means we received a
	 *  SendReceiveBufferComplete msg (ie sent
	 *  NvspMessage1TypeSendReceiveBuffer msg) therefore, we need
	 *  to send a revoke msg here
	 */
	if (net_device->send_section_size) {
		/* Send the revoke send buffer */
		revoke_packet = &net_device->revoke_packet;
		memset(revoke_packet, 0, sizeof(struct nvsp_message));

		revoke_packet->hdr.msg_type =
			NVSP_MSG1_TYPE_REVOKE_SEND_BUF;
		revoke_packet->msg.v1_msg.
			revoke_send_buf.id = NETVSC_SEND_BUFFER_ID;

		ret = vmbus_sendpacket(net_device->dev->channel,
				       revoke_packet,
				       sizeof(struct nvsp_message),
				       (unsigned long)revoke_packet,
				       VM_PKT_DATA_INBAND, 0);
		/*
		 * If we failed here, we might as well return and have a leak
		 * rather than continue and a bugchk
		 */
		if (ret != 0) {
			dev_err(&net_device->dev->device, "unable to send "
				"revoke send buffer to netvsp");
			return -1;
		}
	}

	/* Teardown the gpadl on the vsp end */
	if (net_device->send_buf_gpadl_handle) {
		ret = vmbus_teardown_gpadl(net_device->dev->channel,
					   net_device->send_buf_gpadl_handle);

		/*
		 * If we failed here, we might as well return and have a leak
		 * rather than continue and a bugchk
		 */
		if (ret != 0) {
			dev_err(&net_device->dev->device,
				"unable to teardown send buffer's gpadl");
			return -1;
		}
		net_device->send_buf_gpadl_handle = 0;
	}

	if (net_device->send_buf) {
		/* Free up the receive buffer */
		free_pages((unsigned long)net_device->send_buf,
				get_order(net_device->send_buf_size));
		net_device->send_buf = NULL;
	}

	return ret;
}

static int netvsc_init_send_buf(struct hv_device *device)
{
	int ret = 0;
	int t;
	struct netvsc_device *net_device;
	struct nvsp_message *init_packet;

	net_device = get_outbound_net_device(device);
	if (!net_device) {
		dev_err(&device->device, "unable to get net device..."
			   "device being destroyed?");
		return -1;
	}
	if (net_device->send_buf_size <= 0) {
		ret = -EINVAL;
		goto cleanup;
	}

	net_device->send_buf =
		(void *)__get_free_pages(GFP_KERNEL|__GFP_ZERO,
				get_order(net_device->send_buf_size));
	if (!net_device->send_buf) {
		dev_err(&device->device, "unable to allocate send "
			"buffer of size %d", net_device->send_buf_size);
		ret = -1;
		goto cleanup;
	}

	/*
	 * Establish the gpadl handle for this buffer on this
	 * channel.  Note: This call uses the vmbus connection rather
	 * than the channel to establish the gpadl handle.
	 */
	ret = vmbus_establish_gpadl(device->channel, net_device->send_buf,
				    net_device->send_buf_size,
				    &net_device->send_buf_gpadl_handle);
	if (ret != 0) {
		dev_err(&device->device, "unable to establish send buffer's gpadl");
		goto cleanup;
	}

	/* Notify the NetVsp of the gpadl handle */
	init_packet = &net_device->channel_init_pkt;

	memset(init_packet, 0, sizeof(struct nvsp_message));

	init_packet->hdr.msg_type = NVSP_MSG1_TYPE_SEND_SEND_BUF;
	init_packet->msg.v1_msg.send_recv_buf.
		gpadl_handle = net_device->send_buf_gpadl_handle;
	init_packet->msg.v1_msg.send_recv_buf.id =
		NETVSC_SEND_BUFFER_ID;

	/* Send the gpadl notification request */
	ret = vmbus_sendpacket(device->channel, init_packet,
			       sizeof(struct nvsp_message),
			       (unsigned long)init_packet,
			       VM_PKT_DATA_INBAND,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	if (ret != 0) {
		dev_err(&device->device,
			   "unable to send receive buffer's gpadl to netvsp");
		goto cleanup;
	}

	t = wait_for_completion_timeout(&net_device->channel_init_wait, HZ);

	BUG_ON(t == 0);

	/* Check the response */
	if (init_packet->msg.v1_msg.
	    send_send_buf_complete.status != NVSP_STAT_SUCCESS) {
		dev_err(&device->device, "Unable to complete send buffer "
			   "initialzation with NetVsp - status %d",
			   init_packet->msg.v1_msg.
			   send_send_buf_complete.status);
		ret = -1;
		goto cleanup;
	}

	net_device->send_section_size = init_packet->
	msg.v1_msg.send_send_buf_complete.section_size;

	goto exit;

cleanup:
	netvsc_destroy_send_buf(net_device);

exit:
	put_net_device(device);
	return ret;
}


static int netvsc_connect_vsp(struct hv_device *device)
{
	int ret, t;
	struct netvsc_device *net_device;
	struct nvsp_message *init_packet;
	int ndis_version;

	net_device = get_outbound_net_device(device);
	if (!net_device) {
		dev_err(&device->device, "unable to get net device..."
			   "device being destroyed?");
		return -1;
	}

	init_packet = &net_device->channel_init_pkt;

	memset(init_packet, 0, sizeof(struct nvsp_message));
	init_packet->hdr.msg_type = NVSP_MSG_TYPE_INIT;
	init_packet->msg.init_msg.init.min_protocol_ver =
		NVSP_MIN_PROTOCOL_VERSION;
	init_packet->msg.init_msg.init.max_protocol_ver =
		NVSP_MAX_PROTOCOL_VERSION;

	/* Send the init request */
	ret = vmbus_sendpacket(device->channel, init_packet,
			       sizeof(struct nvsp_message),
			       (unsigned long)init_packet,
			       VM_PKT_DATA_INBAND,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);

	if (ret != 0)
		goto cleanup;

	t = wait_for_completion_timeout(&net_device->channel_init_wait, HZ);

	if (t == 0) {
		ret = -ETIMEDOUT;
		goto cleanup;
	}

	if (init_packet->msg.init_msg.init_complete.status !=
	    NVSP_STAT_SUCCESS) {
		ret = -1;
		goto cleanup;
	}

	if (init_packet->msg.init_msg.init_complete.
	    negotiated_protocol_ver != NVSP_PROTOCOL_VERSION_1) {
		ret = -1;
		goto cleanup;
	}
	/* Send the ndis version */
	memset(init_packet, 0, sizeof(struct nvsp_message));

	ndis_version = 0x00050000;

	init_packet->hdr.msg_type = NVSP_MSG1_TYPE_SEND_NDIS_VER;
	init_packet->msg.v1_msg.
		send_ndis_ver.ndis_major_ver =
				(ndis_version & 0xFFFF0000) >> 16;
	init_packet->msg.v1_msg.
		send_ndis_ver.ndis_minor_ver =
				ndis_version & 0xFFFF;

	/* Send the init request */
	ret = vmbus_sendpacket(device->channel, init_packet,
				sizeof(struct nvsp_message),
				(unsigned long)init_packet,
				VM_PKT_DATA_INBAND, 0);
	if (ret != 0) {
		ret = -1;
		goto cleanup;
	}

	/* Post the big receive buffer to NetVSP */
	ret = netvsc_init_recv_buf(device);
	if (ret == 0)
		ret = netvsc_init_send_buf(device);

cleanup:
	put_net_device(device);
	return ret;
}

static void netvsc_disconnect_vsp(struct netvsc_device *net_device)
{
	netvsc_destroy_recv_buf(net_device);
	netvsc_destroy_send_buf(net_device);
}

/*
 * netvsc_device_remove - Callback when the root bus device is removed
 */
int netvsc_device_remove(struct hv_device *device)
{
	struct netvsc_device *net_device;
	struct hv_netvsc_packet *netvsc_packet, *pos;

	/* Stop outbound traffic ie sends and receives completions */
	net_device = release_outbound_net_device(device);
	if (!net_device) {
		dev_err(&device->device, "No net device present!!");
		return -1;
	}

	/* Wait for all send completions */
	while (atomic_read(&net_device->num_outstanding_sends)) {
		dev_err(&device->device,
			"waiting for %d requests to complete...",
			atomic_read(&net_device->num_outstanding_sends));
		udelay(100);
	}

	netvsc_disconnect_vsp(net_device);

	/* Stop inbound traffic ie receives and sends completions */
	net_device = release_inbound_net_device(device);

	/* At this point, no one should be accessing netDevice except in here */
	dev_notice(&device->device, "net device safe to remove");

	/* Now, we can close the channel safely */
	vmbus_close(device->channel);

	/* Release all resources */
	list_for_each_entry_safe(netvsc_packet, pos,
				 &net_device->recv_pkt_list, list_ent) {
		list_del(&netvsc_packet->list_ent);
		kfree(netvsc_packet);
	}

	free_net_device(net_device);
	return 0;
}

static void netvsc_send_completion(struct hv_device *device,
				   struct vmpacket_descriptor *packet)
{
	struct netvsc_device *net_device;
	struct nvsp_message *nvsp_packet;
	struct hv_netvsc_packet *nvsc_packet;

	net_device = get_inbound_net_device(device);
	if (!net_device) {
		dev_err(&device->device, "unable to get net device..."
			   "device being destroyed?");
		return;
	}

	nvsp_packet = (struct nvsp_message *)((unsigned long)packet +
			(packet->offset8 << 3));

	if ((nvsp_packet->hdr.msg_type == NVSP_MSG_TYPE_INIT_COMPLETE) ||
	    (nvsp_packet->hdr.msg_type ==
	     NVSP_MSG1_TYPE_SEND_RECV_BUF_COMPLETE) ||
	    (nvsp_packet->hdr.msg_type ==
	     NVSP_MSG1_TYPE_SEND_SEND_BUF_COMPLETE)) {
		/* Copy the response back */
		memcpy(&net_device->channel_init_pkt, nvsp_packet,
		       sizeof(struct nvsp_message));
		complete(&net_device->channel_init_wait);
	} else if (nvsp_packet->hdr.msg_type ==
		   NVSP_MSG1_TYPE_SEND_RNDIS_PKT_COMPLETE) {
		/* Get the send context */
		nvsc_packet = (struct hv_netvsc_packet *)(unsigned long)
			packet->trans_id;

		/* Notify the layer above us */
		nvsc_packet->completion.send.send_completion(
			nvsc_packet->completion.send.send_completion_ctx);

		atomic_dec(&net_device->num_outstanding_sends);
	} else {
		dev_err(&device->device, "Unknown send completion packet type- "
			   "%d received!!", nvsp_packet->hdr.msg_type);
	}

	put_net_device(device);
}

int netvsc_send(struct hv_device *device,
			struct hv_netvsc_packet *packet)
{
	struct netvsc_device *net_device;
	int ret = 0;

	struct nvsp_message sendMessage;

	net_device = get_outbound_net_device(device);
	if (!net_device) {
		dev_err(&device->device, "net device (%p) shutting down..."
			   "ignoring outbound packets", net_device);
		return -2;
	}

	sendMessage.hdr.msg_type = NVSP_MSG1_TYPE_SEND_RNDIS_PKT;
	if (packet->is_data_pkt) {
		/* 0 is RMC_DATA; */
		sendMessage.msg.v1_msg.send_rndis_pkt.channel_type = 0;
	} else {
		/* 1 is RMC_CONTROL; */
		sendMessage.msg.v1_msg.send_rndis_pkt.channel_type = 1;
	}

	/* Not using send buffer section */
	sendMessage.msg.v1_msg.send_rndis_pkt.send_buf_section_index =
		0xFFFFFFFF;
	sendMessage.msg.v1_msg.send_rndis_pkt.send_buf_section_size = 0;

	if (packet->page_buf_cnt) {
		ret = vmbus_sendpacket_pagebuffer(device->channel,
						  packet->page_buf,
						  packet->page_buf_cnt,
						  &sendMessage,
						  sizeof(struct nvsp_message),
						  (unsigned long)packet);
	} else {
		ret = vmbus_sendpacket(device->channel, &sendMessage,
				       sizeof(struct nvsp_message),
				       (unsigned long)packet,
				       VM_PKT_DATA_INBAND,
				       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);

	}

	if (ret != 0)
		dev_err(&device->device, "Unable to send packet %p ret %d",
			   packet, ret);

	atomic_inc(&net_device->num_outstanding_sends);
	put_net_device(device);
	return ret;
}

static void netvsc_send_recv_completion(struct hv_device *device,
					u64 transaction_id)
{
	struct nvsp_message recvcompMessage;
	int retries = 0;
	int ret;

	recvcompMessage.hdr.msg_type =
				NVSP_MSG1_TYPE_SEND_RNDIS_PKT_COMPLETE;

	/* FIXME: Pass in the status */
	recvcompMessage.msg.v1_msg.send_rndis_pkt_complete.status =
		NVSP_STAT_SUCCESS;

retry_send_cmplt:
	/* Send the completion */
	ret = vmbus_sendpacket(device->channel, &recvcompMessage,
			       sizeof(struct nvsp_message), transaction_id,
			       VM_PKT_COMP, 0);
	if (ret == 0) {
		/* success */
		/* no-op */
	} else if (ret == -1) {
		/* no more room...wait a bit and attempt to retry 3 times */
		retries++;
		dev_err(&device->device, "unable to send receive completion pkt"
			" (tid %llx)...retrying %d", transaction_id, retries);

		if (retries < 4) {
			udelay(100);
			goto retry_send_cmplt;
		} else {
			dev_err(&device->device, "unable to send receive "
				"completion pkt (tid %llx)...give up retrying",
				transaction_id);
		}
	} else {
		dev_err(&device->device, "unable to send receive "
			"completion pkt - %llx", transaction_id);
	}
}

/* Send a receive completion packet to RNDIS device (ie NetVsp) */
static void netvsc_receive_completion(void *context)
{
	struct hv_netvsc_packet *packet = context;
	struct hv_device *device = (struct hv_device *)packet->device;
	struct netvsc_device *net_device;
	u64 transaction_id = 0;
	bool fsend_receive_comp = false;
	unsigned long flags;

	/*
	 * Even though it seems logical to do a GetOutboundNetDevice() here to
	 * send out receive completion, we are using GetInboundNetDevice()
	 * since we may have disable outbound traffic already.
	 */
	net_device = get_inbound_net_device(device);
	if (!net_device) {
		dev_err(&device->device, "unable to get net device..."
			   "device being destroyed?");
		return;
	}

	/* Overloading use of the lock. */
	spin_lock_irqsave(&net_device->recv_pkt_list_lock, flags);

	packet->xfer_page_pkt->count--;

	/*
	 * Last one in the line that represent 1 xfer page packet.
	 * Return the xfer page packet itself to the freelist
	 */
	if (packet->xfer_page_pkt->count == 0) {
		fsend_receive_comp = true;
		transaction_id = packet->completion.recv.recv_completion_tid;
		list_add_tail(&packet->xfer_page_pkt->list_ent,
			      &net_device->recv_pkt_list);

	}

	/* Put the packet back */
	list_add_tail(&packet->list_ent, &net_device->recv_pkt_list);
	spin_unlock_irqrestore(&net_device->recv_pkt_list_lock, flags);

	/* Send a receive completion for the xfer page packet */
	if (fsend_receive_comp)
		netvsc_send_recv_completion(device, transaction_id);

	put_net_device(device);
}

static void netvsc_receive(struct hv_device *device,
			    struct vmpacket_descriptor *packet)
{
	struct netvsc_device *net_device;
	struct vmtransfer_page_packet_header *vmxferpage_packet;
	struct nvsp_message *nvsp_packet;
	struct hv_netvsc_packet *netvsc_packet = NULL;
	unsigned long start;
	unsigned long end, end_virtual;
	/* struct netvsc_driver *netvscDriver; */
	struct xferpage_packet *xferpage_packet = NULL;
	int i, j;
	int count = 0, bytes_remain = 0;
	unsigned long flags;

	LIST_HEAD(listHead);

	net_device = get_inbound_net_device(device);
	if (!net_device) {
		dev_err(&device->device, "unable to get net device..."
			   "device being destroyed?");
		return;
	}

	/*
	 * All inbound packets other than send completion should be xfer page
	 * packet
	 */
	if (packet->type != VM_PKT_DATA_USING_XFER_PAGES) {
		dev_err(&device->device, "Unknown packet type received - %d",
			   packet->type);
		put_net_device(device);
		return;
	}

	nvsp_packet = (struct nvsp_message *)((unsigned long)packet +
			(packet->offset8 << 3));

	/* Make sure this is a valid nvsp packet */
	if (nvsp_packet->hdr.msg_type !=
	    NVSP_MSG1_TYPE_SEND_RNDIS_PKT) {
		dev_err(&device->device, "Unknown nvsp packet type received-"
			" %d", nvsp_packet->hdr.msg_type);
		put_net_device(device);
		return;
	}

	vmxferpage_packet = (struct vmtransfer_page_packet_header *)packet;

	if (vmxferpage_packet->xfer_pageset_id != NETVSC_RECEIVE_BUFFER_ID) {
		dev_err(&device->device, "Invalid xfer page set id - "
			   "expecting %x got %x", NETVSC_RECEIVE_BUFFER_ID,
			   vmxferpage_packet->xfer_pageset_id);
		put_net_device(device);
		return;
	}

	/*
	 * Grab free packets (range count + 1) to represent this xfer
	 * page packet. +1 to represent the xfer page packet itself.
	 * We grab it here so that we know exactly how many we can
	 * fulfil
	 */
	spin_lock_irqsave(&net_device->recv_pkt_list_lock, flags);
	while (!list_empty(&net_device->recv_pkt_list)) {
		list_move_tail(net_device->recv_pkt_list.next, &listHead);
		if (++count == vmxferpage_packet->range_cnt + 1)
			break;
	}
	spin_unlock_irqrestore(&net_device->recv_pkt_list_lock, flags);

	/*
	 * We need at least 2 netvsc pkts (1 to represent the xfer
	 * page and at least 1 for the range) i.e. we can handled
	 * some of the xfer page packet ranges...
	 */
	if (count < 2) {
		dev_err(&device->device, "Got only %d netvsc pkt...needed "
			"%d pkts. Dropping this xfer page packet completely!",
			count, vmxferpage_packet->range_cnt + 1);

		/* Return it to the freelist */
		spin_lock_irqsave(&net_device->recv_pkt_list_lock, flags);
		for (i = count; i != 0; i--) {
			list_move_tail(listHead.next,
				       &net_device->recv_pkt_list);
		}
		spin_unlock_irqrestore(&net_device->recv_pkt_list_lock,
				       flags);

		netvsc_send_recv_completion(device,
					    vmxferpage_packet->d.trans_id);

		put_net_device(device);
		return;
	}

	/* Remove the 1st packet to represent the xfer page packet itself */
	xferpage_packet = (struct xferpage_packet *)listHead.next;
	list_del(&xferpage_packet->list_ent);

	/* This is how much we can satisfy */
	xferpage_packet->count = count - 1;

	if (xferpage_packet->count != vmxferpage_packet->range_cnt) {
		dev_err(&device->device, "Needed %d netvsc pkts to satisy "
			"this xfer page...got %d",
			vmxferpage_packet->range_cnt, xferpage_packet->count);
	}

	/* Each range represents 1 RNDIS pkt that contains 1 ethernet frame */
	for (i = 0; i < (count - 1); i++) {
		netvsc_packet = (struct hv_netvsc_packet *)listHead.next;
		list_del(&netvsc_packet->list_ent);

		/* Initialize the netvsc packet */
		netvsc_packet->xfer_page_pkt = xferpage_packet;
		netvsc_packet->completion.recv.recv_completion =
					netvsc_receive_completion;
		netvsc_packet->completion.recv.recv_completion_ctx =
					netvsc_packet;
		netvsc_packet->device = device;
		/* Save this so that we can send it back */
		netvsc_packet->completion.recv.recv_completion_tid =
					vmxferpage_packet->d.trans_id;

		netvsc_packet->total_data_buflen =
					vmxferpage_packet->ranges[i].byte_count;
		netvsc_packet->page_buf_cnt = 1;

		netvsc_packet->page_buf[0].len =
					vmxferpage_packet->ranges[i].byte_count;

		start = virt_to_phys((void *)((unsigned long)net_device->
		recv_buf + vmxferpage_packet->ranges[i].byte_offset));

		netvsc_packet->page_buf[0].pfn = start >> PAGE_SHIFT;
		end_virtual = (unsigned long)net_device->recv_buf
		    + vmxferpage_packet->ranges[i].byte_offset
		    + vmxferpage_packet->ranges[i].byte_count - 1;
		end = virt_to_phys((void *)end_virtual);

		/* Calculate the page relative offset */
		netvsc_packet->page_buf[0].offset =
			vmxferpage_packet->ranges[i].byte_offset &
			(PAGE_SIZE - 1);
		if ((end >> PAGE_SHIFT) != (start >> PAGE_SHIFT)) {
			/* Handle frame across multiple pages: */
			netvsc_packet->page_buf[0].len =
				(netvsc_packet->page_buf[0].pfn <<
				 PAGE_SHIFT)
				+ PAGE_SIZE - start;
			bytes_remain = netvsc_packet->total_data_buflen -
					netvsc_packet->page_buf[0].len;
			for (j = 1; j < NETVSC_PACKET_MAXPAGE; j++) {
				netvsc_packet->page_buf[j].offset = 0;
				if (bytes_remain <= PAGE_SIZE) {
					netvsc_packet->page_buf[j].len =
						bytes_remain;
					bytes_remain = 0;
				} else {
					netvsc_packet->page_buf[j].len =
						PAGE_SIZE;
					bytes_remain -= PAGE_SIZE;
				}
				netvsc_packet->page_buf[j].pfn =
				    virt_to_phys((void *)(end_virtual -
						bytes_remain)) >> PAGE_SHIFT;
				netvsc_packet->page_buf_cnt++;
				if (bytes_remain == 0)
					break;
			}
		}

		/* Pass it to the upper layer */
		rndis_filter_receive(device, netvsc_packet);

		netvsc_receive_completion(netvsc_packet->
				completion.recv.recv_completion_ctx);
	}

	put_net_device(device);
}

static void netvsc_channel_cb(void *context)
{
	int ret;
	struct hv_device *device = context;
	struct netvsc_device *net_device;
	u32 bytes_recvd;
	u64 request_id;
	unsigned char *packet;
	struct vmpacket_descriptor *desc;
	unsigned char *buffer;
	int bufferlen = NETVSC_PACKET_SIZE;

	packet = kzalloc(NETVSC_PACKET_SIZE * sizeof(unsigned char),
			 GFP_ATOMIC);
	if (!packet)
		return;
	buffer = packet;

	net_device = get_inbound_net_device(device);
	if (!net_device) {
		dev_err(&device->device, "net device (%p) shutting down..."
			   "ignoring inbound packets", net_device);
		goto out;
	}

	do {
		ret = vmbus_recvpacket_raw(device->channel, buffer, bufferlen,
					   &bytes_recvd, &request_id);
		if (ret == 0) {
			if (bytes_recvd > 0) {
				desc = (struct vmpacket_descriptor *)buffer;
				switch (desc->type) {
				case VM_PKT_COMP:
					netvsc_send_completion(device, desc);
					break;

				case VM_PKT_DATA_USING_XFER_PAGES:
					netvsc_receive(device, desc);
					break;

				default:
					dev_err(&device->device,
						   "unhandled packet type %d, "
						   "tid %llx len %d\n",
						   desc->type, request_id,
						   bytes_recvd);
					break;
				}

				/* reset */
				if (bufferlen > NETVSC_PACKET_SIZE) {
					kfree(buffer);
					buffer = packet;
					bufferlen = NETVSC_PACKET_SIZE;
				}
			} else {
				/* reset */
				if (bufferlen > NETVSC_PACKET_SIZE) {
					kfree(buffer);
					buffer = packet;
					bufferlen = NETVSC_PACKET_SIZE;
				}

				break;
			}
		} else if (ret == -2) {
			/* Handle large packet */
			buffer = kmalloc(bytes_recvd, GFP_ATOMIC);
			if (buffer == NULL) {
				/* Try again next time around */
				dev_err(&device->device,
					   "unable to allocate buffer of size "
					   "(%d)!!", bytes_recvd);
				break;
			}

			bufferlen = bytes_recvd;
		}
	} while (1);

	put_net_device(device);
out:
	kfree(buffer);
	return;
}

/*
 * netvsc_device_add - Callback when the device belonging to this
 * driver is added
 */
int netvsc_device_add(struct hv_device *device, void *additional_info)
{
	int ret = 0;
	int i;
	int ring_size =
	((struct netvsc_device_info *)additional_info)->ring_size;
	struct netvsc_device *net_device;
	struct hv_netvsc_packet *packet, *pos;

	net_device = alloc_net_device(device);
	if (!net_device) {
		ret = -1;
		goto cleanup;
	}

	/* Initialize the NetVSC channel extension */
	net_device->recv_buf_size = NETVSC_RECEIVE_BUFFER_SIZE;
	spin_lock_init(&net_device->recv_pkt_list_lock);

	net_device->send_buf_size = NETVSC_SEND_BUFFER_SIZE;

	INIT_LIST_HEAD(&net_device->recv_pkt_list);

	for (i = 0; i < NETVSC_RECEIVE_PACKETLIST_COUNT; i++) {
		packet = kzalloc(sizeof(struct hv_netvsc_packet) +
				 (NETVSC_RECEIVE_SG_COUNT *
				  sizeof(struct hv_page_buffer)), GFP_KERNEL);
		if (!packet)
			break;

		list_add_tail(&packet->list_ent,
			      &net_device->recv_pkt_list);
	}
	init_completion(&net_device->channel_init_wait);

	/* Open the channel */
	ret = vmbus_open(device->channel, ring_size * PAGE_SIZE,
			 ring_size * PAGE_SIZE, NULL, 0,
			 netvsc_channel_cb, device);

	if (ret != 0) {
		dev_err(&device->device, "unable to open channel: %d", ret);
		ret = -1;
		goto cleanup;
	}

	/* Channel is opened */
	pr_info("hv_netvsc channel opened successfully");

	/* Connect with the NetVsp */
	ret = netvsc_connect_vsp(device);
	if (ret != 0) {
		dev_err(&device->device,
			"unable to connect to NetVSP - %d", ret);
		ret = -1;
		goto close;
	}

	return ret;

close:
	/* Now, we can close the channel safely */
	vmbus_close(device->channel);

cleanup:

	if (net_device) {
		list_for_each_entry_safe(packet, pos,
					 &net_device->recv_pkt_list,
					 list_ent) {
			list_del(&packet->list_ent);
			kfree(packet);
		}

		release_outbound_net_device(device);
		release_inbound_net_device(device);

		free_net_device(net_device);
	}

	return ret;
}

/*
 * netvsc_initialize - Main entry point
 */
int netvsc_initialize(struct hv_driver *drv)
{

	drv->name = driver_name;
	memcpy(&drv->dev_type, &netvsc_device_type, sizeof(struct hv_guid));

	return 0;
}
