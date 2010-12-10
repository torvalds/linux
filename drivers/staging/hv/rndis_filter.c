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
#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/if_ether.h>

#include "osd.h"
#include "logging.h"
#include "netvsc_api.h"
#include "rndis_filter.h"

/* Data types */
struct rndis_filter_driver_object {
	/* The original driver */
	struct netvsc_driver inner_drv;
};

enum rndis_device_state {
	RNDIS_DEV_UNINITIALIZED = 0,
	RNDIS_DEV_INITIALIZING,
	RNDIS_DEV_INITIALIZED,
	RNDIS_DEV_DATAINITIALIZED,
};

struct rndis_device {
	struct netvsc_device *net_dev;

	enum rndis_device_state state;
	u32 link_stat;
	atomic_t new_req_id;

	spinlock_t request_lock;
	struct list_head req_list;

	unsigned char hw_mac_adr[ETH_ALEN];
};

struct rndis_request {
	struct list_head list_ent;
	struct osd_waitevent *waitevent;

	/*
	 * FIXME: We assumed a fixed size response here. If we do ever need to
	 * handle a bigger response, we can either define a max response
	 * message or add a response buffer variable above this field
	 */
	struct rndis_message response_msg;

	/* Simplify allocation by having a netvsc packet inline */
	struct hv_netvsc_packet	pkt;
	struct hv_page_buffer buf;
	/* FIXME: We assumed a fixed size request here. */
	struct rndis_message request_msg;
};


struct rndis_filter_packet {
	void *completion_ctx;
	void (*completion)(void *context);
	struct rndis_message msg;
};


static int rndis_filte_device_add(struct hv_device *dev,
				  void *additional_info);

static int rndis_filter_device_remove(struct hv_device *dev);

static void rndis_filter_cleanup(struct hv_driver *drv);

static int rndis_filter_send(struct hv_device *dev,
			     struct hv_netvsc_packet *pkt);

static void rndis_filter_send_completion(void *ctx);

static void rndis_filter_send_request_completion(void *ctx);


/* The one and only */
static struct rndis_filter_driver_object rndis_filter;

static struct rndis_device *get_rndis_device(void)
{
	struct rndis_device *device;

	device = kzalloc(sizeof(struct rndis_device), GFP_KERNEL);
	if (!device)
		return NULL;

	spin_lock_init(&device->request_lock);

	INIT_LIST_HEAD(&device->req_list);

	device->state = RNDIS_DEV_UNINITIALIZED;

	return device;
}

static struct rndis_request *get_rndis_request(struct rndis_device *dev,
					     u32 msg_type,
					     u32 msg_len)
{
	struct rndis_request *request;
	struct rndis_message *rndis_msg;
	struct rndis_set_request *set;
	unsigned long flags;

	request = kzalloc(sizeof(struct rndis_request), GFP_KERNEL);
	if (!request)
		return NULL;

	request->waitevent = osd_waitevent_create();
	if (!request->waitevent) {
		kfree(request);
		return NULL;
	}

	rndis_msg = &request->request_msg;
	rndis_msg->NdisMessageType = msg_type;
	rndis_msg->MessageLength = msg_len;

	/*
	 * Set the request id. This field is always after the rndis header for
	 * request/response packet types so we just used the SetRequest as a
	 * template
	 */
	set = &rndis_msg->Message.SetRequest;
	set->RequestId = atomic_inc_return(&dev->new_req_id);

	/* Add to the request list */
	spin_lock_irqsave(&dev->request_lock, flags);
	list_add_tail(&request->list_ent, &dev->req_list);
	spin_unlock_irqrestore(&dev->request_lock, flags);

	return request;
}

static void put_rndis_request(struct rndis_device *dev,
			    struct rndis_request *req)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->request_lock, flags);
	list_del(&req->list_ent);
	spin_unlock_irqrestore(&dev->request_lock, flags);

	kfree(req->waitevent);
	kfree(req);
}

