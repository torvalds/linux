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
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include "hv_api.h"
#include "logging.h"
#include "storvsc_api.h"
#include "vmbus_packet_format.h"
#include "vstorage.h"
#include "channel.h"


struct storvsc_request_extension {
	/* LIST_ENTRY ListEntry; */

	struct hv_storvsc_request *request;
	struct hv_device *device;

	/* Synchronize the request/response if needed */
	int wait_condition;
	wait_queue_head_t wait_event;

	struct vstor_packet vstor_packet;
};

/* A storvsc device is a device object that contains a vmbus channel */
struct storvsc_device {
	struct hv_device *device;

	/* 0 indicates the device is being destroyed */
	atomic_t ref_count;

	atomic_t num_outstanding_req;

	/*
	 * Each unique Port/Path/Target represents 1 channel ie scsi
	 * controller. In reality, the pathid, targetid is always 0
	 * and the port is set by us
	 */
	unsigned int port_number;
	unsigned char path_id;
	unsigned char target_id;

	/* LIST_ENTRY OutstandingRequestList; */
	/* HANDLE OutstandingRequestLock; */

	/* Used for vsc/vsp channel reset process */
	struct storvsc_request_extension init_request;
	struct storvsc_request_extension reset_request;
};


static const char *g_driver_name = "storvsc";

/* {ba6163d9-04a1-4d29-b605-72e2ffb1dc7f} */
static const struct hv_guid gStorVscDeviceType = {
	.data = {
		0xd9, 0x63, 0x61, 0xba, 0xa1, 0x04, 0x29, 0x4d,
		0xb6, 0x05, 0x72, 0xe2, 0xff, 0xb1, 0xdc, 0x7f
	}
};


static inline struct storvsc_device *alloc_stor_device(struct hv_device *device)
{
	struct storvsc_device *stor_device;

	stor_device = kzalloc(sizeof(struct storvsc_device), GFP_KERNEL);
	if (!stor_device)
		return NULL;

	/* Set to 2 to allow both inbound and outbound traffics */
	/* (ie get_stor_device() and must_get_stor_device()) to proceed. */
	atomic_cmpxchg(&stor_device->ref_count, 0, 2);

	stor_device->device = device;
	device->ext = stor_device;

	return stor_device;
}

static inline void free_stor_device(struct storvsc_device *device)
{
	/* ASSERT(atomic_read(&device->ref_count) == 0); */
	kfree(device);
}

/* Get the stordevice object iff exists and its refcount > 1 */
static inline struct storvsc_device *get_stor_device(struct hv_device *device)
{
	struct storvsc_device *stor_device;

	stor_device = (struct storvsc_device *)device->ext;
	if (stor_device && atomic_read(&stor_device->ref_count) > 1)
		atomic_inc(&stor_device->ref_count);
	else
		stor_device = NULL;

	return stor_device;
}

/* Get the stordevice object iff exists and its refcount > 0 */
static inline struct storvsc_device *must_get_stor_device(
					struct hv_device *device)
{
	struct storvsc_device *stor_device;

	stor_device = (struct storvsc_device *)device->ext;
	if (stor_device && atomic_read(&stor_device->ref_count))
		atomic_inc(&stor_device->ref_count);
	else
		stor_device = NULL;

	return stor_device;
}

static inline void put_stor_device(struct hv_device *device)
{
	struct storvsc_device *stor_device;

	stor_device = (struct storvsc_device *)device->ext;
	/* ASSERT(stor_device); */

	atomic_dec(&stor_device->ref_count);
	/* ASSERT(atomic_read(&stor_device->ref_count)); */
}

/* Drop ref count to 1 to effectively disable get_stor_device() */
static inline struct storvsc_device *release_stor_device(
					struct hv_device *device)
{
	struct storvsc_device *stor_device;

	stor_device = (struct storvsc_device *)device->ext;
	/* ASSERT(stor_device); */

