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
#include <linux/io.h>
#include "osd.h"
#include "logging.h"
#include "NetVscApi.h"
#include "RndisFilter.h"

/* Data types */
struct rndis_filter_driver_object {
	/* The original driver */
	struct netvsc_driver InnerDriver;
};

enum rndis_device_state {
	RNDIS_DEV_UNINITIALIZED = 0,
	RNDIS_DEV_INITIALIZING,
	RNDIS_DEV_INITIALIZED,
	RNDIS_DEV_DATAINITIALIZED,
};

struct rndis_device {
	struct netvsc_device *NetDevice;

	enum rndis_device_state State;
	u32 LinkStatus;
	atomic_t NewRequestId;

	spinlock_t request_lock;
	struct list_head RequestList;

	unsigned char HwMacAddr[HW_MACADDR_LEN];
};

struct rndis_request {
	struct list_head ListEntry;
	struct osd_waitevent *WaitEvent;

	/*
	 * FIXME: We assumed a fixed size response here. If we do ever need to
	 * handle a bigger response, we can either define a max response
	 * message or add a response buffer variable above this field
	 */
	struct rndis_message ResponseMessage;

	/* Simplify allocation by having a netvsc packet inline */
	struct hv_netvsc_packet	Packet;
	struct hv_page_buffer Buffer;
	/* FIXME: We assumed a fixed size request here. */
	struct rndis_message RequestMessage;
};


struct rndis_filter_packet {
	void *CompletionContext;
	void (*OnCompletion)(void *context);
	struct rndis_message Message;
};


static int RndisFilterOnDeviceAdd(struct hv_device *Device,
				  void *AdditionalInfo);

static int RndisFilterOnDeviceRemove(struct hv_device *Device);

static void RndisFilterOnCleanup(struct hv_driver *Driver);

static int RndisFilterOnClose(struct hv_device *Device);

static int RndisFilterOnSend(struct hv_device *Device,
			     struct hv_netvsc_packet *Packet);

static void RndisFilterOnSendCompletion(void *Context);

static void RndisFilterOnSendRequestCompletion(void *Context);


/* The one and only */
static struct rndis_filter_driver_object gRndisFilter;

static struct rndis_device *GetRndisDevice(void)
{
	struct rndis_device *device;

	device = kzalloc(sizeof(struct rndis_device), GFP_KERNEL);
	if (!device)
		return NULL;

	spin_lock_init(&device->request_lock);

	INIT_LIST_HEAD(&device->RequestList);

	device->State = RNDIS_DEV_UNINITIALIZED;

	return device;
}

static struct rndis_request *GetRndisRequest(struct rndis_device *Device,
					     u32 MessageType,
					     u32 MessageLength)
{
	struct rndis_request *request;
	struct rndis_message *rndisMessage;
	struct rndis_set_request *set;
	unsigned long flags;

	request = kzalloc(sizeof(struct rndis_request), GFP_KERNEL);
	if (!request)
		return NULL;

	request->WaitEvent = osd_WaitEventCreate();
	if (!request->WaitEvent) {
		kfree(request);
		return NULL;
	}

	rndisMessage = &request->RequestMessage;
	rndisMessage->NdisMessageType = MessageType;
	rndisMessage->MessageLength = MessageLength;

	/*
	 * Set the request id. This field is always after the rndis header for
	 * request/response packet types so we just used the SetRequest as a
	 * template
	 */
	set = &rndisMessage->Message.SetRequest;
	set->RequestId = atomic_inc_return(&Device->NewRequestId);

	/* Add to the request list */
	spin_lock_irqsave(&Device->request_lock, flags);
	list_add_tail(&request->ListEntry, &Device->RequestList);
	spin_unlock_irqrestore(&Device->request_lock, flags);

	return request;
}

static void PutRndisRequest(struct rndis_device *Device,
			    struct rndis_request *Request)
{
	unsigned long flags;

	spin_lock_irqsave(&Device->request_lock, flags);
	list_del(&Request->ListEntry);
	spin_unlock_irqrestore(&Device->request_lock, flags);

	kfree(Request->WaitEvent);
	kfree(Request);
}