static void dump_rndis_message(struct rndis_message *rndis_msg)
{
	switch (rndis_msg->NdisMessageType) {
	case REMOTE_NDIS_PACKET_MSG:
		DPRINT_DBG(NETVSC, "REMOTE_NDIS_PACKET_MSG (len %u, "
			   "data offset %u data len %u, # oob %u, "
			   "oob offset %u, oob len %u, pkt offset %u, "
			   "pkt len %u",
			   rndis_msg->MessageLength,
			   rndis_msg->Message.Packet.DataOffset,
			   rndis_msg->Message.Packet.DataLength,
			   rndis_msg->Message.Packet.NumOOBDataElements,
			   rndis_msg->Message.Packet.OOBDataOffset,
			   rndis_msg->Message.Packet.OOBDataLength,
			   rndis_msg->Message.Packet.PerPacketInfoOffset,
			   rndis_msg->Message.Packet.PerPacketInfoLength);
		break;

	case REMOTE_NDIS_INITIALIZE_CMPLT:
		DPRINT_DBG(NETVSC, "REMOTE_NDIS_INITIALIZE_CMPLT "
			"(len %u, id 0x%x, status 0x%x, major %d, minor %d, "
			"device flags %d, max xfer size 0x%x, max pkts %u, "
			"pkt aligned %u)",
			rndis_msg->MessageLength,
			rndis_msg->Message.InitializeComplete.RequestId,
			rndis_msg->Message.InitializeComplete.Status,
			rndis_msg->Message.InitializeComplete.MajorVersion,
			rndis_msg->Message.InitializeComplete.MinorVersion,
			rndis_msg->Message.InitializeComplete.DeviceFlags,
			rndis_msg->Message.InitializeComplete.MaxTransferSize,
			rndis_msg->Message.InitializeComplete.
			   MaxPacketsPerMessage,
			rndis_msg->Message.InitializeComplete.
			   PacketAlignmentFactor);
		break;

	case REMOTE_NDIS_QUERY_CMPLT:
		DPRINT_DBG(NETVSC, "REMOTE_NDIS_QUERY_CMPLT "
			"(len %u, id 0x%x, status 0x%x, buf len %u, "
			"buf offset %u)",
			rndis_msg->MessageLength,
			rndis_msg->Message.QueryComplete.RequestId,
			rndis_msg->Message.QueryComplete.Status,
			rndis_msg->Message.QueryComplete.
			   InformationBufferLength,
			rndis_msg->Message.QueryComplete.
			   InformationBufferOffset);
		break;

	case REMOTE_NDIS_SET_CMPLT:
		DPRINT_DBG(NETVSC,
			"REMOTE_NDIS_SET_CMPLT (len %u, id 0x%x, status 0x%x)",
			rndis_msg->MessageLength,
			rndis_msg->Message.SetComplete.RequestId,
			rndis_msg->Message.SetComplete.Status);
		break;

	case REMOTE_NDIS_INDICATE_STATUS_MSG:
		DPRINT_DBG(NETVSC, "REMOTE_NDIS_INDICATE_STATUS_MSG "
			"(len %u, status 0x%x, buf len %u, buf offset %u)",
			rndis_msg->MessageLength,
			rndis_msg->Message.IndicateStatus.Status,
			rndis_msg->Message.IndicateStatus.StatusBufferLength,
			rndis_msg->Message.IndicateStatus.StatusBufferOffset);
		break;

	default:
		DPRINT_DBG(NETVSC, "0x%x (len %u)",
			rndis_msg->NdisMessageType,
			rndis_msg->MessageLength);
		break;
	}
}

static int rndis_filter_send_request(struct rndis_device *dev,
				  struct rndis_request *req)
{
	int ret;
	struct hv_netvsc_packet *packet;

	/* Setup the packet to send it */
	packet = &req->pkt;

	packet->is_data_pkt = false;
	packet->total_data_buflen = req->request_msg.MessageLength;
	packet->page_buf_cnt = 1;

	packet->page_buf[0].Pfn = virt_to_phys(&req->request_msg) >>
					PAGE_SHIFT;
	packet->page_buf[0].Length = req->request_msg.MessageLength;
	packet->page_buf[0].Offset =
		(unsigned long)&req->request_msg & (PAGE_SIZE - 1);

	packet->completion.send.send_completion_ctx = req;/* packet; */
	packet->completion.send.send_completion =
		rndis_filter_send_request_completion;
	packet->completion.send.send_completion_tid = (unsigned long)dev;

	ret = rndis_filter.inner_drv.send(dev->net_dev->Device, packet);
	return ret;
}

static void rndis_filter_receive_response(struct rndis_device *dev,
				       struct rndis_message *resp)
{
	struct rndis_request *request = NULL;
	bool found = false;
	unsigned long flags;

