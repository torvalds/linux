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
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include "osd.h"
#include "logging.h"
#include "storvsc_api.h"
#include "vmbus_packet_format.h"
#include "vstorage.h"
#include "channel.h"


struct storvsc_request_extension {
	/* LIST_ENTRY ListEntry; */

	struct hv_storvsc_request *Request;
	struct hv_device *Device;

	/* Synchronize the request/response if needed */
	struct osd_waitevent *WaitEvent;

	struct vstor_packet VStorPacket;
};

/* A storvsc device is a device object that contains a vmbus channel */
struct storvsc_device {
	struct hv_device *Device;

	/* 0 indicates the device is being destroyed */
	atomic_t RefCount;

	atomic_t NumOutstandingRequests;

	/*
	 * Each unique Port/Path/Target represents 1 channel ie scsi
	 * controller. In reality, the pathid, targetid is always 0
	 * and the port is set by us
	 */
	unsigned int PortNumber;
	unsigned char PathId;
	unsigned char TargetId;

	/* LIST_ENTRY OutstandingRequestList; */
	/* HANDLE OutstandingRequestLock; */

	/* Used for vsc/vsp channel reset process */
	struct storvsc_request_extension InitRequest;
	struct storvsc_request_extension ResetRequest;
};


static const char *gDriverName = "storvsc";

/* {ba6163d9-04a1-4d29-b605-72e2ffb1dc7f} */
static const struct hv_guid gStorVscDeviceType = {
	.data = {
		0xd9, 0x63, 0x61, 0xba, 0xa1, 0x04, 0x29, 0x4d,
		0xb6, 0x05, 0x72, 0xe2, 0xff, 0xb1, 0xdc, 0x7f
	}
};


static inline struct storvsc_device *AllocStorDevice(struct hv_device *Device)
{
	struct storvsc_device *storDevice;

	storDevice = kzalloc(sizeof(struct storvsc_device), GFP_KERNEL);
	if (!storDevice)
		return NULL;

	/* Set to 2 to allow both inbound and outbound traffics */
	/* (ie GetStorDevice() and MustGetStorDevice()) to proceed. */
	atomic_cmpxchg(&storDevice->RefCount, 0, 2);

	storDevice->Device = Device;
	Device->Extension = storDevice;

	return storDevice;
}

static inline void FreeStorDevice(struct storvsc_device *Device)
{
	/* ASSERT(atomic_read(&Device->RefCount) == 0); */
	kfree(Device);
}

/* Get the stordevice object iff exists and its refcount > 1 */
static inline struct storvsc_device *GetStorDevice(struct hv_device *Device)
{
	struct storvsc_device *storDevice;

	storDevice = (struct storvsc_device *)Device->Extension;
	if (storDevice && atomic_read(&storDevice->RefCount) > 1)
		atomic_inc(&storDevice->RefCount);
	else
		storDevice = NULL;

	return storDevice;
}

/* Get the stordevice object iff exists and its refcount > 0 */
static inline struct storvsc_device *MustGetStorDevice(struct hv_device *Device)
{
	struct storvsc_device *storDevice;

	storDevice = (struct storvsc_device *)Device->Extension;
	if (storDevice && atomic_read(&storDevice->RefCount))
		atomic_inc(&storDevice->RefCount);
	else
		storDevice = NULL;

	return storDevice;
}

static inline void PutStorDevice(struct hv_device *Device)
{
	struct storvsc_device *storDevice;

	storDevice = (struct storvsc_device *)Device->Extension;
	/* ASSERT(storDevice); */

	atomic_dec(&storDevice->RefCount);
	/* ASSERT(atomic_read(&storDevice->RefCount)); */
}

/* Drop ref count to 1 to effectively disable GetStorDevice() */
static inline struct storvsc_device *ReleaseStorDevice(struct hv_device *Device)
{
	struct storvsc_device *storDevice;

	storDevice = (struct storvsc_device *)Device->Extension;
	/* ASSERT(storDevice); */

	/* Busy wait until the ref drop to 2, then set it to 1 */
	while (atomic_cmpxchg(&storDevice->RefCount, 2, 1) != 2)
		udelay(100);

	return storDevice;
}