static void DumpRndisMessage(struct rndis_message *RndisMessage)
{
	switch (RndisMessage->NdisMessageType) {
	case REMOTE_NDIS_PACKET_MSG:
		DPRINT_DBG(NETVSC, "REMOTE_NDIS_PACKET_MSG (len %u, "
			   "data offset %u data len %u, # oob %u, "
			   "oob offset %u, oob len %u, pkt offset %u, "
			   "pkt len %u",
			   RndisMessage->MessageLength,
			   RndisMessage->Message.Packet.DataOffset,
			   RndisMessage->Message.Packet.DataLength,
			   RndisMessage->Message.Packet.NumOOBDataElements,
			   RndisMessage->Message.Packet.OOBDataOffset,
			   RndisMessage->Message.Packet.OOBDataLength,
			   RndisMessage->Message.Packet.PerPacketInfoOffset,
			   RndisMessage->Message.Packet.PerPacketInfoLength);
		break;

	case REMOTE_NDIS_INITIALIZE_CMPLT:
		DPRINT_DBG(NETVSC, "REMOTE_NDIS_INITIALIZE_CMPLT "
			"(len %u, id 0x%x, status 0x%x, major %d, minor %d, "
			"device flags %d, max xfer size 0x%x, max pkts %u, "
			"pkt aligned %u)",
			RndisMessage->MessageLength,
			RndisMessage->Message.InitializeComplete.RequestId,
			RndisMessage->Message.InitializeComplete.Status,
			RndisMessage->Message.InitializeComplete.MajorVersion,
			RndisMessage->Message.InitializeComplete.MinorVersion,
			RndisMessage->Message.InitializeComplete.DeviceFlags,
			RndisMessage->Message.InitializeComplete.MaxTransferSize,
			RndisMessage->Message.InitializeComplete.MaxPacketsPerMessage,
			RndisMessage->Message.InitializeComplete.PacketAlignmentFactor);
		break;

	case REMOTE_NDIS_QUERY_CMPLT:
		DPRINT_DBG(NETVSC, "REMOTE_NDIS_QUERY_CMPLT "
			"(len %u, id 0x%x, status 0x%x, buf len %u, "
			"buf offset %u)",
			RndisMessage->MessageLength,
			RndisMessage->Message.QueryComplete.RequestId,
			RndisMessage->Message.QueryComplete.Status,
			RndisMessage->Message.QueryComplete.InformationBufferLength,
			RndisMessage->Message.QueryComplete.InformationBufferOffset);
		break;

	case REMOTE_NDIS_SET_CMPLT:
		DPRINT_DBG(NETVSC,
			"REMOTE_NDIS_SET_CMPLT (len %u, id 0x%x, status 0x%x)",
			RndisMessage->MessageLength,
			RndisMessage->Message.SetComplete.RequestId,
			RndisMessage->Message.SetComplete.Status);
		break;

	case REMOTE_NDIS_INDICATE_STATUS_MSG:
		DPRINT_DBG(NETVSC, "REMOTE_NDIS_INDICATE_STATUS_MSG "
			"(len %u, status 0x%x, buf len %u, buf offset %u)",
			RndisMessage->MessageLength,
			RndisMessage->Message.IndicateStatus.Status,
			RndisMessage->Message.IndicateStatus.StatusBufferLength,
			RndisMessage->Message.IndicateStatus.StatusBufferOffset);
		break;

	default:
		DPRINT_DBG(NETVSC, "0x%x (len %u)",
			RndisMessage->NdisMessageType,
			RndisMessage->MessageLength);
		break;
	}
}

static int RndisFilterSendRequest(struct rndis_device *Device,
				  struct rndis_request *Request)
{
	int ret;
	struct hv_netvsc_packet *packet;

	DPRINT_ENTER(NETVSC);

	/* Setup the packet to send it */
	packet = &Request->Packet;

	packet->IsDataPacket = false;
	packet->TotalDataBufferLength = Request->RequestMessage.MessageLength;
	packet->PageBufferCount = 1;

	packet->PageBuffers[0].Pfn = virt_to_phys(&Request->RequestMessage) >>
					PAGE_SHIFT;
	packet->PageBuffers[0].Length = Request->RequestMessage.MessageLength;
	packet->PageBuffers[0].Offset =
		(unsigned long)&Request->RequestMessage & (PAGE_SIZE - 1);