	spin_lock_irqsave(&dev->request_lock, flags);
	list_for_each_entry(request, &dev->req_list, list_ent) {
		/*
		 * All request/response message contains RequestId as the 1st
		 * field
		 */
		if (request->request_msg.Message.InitializeRequest.RequestId
		    == resp->Message.InitializeComplete.RequestId) {
			DPRINT_DBG(NETVSC, "found rndis request for "
				"this response (id 0x%x req type 0x%x res "
				"type 0x%x)",
				request->request_msg.Message.
				   InitializeRequest.RequestId,
				request->request_msg.NdisMessageType,
				resp->NdisMessageType);

			found = true;
			break;
		}
	}
	spin_unlock_irqrestore(&dev->request_lock, flags);

	if (found) {
		if (resp->MessageLength <= sizeof(struct rndis_message)) {
			memcpy(&request->response_msg, resp,
			       resp->MessageLength);
		} else {
			DPRINT_ERR(NETVSC, "rndis response buffer overflow "
				  "detected (size %u max %zu)",
				  resp->MessageLength,
				  sizeof(struct rndis_filter_packet));

			if (resp->NdisMessageType ==
			    REMOTE_NDIS_RESET_CMPLT) {
				/* does not have a request id field */
				request->response_msg.Message.ResetComplete.
					Status = STATUS_BUFFER_OVERFLOW;
			} else {
				request->response_msg.Message.
				InitializeComplete.Status =
					STATUS_BUFFER_OVERFLOW;
			}
		}

		osd_waitevent_set(request->waitevent);
	} else {
		DPRINT_ERR(NETVSC, "no rndis request found for this response "
			   "(id 0x%x res type 0x%x)",
			   resp->Message.InitializeComplete.RequestId,
			   resp->NdisMessageType);
	}
}

static void rndis_filter_receive_indicate_status(struct rndis_device *dev,
					     struct rndis_message *resp)
{
	struct rndis_indicate_status *indicate =
			&resp->Message.IndicateStatus;

	if (indicate->Status == RNDIS_STATUS_MEDIA_CONNECT) {
		rndis_filter.inner_drv.link_status_change(
			dev->net_dev->Device, 1);
	} else if (indicate->Status == RNDIS_STATUS_MEDIA_DISCONNECT) {
		rndis_filter.inner_drv.link_status_change(
			dev->net_dev->Device, 0);
	} else {
		/*
		 * TODO:
		 */
	}
}

static void rndis_filter_receive_data(struct rndis_device *dev,
				   struct rndis_message *msg,
				   struct hv_netvsc_packet *pkt)
{
	struct rndis_packet *rndis_pkt;
	u32 data_offset;

	/* empty ethernet frame ?? */
	/* ASSERT(Packet->PageBuffers[0].Length > */
	/* 	RNDIS_MESSAGE_SIZE(struct rndis_packet)); */

	rndis_pkt = &msg->Message.Packet;

	/*
	 * FIXME: Handle multiple rndis pkt msgs that maybe enclosed in this
	 * netvsc packet (ie TotalDataBufferLength != MessageLength)
	 */

	/* Remove the rndis header and pass it back up the stack */
	data_offset = RNDIS_HEADER_SIZE + rndis_pkt->DataOffset;

	pkt->total_data_buflen -= data_offset;
	pkt->page_buf[0].Offset += data_offset;
	pkt->page_buf[0].Length -= data_offset;

	pkt->is_data_pkt = true;

	rndis_filter.inner_drv.recv_cb(dev->net_dev->Device,
						   pkt);
}

static int rndis_filter_receive(struct hv_device *dev,
				struct hv_netvsc_packet	*pkt)
{
	struct netvsc_device *net_dev = dev->Extension;
	struct rndis_device *rndis_dev;
	struct rndis_message rndis_msg;
	struct rndis_message *rndis_hdr;

	if (!net_dev)
		return -EINVAL;

	/* Make sure the rndis device state is initialized */
	if (!net_dev->Extension) {
		DPRINT_ERR(NETVSC, "got rndis message but no rndis device..."
			  "dropping this message!");
		return -1;
	}

	rndis_dev = (struct rndis_device *)net_dev->Extension;
	if (rndis_dev->state == RNDIS_DEV_UNINITIALIZED) {
		DPRINT_ERR(NETVSC, "got rndis message but rndis device "
			   "uninitialized...dropping this message!");
		return -1;
	}

