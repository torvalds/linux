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
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/slab.h>
#include "osd.h"
#include "logging.h"
#include "netvsc.h"
#include "rndis_filter.h"
#include "channel.h"


/* Globals */
static const char *driver_name = "netvsc";

/* {F8615163-DF3E-46c5-913F-F2D2F965ED0E} */
static const struct hv_guid netvsc_device_type = {
	.data = {
		0x63, 0x51, 0x61, 0xF8, 0x3E, 0xDF, 0xc5, 0x46,
		0x91, 0x3F, 0xF2, 0xD2, 0xF9, 0x65, 0xED, 0x0E
	}
};

static int netvsc_device_add(struct hv_device *device, void *additional_info);

static int netvsc_device_remove(struct hv_device *device);

static void netvsc_cleanup(struct hv_driver *driver);

static void netvsc_channel_cb(void *context);

static int netvsc_init_send_buf(struct hv_device *device);

static int netvsc_init_recv_buf(struct hv_device *device);

static int netvsc_destroy_send_buf(struct netvsc_device *net_device);

static int netvsc_destroy_recv_buf(struct netvsc_device *net_device);

static int netvsc_connect_vsp(struct hv_device *device);

static void netvsc_send_completion(struct hv_device *device,
				   struct vmpacket_descriptor *packet);

static int netvsc_send(struct hv_device *device,
			struct hv_netvsc_packet *packet);

static void netvsc_receive(struct hv_device *device,
			    struct vmpacket_descriptor *packet);

static void netvsc_receive_completion(void *context);

static void netvsc_send_recv_completion(struct hv_device *device,
					u64 transaction_id);


static struct netvsc_device *alloc_net_device(struct hv_device *device)
{
	struct netvsc_device *net_device;

	net_device = kzalloc(sizeof(struct netvsc_device), GFP_KERNEL);
	if (!net_device)
		return NULL;

	/* Set to 2 to allow both inbound and outbound traffic */
	atomic_cmpxchg(&net_device->RefCount, 0, 2);

	net_device->Device = device;
	device->Extension = net_device;

	return net_device;
}

static void free_net_device(struct netvsc_device *device)
{
	WARN_ON(atomic_read(&device->RefCount) == 0);
	device->Device->Extension = NULL;
	kfree(device);
}


/* Get the net device object iff exists and its refcount > 1 */
static struct netvsc_device *get_outbound_net_device(struct hv_device *device)
{
	struct netvsc_device *net_device;

	net_device = device->Extension;
	if (net_device && atomic_read(&net_device->RefCount) > 1)
		atomic_inc(&net_device->RefCount);
	else
		net_device = NULL;

	return net_device;
}

/* Get the net device object iff exists and its refcount > 0 */
static struct netvsc_device *get_inbound_net_device(struct hv_device *device)
{
	struct netvsc_device *net_device;

	net_device = device->Extension;
	if (net_device && atomic_read(&net_device->RefCount))
		atomic_inc(&net_device->RefCount);
	else
		net_device = NULL;

	return net_device;
}

static void put_net_device(struct hv_device *device)
{
	struct netvsc_device *net_device;

	net_device = device->Extension;
	/* ASSERT(netDevice); */

	atomic_dec(&net_device->RefCount);
}

static struct netvsc_device *release_outbound_net_device(
		struct hv_device *device)
{
	struct netvsc_device *net_device;

	net_device = device->Extension;
	if (net_device == NULL)
		return NULL;

	/* Busy wait until the ref drop to 2, then set it to 1 */
	while (atomic_cmpxchg(&net_device->RefCount, 2, 1) != 2)
		udelay(100);

	return net_device;
}

static struct netvsc_device *release_inbound_net_device(
		struct hv_device *device)
{
	struct netvsc_device *net_device;

	net_device = device->Extension;
	if (net_device == NULL)
		return NULL;

	/* Busy wait until the ref drop to 1, then set it to 0 */
	while (atomic_cmpxchg(&net_device->RefCount, 1, 0) != 1)
		udelay(100);

	device->Extension = NULL;
	return net_device;
}

/*
 * netvsc_initialize - Main entry point
 */
int netvsc_initialize(struct hv_driver *drv)
{
	struct netvsc_driver *driver = (struct netvsc_driver *)drv;

	DPRINT_DBG(NETVSC, "sizeof(struct hv_netvsc_packet)=%zd, "
		   "sizeof(struct nvsp_message)=%zd, "
		   "sizeof(struct vmtransfer_page_packet_header)=%zd",
		   sizeof(struct hv_netvsc_packet),
		   sizeof(struct nvsp_message),
		   sizeof(struct vmtransfer_page_packet_header));

	/* Make sure we are at least 2 pages since 1 page is used for control */
	/* ASSERT(driver->RingBufferSize >= (PAGE_SIZE << 1)); */

	drv->name = driver_name;
	memcpy(&drv->deviceType, &netvsc_device_type, sizeof(struct hv_guid));

	/* Make sure it is set by the caller */
	/* FIXME: These probably should still be tested in some way */
	/* ASSERT(driver->OnReceiveCallback); */
	/* ASSERT(driver->OnLinkStatusChanged); */

	/* Setup the dispatch table */
	driver->Base.OnDeviceAdd	= netvsc_device_add;
	driver->Base.OnDeviceRemove	= netvsc_device_remove;
	driver->Base.OnCleanup		= netvsc_cleanup;

	driver->OnSend			= netvsc_send;

	rndis_filter_init(driver);
	return 0;
}