	packet->Completion.Send.SendCompletionContext = Request;/* packet; */
	packet->Completion.Send.OnSendCompletion =
		RndisFilterOnSendRequestCompletion;
	packet->Completion.Send.SendCompletionTid = (unsigned long)Device;

	ret = gRndisFilter.InnerDriver.OnSend(Device->NetDevice->Device, packet);
	DPRINT_EXIT(NETVSC);
	return ret;
}

static void RndisFilterReceiveResponse(struct rndis_device *Device,
				       struct rndis_message *Response)
{
	struct rndis_request *request = NULL;
	bool found = false;
	unsigned long flags;

	DPRINT_ENTER(NETVSC);

	spin_lock_irqsave(&Device->request_lock, flags);
	list_for_each_entry(request, &Device->RequestList, ListEntry) {
		/*
		 * All request/response message contains RequestId as the 1st
		 * field
		 */
		if (request->RequestMessage.Message.InitializeRequest.RequestId
		    == Response->Message.InitializeComplete.RequestId) {
			DPRINT_DBG(NETVSC, "found rndis request for "
				"this response (id 0x%x req type 0x%x res "
				"type 0x%x)",
				request->RequestMessage.Message.InitializeRequest.RequestId,
				request->RequestMessage.NdisMessageType,
				Response->NdisMessageType);

			found = true;
			break;
		}
	}
	spin_unlock_irqrestore(&Device->request_lock, flags);

	if (found) {
		if (Response->MessageLength <= sizeof(struct rndis_message)) {
			memcpy(&request->ResponseMessage, Response,
			       Response->MessageLength);
		} else {
			DPRINT_ERR(NETVSC, "rndis response buffer overflow "
				  "detected (size %u max %zu)",
				  Response->MessageLength,
				  sizeof(struct rndis_filter_packet));

			if (Response->NdisMessageType ==
			    REMOTE_NDIS_RESET_CMPLT) {
				/* does not have a request id field */
				request->ResponseMessage.Message.ResetComplete.Status = STATUS_BUFFER_OVERFLOW;
			} else {
				request->ResponseMessage.Message.InitializeComplete.Status = STATUS_BUFFER_OVERFLOW;
			}
		}

		osd_WaitEventSet(request->WaitEvent);
	} else {
		DPRINT_ERR(NETVSC, "no rndis request found for this response "
			   "(id 0x%x res type 0x%x)",
			   Response->Message.InitializeComplete.RequestId,
			   Response->NdisMessageType);
	}

	DPRINT_EXIT(NETVSC);
}

static void RndisFilterReceiveIndicateStatus(struct rndis_device *Device,
					     struct rndis_message *Response)
{
	struct rndis_indicate_status *indicate =
			&Response->Message.IndicateStatus;

	if (indicate->Status == RNDIS_STATUS_MEDIA_CONNECT) {
		gRndisFilter.InnerDriver.OnLinkStatusChanged(Device->NetDevice->Device, 1);
	} else if (indicate->Status == RNDIS_STATUS_MEDIA_DISCONNECT) {
		gRndisFilter.InnerDriver.OnLinkStatusChanged(Device->NetDevice->Device, 0);
	} else {
		/*
		 * TODO:
		 */
	}
}

static void RndisFilterReceiveData(struct rndis_device *Device,
				   struct rndis_message *Message,
				   struct hv_netvsc_packet *Packet)
{
	struct rndis_packet *rndisPacket;
	u32 dataOffset;

	DPRINT_ENTER(NETVSC);

	/* empty ethernet frame ?? */
	ASSERT(Packet->PageBuffers[0].Length >
		RNDIS_MESSAGE_SIZE(struct rndis_packet));

	rndisPacket = &Message->Message.Packet;

	/*
	 * FIXME: Handle multiple rndis pkt msgs that maybe enclosed in this
	 * netvsc packet (ie TotalDataBufferLength != MessageLength)
	 */

	/* Remove the rndis header and pass it back up the stack */
	dataOffset = RNDIS_HEADER_SIZE + rndisPacket->DataOffset;