	rndis_hdr = (struct rndis_message *)kmap_atomic(
			pfn_to_page(pkt->page_buf[0].Pfn), KM_IRQ0);

	rndis_hdr = (void *)((unsigned long)rndis_hdr +
			pkt->page_buf[0].Offset);

	/* Make sure we got a valid rndis message */
	/*
	 * FIXME: There seems to be a bug in set completion msg where its
	 * MessageLength is 16 bytes but the ByteCount field in the xfer page
	 * range shows 52 bytes
	 * */
#if 0
	if (pkt->total_data_buflen != rndis_hdr->MessageLength) {
		kunmap_atomic(rndis_hdr - pkt->page_buf[0].Offset,
			      KM_IRQ0);

		DPRINT_ERR(NETVSC, "invalid rndis message? (expected %u "
			   "bytes got %u)...dropping this message!",
			   rndis_hdr->MessageLength,
			   pkt->total_data_buflen);
		return -1;
	}
#endif

	if ((rndis_hdr->NdisMessageType != REMOTE_NDIS_PACKET_MSG) &&
	    (rndis_hdr->MessageLength > sizeof(struct rndis_message))) {
		DPRINT_ERR(NETVSC, "incoming rndis message buffer overflow "
			   "detected (got %u, max %zu)...marking it an error!",
			   rndis_hdr->MessageLength,
			   sizeof(struct rndis_message));
	}

	memcpy(&rndis_msg, rndis_hdr,
		(rndis_hdr->MessageLength > sizeof(struct rndis_message)) ?
			sizeof(struct rndis_message) :
			rndis_hdr->MessageLength);

	kunmap_atomic(rndis_hdr - pkt->page_buf[0].Offset, KM_IRQ0);

	dump_rndis_message(&rndis_msg);

	switch (rndis_msg.NdisMessageType) {
	case REMOTE_NDIS_PACKET_MSG:
		/* data msg */
		rndis_filter_receive_data(rndis_dev, &rndis_msg, pkt);
		break;

	case REMOTE_NDIS_INITIALIZE_CMPLT:
	case REMOTE_NDIS_QUERY_CMPLT:
	case REMOTE_NDIS_SET_CMPLT:
	/* case REMOTE_NDIS_RESET_CMPLT: */
	/* case REMOTE_NDIS_KEEPALIVE_CMPLT: */
		/* completion msgs */
		rndis_filter_receive_response(rndis_dev, &rndis_msg);
		break;

	case REMOTE_NDIS_INDICATE_STATUS_MSG:
		/* notification msgs */
		rndis_filter_receive_indicate_status(rndis_dev, &rndis_msg);
		break;
	default:
		DPRINT_ERR(NETVSC, "unhandled rndis message (type %u len %u)",
			   rndis_msg.NdisMessageType,
			   rndis_msg.MessageLength);
		break;
	}

	return 0;
}

static int rndis_filter_query_device(struct rndis_device *dev, u32 oid,
				  void *result, u32 *result_size)
{
	struct rndis_request *request;
	u32 inresult_size = *result_size;
	struct rndis_query_request *query;
	struct rndis_query_complete *query_complete;
	int ret = 0;

	if (!result)
		return -EINVAL;

	*result_size = 0;
	request = get_rndis_request(dev, REMOTE_NDIS_QUERY_MSG,
			RNDIS_MESSAGE_SIZE(struct rndis_query_request));
	if (!request) {
		ret = -1;
		goto Cleanup;
	}

	/* Setup the rndis query */
	query = &request->request_msg.Message.QueryRequest;
	query->Oid = oid;
	query->InformationBufferOffset = sizeof(struct rndis_query_request);
	query->InformationBufferLength = 0;
	query->DeviceVcHandle = 0;

	ret = rndis_filter_send_request(dev, request);
	if (ret != 0)
		goto Cleanup;

	osd_waitevent_wait(request->waitevent);

	/* Copy the response back */
	query_complete = &request->response_msg.Message.QueryComplete;

	if (query_complete->InformationBufferLength > inresult_size) {
		ret = -1;
		goto Cleanup;
	}

	memcpy(result,
	       (void *)((unsigned long)query_complete +
			 query_complete->InformationBufferOffset),
	       query_complete->InformationBufferLength);

	*result_size = query_complete->InformationBufferLength;

Cleanup:
	if (request)
		put_rndis_request(dev, request);

	return ret;
}