/* Drop ref count to 0. No one can use StorDevice object. */
static inline struct storvsc_device *FinalReleaseStorDevice(
			struct hv_device *Device)
{
	struct storvsc_device *storDevice;

	storDevice = (struct storvsc_device *)Device->Extension;
	/* ASSERT(storDevice); */

	/* Busy wait until the ref drop to 1, then set it to 0 */
	while (atomic_cmpxchg(&storDevice->RefCount, 1, 0) != 1)
		udelay(100);

	Device->Extension = NULL;
	return storDevice;
}

static int StorVscChannelInit(struct hv_device *Device)
{
	struct storvsc_device *storDevice;
	struct storvsc_request_extension *request;
	struct vstor_packet *vstorPacket;
	int ret;

	storDevice = GetStorDevice(Device);
	if (!storDevice) {
		DPRINT_ERR(STORVSC, "unable to get stor device..."
			   "device being destroyed?");
		return -1;
	}

	request = &storDevice->InitRequest;
	vstorPacket = &request->VStorPacket;

	/*
	 * Now, initiate the vsc/vsp initialization protocol on the open
	 * channel
	 */
	memset(request, 0, sizeof(struct storvsc_request_extension));
	request->WaitEvent = osd_waitevent_create();
	if (!request->WaitEvent) {
		ret = -ENOMEM;
		goto nomem;
	}

	vstorPacket->operation = VSTOR_OPERATION_BEGIN_INITIALIZATION;
	vstorPacket->flags = REQUEST_COMPLETION_FLAG;

	/*SpinlockAcquire(gDriverExt.packetListLock);
	INSERT_TAIL_LIST(&gDriverExt.packetList, &packet->listEntry.entry);
	SpinlockRelease(gDriverExt.packetListLock);*/

	DPRINT_INFO(STORVSC, "BEGIN_INITIALIZATION_OPERATION...");

	ret = vmbus_sendpacket(Device->channel, vstorPacket,
			       sizeof(struct vstor_packet),
			       (unsigned long)request,
			       VmbusPacketTypeDataInBand,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	if (ret != 0) {
		DPRINT_ERR(STORVSC,
			   "unable to send BEGIN_INITIALIZATION_OPERATION");
		goto Cleanup;
	}

	osd_waitevent_wait(request->WaitEvent);

	if (vstorPacket->operation != VSTOR_OPERATION_COMPLETE_IO ||
	    vstorPacket->status != 0) {
		DPRINT_ERR(STORVSC, "BEGIN_INITIALIZATION_OPERATION failed "
			   "(op %d status 0x%x)",
			   vstorPacket->operation, vstorPacket->status);
		goto Cleanup;
	}

	DPRINT_INFO(STORVSC, "QUERY_PROTOCOL_VERSION_OPERATION...");

	/* reuse the packet for version range supported */
	memset(vstorPacket, 0, sizeof(struct vstor_packet));
	vstorPacket->operation = VSTOR_OPERATION_QUERY_PROTOCOL_VERSION;
	vstorPacket->flags = REQUEST_COMPLETION_FLAG;

	vstorPacket->version.major_minor = VMSTOR_PROTOCOL_VERSION_CURRENT;
	FILL_VMSTOR_REVISION(vstorPacket->version.revision);

	ret = vmbus_sendpacket(Device->channel, vstorPacket,
			       sizeof(struct vstor_packet),
			       (unsigned long)request,
			       VmbusPacketTypeDataInBand,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	if (ret != 0) {
		DPRINT_ERR(STORVSC,
			   "unable to send BEGIN_INITIALIZATION_OPERATION");
		goto Cleanup;
	}

	osd_waitevent_wait(request->WaitEvent);

	/* TODO: Check returned version */
	if (vstorPacket->operation != VSTOR_OPERATION_COMPLETE_IO ||
	    vstorPacket->status != 0) {
		DPRINT_ERR(STORVSC, "QUERY_PROTOCOL_VERSION_OPERATION failed "
			   "(op %d status 0x%x)",
			   vstorPacket->operation, vstorPacket->status);
		goto Cleanup;
	}

	/* Query channel properties */
	DPRINT_INFO(STORVSC, "QUERY_PROPERTIES_OPERATION...");

	memset(vstorPacket, 0, sizeof(struct vstor_packet));
	vstorPacket->operation = VSTOR_OPERATION_QUERY_PROPERTIES;
	vstorPacket->flags = REQUEST_COMPLETION_FLAG;
	vstorPacket->storage_channel_properties.port_number =
					storDevice->PortNumber;

	ret = vmbus_sendpacket(Device->channel, vstorPacket,
			       sizeof(struct vstor_packet),
			       (unsigned long)request,
			       VmbusPacketTypeDataInBand,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);

	if (ret != 0) {
		DPRINT_ERR(STORVSC,
			   "unable to send QUERY_PROPERTIES_OPERATION");
		goto Cleanup;
	}

	osd_waitevent_wait(request->WaitEvent);

	/* TODO: Check returned version */
	if (vstorPacket->operation != VSTOR_OPERATION_COMPLETE_IO ||
	    vstorPacket->status != 0) {
		DPRINT_ERR(STORVSC, "QUERY_PROPERTIES_OPERATION failed "
			   "(op %d status 0x%x)",
			   vstorPacket->operation, vstorPacket->status);
		goto Cleanup;
	}

	storDevice->PathId = vstorPacket->storage_channel_properties.path_id;
	storDevice->TargetId
		= vstorPacket->storage_channel_properties.target_id;

	DPRINT_DBG(STORVSC, "channel flag 0x%x, max xfer len 0x%x",
		   vstorPacket->storage_channel_properties.flags,
		   vstorPacket->storage_channel_properties.max_transfer_bytes);

	DPRINT_INFO(STORVSC, "END_INITIALIZATION_OPERATION...");

	memset(vstorPacket, 0, sizeof(struct vstor_packet));
	vstorPacket->operation = VSTOR_OPERATION_END_INITIALIZATION;
	vstorPacket->flags = REQUEST_COMPLETION_FLAG;

	ret = vmbus_sendpacket(Device->channel, vstorPacket,
			       sizeof(struct vstor_packet),
			       (unsigned long)request,
			       VmbusPacketTypeDataInBand,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);

	if (ret != 0) {
		DPRINT_ERR(STORVSC,
			   "unable to send END_INITIALIZATION_OPERATION");
		goto Cleanup;
	}

	osd_waitevent_wait(request->WaitEvent);

	if (vstorPacket->operation != VSTOR_OPERATION_COMPLETE_IO ||
	    vstorPacket->status != 0) {
		DPRINT_ERR(STORVSC, "END_INITIALIZATION_OPERATION failed "
			   "(op %d status 0x%x)",
			   vstorPacket->operation, vstorPacket->status);
		goto Cleanup;
	}

	DPRINT_INFO(STORVSC, "**** storage channel up and running!! ****");

Cleanup:
	kfree(request->WaitEvent);
	request->WaitEvent = NULL;
nomem:
	PutStorDevice(Device);
	return ret;
}

static void StorVscOnIOCompletion(struct hv_device *Device,
				  struct vstor_packet *VStorPacket,
				  struct storvsc_request_extension *RequestExt)
{
	struct hv_storvsc_request *request;
	struct storvsc_device *storDevice;

	storDevice = MustGetStorDevice(Device);
	if (!storDevice) {
		DPRINT_ERR(STORVSC, "unable to get stor device..."
			   "device being destroyed?");
		return;
	}

	DPRINT_DBG(STORVSC, "IO_COMPLETE_OPERATION - request extension %p "
		   "completed bytes xfer %u", RequestExt,
		   VStorPacket->vm_srb.data_transfer_length);

	/* ASSERT(RequestExt != NULL); */
	/* ASSERT(RequestExt->Request != NULL); */

	request = RequestExt->Request;

	/* ASSERT(request->OnIOCompletion != NULL); */

	/* Copy over the status...etc */
	request->status = VStorPacket->vm_srb.scsi_status;

	if (request->status != 0 || VStorPacket->vm_srb.srb_status != 1) {
		DPRINT_WARN(STORVSC,
			    "cmd 0x%x scsi status 0x%x srb status 0x%x\n",
			    request->cdb[0], VStorPacket->vm_srb.scsi_status,
			    VStorPacket->vm_srb.srb_status);
	}

	if ((request->status & 0xFF) == 0x02) {
		/* CHECK_CONDITION */
		if (VStorPacket->vm_srb.srb_status & 0x80) {
			/* autosense data available */
			DPRINT_WARN(STORVSC, "storvsc pkt %p autosense data "
				    "valid - len %d\n", RequestExt,
				    VStorPacket->vm_srb.sense_info_length);

			/* ASSERT(VStorPacket->vm_srb.sense_info_length <= */
			/* 	request->SenseBufferSize); */
			memcpy(request->sense_buffer,
			       VStorPacket->vm_srb.sense_data,
			       VStorPacket->vm_srb.sense_info_length);

			request->sense_buffer_size =
					VStorPacket->vm_srb.sense_info_length;
		}
	}

	/* TODO: */
	request->bytes_xfer = VStorPacket->vm_srb.data_transfer_length;

	request->on_io_completion(request);

	atomic_dec(&storDevice->NumOutstandingRequests);

	PutStorDevice(Device);
}

static void StorVscOnReceive(struct hv_device *Device,
			     struct vstor_packet *VStorPacket,
			     struct storvsc_request_extension *RequestExt)
{
	switch (VStorPacket->operation) {
	case VSTOR_OPERATION_COMPLETE_IO:
		DPRINT_DBG(STORVSC, "IO_COMPLETE_OPERATION");
		StorVscOnIOCompletion(Device, VStorPacket, RequestExt);
		break;
	case VSTOR_OPERATION_REMOVE_DEVICE:
		DPRINT_INFO(STORVSC, "REMOVE_DEVICE_OPERATION");
		/* TODO: */
		break;

	default:
		DPRINT_INFO(STORVSC, "Unknown operation received - %d",
			    VStorPacket->operation);
		break;
	}
}

static void StorVscOnChannelCallback(void *context)
{
	struct hv_device *device = (struct hv_device *)context;
	struct storvsc_device *storDevice;
	u32 bytesRecvd;
	u64 requestId;
	unsigned char packet[ALIGN_UP(sizeof(struct vstor_packet), 8)];
	struct storvsc_request_extension *request;
	int ret;

	/* ASSERT(device); */

	storDevice = MustGetStorDevice(device);
	if (!storDevice) {
		DPRINT_ERR(STORVSC, "unable to get stor device..."
			   "device being destroyed?");
		return;
	}

	do {
		ret = vmbus_recvpacket(device->channel, packet,
				       ALIGN_UP(sizeof(struct vstor_packet), 8),
				       &bytesRecvd, &requestId);
		if (ret == 0 && bytesRecvd > 0) {
			DPRINT_DBG(STORVSC, "receive %d bytes - tid %llx",
				   bytesRecvd, requestId);

			/* ASSERT(bytesRecvd == sizeof(struct vstor_packet)); */

			request = (struct storvsc_request_extension *)
					(unsigned long)requestId;
			/* ASSERT(request);c */

			/* if (vstorPacket.Flags & SYNTHETIC_FLAG) */
			if ((request == &storDevice->InitRequest) ||
			    (request == &storDevice->ResetRequest)) {
				/* DPRINT_INFO(STORVSC,
				 *             "reset completion - operation "
				 *             "%u status %u",
				 *             vstorPacket.Operation,
				 *             vstorPacket.Status); */

				memcpy(&request->VStorPacket, packet,
				       sizeof(struct vstor_packet));

				osd_waitevent_set(request->WaitEvent);
			} else {
				StorVscOnReceive(device,
						(struct vstor_packet *)packet,
						request);
			}
		} else {
			/* DPRINT_DBG(STORVSC, "nothing else to read..."); */
			break;
		}
	} while (1);

	PutStorDevice(device);
	return;
}

static int StorVscConnectToVsp(struct hv_device *Device)
{
	struct vmstorage_channel_properties props;
	struct storvsc_driver_object *storDriver;
	int ret;

	storDriver = (struct storvsc_driver_object *)Device->Driver;
	memset(&props, 0, sizeof(struct vmstorage_channel_properties));

	/* Open the channel */
	ret = vmbus_open(Device->channel,
			 storDriver->ring_buffer_size,
			 storDriver->ring_buffer_size,
			 (void *)&props,
			 sizeof(struct vmstorage_channel_properties),
			 StorVscOnChannelCallback, Device);

	DPRINT_DBG(STORVSC, "storage props: path id %d, tgt id %d, max xfer %d",
		   props.path_id, props.target_id, props.max_transfer_bytes);

	if (ret != 0) {
		DPRINT_ERR(STORVSC, "unable to open channel: %d", ret);
		return -1;
	}

	ret = StorVscChannelInit(Device);

	return ret;
}

/*
 * StorVscOnDeviceAdd - Callback when the device belonging to this driver is added
 */
static int StorVscOnDeviceAdd(struct hv_device *Device, void *AdditionalInfo)
{
	struct storvsc_device *storDevice;
	/* struct vmstorage_channel_properties *props; */
	struct storvsc_device_info *deviceInfo;
	int ret = 0;

	deviceInfo = (struct storvsc_device_info *)AdditionalInfo;
	storDevice = AllocStorDevice(Device);
	if (!storDevice) {
		ret = -1;
		goto Cleanup;
	}

	/* Save the channel properties to our storvsc channel */
	/* props = (struct vmstorage_channel_properties *)
	 *		channel->offerMsg.Offer.u.Standard.UserDefined; */

	/* FIXME: */
	/*
	 * If we support more than 1 scsi channel, we need to set the
	 * port number here to the scsi channel but how do we get the
	 * scsi channel prior to the bus scan
	 */

	/* storChannel->PortNumber = 0;
	storChannel->PathId = props->PathId;
	storChannel->TargetId = props->TargetId; */

	storDevice->PortNumber = deviceInfo->port_number;
	/* Send it back up */
	ret = StorVscConnectToVsp(Device);

	/* deviceInfo->PortNumber = storDevice->PortNumber; */
	deviceInfo->path_id = storDevice->PathId;
	deviceInfo->target_id = storDevice->TargetId;

	DPRINT_DBG(STORVSC, "assigned port %u, path %u target %u\n",
		   storDevice->PortNumber, storDevice->PathId,
		   storDevice->TargetId);

Cleanup:
	return ret;
}

/*
 * StorVscOnDeviceRemove - Callback when the our device is being removed
 */
static int StorVscOnDeviceRemove(struct hv_device *Device)
{
	struct storvsc_device *storDevice;

	DPRINT_INFO(STORVSC, "disabling storage device (%p)...",
		    Device->Extension);

	storDevice = ReleaseStorDevice(Device);

	/*
	 * At this point, all outbound traffic should be disable. We
	 * only allow inbound traffic (responses) to proceed so that
	 * outstanding requests can be completed.
	 */
	while (atomic_read(&storDevice->NumOutstandingRequests)) {
		DPRINT_INFO(STORVSC, "waiting for %d requests to complete...",
			    atomic_read(&storDevice->NumOutstandingRequests));
		udelay(100);
	}

	DPRINT_INFO(STORVSC, "removing storage device (%p)...",
		    Device->Extension);

	storDevice = FinalReleaseStorDevice(Device);

	DPRINT_INFO(STORVSC, "storage device (%p) safe to remove", storDevice);

	/* Close the channel */
	vmbus_close(Device->channel);

	FreeStorDevice(storDevice);
	return 0;
}

int StorVscOnHostReset(struct hv_device *Device)
{
	struct storvsc_device *storDevice;
	struct storvsc_request_extension *request;
	struct vstor_packet *vstorPacket;
	int ret;

	DPRINT_INFO(STORVSC, "resetting host adapter...");

	storDevice = GetStorDevice(Device);
	if (!storDevice) {
		DPRINT_ERR(STORVSC, "unable to get stor device..."
			   "device being destroyed?");
		return -1;
	}

	request = &storDevice->ResetRequest;
	vstorPacket = &request->VStorPacket;

	request->WaitEvent = osd_waitevent_create();
	if (!request->WaitEvent) {
		ret = -ENOMEM;
		goto Cleanup;
	}

	vstorPacket->operation = VSTOR_OPERATION_RESET_BUS;
	vstorPacket->flags = REQUEST_COMPLETION_FLAG;
	vstorPacket->vm_srb.path_id = storDevice->PathId;

	ret = vmbus_sendpacket(Device->channel, vstorPacket,
			       sizeof(struct vstor_packet),
			       (unsigned long)&storDevice->ResetRequest,
			       VmbusPacketTypeDataInBand,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	if (ret != 0) {
		DPRINT_ERR(STORVSC, "Unable to send reset packet %p ret %d",
			   vstorPacket, ret);
		goto Cleanup;
	}

	/* FIXME: Add a timeout */
	osd_waitevent_wait(request->WaitEvent);

	kfree(request->WaitEvent);
	DPRINT_INFO(STORVSC, "host adapter reset completed");

	/*
	 * At this point, all outstanding requests in the adapter
	 * should have been flushed out and return to us
	 */

Cleanup:
	PutStorDevice(Device);
	return ret;
}

/*
 * StorVscOnIORequest - Callback to initiate an I/O request
 */
static int StorVscOnIORequest(struct hv_device *Device,
			      struct hv_storvsc_request *Request)
{
	struct storvsc_device *storDevice;
	struct storvsc_request_extension *requestExtension;
	struct vstor_packet *vstorPacket;
	int ret = 0;

	requestExtension =
		(struct storvsc_request_extension *)Request->extension;
	vstorPacket = &requestExtension->VStorPacket;
	storDevice = GetStorDevice(Device);

	DPRINT_DBG(STORVSC, "enter - Device %p, DeviceExt %p, Request %p, "
		   "Extension %p", Device, storDevice, Request,
		   requestExtension);

	DPRINT_DBG(STORVSC, "req %p len %d bus %d, target %d, lun %d cdblen %d",
		   Request, Request->data_buffer.Length, Request->bus,
		   Request->target_id, Request->lun_id, Request->cdb_len);

	if (!storDevice) {
		DPRINT_ERR(STORVSC, "unable to get stor device..."
			   "device being destroyed?");
		return -2;
	}

	/* print_hex_dump_bytes("", DUMP_PREFIX_NONE, Request->Cdb,
	 *			Request->CdbLen); */

	requestExtension->Request = Request;
	requestExtension->Device  = Device;

	memset(vstorPacket, 0 , sizeof(struct vstor_packet));

	vstorPacket->flags |= REQUEST_COMPLETION_FLAG;

	vstorPacket->vm_srb.length = sizeof(struct vmscsi_request);

	vstorPacket->vm_srb.port_number = Request->host;
	vstorPacket->vm_srb.path_id = Request->bus;
	vstorPacket->vm_srb.target_id = Request->target_id;
	vstorPacket->vm_srb.lun = Request->lun_id;

	vstorPacket->vm_srb.sense_info_length = SENSE_BUFFER_SIZE;

	/* Copy over the scsi command descriptor block */
	vstorPacket->vm_srb.cdb_length = Request->cdb_len;
	memcpy(&vstorPacket->vm_srb.cdb, Request->cdb, Request->cdb_len);

	vstorPacket->vm_srb.data_in = Request->type;
	vstorPacket->vm_srb.data_transfer_length = Request->data_buffer.Length;

	vstorPacket->operation = VSTOR_OPERATION_EXECUTE_SRB;

	DPRINT_DBG(STORVSC, "srb - len %d port %d, path %d, target %d, "
		   "lun %d senselen %d cdblen %d",
		   vstorPacket->vm_srb.length,
		   vstorPacket->vm_srb.port_number,
		   vstorPacket->vm_srb.path_id,
		   vstorPacket->vm_srb.target_id,
		   vstorPacket->vm_srb.lun,
		   vstorPacket->vm_srb.sense_info_length,
		   vstorPacket->vm_srb.cdb_length);

	if (requestExtension->Request->data_buffer.Length) {
		ret = vmbus_sendpacket_multipagebuffer(Device->channel,
				&requestExtension->Request->data_buffer,
				vstorPacket,
				sizeof(struct vstor_packet),
				(unsigned long)requestExtension);
	} else {
		ret = vmbus_sendpacket(Device->channel, vstorPacket,
				       sizeof(struct vstor_packet),
				       (unsigned long)requestExtension,
				       VmbusPacketTypeDataInBand,
				       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	}

	if (ret != 0) {
		DPRINT_DBG(STORVSC, "Unable to send packet %p ret %d",
			   vstorPacket, ret);
	}

	atomic_inc(&storDevice->NumOutstandingRequests);

	PutStorDevice(Device);
	return ret;
}

/*
 * StorVscOnCleanup - Perform any cleanup when the driver is removed
 */
static void StorVscOnCleanup(struct hv_driver *Driver)
{
}

/*
 * StorVscInitialize - Main entry point
 */
int StorVscInitialize(struct hv_driver *Driver)
{
	struct storvsc_driver_object *storDriver;

	storDriver = (struct storvsc_driver_object *)Driver;

	DPRINT_DBG(STORVSC, "sizeof(STORVSC_REQUEST)=%zd "
		   "sizeof(struct storvsc_request_extension)=%zd "
		   "sizeof(struct vstor_packet)=%zd, "
		   "sizeof(struct vmscsi_request)=%zd",
		   sizeof(struct hv_storvsc_request),
		   sizeof(struct storvsc_request_extension),
		   sizeof(struct vstor_packet),
		   sizeof(struct vmscsi_request));

	/* Make sure we are at least 2 pages since 1 page is used for control */
	/* ASSERT(storDriver->RingBufferSize >= (PAGE_SIZE << 1)); */

	Driver->name = gDriverName;
	memcpy(&Driver->deviceType, &gStorVscDeviceType,
	       sizeof(struct hv_guid));

	storDriver->request_ext_size = sizeof(struct storvsc_request_extension);

	/*
	 * Divide the ring buffer data size (which is 1 page less
	 * than the ring buffer size since that page is reserved for
	 * the ring buffer indices) by the max request size (which is
	 * vmbus_channel_packet_multipage_buffer + struct vstor_packet + u64)
	 */
	storDriver->max_outstanding_req_per_channel =
		((storDriver->ring_buffer_size - PAGE_SIZE) /
		  ALIGN_UP(MAX_MULTIPAGE_BUFFER_PACKET +
			   sizeof(struct vstor_packet) + sizeof(u64),
			   sizeof(u64)));

	DPRINT_INFO(STORVSC, "max io %u, currently %u\n",
		    storDriver->max_outstanding_req_per_channel,
		    STORVSC_MAX_IO_REQUESTS);

	/* Setup the dispatch table */
	storDriver->base.OnDeviceAdd	= StorVscOnDeviceAdd;
	storDriver->base.OnDeviceRemove	= StorVscOnDeviceRemove;
	storDriver->base.OnCleanup	= StorVscOnCleanup;

	storDriver->on_io_request	= StorVscOnIORequest;

	return 0;
}