	Packet->TotalDataBufferLength -= dataOffset;
	Packet->PageBuffers[0].Offset += dataOffset;
	Packet->PageBuffers[0].Length -= dataOffset;

	Packet->IsDataPacket = true;

	gRndisFilter.InnerDriver.OnReceiveCallback(Device->NetDevice->Device,
						   Packet);

	DPRINT_EXIT(NETVSC);
}

static int RndisFilterOnReceive(struct hv_device *Device,
				struct hv_netvsc_packet	*Packet)
{
	struct netvsc_device *netDevice = Device->Extension;
	struct rndis_device *rndisDevice;
	struct rndis_message rndisMessage;
	struct rndis_message *rndisHeader;

	DPRINT_ENTER(NETVSC);

	ASSERT(netDevice);
	/* Make sure the rndis device state is initialized */
	if (!netDevice->Extension) {
		DPRINT_ERR(NETVSC, "got rndis message but no rndis device..."
			  "dropping this message!");
		DPRINT_EXIT(NETVSC);
		return -1;
	}

	rndisDevice = (struct rndis_device *)netDevice->Extension;
	if (rndisDevice->State == RNDIS_DEV_UNINITIALIZED) {
		DPRINT_ERR(NETVSC, "got rndis message but rndis device "
			   "uninitialized...dropping this message!");
		DPRINT_EXIT(NETVSC);
		return -1;
	}

	rndisHeader = (struct rndis_message *)kmap_atomic(
			pfn_to_page(Packet->PageBuffers[0].Pfn), KM_IRQ0);

	rndisHeader = (void *)((unsigned long)rndisHeader +
			Packet->PageBuffers[0].Offset);

	/* Make sure we got a valid rndis message */
	/*
	 * FIXME: There seems to be a bug in set completion msg where its
	 * MessageLength is 16 bytes but the ByteCount field in the xfer page
	 * range shows 52 bytes
	 * */
#if 0
	if (Packet->TotalDataBufferLength != rndisHeader->MessageLength) {
		kunmap_atomic(rndisHeader - Packet->PageBuffers[0].Offset,
			      KM_IRQ0);

		DPRINT_ERR(NETVSC, "invalid rndis message? (expected %u "
			   "bytes got %u)...dropping this message!",
			   rndisHeader->MessageLength,
			   Packet->TotalDataBufferLength);
		DPRINT_EXIT(NETVSC);
		return -1;
	}
#endif

	if ((rndisHeader->NdisMessageType != REMOTE_NDIS_PACKET_MSG) &&
	    (rndisHeader->MessageLength > sizeof(struct rndis_message))) {
		DPRINT_ERR(NETVSC, "incoming rndis message buffer overflow "
			   "detected (got %u, max %zu)...marking it an error!",
			   rndisHeader->MessageLength,
			   sizeof(struct rndis_message));
	}

	memcpy(&rndisMessage, rndisHeader,
		(rndisHeader->MessageLength > sizeof(struct rndis_message)) ?
			sizeof(struct rndis_message) :
			rndisHeader->MessageLength);

	kunmap_atomic(rndisHeader - Packet->PageBuffers[0].Offset, KM_IRQ0);

	DumpRndisMessage(&rndisMessage);

	switch (rndisMessage.NdisMessageType) {
	case REMOTE_NDIS_PACKET_MSG:
		/* data msg */
		RndisFilterReceiveData(rndisDevice, &rndisMessage, Packet);
		break;

	case REMOTE_NDIS_INITIALIZE_CMPLT:
	case REMOTE_NDIS_QUERY_CMPLT:
	case REMOTE_NDIS_SET_CMPLT:
	/* case REMOTE_NDIS_RESET_CMPLT: */
	/* case REMOTE_NDIS_KEEPALIVE_CMPLT: */
		/* completion msgs */
		RndisFilterReceiveResponse(rndisDevice, &rndisMessage);
		break;

	case REMOTE_NDIS_INDICATE_STATUS_MSG:
		/* notification msgs */
		RndisFilterReceiveIndicateStatus(rndisDevice, &rndisMessage);
		break;
	default:
		DPRINT_ERR(NETVSC, "unhandled rndis message (type %u len %u)",
			   rndisMessage.NdisMessageType,
			   rndisMessage.MessageLength);
		break;
	}