static int rndis_filter_query_device_mac(struct rndis_device *dev)
{
	u32 size = ETH_ALEN;

	return rndis_filter_query_device(dev,
				      RNDIS_OID_802_3_PERMANENT_ADDRESS,
				      dev->hw_mac_adr, &size);
}

static int rndis_filter_query_device_link_status(struct rndis_device *dev)
{
	u32 size = sizeof(u32);

	return rndis_filter_query_device(dev,
				      RNDIS_OID_GEN_MEDIA_CONNECT_STATUS,
				      &dev->link_stat, &size);
}

static int rndis_filter_set_packet_filter(struct rndis_device *dev,
				      u32 new_filter)
{
	struct rndis_request *request;
	struct rndis_set_request *set;
	struct rndis_set_complete *set_complete;
	u32 status;
	int ret;

	/* ASSERT(RNDIS_MESSAGE_SIZE(struct rndis_set_request) + sizeof(u32) <= */
	/* 	sizeof(struct rndis_message)); */

	request = get_rndis_request(dev, REMOTE_NDIS_SET_MSG,
			RNDIS_MESSAGE_SIZE(struct rndis_set_request) +
			sizeof(u32));
	if (!request) {
		ret = -1;
		goto Cleanup;
	}

	/* Setup the rndis set */
	set = &request->request_msg.Message.SetRequest;
	set->Oid = RNDIS_OID_GEN_CURRENT_PACKET_FILTER;
	set->InformationBufferLength = sizeof(u32);
	set->InformationBufferOffset = sizeof(struct rndis_set_request);

	memcpy((void *)(unsigned long)set + sizeof(struct rndis_set_request),
	       &new_filter, sizeof(u32));

	ret = rndis_filter_send_request(dev, request);
	if (ret != 0)
		goto Cleanup;

	ret = osd_waitevent_waitex(request->waitevent, 2000/*2sec*/);
	if (!ret) {
		ret = -1;
		DPRINT_ERR(NETVSC, "timeout before we got a set response...");
		/*
		 * We cant deallocate the request since we may still receive a
		 * send completion for it.
		 */
		goto Exit;
	} else {
		if (ret > 0)
			ret = 0;
		set_complete = &request->response_msg.Message.SetComplete;
		status = set_complete->Status;
	}

Cleanup:
	if (request)
		put_rndis_request(dev, request);
Exit:
	return ret;
}

int rndis_filter_init(struct netvsc_driver *drv)
{
	DPRINT_DBG(NETVSC, "sizeof(struct rndis_filter_packet) == %zd",
		   sizeof(struct rndis_filter_packet));

	drv->req_ext_size = sizeof(struct rndis_filter_packet);

	/* Driver->Context = rndisDriver; */

	memset(&rndis_filter, 0, sizeof(struct rndis_filter_driver_object));

	/*rndisDriver->Driver = Driver;

	ASSERT(Driver->OnLinkStatusChanged);
	rndisDriver->OnLinkStatusChanged = Driver->OnLinkStatusChanged;*/

	/* Save the original dispatch handlers before we override it */
	rndis_filter.inner_drv.base.OnDeviceAdd = drv->base.OnDeviceAdd;
	rndis_filter.inner_drv.base.OnDeviceRemove =
					drv->base.OnDeviceRemove;
	rndis_filter.inner_drv.base.OnCleanup = drv->base.OnCleanup;

	/* ASSERT(Driver->OnSend); */
	/* ASSERT(Driver->OnReceiveCallback); */
	rndis_filter.inner_drv.send = drv->send;
	rndis_filter.inner_drv.recv_cb = drv->recv_cb;
	rndis_filter.inner_drv.link_status_change =
					drv->link_status_change;

	/* Override */
	drv->base.OnDeviceAdd = rndis_filte_device_add;
	drv->base.OnDeviceRemove = rndis_filter_device_remove;
	drv->base.OnCleanup = rndis_filter_cleanup;
	drv->send = rndis_filter_send;
	/* Driver->QueryLinkStatus = RndisFilterQueryDeviceLinkStatus; */
	drv->recv_cb = rndis_filter_receive;

	return 0;
}