static int netvsc_init_recv_buf(struct hv_device *device)
{
	int ret = 0;
	struct netvsc_device *net_device;
	struct nvsp_message *init_packet;

	net_device = get_outbound_net_device(device);
	if (!net_device) {
		DPRINT_ERR(NETVSC, "unable to get net device..."
			   "device being destroyed?");
		return -1;
	}
	/* ASSERT(netDevice->ReceiveBufferSize > 0); */
	/* page-size grandularity */
	/* ASSERT((netDevice->ReceiveBufferSize & (PAGE_SIZE - 1)) == 0); */

	net_device->ReceiveBuffer =
		osd_page_alloc(net_device->ReceiveBufferSize >> PAGE_SHIFT);
	if (!net_device->ReceiveBuffer) {
		DPRINT_ERR(NETVSC,
			   "unable to allocate receive buffer of size %d",
			   net_device->ReceiveBufferSize);
		ret = -1;
		goto Cleanup;
	}
	/* page-aligned buffer */
	/* ASSERT(((unsigned long)netDevice->ReceiveBuffer & (PAGE_SIZE - 1)) == */
	/* 	0); */

	DPRINT_INFO(NETVSC, "Establishing receive buffer's GPADL...");

	/*
	 * Establish the gpadl handle for this buffer on this
	 * channel.  Note: This call uses the vmbus connection rather
	 * than the channel to establish the gpadl handle.
	 */
	ret = vmbus_establish_gpadl(device->channel, net_device->ReceiveBuffer,
				    net_device->ReceiveBufferSize,
				    &net_device->ReceiveBufferGpadlHandle);
	if (ret != 0) {
		DPRINT_ERR(NETVSC,
			   "unable to establish receive buffer's gpadl");
		goto Cleanup;
	}

	/* osd_waitevent_wait(ext->ChannelInitEvent); */

	/* Notify the NetVsp of the gpadl handle */
	DPRINT_INFO(NETVSC, "Sending NvspMessage1TypeSendReceiveBuffer...");

	init_packet = &net_device->ChannelInitPacket;

	memset(init_packet, 0, sizeof(struct nvsp_message));

	init_packet->Header.MessageType = NvspMessage1TypeSendReceiveBuffer;
	init_packet->Messages.Version1Messages.SendReceiveBuffer.
		GpadlHandle = net_device->ReceiveBufferGpadlHandle;
	init_packet->Messages.Version1Messages.
		SendReceiveBuffer.Id = NETVSC_RECEIVE_BUFFER_ID;

	/* Send the gpadl notification request */
	ret = vmbus_sendpacket(device->channel, init_packet,
			       sizeof(struct nvsp_message),
			       (unsigned long)init_packet,
			       VmbusPacketTypeDataInBand,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	if (ret != 0) {
		DPRINT_ERR(NETVSC,
			   "unable to send receive buffer's gpadl to netvsp");
		goto Cleanup;
	}

	osd_waitevent_wait(net_device->ChannelInitEvent);

	/* Check the response */
	if (init_packet->Messages.Version1Messages.
	    SendReceiveBufferComplete.Status != NvspStatusSuccess) {
		DPRINT_ERR(NETVSC, "Unable to complete receive buffer "
			   "initialzation with NetVsp - status %d",
			   init_packet->Messages.Version1Messages.
			   SendReceiveBufferComplete.Status);
		ret = -1;
		goto Cleanup;
	}

	/* Parse the response */
	/* ASSERT(netDevice->ReceiveSectionCount == 0); */
	/* ASSERT(netDevice->ReceiveSections == NULL); */

	net_device->ReceiveSectionCount = init_packet->Messages.
		Version1Messages.SendReceiveBufferComplete.NumSections;

	net_device->ReceiveSections = kmalloc(net_device->ReceiveSectionCount
		* sizeof(struct nvsp_1_receive_buffer_section), GFP_KERNEL);
	if (net_device->ReceiveSections == NULL) {
		ret = -1;
		goto Cleanup;
	}

	memcpy(net_device->ReceiveSections,
		init_packet->Messages.Version1Messages.
	       SendReceiveBufferComplete.Sections,
		net_device->ReceiveSectionCount *
	       sizeof(struct nvsp_1_receive_buffer_section));

	DPRINT_INFO(NETVSC, "Receive sections info (count %d, offset %d, "
		    "endoffset %d, suballoc size %d, num suballocs %d)",
		    net_device->ReceiveSectionCount,
		    net_device->ReceiveSections[0].Offset,
		    net_device->ReceiveSections[0].EndOffset,
		    net_device->ReceiveSections[0].SubAllocationSize,
		    net_device->ReceiveSections[0].NumSubAllocations);

	/*
	 * For 1st release, there should only be 1 section that represents the
	 * entire receive buffer
	 */
	if (net_device->ReceiveSectionCount != 1 ||
	    net_device->ReceiveSections->Offset != 0) {
		ret = -1;
		goto Cleanup;
	}

	goto Exit;

Cleanup:
	netvsc_destroy_recv_buf(net_device);

Exit:
	put_net_device(device);
	return ret;
}

static int netvsc_init_send_buf(struct hv_device *device)
{
	int ret = 0;
	struct netvsc_device *net_device;
	struct nvsp_message *init_packet;

	net_device = get_outbound_net_device(device);
	if (!net_device) {
		DPRINT_ERR(NETVSC, "unable to get net device..."
			   "device being destroyed?");
		return -1;
	}
	if (net_device->SendBufferSize <= 0) {
		ret = -EINVAL;
		goto Cleanup;
	}

	/* page-size grandularity */
	/* ASSERT((netDevice->SendBufferSize & (PAGE_SIZE - 1)) == 0); */

	net_device->SendBuffer =
		osd_page_alloc(net_device->SendBufferSize >> PAGE_SHIFT);
	if (!net_device->SendBuffer) {
		DPRINT_ERR(NETVSC, "unable to allocate send buffer of size %d",
			   net_device->SendBufferSize);
		ret = -1;
		goto Cleanup;
	}
	/* page-aligned buffer */
	/* ASSERT(((unsigned long)netDevice->SendBuffer & (PAGE_SIZE - 1)) == 0); */

	DPRINT_INFO(NETVSC, "Establishing send buffer's GPADL...");

	/*
	 * Establish the gpadl handle for this buffer on this
	 * channel.  Note: This call uses the vmbus connection rather
	 * than the channel to establish the gpadl handle.
	 */
	ret = vmbus_establish_gpadl(device->channel, net_device->SendBuffer,
				    net_device->SendBufferSize,
				    &net_device->SendBufferGpadlHandle);
	if (ret != 0) {
		DPRINT_ERR(NETVSC, "unable to establish send buffer's gpadl");
		goto Cleanup;
	}

	/* osd_waitevent_wait(ext->ChannelInitEvent); */

	/* Notify the NetVsp of the gpadl handle */
	DPRINT_INFO(NETVSC, "Sending NvspMessage1TypeSendSendBuffer...");

	init_packet = &net_device->ChannelInitPacket;

	memset(init_packet, 0, sizeof(struct nvsp_message));

	init_packet->Header.MessageType = NvspMessage1TypeSendSendBuffer;
	init_packet->Messages.Version1Messages.SendReceiveBuffer.
		GpadlHandle = net_device->SendBufferGpadlHandle;
	init_packet->Messages.Version1Messages.SendReceiveBuffer.Id =
		NETVSC_SEND_BUFFER_ID;

	/* Send the gpadl notification request */
	ret = vmbus_sendpacket(device->channel, init_packet,
			       sizeof(struct nvsp_message),
			       (unsigned long)init_packet,
			       VmbusPacketTypeDataInBand,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	if (ret != 0) {
		DPRINT_ERR(NETVSC,
			   "unable to send receive buffer's gpadl to netvsp");
		goto Cleanup;
	}

	osd_waitevent_wait(net_device->ChannelInitEvent);

	/* Check the response */
	if (init_packet->Messages.Version1Messages.
	    SendSendBufferComplete.Status != NvspStatusSuccess) {
		DPRINT_ERR(NETVSC, "Unable to complete send buffer "
			   "initialzation with NetVsp - status %d",
			   init_packet->Messages.Version1Messages.
			   SendSendBufferComplete.Status);
		ret = -1;
		goto Cleanup;
	}

	net_device->SendSectionSize = init_packet->
	Messages.Version1Messages.SendSendBufferComplete.SectionSize;

	goto Exit;

Cleanup:
	netvsc_destroy_send_buf(net_device);

Exit:
	put_net_device(device);
	return ret;
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
	if (net_device->ReceiveSectionCount) {
		DPRINT_INFO(NETVSC,
			    "Sending NvspMessage1TypeRevokeReceiveBuffer...");

		/* Send the revoke receive buffer */
		revoke_packet = &net_device->RevokePacket;
		memset(revoke_packet, 0, sizeof(struct nvsp_message));

		revoke_packet->Header.MessageType =
			NvspMessage1TypeRevokeReceiveBuffer;
		revoke_packet->Messages.Version1Messages.
		RevokeReceiveBuffer.Id = NETVSC_RECEIVE_BUFFER_ID;

		ret = vmbus_sendpacket(net_device->Device->channel,
				       revoke_packet,
				       sizeof(struct nvsp_message),
				       (unsigned long)revoke_packet,
				       VmbusPacketTypeDataInBand, 0);
		/*
		 * If we failed here, we might as well return and
		 * have a leak rather than continue and a bugchk
		 */
		if (ret != 0) {
			DPRINT_ERR(NETVSC, "unable to send revoke receive "
				   "buffer to netvsp");
			return -1;
		}
	}

	/* Teardown the gpadl on the vsp end */
	if (net_device->ReceiveBufferGpadlHandle) {
		DPRINT_INFO(NETVSC, "Tearing down receive buffer's GPADL...");

		ret = vmbus_teardown_gpadl(net_device->Device->channel,
			   net_device->ReceiveBufferGpadlHandle);

		/* If we failed here, we might as well return and have a leak rather than continue and a bugchk */
		if (ret != 0) {
			DPRINT_ERR(NETVSC,
				   "unable to teardown receive buffer's gpadl");
			return -1;
		}
		net_device->ReceiveBufferGpadlHandle = 0;
	}

	if (net_device->ReceiveBuffer) {
		DPRINT_INFO(NETVSC, "Freeing up receive buffer...");

		/* Free up the receive buffer */
		osd_page_free(net_device->ReceiveBuffer,
			     net_device->ReceiveBufferSize >> PAGE_SHIFT);
		net_device->ReceiveBuffer = NULL;
	}

	if (net_device->ReceiveSections) {
		net_device->ReceiveSectionCount = 0;
		kfree(net_device->ReceiveSections);
		net_device->ReceiveSections = NULL;
	}

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
	if (net_device->SendSectionSize) {
		DPRINT_INFO(NETVSC,
			    "Sending NvspMessage1TypeRevokeSendBuffer...");

		/* Send the revoke send buffer */
		revoke_packet = &net_device->RevokePacket;
		memset(revoke_packet, 0, sizeof(struct nvsp_message));

		revoke_packet->Header.MessageType =
			NvspMessage1TypeRevokeSendBuffer;
		revoke_packet->Messages.Version1Messages.
			RevokeSendBuffer.Id = NETVSC_SEND_BUFFER_ID;

		ret = vmbus_sendpacket(net_device->Device->channel,
				       revoke_packet,
				       sizeof(struct nvsp_message),
				       (unsigned long)revoke_packet,
				       VmbusPacketTypeDataInBand, 0);
		/*
		 * If we failed here, we might as well return and have a leak
		 * rather than continue and a bugchk
		 */
		if (ret != 0) {
			DPRINT_ERR(NETVSC, "unable to send revoke send buffer "
				   "to netvsp");
			return -1;
		}
	}

	/* Teardown the gpadl on the vsp end */
	if (net_device->SendBufferGpadlHandle) {
		DPRINT_INFO(NETVSC, "Tearing down send buffer's GPADL...");
		ret = vmbus_teardown_gpadl(net_device->Device->channel,
					   net_device->SendBufferGpadlHandle);

		/*
		 * If we failed here, we might as well return and have a leak
		 * rather than continue and a bugchk
		 */
		if (ret != 0) {
			DPRINT_ERR(NETVSC, "unable to teardown send buffer's "
				   "gpadl");
			return -1;
		}
		net_device->SendBufferGpadlHandle = 0;
	}

	if (net_device->SendBuffer) {
		DPRINT_INFO(NETVSC, "Freeing up send buffer...");

		/* Free up the receive buffer */
		osd_page_free(net_device->SendBuffer,
			     net_device->SendBufferSize >> PAGE_SHIFT);
		net_device->SendBuffer = NULL;
	}

	return ret;
}


static int netvsc_connect_vsp(struct hv_device *device)
{
	int ret;
	struct netvsc_device *net_device;
	struct nvsp_message *init_packet;
	int ndis_version;

	net_device = get_outbound_net_device(device);
	if (!net_device) {
		DPRINT_ERR(NETVSC, "unable to get net device..."
			   "device being destroyed?");
		return -1;
	}

	init_packet = &net_device->ChannelInitPacket;

	memset(init_packet, 0, sizeof(struct nvsp_message));
	init_packet->Header.MessageType = NvspMessageTypeInit;
	init_packet->Messages.InitMessages.Init.MinProtocolVersion =
		NVSP_MIN_PROTOCOL_VERSION;
	init_packet->Messages.InitMessages.Init.MaxProtocolVersion =
		NVSP_MAX_PROTOCOL_VERSION;

	DPRINT_INFO(NETVSC, "Sending NvspMessageTypeInit...");

	/* Send the init request */
	ret = vmbus_sendpacket(device->channel, init_packet,
			       sizeof(struct nvsp_message),
			       (unsigned long)init_packet,
			       VmbusPacketTypeDataInBand,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);

	if (ret != 0) {
		DPRINT_ERR(NETVSC, "unable to send NvspMessageTypeInit");
		goto Cleanup;
	}

	osd_waitevent_wait(net_device->ChannelInitEvent);

	/* Now, check the response */
	/* ASSERT(initPacket->Messages.InitMessages.InitComplete.MaximumMdlChainLength <= MAX_MULTIPAGE_BUFFER_COUNT); */
	DPRINT_INFO(NETVSC, "NvspMessageTypeInit status(%d) max mdl chain (%d)",
		init_packet->Messages.InitMessages.InitComplete.Status,
		init_packet->Messages.InitMessages.
		    InitComplete.MaximumMdlChainLength);

	if (init_packet->Messages.InitMessages.InitComplete.Status !=
	    NvspStatusSuccess) {
		DPRINT_ERR(NETVSC,
			"unable to initialize with netvsp (status 0x%x)",
			init_packet->Messages.InitMessages.InitComplete.Status);
		ret = -1;
		goto Cleanup;
	}

	if (init_packet->Messages.InitMessages.InitComplete.
	    NegotiatedProtocolVersion != NVSP_PROTOCOL_VERSION_1) {
		DPRINT_ERR(NETVSC, "unable to initialize with netvsp "
			   "(version expected 1 got %d)",
			   init_packet->Messages.InitMessages.
			   InitComplete.NegotiatedProtocolVersion);
		ret = -1;
		goto Cleanup;
	}
	DPRINT_INFO(NETVSC, "Sending NvspMessage1TypeSendNdisVersion...");

	/* Send the ndis version */
	memset(init_packet, 0, sizeof(struct nvsp_message));

	ndis_version = 0x00050000;

	init_packet->Header.MessageType = NvspMessage1TypeSendNdisVersion;
	init_packet->Messages.Version1Messages.
		SendNdisVersion.NdisMajorVersion =
				(ndis_version & 0xFFFF0000) >> 16;
	init_packet->Messages.Version1Messages.
		SendNdisVersion.NdisMinorVersion =
				ndis_version & 0xFFFF;

	/* Send the init request */
	ret = vmbus_sendpacket(device->channel, init_packet,
			       sizeof(struct nvsp_message),
			       (unsigned long)init_packet,
			       VmbusPacketTypeDataInBand, 0);
	if (ret != 0) {
		DPRINT_ERR(NETVSC,
			   "unable to send NvspMessage1TypeSendNdisVersion");
		ret = -1;
		goto Cleanup;
	}
	/*
	 * BUGBUG - We have to wait for the above msg since the
	 * netvsp uses KMCL which acknowledges packet (completion
	 * packet) since our Vmbus always set the
	 * VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED flag
	 */
	 /* osd_waitevent_wait(NetVscChannel->ChannelInitEvent); */

	/* Post the big receive buffer to NetVSP */
	ret = netvsc_init_recv_buf(device);
	if (ret == 0)
		ret = netvsc_init_send_buf(device);

Cleanup:
	put_net_device(device);
	return ret;
}

static void NetVscDisconnectFromVsp(struct netvsc_device *net_device)
{
	netvsc_destroy_recv_buf(net_device);
	netvsc_destroy_send_buf(net_device);
}

/*
 * netvsc_device_add - Callback when the device belonging to this
 * driver is added
 */
static int netvsc_device_add(struct hv_device *device, void *additional_info)
{
	int ret = 0;
	int i;
	struct netvsc_device *net_device;
	struct hv_netvsc_packet *packet, *pos;
	struct netvsc_driver *net_driver =
				(struct netvsc_driver *)device->Driver;

	net_device = alloc_net_device(device);
	if (!net_device) {
		ret = -1;
		goto Cleanup;
	}

	DPRINT_DBG(NETVSC, "netvsc channel object allocated - %p", net_device);

	/* Initialize the NetVSC channel extension */
	net_device->ReceiveBufferSize = NETVSC_RECEIVE_BUFFER_SIZE;
	spin_lock_init(&net_device->receive_packet_list_lock);

	net_device->SendBufferSize = NETVSC_SEND_BUFFER_SIZE;

	INIT_LIST_HEAD(&net_device->ReceivePacketList);

	for (i = 0; i < NETVSC_RECEIVE_PACKETLIST_COUNT; i++) {
		packet = kzalloc(sizeof(struct hv_netvsc_packet) +
				 (NETVSC_RECEIVE_SG_COUNT *
				  sizeof(struct hv_page_buffer)), GFP_KERNEL);
		if (!packet) {
			DPRINT_DBG(NETVSC, "unable to allocate netvsc pkts "
				   "for receive pool (wanted %d got %d)",
				   NETVSC_RECEIVE_PACKETLIST_COUNT, i);
			break;
		}
		list_add_tail(&packet->ListEntry,
			      &net_device->ReceivePacketList);
	}
	net_device->ChannelInitEvent = osd_waitevent_create();
	if (!net_device->ChannelInitEvent) {
		ret = -ENOMEM;
		goto Cleanup;
	}

	/* Open the channel */
	ret = vmbus_open(device->channel, net_driver->RingBufferSize,
			 net_driver->RingBufferSize, NULL, 0,
			 netvsc_channel_cb, device);

	if (ret != 0) {
		DPRINT_ERR(NETVSC, "unable to open channel: %d", ret);
		ret = -1;
		goto Cleanup;
	}

	/* Channel is opened */
	DPRINT_INFO(NETVSC, "*** NetVSC channel opened successfully! ***");

	/* Connect with the NetVsp */
	ret = netvsc_connect_vsp(device);
	if (ret != 0) {
		DPRINT_ERR(NETVSC, "unable to connect to NetVSP - %d", ret);
		ret = -1;
		goto close;
	}

	DPRINT_INFO(NETVSC, "*** NetVSC channel handshake result - %d ***",
		    ret);

	return ret;

close:
	/* Now, we can close the channel safely */
	vmbus_close(device->channel);

Cleanup:

	if (net_device) {
		kfree(net_device->ChannelInitEvent);

		list_for_each_entry_safe(packet, pos,
					 &net_device->ReceivePacketList,
					 ListEntry) {
			list_del(&packet->ListEntry);
			kfree(packet);
		}

		release_outbound_net_device(device);
		release_inbound_net_device(device);

		free_net_device(net_device);
	}

	return ret;
}

/*
 * netvsc_device_remove - Callback when the root bus device is removed
 */
static int netvsc_device_remove(struct hv_device *device)
{
	struct netvsc_device *net_device;
	struct hv_netvsc_packet *netvsc_packet, *pos;

	DPRINT_INFO(NETVSC, "Disabling outbound traffic on net device (%p)...",
		    device->Extension);

	/* Stop outbound traffic ie sends and receives completions */
	net_device = release_outbound_net_device(device);
	if (!net_device) {
		DPRINT_ERR(NETVSC, "No net device present!!");
		return -1;
	}

	/* Wait for all send completions */
	while (atomic_read(&net_device->NumOutstandingSends)) {
		DPRINT_INFO(NETVSC, "waiting for %d requests to complete...",
			    atomic_read(&net_device->NumOutstandingSends));
		udelay(100);
	}

	DPRINT_INFO(NETVSC, "Disconnecting from netvsp...");

	NetVscDisconnectFromVsp(net_device);

	DPRINT_INFO(NETVSC, "Disabling inbound traffic on net device (%p)...",
		    device->Extension);

	/* Stop inbound traffic ie receives and sends completions */
	net_device = release_inbound_net_device(device);

	/* At this point, no one should be accessing netDevice except in here */
	DPRINT_INFO(NETVSC, "net device (%p) safe to remove", net_device);

	/* Now, we can close the channel safely */
	vmbus_close(device->channel);

	/* Release all resources */
	list_for_each_entry_safe(netvsc_packet, pos,
				 &net_device->ReceivePacketList, ListEntry) {
		list_del(&netvsc_packet->ListEntry);
		kfree(netvsc_packet);
	}

	kfree(net_device->ChannelInitEvent);
	free_net_device(net_device);
	return 0;
}

/*
 * netvsc_cleanup - Perform any cleanup when the driver is removed
 */
static void netvsc_cleanup(struct hv_driver *drv)
{
}

static void netvsc_send_completion(struct hv_device *device,
				   struct vmpacket_descriptor *packet)
{
	struct netvsc_device *net_device;
	struct nvsp_message *nvsp_packet;
	struct hv_netvsc_packet *nvsc_packet;

	net_device = get_inbound_net_device(device);
	if (!net_device) {
		DPRINT_ERR(NETVSC, "unable to get net device..."
			   "device being destroyed?");
		return;
	}

	nvsp_packet = (struct nvsp_message *)((unsigned long)packet +
			(packet->DataOffset8 << 3));

	DPRINT_DBG(NETVSC, "send completion packet - type %d",
		   nvsp_packet->Header.MessageType);

	if ((nvsp_packet->Header.MessageType == NvspMessageTypeInitComplete) ||
	    (nvsp_packet->Header.MessageType ==
	     NvspMessage1TypeSendReceiveBufferComplete) ||
	    (nvsp_packet->Header.MessageType ==
	     NvspMessage1TypeSendSendBufferComplete)) {
		/* Copy the response back */
		memcpy(&net_device->ChannelInitPacket, nvsp_packet,
		       sizeof(struct nvsp_message));
		osd_waitevent_set(net_device->ChannelInitEvent);
	} else if (nvsp_packet->Header.MessageType ==
		   NvspMessage1TypeSendRNDISPacketComplete) {
		/* Get the send context */
		nvsc_packet = (struct hv_netvsc_packet *)(unsigned long)
			packet->TransactionId;
		/* ASSERT(nvscPacket); */

		/* Notify the layer above us */
		nvsc_packet->Completion.Send.OnSendCompletion(
			nvsc_packet->Completion.Send.SendCompletionContext);

		atomic_dec(&net_device->NumOutstandingSends);
	} else {
		DPRINT_ERR(NETVSC, "Unknown send completion packet type - "
			   "%d received!!", nvsp_packet->Header.MessageType);
	}

	put_net_device(device);
}

static int netvsc_send(struct hv_device *device,
			struct hv_netvsc_packet *packet)
{
	struct netvsc_device *net_device;
	int ret = 0;

	struct nvsp_message sendMessage;

	net_device = get_outbound_net_device(device);
	if (!net_device) {
		DPRINT_ERR(NETVSC, "net device (%p) shutting down..."
			   "ignoring outbound packets", net_device);
		return -2;
	}

	sendMessage.Header.MessageType = NvspMessage1TypeSendRNDISPacket;
	if (packet->IsDataPacket) {
		/* 0 is RMC_DATA; */
		sendMessage.Messages.Version1Messages.SendRNDISPacket.ChannelType = 0;
	} else {
		/* 1 is RMC_CONTROL; */
		sendMessage.Messages.Version1Messages.SendRNDISPacket.ChannelType = 1;
	}

	/* Not using send buffer section */
	sendMessage.Messages.Version1Messages.SendRNDISPacket.SendBufferSectionIndex = 0xFFFFFFFF;
	sendMessage.Messages.Version1Messages.SendRNDISPacket.SendBufferSectionSize = 0;

	if (packet->PageBufferCount) {
		ret = vmbus_sendpacket_pagebuffer(device->channel,
						  packet->PageBuffers,
						  packet->PageBufferCount,
						  &sendMessage,
						  sizeof(struct nvsp_message),
						  (unsigned long)packet);
	} else {
		ret = vmbus_sendpacket(device->channel, &sendMessage,
				       sizeof(struct nvsp_message),
				       (unsigned long)packet,
				       VmbusPacketTypeDataInBand,
				       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);

	}

	if (ret != 0)
		DPRINT_ERR(NETVSC, "Unable to send packet %p ret %d",
			   packet, ret);

	atomic_inc(&net_device->NumOutstandingSends);
	put_net_device(device);
	return ret;
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
		DPRINT_ERR(NETVSC, "unable to get net device..."
			   "device being destroyed?");
		return;
	}

	/*
	 * All inbound packets other than send completion should be xfer page
	 * packet
	 */
	if (packet->Type != VmbusPacketTypeDataUsingTransferPages) {
		DPRINT_ERR(NETVSC, "Unknown packet type received - %d",
			   packet->Type);
		put_net_device(device);
		return;
	}

	nvsp_packet = (struct nvsp_message *)((unsigned long)packet +
			(packet->DataOffset8 << 3));

	/* Make sure this is a valid nvsp packet */
	if (nvsp_packet->Header.MessageType !=
	    NvspMessage1TypeSendRNDISPacket) {
		DPRINT_ERR(NETVSC, "Unknown nvsp packet type received - %d",
			   nvsp_packet->Header.MessageType);
		put_net_device(device);
		return;
	}

	DPRINT_DBG(NETVSC, "NVSP packet received - type %d",
		   nvsp_packet->Header.MessageType);

	vmxferpage_packet = (struct vmtransfer_page_packet_header *)packet;

	if (vmxferpage_packet->TransferPageSetId != NETVSC_RECEIVE_BUFFER_ID) {
		DPRINT_ERR(NETVSC, "Invalid xfer page set id - "
			   "expecting %x got %x", NETVSC_RECEIVE_BUFFER_ID,
			   vmxferpage_packet->TransferPageSetId);
		put_net_device(device);
		return;
	}

	DPRINT_DBG(NETVSC, "xfer page - range count %d",
		   vmxferpage_packet->RangeCount);

	/*
	 * Grab free packets (range count + 1) to represent this xfer
	 * page packet. +1 to represent the xfer page packet itself.
	 * We grab it here so that we know exactly how many we can
	 * fulfil
	 */
	spin_lock_irqsave(&net_device->receive_packet_list_lock, flags);
	while (!list_empty(&net_device->ReceivePacketList)) {
		list_move_tail(net_device->ReceivePacketList.next, &listHead);
		if (++count == vmxferpage_packet->RangeCount + 1)
			break;
	}
	spin_unlock_irqrestore(&net_device->receive_packet_list_lock, flags);

	/*
	 * We need at least 2 netvsc pkts (1 to represent the xfer
	 * page and at least 1 for the range) i.e. we can handled
	 * some of the xfer page packet ranges...
	 */
	if (count < 2) {
		DPRINT_ERR(NETVSC, "Got only %d netvsc pkt...needed %d pkts. "
			   "Dropping this xfer page packet completely!",
			   count, vmxferpage_packet->RangeCount + 1);

		/* Return it to the freelist */
		spin_lock_irqsave(&net_device->receive_packet_list_lock, flags);
		for (i = count; i != 0; i--) {
			list_move_tail(listHead.next,
				       &net_device->ReceivePacketList);
		}
		spin_unlock_irqrestore(&net_device->receive_packet_list_lock,
				       flags);

		netvsc_send_recv_completion(device,
					    vmxferpage_packet->d.TransactionId);

		put_net_device(device);
		return;
	}

	/* Remove the 1st packet to represent the xfer page packet itself */
	xferpage_packet = (struct xferpage_packet *)listHead.next;
	list_del(&xferpage_packet->ListEntry);

	/* This is how much we can satisfy */
	xferpage_packet->Count = count - 1;
	/* ASSERT(xferpagePacket->Count > 0 && xferpagePacket->Count <= */
	/* 	vmxferpagePacket->RangeCount); */

	if (xferpage_packet->Count != vmxferpage_packet->RangeCount) {
		DPRINT_INFO(NETVSC, "Needed %d netvsc pkts to satisy this xfer "
			    "page...got %d", vmxferpage_packet->RangeCount,
			    xferpage_packet->Count);
	}

	/* Each range represents 1 RNDIS pkt that contains 1 ethernet frame */
	for (i = 0; i < (count - 1); i++) {
		netvsc_packet = (struct hv_netvsc_packet *)listHead.next;
		list_del(&netvsc_packet->ListEntry);

		/* Initialize the netvsc packet */
		netvsc_packet->XferPagePacket = xferpage_packet;
		netvsc_packet->Completion.Recv.OnReceiveCompletion =
					netvsc_receive_completion;
		netvsc_packet->Completion.Recv.ReceiveCompletionContext =
					netvsc_packet;
		netvsc_packet->Device = device;
		/* Save this so that we can send it back */
		netvsc_packet->Completion.Recv.ReceiveCompletionTid =
					vmxferpage_packet->d.TransactionId;

		netvsc_packet->TotalDataBufferLength =
					vmxferpage_packet->Ranges[i].ByteCount;
		netvsc_packet->PageBufferCount = 1;

		/* ASSERT(vmxferpagePacket->Ranges[i].ByteOffset + */
		/* 	vmxferpagePacket->Ranges[i].ByteCount < */
		/* 	netDevice->ReceiveBufferSize); */

		netvsc_packet->PageBuffers[0].Length =
					vmxferpage_packet->Ranges[i].ByteCount;

		start = virt_to_phys((void *)((unsigned long)net_device->
		ReceiveBuffer + vmxferpage_packet->Ranges[i].ByteOffset));

		netvsc_packet->PageBuffers[0].Pfn = start >> PAGE_SHIFT;
		end_virtual = (unsigned long)net_device->ReceiveBuffer
		    + vmxferpage_packet->Ranges[i].ByteOffset
		    + vmxferpage_packet->Ranges[i].ByteCount - 1;
		end = virt_to_phys((void *)end_virtual);

		/* Calculate the page relative offset */
		netvsc_packet->PageBuffers[0].Offset =
			vmxferpage_packet->Ranges[i].ByteOffset &
			(PAGE_SIZE - 1);
		if ((end >> PAGE_SHIFT) != (start >> PAGE_SHIFT)) {
			/* Handle frame across multiple pages: */
			netvsc_packet->PageBuffers[0].Length =
				(netvsc_packet->PageBuffers[0].Pfn <<
				 PAGE_SHIFT)
				+ PAGE_SIZE - start;
			bytes_remain = netvsc_packet->TotalDataBufferLength -
					netvsc_packet->PageBuffers[0].Length;
			for (j = 1; j < NETVSC_PACKET_MAXPAGE; j++) {
				netvsc_packet->PageBuffers[j].Offset = 0;
				if (bytes_remain <= PAGE_SIZE) {
					netvsc_packet->PageBuffers[j].Length =
						bytes_remain;
					bytes_remain = 0;
				} else {
					netvsc_packet->PageBuffers[j].Length =
						PAGE_SIZE;
					bytes_remain -= PAGE_SIZE;
				}
				netvsc_packet->PageBuffers[j].Pfn =
				    virt_to_phys((void *)(end_virtual -
						bytes_remain)) >> PAGE_SHIFT;
				netvsc_packet->PageBufferCount++;
				if (bytes_remain == 0)
					break;
			}
			/* ASSERT(bytesRemain == 0); */
		}
		DPRINT_DBG(NETVSC, "[%d] - (abs offset %u len %u) => "
			   "(pfn %llx, offset %u, len %u)", i,
			   vmxferpage_packet->Ranges[i].ByteOffset,
			   vmxferpage_packet->Ranges[i].ByteCount,
			   netvsc_packet->PageBuffers[0].Pfn,
			   netvsc_packet->PageBuffers[0].Offset,
			   netvsc_packet->PageBuffers[0].Length);

		/* Pass it to the upper layer */
		((struct netvsc_driver *)device->Driver)->
			OnReceiveCallback(device, netvsc_packet);

		netvsc_receive_completion(netvsc_packet->
				Completion.Recv.ReceiveCompletionContext);
	}

	/* ASSERT(list_empty(&listHead)); */

	put_net_device(device);
}

static void netvsc_send_recv_completion(struct hv_device *device,
					u64 transaction_id)
{
	struct nvsp_message recvcompMessage;
	int retries = 0;
	int ret;

	DPRINT_DBG(NETVSC, "Sending receive completion pkt - %llx",
		   transaction_id);

	recvcompMessage.Header.MessageType =
				NvspMessage1TypeSendRNDISPacketComplete;

	/* FIXME: Pass in the status */
	recvcompMessage.Messages.Version1Messages.SendRNDISPacketComplete.Status = NvspStatusSuccess;

retry_send_cmplt:
	/* Send the completion */
	ret = vmbus_sendpacket(device->channel, &recvcompMessage,
			       sizeof(struct nvsp_message), transaction_id,
			       VmbusPacketTypeCompletion, 0);
	if (ret == 0) {
		/* success */
		/* no-op */
	} else if (ret == -1) {
		/* no more room...wait a bit and attempt to retry 3 times */
		retries++;
		DPRINT_ERR(NETVSC, "unable to send receive completion pkt "
			   "(tid %llx)...retrying %d", transaction_id, retries);

		if (retries < 4) {
			udelay(100);
			goto retry_send_cmplt;
		} else {
			DPRINT_ERR(NETVSC, "unable to send receive completion "
				  "pkt (tid %llx)...give up retrying",
				  transaction_id);
		}
	} else {
		DPRINT_ERR(NETVSC, "unable to send receive completion pkt - "
			   "%llx", transaction_id);
	}
}

/* Send a receive completion packet to RNDIS device (ie NetVsp) */
static void netvsc_receive_completion(void *context)
{
	struct hv_netvsc_packet *packet = context;
	struct hv_device *device = (struct hv_device *)packet->Device;
	struct netvsc_device *net_device;
	u64 transaction_id = 0;
	bool fsend_receive_comp = false;
	unsigned long flags;

	/* ASSERT(packet->XferPagePacket); */

	/*
	 * Even though it seems logical to do a GetOutboundNetDevice() here to
	 * send out receive completion, we are using GetInboundNetDevice()
	 * since we may have disable outbound traffic already.
	 */
	net_device = get_inbound_net_device(device);
	if (!net_device) {
		DPRINT_ERR(NETVSC, "unable to get net device..."
			   "device being destroyed?");
		return;
	}

	/* Overloading use of the lock. */
	spin_lock_irqsave(&net_device->receive_packet_list_lock, flags);

	/* ASSERT(packet->XferPagePacket->Count > 0); */
	packet->XferPagePacket->Count--;

	/*
	 * Last one in the line that represent 1 xfer page packet.
	 * Return the xfer page packet itself to the freelist
	 */
	if (packet->XferPagePacket->Count == 0) {
		fsend_receive_comp = true;
		transaction_id = packet->Completion.Recv.ReceiveCompletionTid;
		list_add_tail(&packet->XferPagePacket->ListEntry,
			      &net_device->ReceivePacketList);

	}

	/* Put the packet back */
	list_add_tail(&packet->ListEntry, &net_device->ReceivePacketList);
	spin_unlock_irqrestore(&net_device->receive_packet_list_lock, flags);

	/* Send a receive completion for the xfer page packet */
	if (fsend_receive_comp)
		netvsc_send_recv_completion(device, transaction_id);

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

	/* ASSERT(device); */

	packet = kzalloc(NETVSC_PACKET_SIZE * sizeof(unsigned char),
			 GFP_KERNEL);
	if (!packet)
		return;
	buffer = packet;

	net_device = get_inbound_net_device(device);
	if (!net_device) {
		DPRINT_ERR(NETVSC, "net device (%p) shutting down..."
			   "ignoring inbound packets", net_device);
		goto out;
	}

	do {
		ret = vmbus_recvpacket_raw(device->channel, buffer, bufferlen,
					   &bytes_recvd, &request_id);
		if (ret == 0) {
			if (bytes_recvd > 0) {
				DPRINT_DBG(NETVSC, "receive %d bytes, tid %llx",
					   bytes_recvd, request_id);

				desc = (struct vmpacket_descriptor *)buffer;
				switch (desc->Type) {
				case VmbusPacketTypeCompletion:
					netvsc_send_completion(device, desc);
					break;

				case VmbusPacketTypeDataUsingTransferPages:
					netvsc_receive(device, desc);
					break;

				default:
					DPRINT_ERR(NETVSC,
						   "unhandled packet type %d, "
						   "tid %llx len %d\n",
						   desc->Type, request_id,
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
				DPRINT_ERR(NETVSC,
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