	DPRINT_EXIT(NETVSC);
	return 0;
}

static int RndisFilterQueryDevice(struct rndis_device *Device, u32 Oid,
				  void *Result, u32 *ResultSize)
{
	struct rndis_request *request;
	u32 inresultSize = *ResultSize;
	struct rndis_query_request *query;
	struct rndis_query_complete *queryComplete;
	int ret = 0;

	DPRINT_ENTER(NETVSC);

	ASSERT(Result);

	*ResultSize = 0;
	request = GetRndisRequest(Device, REMOTE_NDIS_QUERY_MSG,
			RNDIS_MESSAGE_SIZE(struct rndis_query_request));
	if (!request) {
		ret = -1;
		goto Cleanup;
	}

	/* Setup the rndis query */
	query = &request->RequestMessage.Message.QueryRequest;
	query->Oid = Oid;
	query->InformationBufferOffset = sizeof(struct rndis_query_request);
	query->InformationBufferLength = 0;
	query->DeviceVcHandle = 0;

	ret = RndisFilterSendRequest(Device, request);
	if (ret != 0)
		goto Cleanup;

	osd_WaitEventWait(request->WaitEvent);

	/* Copy the response back */
	queryComplete = &request->ResponseMessage.Message.QueryComplete;

	if (queryComplete->InformationBufferLength > inresultSize) {
		ret = -1;
		goto Cleanup;
	}

	memcpy(Result,
	       (void *)((unsigned long)queryComplete +
			 queryComplete->InformationBufferOffset),
	       queryComplete->InformationBufferLength);

	*ResultSize = queryComplete->InformationBufferLength;

Cleanup:
	if (request)
		PutRndisRequest(Device, request);
	DPRINT_EXIT(NETVSC);

	return ret;
}

static int RndisFilterQueryDeviceMac(struct rndis_device *Device)
{
	u32 size = HW_MACADDR_LEN;

	return RndisFilterQueryDevice(Device,
				      RNDIS_OID_802_3_PERMANENT_ADDRESS,
				      Device->HwMacAddr, &size);
}

static int RndisFilterQueryDeviceLinkStatus(struct rndis_device *Device)
{
	u32 size = sizeof(u32);

	return RndisFilterQueryDevice(Device,
				      RNDIS_OID_GEN_MEDIA_CONNECT_STATUS,
				      &Device->LinkStatus, &size);
}

static int RndisFilterSetPacketFilter(struct rndis_device *Device,
				      u32 NewFilter)
{
	struct rndis_request *request;
	struct rndis_set_request *set;
	struct rndis_set_complete *setComplete;
	u32 status;
	int ret;

	DPRINT_ENTER(NETVSC);

	ASSERT(RNDIS_MESSAGE_SIZE(struct rndis_set_request) + sizeof(u32) <=
		sizeof(struct rndis_message));

	request = GetRndisRequest(Device, REMOTE_NDIS_SET_MSG,
			RNDIS_MESSAGE_SIZE(struct rndis_set_request) +
			sizeof(u32));
	if (!request) {
		ret = -1;
		goto Cleanup;
	}

	/* Setup the rndis set */
	set = &request->RequestMessage.Message.SetRequest;
	set->Oid = RNDIS_OID_GEN_CURRENT_PACKET_FILTER;
	set->InformationBufferLength = sizeof(u32);
	set->InformationBufferOffset = sizeof(struct rndis_set_request);

	memcpy((void *)(unsigned long)set + sizeof(struct rndis_set_request),
	       &NewFilter, sizeof(u32));

	ret = RndisFilterSendRequest(Device, request);
	if (ret != 0)
		goto Cleanup;

	ret = osd_WaitEventWaitEx(request->WaitEvent, 2000/*2sec*/);
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
		setComplete = &request->ResponseMessage.Message.SetComplete;
		status = setComplete->Status;
	}

Cleanup:
	if (request)
		PutRndisRequest(Device, request);
Exit:
	DPRINT_EXIT(NETVSC);

	return ret;
}