static int rndis_filter_init_device(struct rndis_device *dev)
{
	struct rndis_request *request;
	struct rndis_initialize_request *init;
	struct rndis_initialize_complete *init_complete;
	u32 status;
	int ret;

	request = get_rndis_request(dev, REMOTE_NDIS_INITIALIZE_MSG,
			RNDIS_MESSAGE_SIZE(struct rndis_initialize_request));
	if (!request) {
		ret = -1;
		goto Cleanup;
	}

	/* Setup the rndis set */
	init = &request->request_msg.Message.InitializeRequest;
	init->MajorVersion = RNDIS_MAJOR_VERSION;
	init->MinorVersion = RNDIS_MINOR_VERSION;
	/* FIXME: Use 1536 - rounded ethernet frame size */
	init->MaxTransferSize = 2048;

	dev->state = RNDIS_DEV_INITIALIZING;

	ret = rndis_filter_send_request(dev, request);
	if (ret != 0) {
		dev->state = RNDIS_DEV_UNINITIALIZED;
		goto Cleanup;
	}

	osd_waitevent_wait(request->waitevent);

	init_complete = &request->response_msg.Message.InitializeComplete;
	status = init_complete->Status;
	if (status == RNDIS_STATUS_SUCCESS) {
		dev->state = RNDIS_DEV_INITIALIZED;
		ret = 0;
	} else {
		dev->state = RNDIS_DEV_UNINITIALIZED;
		ret = -1;
	}

Cleanup:
	if (request)
		put_rndis_request(dev, request);

	return ret;
}

static void rndis_filter_halt_device(struct rndis_device *dev)
{
	struct rndis_request *request;
	struct rndis_halt_request *halt;

	/* Attempt to do a rndis device halt */
	request = get_rndis_request(dev, REMOTE_NDIS_HALT_MSG,
				RNDIS_MESSAGE_SIZE(struct rndis_halt_request));
	if (!request)
		goto Cleanup;

	/* Setup the rndis set */
	halt = &request->request_msg.Message.HaltRequest;
	halt->RequestId = atomic_inc_return(&dev->new_req_id);

	/* Ignore return since this msg is optional. */
	rndis_filter_send_request(dev, request);

	dev->state = RNDIS_DEV_UNINITIALIZED;

Cleanup:
	if (request)
		put_rndis_request(dev, request);
	return;
}

static int rndis_filter_open_device(struct rndis_device *dev)
{
	int ret;

	if (dev->state != RNDIS_DEV_INITIALIZED)
		return 0;

	ret = rndis_filter_set_packet_filter(dev,
					 NDIS_PACKET_TYPE_BROADCAST |
					 NDIS_PACKET_TYPE_ALL_MULTICAST |
					 NDIS_PACKET_TYPE_DIRECTED);
	if (ret == 0)
		dev->state = RNDIS_DEV_DATAINITIALIZED;

	return ret;
}

static int rndis_filter_close_device(struct rndis_device *dev)
{
	int ret;

	if (dev->state != RNDIS_DEV_DATAINITIALIZED)
		return 0;

	ret = rndis_filter_set_packet_filter(dev, 0);
	if (ret == 0)
		dev->state = RNDIS_DEV_INITIALIZED;

	return ret;
}

static int rndis_filte_device_add(struct hv_device *dev,
				  void *additional_info)
{
	int ret;
	struct netvsc_device *netDevice;
	struct rndis_device *rndisDevice;
	struct netvsc_device_info *deviceInfo = additional_info;

	rndisDevice = get_rndis_device();
	if (!rndisDevice)
		return -1;

	DPRINT_DBG(NETVSC, "rndis device object allocated - %p", rndisDevice);

	/*
	 * Let the inner driver handle this first to create the netvsc channel
	 * NOTE! Once the channel is created, we may get a receive callback
	 * (RndisFilterOnReceive()) before this call is completed
	 */
	ret = rndis_filter.inner_drv.base.OnDeviceAdd(dev, additional_info);
	if (ret != 0) {
		kfree(rndisDevice);
		return ret;
	}


	/* Initialize the rndis device */
	netDevice = dev->Extension;
	/* ASSERT(netDevice); */
	/* ASSERT(netDevice->Device); */

	netDevice->Extension = rndisDevice;
	rndisDevice->net_dev = netDevice;

	/* Send the rndis initialization message */
	ret = rndis_filter_init_device(rndisDevice);
	if (ret != 0) {
		/*
		 * TODO: If rndis init failed, we will need to shut down the
		 * channel
		 */
	}

	/* Get the mac address */
	ret = rndis_filter_query_device_mac(rndisDevice);
	if (ret != 0) {
		/*
		 * TODO: shutdown rndis device and the channel
		 */
	}

	DPRINT_INFO(NETVSC, "Device 0x%p mac addr %pM",
		    rndisDevice, rndisDevice->hw_mac_adr);

	memcpy(deviceInfo->mac_adr, rndisDevice->hw_mac_adr, ETH_ALEN);

	rndis_filter_query_device_link_status(rndisDevice);

	deviceInfo->link_state = rndisDevice->link_stat;
	DPRINT_INFO(NETVSC, "Device 0x%p link state %s", rndisDevice,
		    ((deviceInfo->link_state) ? ("down") : ("up")));

	return ret;
}