	/* Busy wait until the ref drop to 2, then set it to 1 */
	while (atomic_cmpxchg(&stor_device->ref_count, 2, 1) != 2)
		udelay(100);

	return stor_device;
}

/* Drop ref count to 0. No one can use stor_device object. */
static inline struct storvsc_device *final_release_stor_device(
			struct hv_device *device)
{
	struct storvsc_device *stor_device;

	stor_device = (struct storvsc_device *)device->ext;
	/* ASSERT(stor_device); */

	/* Busy wait until the ref drop to 1, then set it to 0 */
	while (atomic_cmpxchg(&stor_device->ref_count, 1, 0) != 1)
		udelay(100);

	device->ext = NULL;
	return stor_device;
}

static int stor_vsc_channel_init(struct hv_device *device)
{
	struct storvsc_device *stor_device;
	struct storvsc_request_extension *request;
	struct vstor_packet *vstor_packet;
	int ret;

	stor_device = get_stor_device(device);
	if (!stor_device) {
		DPRINT_ERR(STORVSC, "unable to get stor device..."
			   "device being destroyed?");
		return -1;
	}

	request = &stor_device->init_request;
	vstor_packet = &request->vstor_packet;

	/*
	 * Now, initiate the vsc/vsp initialization protocol on the open
	 * channel
	 */
	memset(request, 0, sizeof(struct storvsc_request_extension));
	init_waitqueue_head(&request->wait_event);
	vstor_packet->operation = VSTOR_OPERATION_BEGIN_INITIALIZATION;
	vstor_packet->flags = REQUEST_COMPLETION_FLAG;

	DPRINT_INFO(STORVSC, "BEGIN_INITIALIZATION_OPERATION...");

	request->wait_condition = 0;
	ret = vmbus_sendpacket(device->channel, vstor_packet,
			       sizeof(struct vstor_packet),
			       (unsigned long)request,
			       VM_PKT_DATA_INBAND,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	if (ret != 0) {
		DPRINT_ERR(STORVSC,
			   "unable to send BEGIN_INITIALIZATION_OPERATION");
		goto cleanup;
	}

	wait_event_timeout(request->wait_event, request->wait_condition,
			msecs_to_jiffies(1000));
	if (request->wait_condition == 0) {
		ret = -ETIMEDOUT;
		goto cleanup;
	}


	if (vstor_packet->operation != VSTOR_OPERATION_COMPLETE_IO ||
	    vstor_packet->status != 0) {
		DPRINT_ERR(STORVSC, "BEGIN_INITIALIZATION_OPERATION failed "
			   "(op %d status 0x%x)",
			   vstor_packet->operation, vstor_packet->status);
		goto cleanup;
	}

	DPRINT_INFO(STORVSC, "QUERY_PROTOCOL_VERSION_OPERATION...");

	/* reuse the packet for version range supported */
	memset(vstor_packet, 0, sizeof(struct vstor_packet));
	vstor_packet->operation = VSTOR_OPERATION_QUERY_PROTOCOL_VERSION;
	vstor_packet->flags = REQUEST_COMPLETION_FLAG;

	vstor_packet->version.major_minor = VMSTOR_PROTOCOL_VERSION_CURRENT;
	FILL_VMSTOR_REVISION(vstor_packet->version.revision);

	request->wait_condition = 0;
	ret = vmbus_sendpacket(device->channel, vstor_packet,
			       sizeof(struct vstor_packet),
			       (unsigned long)request,
			       VM_PKT_DATA_INBAND,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	if (ret != 0) {
		DPRINT_ERR(STORVSC,
			   "unable to send BEGIN_INITIALIZATION_OPERATION");
		goto cleanup;
	}

	wait_event_timeout(request->wait_event, request->wait_condition,
			msecs_to_jiffies(1000));
	if (request->wait_condition == 0) {
		ret = -ETIMEDOUT;
		goto cleanup;
	}

	/* TODO: Check returned version */
	if (vstor_packet->operation != VSTOR_OPERATION_COMPLETE_IO ||
	    vstor_packet->status != 0) {
		DPRINT_ERR(STORVSC, "QUERY_PROTOCOL_VERSION_OPERATION failed "
			   "(op %d status 0x%x)",
			   vstor_packet->operation, vstor_packet->status);
		goto cleanup;
	}

	/* Query channel properties */
	DPRINT_INFO(STORVSC, "QUERY_PROPERTIES_OPERATION...");

	memset(vstor_packet, 0, sizeof(struct vstor_packet));
	vstor_packet->operation = VSTOR_OPERATION_QUERY_PROPERTIES;
	vstor_packet->flags = REQUEST_COMPLETION_FLAG;
	vstor_packet->storage_channel_properties.port_number =
					stor_device->port_number;

	request->wait_condition = 0;
	ret = vmbus_sendpacket(device->channel, vstor_packet,
			       sizeof(struct vstor_packet),
			       (unsigned long)request,
			       VM_PKT_DATA_INBAND,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);

	if (ret != 0) {
		DPRINT_ERR(STORVSC,
			   "unable to send QUERY_PROPERTIES_OPERATION");
		goto cleanup;
	}

	wait_event_timeout(request->wait_event, request->wait_condition,
			msecs_to_jiffies(1000));
	if (request->wait_condition == 0) {
		ret = -ETIMEDOUT;
		goto cleanup;
	}

	/* TODO: Check returned version */
	if (vstor_packet->operation != VSTOR_OPERATION_COMPLETE_IO ||
	    vstor_packet->status != 0) {
		DPRINT_ERR(STORVSC, "QUERY_PROPERTIES_OPERATION failed "
			   "(op %d status 0x%x)",
			   vstor_packet->operation, vstor_packet->status);
		goto cleanup;
	}

	stor_device->path_id = vstor_packet->storage_channel_properties.path_id;
	stor_device->target_id
		= vstor_packet->storage_channel_properties.target_id;

	DPRINT_DBG(STORVSC, "channel flag 0x%x, max xfer len 0x%x",
		   vstor_packet->storage_channel_properties.flags,
		   vstor_packet->storage_channel_properties.max_transfer_bytes);

	DPRINT_INFO(STORVSC, "END_INITIALIZATION_OPERATION...");

	memset(vstor_packet, 0, sizeof(struct vstor_packet));
	vstor_packet->operation = VSTOR_OPERATION_END_INITIALIZATION;
	vstor_packet->flags = REQUEST_COMPLETION_FLAG;

	request->wait_condition = 0;
	ret = vmbus_sendpacket(device->channel, vstor_packet,
			       sizeof(struct vstor_packet),
			       (unsigned long)request,
			       VM_PKT_DATA_INBAND,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);

	if (ret != 0) {
		DPRINT_ERR(STORVSC,
			   "unable to send END_INITIALIZATION_OPERATION");
		goto cleanup;
	}

	wait_event_timeout(request->wait_event, request->wait_condition,
			msecs_to_jiffies(1000));
	if (request->wait_condition == 0) {
		ret = -ETIMEDOUT;
		goto cleanup;
	}

	if (vstor_packet->operation != VSTOR_OPERATION_COMPLETE_IO ||
	    vstor_packet->status != 0) {
		DPRINT_ERR(STORVSC, "END_INITIALIZATION_OPERATION failed "
			   "(op %d status 0x%x)",
			   vstor_packet->operation, vstor_packet->status);
		goto cleanup;
	}

	DPRINT_INFO(STORVSC, "**** storage channel up and running!! ****");

cleanup:
	put_stor_device(device);
	return ret;
}

static void stor_vsc_on_io_completion(struct hv_device *device,
				  struct vstor_packet *vstor_packet,
				  struct storvsc_request_extension *request_ext)
{
	struct hv_storvsc_request *request;
	struct storvsc_device *stor_device;

	stor_device = must_get_stor_device(device);
	if (!stor_device) {
		DPRINT_ERR(STORVSC, "unable to get stor device..."
			   "device being destroyed?");
		return;
	}

	DPRINT_DBG(STORVSC, "IO_COMPLETE_OPERATION - request extension %p "
		   "completed bytes xfer %u", request_ext,
		   vstor_packet->vm_srb.data_transfer_length);

	/* ASSERT(request_ext != NULL); */
	/* ASSERT(request_ext->request != NULL); */

	request = request_ext->request;

	/* ASSERT(request->OnIOCompletion != NULL); */

	/* Copy over the status...etc */
	request->status = vstor_packet->vm_srb.scsi_status;

	if (request->status != 0 || vstor_packet->vm_srb.srb_status != 1) {
		DPRINT_WARN(STORVSC,
			    "cmd 0x%x scsi status 0x%x srb status 0x%x\n",
			    request->cdb[0], vstor_packet->vm_srb.scsi_status,
			    vstor_packet->vm_srb.srb_status);
	}

	if ((request->status & 0xFF) == 0x02) {
		/* CHECK_CONDITION */
		if (vstor_packet->vm_srb.srb_status & 0x80) {
			/* autosense data available */
			DPRINT_WARN(STORVSC, "storvsc pkt %p autosense data "
				    "valid - len %d\n", request_ext,
				    vstor_packet->vm_srb.sense_info_length);

			/* ASSERT(vstor_packet->vm_srb.sense_info_length <= */
			/* 	request->SenseBufferSize); */
			memcpy(request->sense_buffer,
			       vstor_packet->vm_srb.sense_data,
			       vstor_packet->vm_srb.sense_info_length);

			request->sense_buffer_size =
					vstor_packet->vm_srb.sense_info_length;
		}
	}

	/* TODO: */
	request->bytes_xfer = vstor_packet->vm_srb.data_transfer_length;

	request->on_io_completion(request);

	atomic_dec(&stor_device->num_outstanding_req);

	put_stor_device(device);
}

static void stor_vsc_on_receive(struct hv_device *device,
			     struct vstor_packet *vstor_packet,
			     struct storvsc_request_extension *request_ext)
{
	switch (vstor_packet->operation) {
	case VSTOR_OPERATION_COMPLETE_IO:
		DPRINT_DBG(STORVSC, "IO_COMPLETE_OPERATION");
		stor_vsc_on_io_completion(device, vstor_packet, request_ext);
		break;
	case VSTOR_OPERATION_REMOVE_DEVICE:
		DPRINT_INFO(STORVSC, "REMOVE_DEVICE_OPERATION");
		/* TODO: */
		break;

	default:
		DPRINT_INFO(STORVSC, "Unknown operation received - %d",
			    vstor_packet->operation);
		break;
	}
}

static void stor_vsc_on_channel_callback(void *context)
{
	struct hv_device *device = (struct hv_device *)context;
	struct storvsc_device *stor_device;
	u32 bytes_recvd;
	u64 request_id;
	unsigned char packet[ALIGN(sizeof(struct vstor_packet), 8)];
	struct storvsc_request_extension *request;
	int ret;

	/* ASSERT(device); */

	stor_device = must_get_stor_device(device);
	if (!stor_device) {
		DPRINT_ERR(STORVSC, "unable to get stor device..."
			   "device being destroyed?");
		return;
	}

	do {
		ret = vmbus_recvpacket(device->channel, packet,
				       ALIGN(sizeof(struct vstor_packet), 8),
				       &bytes_recvd, &request_id);
		if (ret == 0 && bytes_recvd > 0) {
			DPRINT_DBG(STORVSC, "receive %d bytes - tid %llx",
				   bytes_recvd, request_id);

			/* ASSERT(bytes_recvd ==
					sizeof(struct vstor_packet)); */

			request = (struct storvsc_request_extension *)
					(unsigned long)request_id;
			/* ASSERT(request);c */

			/* if (vstor_packet.Flags & SYNTHETIC_FLAG) */
			if ((request == &stor_device->init_request) ||
			    (request == &stor_device->reset_request)) {
				/* DPRINT_INFO(STORVSC,
				 *             "reset completion - operation "
				 *             "%u status %u",
				 *             vstor_packet.Operation,
				 *             vstor_packet.Status); */

				memcpy(&request->vstor_packet, packet,
				       sizeof(struct vstor_packet));
				request->wait_condition = 1;
				wake_up(&request->wait_event);
			} else {
				stor_vsc_on_receive(device,
						(struct vstor_packet *)packet,
						request);
			}
		} else {
			/* DPRINT_DBG(STORVSC, "nothing else to read..."); */
			break;
		}
	} while (1);

	put_stor_device(device);
	return;
}

static int stor_vsc_connect_to_vsp(struct hv_device *device)
{
	struct vmstorage_channel_properties props;
	struct storvsc_driver_object *stor_driver;
	int ret;

	stor_driver = (struct storvsc_driver_object *)device->drv;
	memset(&props, 0, sizeof(struct vmstorage_channel_properties));

	/* Open the channel */
	ret = vmbus_open(device->channel,
			 stor_driver->ring_buffer_size,
			 stor_driver->ring_buffer_size,
			 (void *)&props,
			 sizeof(struct vmstorage_channel_properties),
			 stor_vsc_on_channel_callback, device);

	DPRINT_DBG(STORVSC, "storage props: path id %d, tgt id %d, max xfer %d",
		   props.path_id, props.target_id, props.max_transfer_bytes);

	if (ret != 0) {
		DPRINT_ERR(STORVSC, "unable to open channel: %d", ret);
		return -1;
	}

	ret = stor_vsc_channel_init(device);

	return ret;
}

/*
 * stor_vsc_on_device_add - Callback when the device belonging to this driver
 * is added
 */
static int stor_vsc_on_device_add(struct hv_device *device,
					void *additional_info)
{
	struct storvsc_device *stor_device;
	/* struct vmstorage_channel_properties *props; */
	struct storvsc_device_info *device_info;
	int ret = 0;

	device_info = (struct storvsc_device_info *)additional_info;
	stor_device = alloc_stor_device(device);
	if (!stor_device) {
		ret = -1;
		goto cleanup;
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

	stor_device->port_number = device_info->port_number;
	/* Send it back up */
	ret = stor_vsc_connect_to_vsp(device);

	/* device_info->PortNumber = stor_device->PortNumber; */
	device_info->path_id = stor_device->path_id;
	device_info->target_id = stor_device->target_id;

	DPRINT_DBG(STORVSC, "assigned port %u, path %u target %u\n",
		   stor_device->port_number, stor_device->path_id,
		   stor_device->target_id);

cleanup:
	return ret;
}

/*
 * stor_vsc_on_device_remove - Callback when the our device is being removed
 */
static int stor_vsc_on_device_remove(struct hv_device *device)
{
	struct storvsc_device *stor_device;

	DPRINT_INFO(STORVSC, "disabling storage device (%p)...",
		    device->ext);

	stor_device = release_stor_device(device);

	/*
	 * At this point, all outbound traffic should be disable. We
	 * only allow inbound traffic (responses) to proceed so that
	 * outstanding requests can be completed.
	 */
	while (atomic_read(&stor_device->num_outstanding_req)) {
		DPRINT_INFO(STORVSC, "waiting for %d requests to complete...",
			    atomic_read(&stor_device->num_outstanding_req));
		udelay(100);
	}

	DPRINT_INFO(STORVSC, "removing storage device (%p)...",
		    device->ext);

	stor_device = final_release_stor_device(device);

	DPRINT_INFO(STORVSC, "storage device (%p) safe to remove", stor_device);

	/* Close the channel */
	vmbus_close(device->channel);

	free_stor_device(stor_device);
	return 0;
}

int stor_vsc_on_host_reset(struct hv_device *device)
{
	struct storvsc_device *stor_device;
	struct storvsc_request_extension *request;
	struct vstor_packet *vstor_packet;
	int ret;

	DPRINT_INFO(STORVSC, "resetting host adapter...");

	stor_device = get_stor_device(device);
	if (!stor_device) {
		DPRINT_ERR(STORVSC, "unable to get stor device..."
			   "device being destroyed?");
		return -1;
	}

	request = &stor_device->reset_request;
	vstor_packet = &request->vstor_packet;

	init_waitqueue_head(&request->wait_event);

	vstor_packet->operation = VSTOR_OPERATION_RESET_BUS;
	vstor_packet->flags = REQUEST_COMPLETION_FLAG;
	vstor_packet->vm_srb.path_id = stor_device->path_id;

	request->wait_condition = 0;
	ret = vmbus_sendpacket(device->channel, vstor_packet,
			       sizeof(struct vstor_packet),
			       (unsigned long)&stor_device->reset_request,
			       VM_PKT_DATA_INBAND,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	if (ret != 0) {
		DPRINT_ERR(STORVSC, "Unable to send reset packet %p ret %d",
			   vstor_packet, ret);
		goto cleanup;
	}

	wait_event_timeout(request->wait_event, request->wait_condition,
			msecs_to_jiffies(1000));
	if (request->wait_condition == 0) {
		ret = -ETIMEDOUT;
		goto cleanup;
	}

	DPRINT_INFO(STORVSC, "host adapter reset completed");

	/*
	 * At this point, all outstanding requests in the adapter
	 * should have been flushed out and return to us
	 */

cleanup:
	put_stor_device(device);
	return ret;
}

/*
 * stor_vsc_on_io_request - Callback to initiate an I/O request
 */
static int stor_vsc_on_io_request(struct hv_device *device,
			      struct hv_storvsc_request *request)
{
	struct storvsc_device *stor_device;
	struct storvsc_request_extension *request_extension;
	struct vstor_packet *vstor_packet;
	int ret = 0;

	request_extension =
		(struct storvsc_request_extension *)request->extension;
	vstor_packet = &request_extension->vstor_packet;
	stor_device = get_stor_device(device);

	DPRINT_DBG(STORVSC, "enter - Device %p, DeviceExt %p, Request %p, "
		   "Extension %p", device, stor_device, request,
		   request_extension);

	DPRINT_DBG(STORVSC, "req %p len %d bus %d, target %d, lun %d cdblen %d",
		   request, request->data_buffer.len, request->bus,
		   request->target_id, request->lun_id, request->cdb_len);

	if (!stor_device) {
		DPRINT_ERR(STORVSC, "unable to get stor device..."
			   "device being destroyed?");
		return -2;
	}

	/* print_hex_dump_bytes("", DUMP_PREFIX_NONE, request->Cdb,
	 *			request->CdbLen); */

	request_extension->request = request;
	request_extension->device  = device;

	memset(vstor_packet, 0 , sizeof(struct vstor_packet));

	vstor_packet->flags |= REQUEST_COMPLETION_FLAG;

	vstor_packet->vm_srb.length = sizeof(struct vmscsi_request);

	vstor_packet->vm_srb.port_number = request->host;
	vstor_packet->vm_srb.path_id = request->bus;
	vstor_packet->vm_srb.target_id = request->target_id;
	vstor_packet->vm_srb.lun = request->lun_id;

	vstor_packet->vm_srb.sense_info_length = SENSE_BUFFER_SIZE;

	/* Copy over the scsi command descriptor block */
	vstor_packet->vm_srb.cdb_length = request->cdb_len;
	memcpy(&vstor_packet->vm_srb.cdb, request->cdb, request->cdb_len);

	vstor_packet->vm_srb.data_in = request->type;
	vstor_packet->vm_srb.data_transfer_length = request->data_buffer.len;

	vstor_packet->operation = VSTOR_OPERATION_EXECUTE_SRB;

	DPRINT_DBG(STORVSC, "srb - len %d port %d, path %d, target %d, "
		   "lun %d senselen %d cdblen %d",
		   vstor_packet->vm_srb.length,
		   vstor_packet->vm_srb.port_number,
		   vstor_packet->vm_srb.path_id,
		   vstor_packet->vm_srb.target_id,
		   vstor_packet->vm_srb.lun,
		   vstor_packet->vm_srb.sense_info_length,
		   vstor_packet->vm_srb.cdb_length);

	if (request_extension->request->data_buffer.len) {
		ret = vmbus_sendpacket_multipagebuffer(device->channel,
				&request_extension->request->data_buffer,
				vstor_packet,
				sizeof(struct vstor_packet),
				(unsigned long)request_extension);
	} else {
		ret = vmbus_sendpacket(device->channel, vstor_packet,
				       sizeof(struct vstor_packet),
				       (unsigned long)request_extension,
				       VM_PKT_DATA_INBAND,
				       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	}

	if (ret != 0) {
		DPRINT_DBG(STORVSC, "Unable to send packet %p ret %d",
			   vstor_packet, ret);
	}

	atomic_inc(&stor_device->num_outstanding_req);

	put_stor_device(device);
	return ret;
}

/*
 * stor_vsc_on_cleanup - Perform any cleanup when the driver is removed
 */
static void stor_vsc_on_cleanup(struct hv_driver *driver)
{
}

/*
 * stor_vsc_initialize - Main entry point
 */
int stor_vsc_initialize(struct hv_driver *driver)
{
	struct storvsc_driver_object *stor_driver;

	stor_driver = (struct storvsc_driver_object *)driver;

	DPRINT_DBG(STORVSC, "sizeof(STORVSC_REQUEST)=%zd "
		   "sizeof(struct storvsc_request_extension)=%zd "
		   "sizeof(struct vstor_packet)=%zd, "
		   "sizeof(struct vmscsi_request)=%zd",
		   sizeof(struct hv_storvsc_request),
		   sizeof(struct storvsc_request_extension),
		   sizeof(struct vstor_packet),
		   sizeof(struct vmscsi_request));

	/* Make sure we are at least 2 pages since 1 page is used for control */
	/* ASSERT(stor_driver->RingBufferSize >= (PAGE_SIZE << 1)); */

	driver->name = g_driver_name;
	memcpy(&driver->dev_type, &gStorVscDeviceType,
	       sizeof(struct hv_guid));

	stor_driver->request_ext_size =
			sizeof(struct storvsc_request_extension);

	/*
	 * Divide the ring buffer data size (which is 1 page less
	 * than the ring buffer size since that page is reserved for
	 * the ring buffer indices) by the max request size (which is
	 * vmbus_channel_packet_multipage_buffer + struct vstor_packet + u64)
	 */
	stor_driver->max_outstanding_req_per_channel =
		((stor_driver->ring_buffer_size - PAGE_SIZE) /
		  ALIGN(MAX_MULTIPAGE_BUFFER_PACKET +
			   sizeof(struct vstor_packet) + sizeof(u64),
			   sizeof(u64)));

	DPRINT_INFO(STORVSC, "max io %u, currently %u\n",
		    stor_driver->max_outstanding_req_per_channel,
		    STORVSC_MAX_IO_REQUESTS);

	/* Setup the dispatch table */
	stor_driver->base.dev_add	= stor_vsc_on_device_add;
	stor_driver->base.dev_rm	= stor_vsc_on_device_remove;
	stor_driver->base.cleanup	= stor_vsc_on_cleanup;

	stor_driver->on_io_request	= stor_vsc_on_io_request;

	return 0;
}