int RndisFilterInit(struct netvsc_driver *Driver)
{
	DPRINT_ENTER(NETVSC);

	DPRINT_DBG(NETVSC, "sizeof(struct rndis_filter_packet) == %zd",
		   sizeof(struct rndis_filter_packet));

	Driver->RequestExtSize = sizeof(struct rndis_filter_packet);
	Driver->AdditionalRequestPageBufferCount = 1; /* For rndis header */

	/* Driver->Context = rndisDriver; */

	memset(&gRndisFilter, 0, sizeof(struct rndis_filter_driver_object));

	/*rndisDriver->Driver = Driver;

	ASSERT(Driver->OnLinkStatusChanged);
	rndisDriver->OnLinkStatusChanged = Driver->OnLinkStatusChanged;*/

	/* Save the original dispatch handlers before we override it */
	gRndisFilter.InnerDriver.Base.OnDeviceAdd = Driver->Base.OnDeviceAdd;
	gRndisFilter.InnerDriver.Base.OnDeviceRemove =
					Driver->Base.OnDeviceRemove;
	gRndisFilter.InnerDriver.Base.OnCleanup = Driver->Base.OnCleanup;

	ASSERT(Driver->OnSend);
	ASSERT(Driver->OnReceiveCallback);
	gRndisFilter.InnerDriver.OnSend = Driver->OnSend;
	gRndisFilter.InnerDriver.OnReceiveCallback = Driver->OnReceiveCallback;
	gRndisFilter.InnerDriver.OnLinkStatusChanged =
					Driver->OnLinkStatusChanged;

	/* Override */
	Driver->Base.OnDeviceAdd = RndisFilterOnDeviceAdd;
	Driver->Base.OnDeviceRemove = RndisFilterOnDeviceRemove;
	Driver->Base.OnCleanup = RndisFilterOnCleanup;
	Driver->OnSend = RndisFilterOnSend;
	Driver->OnClose = RndisFilterOnClose;
	/* Driver->QueryLinkStatus = RndisFilterQueryDeviceLinkStatus; */
	Driver->OnReceiveCallback = RndisFilterOnReceive;

	DPRINT_EXIT(NETVSC);

	return 0;
}

static int RndisFilterInitDevice(struct rndis_device *Device)
{
	struct rndis_request *request;
	struct rndis_initialize_request *init;
	struct rndis_initialize_complete *initComplete;
	u32 status;
	int ret;

	DPRINT_ENTER(NETVSC);

	request = GetRndisRequest(Device, REMOTE_NDIS_INITIALIZE_MSG,
			RNDIS_MESSAGE_SIZE(struct rndis_initialize_request));
	if (!request) {
		ret = -1;
		goto Cleanup;
	}

	/* Setup the rndis set */
	init = &request->RequestMessage.Message.InitializeRequest;
	init->MajorVersion = RNDIS_MAJOR_VERSION;
	init->MinorVersion = RNDIS_MINOR_VERSION;
	/* FIXME: Use 1536 - rounded ethernet frame size */
	init->MaxTransferSize = 2048;

	Device->State = RNDIS_DEV_INITIALIZING;

	ret = RndisFilterSendRequest(Device, request);
	if (ret != 0) {
		Device->State = RNDIS_DEV_UNINITIALIZED;
		goto Cleanup;
	}

	osd_WaitEventWait(request->WaitEvent);

	initComplete = &request->ResponseMessage.Message.InitializeComplete;
	status = initComplete->Status;
	if (status == RNDIS_STATUS_SUCCESS) {
		Device->State = RNDIS_DEV_INITIALIZED;
		ret = 0;
	} else {
		Device->State = RNDIS_DEV_UNINITIALIZED;
		ret = -1;
	}

Cleanup:
	if (request)
		PutRndisRequest(Device, request);
	DPRINT_EXIT(NETVSC);

	return ret;
}

static void RndisFilterHaltDevice(struct rndis_device *Device)
{
	struct rndis_request *request;
	struct rndis_halt_request *halt;

	DPRINT_ENTER(NETVSC);

	/* Attempt to do a rndis device halt */
	request = GetRndisRequest(Device, REMOTE_NDIS_HALT_MSG,
				RNDIS_MESSAGE_SIZE(struct rndis_halt_request));
	if (!request)
		goto Cleanup;

	/* Setup the rndis set */
	halt = &request->RequestMessage.Message.HaltRequest;
	halt->RequestId = atomic_inc_return(&Device->NewRequestId);

	/* Ignore return since this msg is optional. */
	RndisFilterSendRequest(Device, request);

	Device->State = RNDIS_DEV_UNINITIALIZED;

Cleanup:
	if (request)
		PutRndisRequest(Device, request);
	DPRINT_EXIT(NETVSC);
	return;
}