static int rndis_filter_device_remove(struct hv_device *dev)
{
	struct netvsc_device *net_dev = dev->Extension;
	struct rndis_device *rndis_dev = net_dev->Extension;

	/* Halt and release the rndis device */
	rndis_filter_halt_device(rndis_dev);

	kfree(rndis_dev);
	net_dev->Extension = NULL;

	/* Pass control to inner driver to remove the device */
	rndis_filter.inner_drv.base.OnDeviceRemove(dev);

	return 0;
}

static void rndis_filter_cleanup(struct hv_driver *drv)
{
}

int rndis_filter_open(struct hv_device *dev)
{
	struct netvsc_device *netDevice = dev->Extension;

	if (!netDevice)
		return -EINVAL;

	return rndis_filter_open_device(netDevice->Extension);
}

int rndis_filter_close(struct hv_device *dev)
{
	struct netvsc_device *netDevice = dev->Extension;

	if (!netDevice)
		return -EINVAL;

	return rndis_filter_close_device(netDevice->Extension);
}

static int rndis_filter_send(struct hv_device *dev,
			     struct hv_netvsc_packet *pkt)
{
	int ret;
	struct rndis_filter_packet *filterPacket;
	struct rndis_message *rndisMessage;
	struct rndis_packet *rndisPacket;
	u32 rndisMessageSize;

	/* Add the rndis header */
	filterPacket = (struct rndis_filter_packet *)pkt->extension;
	/* ASSERT(filterPacket); */

	memset(filterPacket, 0, sizeof(struct rndis_filter_packet));

	rndisMessage = &filterPacket->msg;
	rndisMessageSize = RNDIS_MESSAGE_SIZE(struct rndis_packet);

	rndisMessage->NdisMessageType = REMOTE_NDIS_PACKET_MSG;
	rndisMessage->MessageLength = pkt->total_data_buflen +
				      rndisMessageSize;

	rndisPacket = &rndisMessage->Message.Packet;
	rndisPacket->DataOffset = sizeof(struct rndis_packet);
	rndisPacket->DataLength = pkt->total_data_buflen;

	pkt->is_data_pkt = true;
	pkt->page_buf[0].Pfn = virt_to_phys(rndisMessage) >> PAGE_SHIFT;
	pkt->page_buf[0].Offset =
			(unsigned long)rndisMessage & (PAGE_SIZE-1);
	pkt->page_buf[0].Length = rndisMessageSize;

	/* Save the packet send completion and context */
	filterPacket->completion = pkt->completion.send.send_completion;
	filterPacket->completion_ctx =
				pkt->completion.send.send_completion_ctx;

	/* Use ours */
	pkt->completion.send.send_completion = rndis_filter_send_completion;
	pkt->completion.send.send_completion_ctx = filterPacket;

	ret = rndis_filter.inner_drv.send(dev, pkt);
	if (ret != 0) {
		/*
		 * Reset the completion to originals to allow retries from
		 * above
		 */
		pkt->completion.send.send_completion =
				filterPacket->completion;
		pkt->completion.send.send_completion_ctx =
				filterPacket->completion_ctx;
	}

	return ret;
}

static void rndis_filter_send_completion(void *ctx)
{
	struct rndis_filter_packet *filterPacket = ctx;

	/* Pass it back to the original handler */
	filterPacket->completion(filterPacket->completion_ctx);
}


static void rndis_filter_send_request_completion(void *ctx)
{
	/* Noop */
}