static int RndisFilterOpenDevice(struct rndis_device *Device)
{
	int ret;

	DPRINT_ENTER(NETVSC);

	if (Device->State != RNDIS_DEV_INITIALIZED)
		return 0;

	ret = RndisFilterSetPacketFilter(Device,
					 NDIS_PACKET_TYPE_BROADCAST |
					 NDIS_PACKET_TYPE_DIRECTED);
	if (ret == 0)
		Device->State = RNDIS_DEV_DATAINITIALIZED;

	DPRINT_EXIT(NETVSC);
	return ret;
}

static int RndisFilterCloseDevice(struct rndis_device *Device)
{
	int ret;

	DPRINT_ENTER(NETVSC);

	if (Device->State != RNDIS_DEV_DATAINITIALIZED)
		return 0;

	ret = RndisFilterSetPacketFilter(Device, 0);
	if (ret == 0)
		Device->State = RNDIS_DEV_INITIALIZED;

	DPRINT_EXIT(NETVSC);

	return ret;
}

static int RndisFilterOnDeviceAdd(struct hv_device *Device,
				  void *AdditionalInfo)
{
	int ret;
	struct netvsc_device *netDevice;
	struct rndis_device *rndisDevice;
	struct netvsc_device_info *deviceInfo = AdditionalInfo;

	DPRINT_ENTER(NETVSC);

	rndisDevice = GetRndisDevice();
	if (!rndisDevice) {
		DPRINT_EXIT(NETVSC);
		return -1;
	}

	DPRINT_DBG(NETVSC, "rndis device object allocated - %p", rndisDevice);

	/*
	 * Let the inner driver handle this first to create the netvsc channel
	 * NOTE! Once the channel is created, we may get a receive callback
	 * (RndisFilterOnReceive()) before this call is completed
	 */
	ret = gRndisFilter.InnerDriver.Base.OnDeviceAdd(Device, AdditionalInfo);
	if (ret != 0) {
		kfree(rndisDevice);
		DPRINT_EXIT(NETVSC);
		return ret;
	}


	/* Initialize the rndis device */
	netDevice = Device->Extension;
	ASSERT(netDevice);
	ASSERT(netDevice->Device);

	netDevice->Extension = rndisDevice;
	rndisDevice->NetDevice = netDevice;

	/* Send the rndis initialization message */
	ret = RndisFilterInitDevice(rndisDevice);
	if (ret != 0) {
		/*
		 * TODO: If rndis init failed, we will need to shut down the
		 * channel
		 */
	}

	/* Get the mac address */
	ret = RndisFilterQueryDeviceMac(rndisDevice);
	if (ret != 0) {
		/*
		 * TODO: shutdown rndis device and the channel
		 */
	}

	DPRINT_INFO(NETVSC, "Device 0x%p mac addr %02x%02x%02x%02x%02x%02x",
		    rndisDevice,
		    rndisDevice->HwMacAddr[0],
		    rndisDevice->HwMacAddr[1],
		    rndisDevice->HwMacAddr[2],
		    rndisDevice->HwMacAddr[3],
		    rndisDevice->HwMacAddr[4],
		    rndisDevice->HwMacAddr[5]);

	memcpy(deviceInfo->MacAddr, rndisDevice->HwMacAddr, HW_MACADDR_LEN);

	RndisFilterQueryDeviceLinkStatus(rndisDevice);

	deviceInfo->LinkState = rndisDevice->LinkStatus;
	DPRINT_INFO(NETVSC, "Device 0x%p link state %s", rndisDevice,
		    ((deviceInfo->LinkState) ? ("down") : ("up")));

	DPRINT_EXIT(NETVSC);

	return ret;
}

static int RndisFilterOnDeviceRemove(struct hv_device *Device)
{
	struct netvsc_device *netDevice = Device->Extension;
	struct rndis_device *rndisDevice = netDevice->Extension;

	DPRINT_ENTER(NETVSC);

	/* Halt and release the rndis device */
	RndisFilterHaltDevice(rndisDevice);

	kfree(rndisDevice);
	netDevice->Extension = NULL;

	/* Pass control to inner driver to remove the device */
	gRndisFilter.InnerDriver.Base.OnDeviceRemove(Device);

	DPRINT_EXIT(NETVSC);

	return 0;
}

static void RndisFilterOnCleanup(struct hv_driver *Driver)
{
	DPRINT_ENTER(NETVSC);

	DPRINT_EXIT(NETVSC);
}

int RndisFilterOnOpen(struct hv_device *Device)
{
	int ret;
	struct netvsc_device *netDevice = Device->Extension;

	DPRINT_ENTER(NETVSC);

	ASSERT(netDevice);
	ret = RndisFilterOpenDevice(netDevice->Extension);

	DPRINT_EXIT(NETVSC);

	return ret;
}

static int RndisFilterOnClose(struct hv_device *Device)
{
	int ret;
	struct netvsc_device *netDevice = Device->Extension;

	DPRINT_ENTER(NETVSC);

	ASSERT(netDevice);
	ret = RndisFilterCloseDevice(netDevice->Extension);

	DPRINT_EXIT(NETVSC);

	return ret;
}

static int RndisFilterOnSend(struct hv_device *Device,
			     struct hv_netvsc_packet *Packet)
{
	int ret;
	struct rndis_filter_packet *filterPacket;
	struct rndis_message *rndisMessage;
	struct rndis_packet *rndisPacket;
	u32 rndisMessageSize;

	DPRINT_ENTER(NETVSC);

	/* Add the rndis header */
	filterPacket = (struct rndis_filter_packet *)Packet->Extension;
	ASSERT(filterPacket);

	memset(filterPacket, 0, sizeof(struct rndis_filter_packet));

	rndisMessage = &filterPacket->Message;
	rndisMessageSize = RNDIS_MESSAGE_SIZE(struct rndis_packet);

	rndisMessage->NdisMessageType = REMOTE_NDIS_PACKET_MSG;
	rndisMessage->MessageLength = Packet->TotalDataBufferLength +
				      rndisMessageSize;

	rndisPacket = &rndisMessage->Message.Packet;
	rndisPacket->DataOffset = sizeof(struct rndis_packet);
	rndisPacket->DataLength = Packet->TotalDataBufferLength;

	Packet->IsDataPacket = true;
	Packet->PageBuffers[0].Pfn = virt_to_phys(rndisMessage) >> PAGE_SHIFT;
	Packet->PageBuffers[0].Offset =
			(unsigned long)rndisMessage & (PAGE_SIZE-1);
	Packet->PageBuffers[0].Length = rndisMessageSize;

	/* Save the packet send completion and context */
	filterPacket->OnCompletion = Packet->Completion.Send.OnSendCompletion;
	filterPacket->CompletionContext =
				Packet->Completion.Send.SendCompletionContext;

	/* Use ours */
	Packet->Completion.Send.OnSendCompletion = RndisFilterOnSendCompletion;
	Packet->Completion.Send.SendCompletionContext = filterPacket;

	ret = gRndisFilter.InnerDriver.OnSend(Device, Packet);
	if (ret != 0) {
		/*
		 * Reset the completion to originals to allow retries from
		 * above
		 */
		Packet->Completion.Send.OnSendCompletion =
				filterPacket->OnCompletion;
		Packet->Completion.Send.SendCompletionContext =
				filterPacket->CompletionContext;
	}

	DPRINT_EXIT(NETVSC);

	return ret;
}

static void RndisFilterOnSendCompletion(void *Context)
{
	struct rndis_filter_packet *filterPacket = Context;

	DPRINT_ENTER(NETVSC);

	/* Pass it back to the original handler */
	filterPacket->OnCompletion(filterPacket->CompletionContext);

	DPRINT_EXIT(NETVSC);
}


static void RndisFilterOnSendRequestCompletion(void *Context)
{
	DPRINT_ENTER(NETVSC);

	/* Noop */
	DPRINT_EXIT(NETVSC);
}
