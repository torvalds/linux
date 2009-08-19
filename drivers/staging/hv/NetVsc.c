/*
 *
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
 *   Hank Janssen  <hjanssen@microsoft.com>
 *
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <asm/io.h>
#include "osd.h"
#include "include/logging.h"
#include "NetVsc.h"
#include "RndisFilter.h"


/* Globals */
static const char* gDriverName="netvsc";

/* {F8615163-DF3E-46c5-913F-F2D2F965ED0E} */
static const struct hv_guid gNetVscDeviceType = {
	.data = {
		0x63, 0x51, 0x61, 0xF8, 0x3E, 0xDF, 0xc5, 0x46,
		0x91, 0x3F, 0xF2, 0xD2, 0xF9, 0x65, 0xED, 0x0E
	}
};


/* Internal routines */
static int
NetVscOnDeviceAdd(
	struct hv_device *Device,
	void			*AdditionalInfo
	);

static int
NetVscOnDeviceRemove(
	struct hv_device *Device
	);

static void
NetVscOnCleanup(
	struct hv_driver *Driver
	);

static void
NetVscOnChannelCallback(
	void * context
	);

static int
NetVscInitializeSendBufferWithNetVsp(
	struct hv_device *Device
	);

static int
NetVscInitializeReceiveBufferWithNetVsp(
	struct hv_device *Device
	);

static int
NetVscDestroySendBuffer(
	struct NETVSC_DEVICE	*NetDevice
	);

static int
NetVscDestroyReceiveBuffer(
	struct NETVSC_DEVICE	*NetDevice
	);

static int
NetVscConnectToVsp(
	struct hv_device *Device
	);

static void
NetVscOnSendCompletion(
	struct hv_device *Device,
	VMPACKET_DESCRIPTOR *Packet
	);

static int
NetVscOnSend(
	struct hv_device *Device,
	struct hv_netvsc_packet	*Packet
	);

static void
NetVscOnReceive(
	struct hv_device *Device,
	VMPACKET_DESCRIPTOR *Packet
	);

static void
NetVscOnReceiveCompletion(
	void * Context
	);

static void
NetVscSendReceiveCompletion(
	struct hv_device *Device,
	u64			TransactionId
	);

static inline struct NETVSC_DEVICE *AllocNetDevice(struct hv_device *Device)
{
	struct NETVSC_DEVICE *netDevice;

	netDevice = kzalloc(sizeof(struct NETVSC_DEVICE), GFP_KERNEL);
	if (!netDevice)
		return NULL;

	/* Set to 2 to allow both inbound and outbound traffic */
	atomic_cmpxchg(&netDevice->RefCount, 0, 2);

	netDevice->Device = Device;
	Device->Extension = netDevice;

	return netDevice;
}

static inline void FreeNetDevice(struct NETVSC_DEVICE *Device)
{
	ASSERT(atomic_read(&Device->RefCount) == 0);
	Device->Device->Extension = NULL;
	kfree(Device);
}


/* Get the net device object iff exists and its refcount > 1 */
static inline struct NETVSC_DEVICE *GetOutboundNetDevice(struct hv_device *Device)
{
	struct NETVSC_DEVICE *netDevice;

	netDevice = (struct NETVSC_DEVICE*)Device->Extension;
	if (netDevice && atomic_read(&netDevice->RefCount) > 1)
		atomic_inc(&netDevice->RefCount);
	else
		netDevice = NULL;

	return netDevice;
}

/* Get the net device object iff exists and its refcount > 0 */
static inline struct NETVSC_DEVICE *GetInboundNetDevice(struct hv_device *Device)
{
	struct NETVSC_DEVICE *netDevice;

	netDevice = (struct NETVSC_DEVICE*)Device->Extension;
	if (netDevice && atomic_read(&netDevice->RefCount))
		atomic_inc(&netDevice->RefCount);
	else
		netDevice = NULL;

	return netDevice;
}

static inline void PutNetDevice(struct hv_device *Device)
{
	struct NETVSC_DEVICE *netDevice;

	netDevice = (struct NETVSC_DEVICE*)Device->Extension;
	ASSERT(netDevice);

	atomic_dec(&netDevice->RefCount);
}

static inline struct NETVSC_DEVICE *ReleaseOutboundNetDevice(struct hv_device *Device)
{
	struct NETVSC_DEVICE *netDevice;

	netDevice = (struct NETVSC_DEVICE*)Device->Extension;
	if (netDevice == NULL)
		return NULL;

	/* Busy wait until the ref drop to 2, then set it to 1 */
	while (atomic_cmpxchg(&netDevice->RefCount, 2, 1) != 2)
	{
		udelay(100);
	}

	return netDevice;
}

static inline struct NETVSC_DEVICE *ReleaseInboundNetDevice(struct hv_device *Device)
{
	struct NETVSC_DEVICE *netDevice;

	netDevice = (struct NETVSC_DEVICE*)Device->Extension;
	if (netDevice == NULL)
		return NULL;

	/* Busy wait until the ref drop to 1, then set it to 0 */
	while (atomic_cmpxchg(&netDevice->RefCount, 1, 0) != 1)
	{
		udelay(100);
	}

	Device->Extension = NULL;
	return netDevice;
}

/*++;


Name:
	NetVscInitialize()

Description:
	Main entry point

--*/
int
NetVscInitialize(
	struct hv_driver *drv
	)
{
	NETVSC_DRIVER_OBJECT* driver = (NETVSC_DRIVER_OBJECT*)drv;
	int ret=0;

	DPRINT_ENTER(NETVSC);

	DPRINT_DBG(NETVSC, "sizeof(struct hv_netvsc_packet)=%zd, sizeof(NVSP_MESSAGE)=%zd, sizeof(VMTRANSFER_PAGE_PACKET_HEADER)=%zd",
		sizeof(struct hv_netvsc_packet), sizeof(NVSP_MESSAGE), sizeof(VMTRANSFER_PAGE_PACKET_HEADER));

	/* Make sure we are at least 2 pages since 1 page is used for control */
	ASSERT(driver->RingBufferSize >= (PAGE_SIZE << 1));

	drv->name = gDriverName;
	memcpy(&drv->deviceType, &gNetVscDeviceType, sizeof(struct hv_guid));

	/* Make sure it is set by the caller */
	ASSERT(driver->OnReceiveCallback);
	ASSERT(driver->OnLinkStatusChanged);

	/* Setup the dispatch table */
	driver->Base.OnDeviceAdd		= NetVscOnDeviceAdd;
	driver->Base.OnDeviceRemove		= NetVscOnDeviceRemove;
	driver->Base.OnCleanup			= NetVscOnCleanup;

	driver->OnSend					= NetVscOnSend;

	RndisFilterInit(driver);

	DPRINT_EXIT(NETVSC);

	return ret;
}

static int
NetVscInitializeReceiveBufferWithNetVsp(
	struct hv_device *Device
	)
{
	int ret=0;
	struct NETVSC_DEVICE *netDevice;
	NVSP_MESSAGE *initPacket;

	DPRINT_ENTER(NETVSC);

	netDevice = GetOutboundNetDevice(Device);
	if (!netDevice)
	{
		DPRINT_ERR(NETVSC, "unable to get net device...device being destroyed?");
		DPRINT_EXIT(NETVSC);
		return -1;
	}
	ASSERT(netDevice->ReceiveBufferSize > 0);
	ASSERT((netDevice->ReceiveBufferSize & (PAGE_SIZE-1)) == 0); /* page-size grandularity */

	netDevice->ReceiveBuffer = osd_PageAlloc(netDevice->ReceiveBufferSize >> PAGE_SHIFT);
	if (!netDevice->ReceiveBuffer)
	{
		DPRINT_ERR(NETVSC, "unable to allocate receive buffer of size %d", netDevice->ReceiveBufferSize);
		ret = -1;
		goto Cleanup;
	}
	ASSERT(((unsigned long)netDevice->ReceiveBuffer & (PAGE_SIZE-1)) == 0); /* page-aligned buffer */

	DPRINT_INFO(NETVSC, "Establishing receive buffer's GPADL...");

	/*
	 * Establish the gpadl handle for this buffer on this
	 * channel.  Note: This call uses the vmbus connection rather
	 * than the channel to establish the gpadl handle.
	 */
	ret = Device->Driver->VmbusChannelInterface.EstablishGpadl(Device,
																netDevice->ReceiveBuffer,
																netDevice->ReceiveBufferSize,
																&netDevice->ReceiveBufferGpadlHandle);

	if (ret != 0)
	{
		DPRINT_ERR(NETVSC, "unable to establish receive buffer's gpadl");
		goto Cleanup;
	}

	/* osd_WaitEventWait(ext->ChannelInitEvent); */

	/* Notify the NetVsp of the gpadl handle */
	DPRINT_INFO(NETVSC, "Sending NvspMessage1TypeSendReceiveBuffer...");

	initPacket = &netDevice->ChannelInitPacket;

	memset(initPacket, 0, sizeof(NVSP_MESSAGE));

    initPacket->Header.MessageType = NvspMessage1TypeSendReceiveBuffer;
    initPacket->Messages.Version1Messages.SendReceiveBuffer.GpadlHandle = netDevice->ReceiveBufferGpadlHandle;
    initPacket->Messages.Version1Messages.SendReceiveBuffer.Id = NETVSC_RECEIVE_BUFFER_ID;

	/* Send the gpadl notification request */
	ret = Device->Driver->VmbusChannelInterface.SendPacket(Device,
															initPacket,
															sizeof(NVSP_MESSAGE),
															(unsigned long)initPacket,
															VmbusPacketTypeDataInBand,
															VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	if (ret != 0)
	{
		DPRINT_ERR(NETVSC, "unable to send receive buffer's gpadl to netvsp");
		goto Cleanup;
	}

	osd_WaitEventWait(netDevice->ChannelInitEvent);

	/* Check the response */
	if (initPacket->Messages.Version1Messages.SendReceiveBufferComplete.Status != NvspStatusSuccess)
	{
		DPRINT_ERR(NETVSC,
			"Unable to complete receive buffer initialzation with NetVsp - status %d",
			initPacket->Messages.Version1Messages.SendReceiveBufferComplete.Status);
		ret = -1;
		goto Cleanup;
	}

	/* Parse the response */
	ASSERT(netDevice->ReceiveSectionCount == 0);
	ASSERT(netDevice->ReceiveSections == NULL);

	netDevice->ReceiveSectionCount = initPacket->Messages.Version1Messages.SendReceiveBufferComplete.NumSections;

	netDevice->ReceiveSections = kmalloc(netDevice->ReceiveSectionCount * sizeof(NVSP_1_RECEIVE_BUFFER_SECTION), GFP_KERNEL);
	if (netDevice->ReceiveSections == NULL)
	{
		ret = -1;
		goto Cleanup;
	}

	memcpy(netDevice->ReceiveSections,
		initPacket->Messages.Version1Messages.SendReceiveBufferComplete.Sections,
		netDevice->ReceiveSectionCount * sizeof(NVSP_1_RECEIVE_BUFFER_SECTION));

	DPRINT_INFO(NETVSC,
		"Receive sections info (count %d, offset %d, endoffset %d, suballoc size %d, num suballocs %d)",
		netDevice->ReceiveSectionCount, netDevice->ReceiveSections[0].Offset, netDevice->ReceiveSections[0].EndOffset,
		netDevice->ReceiveSections[0].SubAllocationSize, netDevice->ReceiveSections[0].NumSubAllocations);


	/* For 1st release, there should only be 1 section that represents the entire receive buffer */
	if (netDevice->ReceiveSectionCount != 1 ||
		netDevice->ReceiveSections->Offset != 0 )
	{
		ret = -1;
		goto Cleanup;
	}

	goto Exit;

Cleanup:
	NetVscDestroyReceiveBuffer(netDevice);

Exit:
	PutNetDevice(Device);
	DPRINT_EXIT(NETVSC);
	return ret;
}


static int
NetVscInitializeSendBufferWithNetVsp(
	struct hv_device *Device
	)
{
	int ret=0;
	struct NETVSC_DEVICE *netDevice;
	NVSP_MESSAGE *initPacket;

	DPRINT_ENTER(NETVSC);

	netDevice = GetOutboundNetDevice(Device);
	if (!netDevice)
	{
		DPRINT_ERR(NETVSC, "unable to get net device...device being destroyed?");
		DPRINT_EXIT(NETVSC);
		return -1;
	}
	ASSERT(netDevice->SendBufferSize > 0);
	ASSERT((netDevice->SendBufferSize & (PAGE_SIZE-1)) == 0); /* page-size grandularity */

	netDevice->SendBuffer = osd_PageAlloc(netDevice->SendBufferSize >> PAGE_SHIFT);
	if (!netDevice->SendBuffer)
	{
		DPRINT_ERR(NETVSC, "unable to allocate send buffer of size %d", netDevice->SendBufferSize);
		ret = -1;
		goto Cleanup;
	}
	ASSERT(((unsigned long)netDevice->SendBuffer & (PAGE_SIZE-1)) == 0); /* page-aligned buffer */

	DPRINT_INFO(NETVSC, "Establishing send buffer's GPADL...");

	/*
	 * Establish the gpadl handle for this buffer on this
	 * channel.  Note: This call uses the vmbus connection rather
	 * than the channel to establish the gpadl handle.
	 */
	ret = Device->Driver->VmbusChannelInterface.EstablishGpadl(Device,
								   netDevice->SendBuffer,
								   netDevice->SendBufferSize,
								   &netDevice->SendBufferGpadlHandle);

	if (ret != 0)
	{
		DPRINT_ERR(NETVSC, "unable to establish send buffer's gpadl");
		goto Cleanup;
	}

	/* osd_WaitEventWait(ext->ChannelInitEvent); */

	/* Notify the NetVsp of the gpadl handle */
	DPRINT_INFO(NETVSC, "Sending NvspMessage1TypeSendSendBuffer...");

	initPacket = &netDevice->ChannelInitPacket;

	memset(initPacket, 0, sizeof(NVSP_MESSAGE));

    initPacket->Header.MessageType = NvspMessage1TypeSendSendBuffer;
    initPacket->Messages.Version1Messages.SendReceiveBuffer.GpadlHandle = netDevice->SendBufferGpadlHandle;
    initPacket->Messages.Version1Messages.SendReceiveBuffer.Id = NETVSC_SEND_BUFFER_ID;

	/* Send the gpadl notification request */
	ret = Device->Driver->VmbusChannelInterface.SendPacket(Device,
															initPacket,
															sizeof(NVSP_MESSAGE),
															(unsigned long)initPacket,
															VmbusPacketTypeDataInBand,
															VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	if (ret != 0)
	{
		DPRINT_ERR(NETVSC, "unable to send receive buffer's gpadl to netvsp");
		goto Cleanup;
	}

	osd_WaitEventWait(netDevice->ChannelInitEvent);

	/* Check the response */
	if (initPacket->Messages.Version1Messages.SendSendBufferComplete.Status != NvspStatusSuccess)
	{
		DPRINT_ERR(NETVSC,
			"Unable to complete send buffer initialzation with NetVsp - status %d",
			initPacket->Messages.Version1Messages.SendSendBufferComplete.Status);
		ret = -1;
		goto Cleanup;
	}

	netDevice->SendSectionSize = initPacket->Messages.Version1Messages.SendSendBufferComplete.SectionSize;

	goto Exit;

Cleanup:
	NetVscDestroySendBuffer(netDevice);

Exit:
	PutNetDevice(Device);
	DPRINT_EXIT(NETVSC);
	return ret;
}

static int
NetVscDestroyReceiveBuffer(
	struct NETVSC_DEVICE	*NetDevice
	)
{
	NVSP_MESSAGE *revokePacket;
	int ret=0;


	DPRINT_ENTER(NETVSC);

	/*
	 * If we got a section count, it means we received a
	 * SendReceiveBufferComplete msg (ie sent
	 * NvspMessage1TypeSendReceiveBuffer msg) therefore, we need
	 * to send a revoke msg here
	 */
	if (NetDevice->ReceiveSectionCount)
	{
		DPRINT_INFO(NETVSC, "Sending NvspMessage1TypeRevokeReceiveBuffer...");

		/* Send the revoke receive buffer */
		revokePacket = &NetDevice->RevokePacket;
		memset(revokePacket, 0, sizeof(NVSP_MESSAGE));

		revokePacket->Header.MessageType = NvspMessage1TypeRevokeReceiveBuffer;
		revokePacket->Messages.Version1Messages.RevokeReceiveBuffer.Id = NETVSC_RECEIVE_BUFFER_ID;

		ret = NetDevice->Device->Driver->VmbusChannelInterface.SendPacket(NetDevice->Device,
																			revokePacket,
																			sizeof(NVSP_MESSAGE),
																			(unsigned long)revokePacket,
																			VmbusPacketTypeDataInBand,
																			0);
		/*
		 * If we failed here, we might as well return and
		 * have a leak rather than continue and a bugchk
		 */
		if (ret != 0)
		{
			DPRINT_ERR(NETVSC, "unable to send revoke receive buffer to netvsp");
			DPRINT_EXIT(NETVSC);
			return -1;
		}
	}

	/* Teardown the gpadl on the vsp end */
	if (NetDevice->ReceiveBufferGpadlHandle)
	{
		DPRINT_INFO(NETVSC, "Tearing down receive buffer's GPADL...");

		ret = NetDevice->Device->Driver->VmbusChannelInterface.TeardownGpadl(NetDevice->Device,
																				NetDevice->ReceiveBufferGpadlHandle);

		/* If we failed here, we might as well return and have a leak rather than continue and a bugchk */
		if (ret != 0)
		{
			DPRINT_ERR(NETVSC, "unable to teardown receive buffer's gpadl");
			DPRINT_EXIT(NETVSC);
			return -1;
		}
		NetDevice->ReceiveBufferGpadlHandle = 0;
	}

	if (NetDevice->ReceiveBuffer)
	{
		DPRINT_INFO(NETVSC, "Freeing up receive buffer...");

		/* Free up the receive buffer */
		osd_PageFree(NetDevice->ReceiveBuffer, NetDevice->ReceiveBufferSize >> PAGE_SHIFT);
		NetDevice->ReceiveBuffer = NULL;
	}

	if (NetDevice->ReceiveSections)
	{
		kfree(NetDevice->ReceiveSections);
		NetDevice->ReceiveSections = NULL;
		NetDevice->ReceiveSectionCount = 0;
	}

	DPRINT_EXIT(NETVSC);

	return ret;
}




static int
NetVscDestroySendBuffer(
	struct NETVSC_DEVICE	*NetDevice
	)
{
	NVSP_MESSAGE *revokePacket;
	int ret=0;


	DPRINT_ENTER(NETVSC);

	/*
	 * If we got a section count, it means we received a
	 *  SendReceiveBufferComplete msg (ie sent
	 *  NvspMessage1TypeSendReceiveBuffer msg) therefore, we need
	 *  to send a revoke msg here
	 */
	if (NetDevice->SendSectionSize)
	{
		DPRINT_INFO(NETVSC, "Sending NvspMessage1TypeRevokeSendBuffer...");

		/* Send the revoke send buffer */
		revokePacket = &NetDevice->RevokePacket;
		memset(revokePacket, 0, sizeof(NVSP_MESSAGE));

		revokePacket->Header.MessageType = NvspMessage1TypeRevokeSendBuffer;
		revokePacket->Messages.Version1Messages.RevokeSendBuffer.Id = NETVSC_SEND_BUFFER_ID;

		ret = NetDevice->Device->Driver->VmbusChannelInterface.SendPacket(NetDevice->Device,
																			revokePacket,
																			sizeof(NVSP_MESSAGE),
																			(unsigned long)revokePacket,
																			VmbusPacketTypeDataInBand,
																			0);
		/* If we failed here, we might as well return and have a leak rather than continue and a bugchk */
		if (ret != 0)
		{
			DPRINT_ERR(NETVSC, "unable to send revoke send buffer to netvsp");
			DPRINT_EXIT(NETVSC);
			return -1;
		}
	}

	/* Teardown the gpadl on the vsp end */
	if (NetDevice->SendBufferGpadlHandle)
	{
		DPRINT_INFO(NETVSC, "Tearing down send buffer's GPADL...");

		ret = NetDevice->Device->Driver->VmbusChannelInterface.TeardownGpadl(NetDevice->Device,
																				NetDevice->SendBufferGpadlHandle);

		/* If we failed here, we might as well return and have a leak rather than continue and a bugchk */
		if (ret != 0)
		{
			DPRINT_ERR(NETVSC, "unable to teardown send buffer's gpadl");
			DPRINT_EXIT(NETVSC);
			return -1;
		}
		NetDevice->SendBufferGpadlHandle = 0;
	}

	if (NetDevice->SendBuffer)
	{
		DPRINT_INFO(NETVSC, "Freeing up send buffer...");

		/* Free up the receive buffer */
		osd_PageFree(NetDevice->SendBuffer, NetDevice->SendBufferSize >> PAGE_SHIFT);
		NetDevice->SendBuffer = NULL;
	}

	DPRINT_EXIT(NETVSC);

	return ret;
}



static int
NetVscConnectToVsp(
	struct hv_device *Device
	)
{
	int ret=0;
	struct NETVSC_DEVICE *netDevice;
	NVSP_MESSAGE *initPacket;
	int ndisVersion;

	DPRINT_ENTER(NETVSC);

	netDevice = GetOutboundNetDevice(Device);
	if (!netDevice)
	{
		DPRINT_ERR(NETVSC, "unable to get net device...device being destroyed?");
		DPRINT_EXIT(NETVSC);
		return -1;
	}

	initPacket = &netDevice->ChannelInitPacket;

	memset(initPacket, 0, sizeof(NVSP_MESSAGE));
	initPacket->Header.MessageType = NvspMessageTypeInit;
    initPacket->Messages.InitMessages.Init.MinProtocolVersion = NVSP_MIN_PROTOCOL_VERSION;
    initPacket->Messages.InitMessages.Init.MaxProtocolVersion = NVSP_MAX_PROTOCOL_VERSION;

	DPRINT_INFO(NETVSC, "Sending NvspMessageTypeInit...");

	/* Send the init request */
	ret = Device->Driver->VmbusChannelInterface.SendPacket(Device,
															initPacket,
															sizeof(NVSP_MESSAGE),
															(unsigned long)initPacket,
															VmbusPacketTypeDataInBand,
															VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);

	if( ret != 0)
	{
		DPRINT_ERR(NETVSC, "unable to send NvspMessageTypeInit");
		goto Cleanup;
	}

	osd_WaitEventWait(netDevice->ChannelInitEvent);

	/* Now, check the response */
	/* ASSERT(initPacket->Messages.InitMessages.InitComplete.MaximumMdlChainLength <= MAX_MULTIPAGE_BUFFER_COUNT); */
	DPRINT_INFO(NETVSC, "NvspMessageTypeInit status(%d) max mdl chain (%d)",
		initPacket->Messages.InitMessages.InitComplete.Status,
		initPacket->Messages.InitMessages.InitComplete.MaximumMdlChainLength);

	if (initPacket->Messages.InitMessages.InitComplete.Status != NvspStatusSuccess)
	{
		DPRINT_ERR(NETVSC, "unable to initialize with netvsp (status 0x%x)", initPacket->Messages.InitMessages.InitComplete.Status);
		ret = -1;
		goto Cleanup;
	}

	if (initPacket->Messages.InitMessages.InitComplete.NegotiatedProtocolVersion != NVSP_PROTOCOL_VERSION_1)
	{
		DPRINT_ERR(NETVSC, "unable to initialize with netvsp (version expected 1 got %d)",
			initPacket->Messages.InitMessages.InitComplete.NegotiatedProtocolVersion);
		ret = -1;
		goto Cleanup;
	}
	DPRINT_INFO(NETVSC, "Sending NvspMessage1TypeSendNdisVersion...");

	/* Send the ndis version */
	memset(initPacket, 0, sizeof(NVSP_MESSAGE));

    ndisVersion = 0x00050000;

    initPacket->Header.MessageType = NvspMessage1TypeSendNdisVersion;
    initPacket->Messages.Version1Messages.SendNdisVersion.NdisMajorVersion = (ndisVersion & 0xFFFF0000) >> 16;
    initPacket->Messages.Version1Messages.SendNdisVersion.NdisMinorVersion = ndisVersion & 0xFFFF;

	/* Send the init request */
	ret = Device->Driver->VmbusChannelInterface.SendPacket(Device,
															initPacket,
															sizeof(NVSP_MESSAGE),
															(unsigned long)initPacket,
															VmbusPacketTypeDataInBand,
															0);
	if (ret != 0)
	{
		DPRINT_ERR(NETVSC, "unable to send NvspMessage1TypeSendNdisVersion");
		ret = -1;
		goto Cleanup;
	}
	/*
	 * BUGBUG - We have to wait for the above msg since the
	 * netvsp uses KMCL which acknowledges packet (completion
	 * packet) since our Vmbus always set the
	 * VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED flag
	 */
	 /* osd_WaitEventWait(NetVscChannel->ChannelInitEvent); */

	/* Post the big receive buffer to NetVSP */
	ret = NetVscInitializeReceiveBufferWithNetVsp(Device);
	if (ret == 0)
	{
		ret = NetVscInitializeSendBufferWithNetVsp(Device);
	}

Cleanup:
	PutNetDevice(Device);
	DPRINT_EXIT(NETVSC);
	return ret;
}

static void
NetVscDisconnectFromVsp(
	struct NETVSC_DEVICE	*NetDevice
	)
{
	DPRINT_ENTER(NETVSC);

	NetVscDestroyReceiveBuffer(NetDevice);
	NetVscDestroySendBuffer(NetDevice);

	DPRINT_EXIT(NETVSC);
}


/*++

Name:
	NetVscOnDeviceAdd()

Description:
	Callback when the device belonging to this driver is added

--*/
static int
NetVscOnDeviceAdd(
	struct hv_device *Device,
	void			*AdditionalInfo
	)
{
	int ret=0;
	int i;

	struct NETVSC_DEVICE *netDevice;
	struct hv_netvsc_packet *packet;
	LIST_ENTRY *entry;

	NETVSC_DRIVER_OBJECT *netDriver = (NETVSC_DRIVER_OBJECT*) Device->Driver;;

	DPRINT_ENTER(NETVSC);

	netDevice = AllocNetDevice(Device);
	if (!netDevice)
	{
		ret = -1;
		goto Cleanup;
	}

	DPRINT_DBG(NETVSC, "netvsc channel object allocated - %p", netDevice);

	/* Initialize the NetVSC channel extension */
	netDevice->ReceiveBufferSize = NETVSC_RECEIVE_BUFFER_SIZE;
	spin_lock_init(&netDevice->receive_packet_list_lock);

	netDevice->SendBufferSize = NETVSC_SEND_BUFFER_SIZE;

	INITIALIZE_LIST_HEAD(&netDevice->ReceivePacketList);

	for (i=0; i < NETVSC_RECEIVE_PACKETLIST_COUNT; i++)
	{
		packet = kzalloc(sizeof(struct hv_netvsc_packet) + (NETVSC_RECEIVE_SG_COUNT* sizeof(PAGE_BUFFER)), GFP_KERNEL);
		if (!packet)
		{
			DPRINT_DBG(NETVSC, "unable to allocate netvsc pkts for receive pool (wanted %d got %d)", NETVSC_RECEIVE_PACKETLIST_COUNT, i);
			break;
		}

		INSERT_TAIL_LIST(&netDevice->ReceivePacketList, &packet->ListEntry);
	}
	netDevice->ChannelInitEvent = osd_WaitEventCreate();

	/* Open the channel */
	ret = Device->Driver->VmbusChannelInterface.Open(Device,
							 netDriver->RingBufferSize,
							 netDriver->RingBufferSize,
							 NULL, 0,
							 NetVscOnChannelCallback,
							 Device
							 );

	if (ret != 0)
	{
		DPRINT_ERR(NETVSC, "unable to open channel: %d", ret);
		ret = -1;
		goto Cleanup;
	}

	/* Channel is opened */
	DPRINT_INFO(NETVSC, "*** NetVSC channel opened successfully! ***");

	/* Connect with the NetVsp */
	ret = NetVscConnectToVsp(Device);
	if (ret != 0)
	{
		DPRINT_ERR(NETVSC, "unable to connect to NetVSP - %d", ret);
		ret = -1;
		goto Close;
	}

	DPRINT_INFO(NETVSC, "*** NetVSC channel handshake result - %d ***", ret);

	DPRINT_EXIT(NETVSC);
	return ret;

Close:
	/* Now, we can close the channel safely */
	Device->Driver->VmbusChannelInterface.Close(Device);

Cleanup:

	if (netDevice)
	{
		kfree(netDevice->ChannelInitEvent);

		while (!IsListEmpty(&netDevice->ReceivePacketList))
		{
			entry = REMOVE_HEAD_LIST(&netDevice->ReceivePacketList);
			packet = CONTAINING_RECORD(entry, struct hv_netvsc_packet, ListEntry);
			kfree(packet);
		}

		ReleaseOutboundNetDevice(Device);
		ReleaseInboundNetDevice(Device);

		FreeNetDevice(netDevice);
	}

	DPRINT_EXIT(NETVSC);
	return ret;
}


/*++

Name:
	NetVscOnDeviceRemove()

Description:
	Callback when the root bus device is removed

--*/
static int
NetVscOnDeviceRemove(
	struct hv_device *Device
	)
{
	struct NETVSC_DEVICE *netDevice;
	struct hv_netvsc_packet *netvscPacket;
	int ret=0;
	LIST_ENTRY *entry;

	DPRINT_ENTER(NETVSC);

	DPRINT_INFO(NETVSC, "Disabling outbound traffic on net device (%p)...", Device->Extension);

	/* Stop outbound traffic ie sends and receives completions */
	netDevice = ReleaseOutboundNetDevice(Device);
	if (!netDevice)
	{
		DPRINT_ERR(NETVSC, "No net device present!!");
		return -1;
	}

	/* Wait for all send completions */
	while (atomic_read(&netDevice->NumOutstandingSends))
	{
		DPRINT_INFO(NETVSC, "waiting for %d requests to complete...", atomic_read(&netDevice->NumOutstandingSends));

		udelay(100);
	}

	DPRINT_INFO(NETVSC, "Disconnecting from netvsp...");

	NetVscDisconnectFromVsp(netDevice);

	DPRINT_INFO(NETVSC, "Disabling inbound traffic on net device (%p)...", Device->Extension);

	/* Stop inbound traffic ie receives and sends completions */
	netDevice = ReleaseInboundNetDevice(Device);

	/* At this point, no one should be accessing netDevice except in here */
	DPRINT_INFO(NETVSC, "net device (%p) safe to remove", netDevice);

	/* Now, we can close the channel safely */
	Device->Driver->VmbusChannelInterface.Close(Device);

	/* Release all resources */
	while (!IsListEmpty(&netDevice->ReceivePacketList))
	{
		entry = REMOVE_HEAD_LIST(&netDevice->ReceivePacketList);
		netvscPacket = CONTAINING_RECORD(entry, struct hv_netvsc_packet, ListEntry);

		kfree(netvscPacket);
	}

	kfree(netDevice->ChannelInitEvent);
	FreeNetDevice(netDevice);

	DPRINT_EXIT(NETVSC);
	return ret;
}



/*++

Name:
	NetVscOnCleanup()

Description:
	Perform any cleanup when the driver is removed

--*/
static void
NetVscOnCleanup(
	struct hv_driver *drv
	)
{
	DPRINT_ENTER(NETVSC);

	DPRINT_EXIT(NETVSC);
}

static void
NetVscOnSendCompletion(
	struct hv_device *Device,
	VMPACKET_DESCRIPTOR *Packet
	)
{
	struct NETVSC_DEVICE *netDevice;
	NVSP_MESSAGE *nvspPacket;
	struct hv_netvsc_packet *nvscPacket;

	DPRINT_ENTER(NETVSC);

	netDevice = GetInboundNetDevice(Device);
	if (!netDevice)
	{
		DPRINT_ERR(NETVSC, "unable to get net device...device being destroyed?");
		DPRINT_EXIT(NETVSC);
		return;
	}

	nvspPacket = (NVSP_MESSAGE*)((unsigned long)Packet + (Packet->DataOffset8 << 3));

	DPRINT_DBG(NETVSC, "send completion packet - type %d", nvspPacket->Header.MessageType);

	if (nvspPacket->Header.MessageType == NvspMessageTypeInitComplete ||
		nvspPacket->Header.MessageType == NvspMessage1TypeSendReceiveBufferComplete ||
		nvspPacket->Header.MessageType == NvspMessage1TypeSendSendBufferComplete)
	{
		/* Copy the response back */
		memcpy(&netDevice->ChannelInitPacket, nvspPacket, sizeof(NVSP_MESSAGE));
		osd_WaitEventSet(netDevice->ChannelInitEvent);
	}
	else if (nvspPacket->Header.MessageType == NvspMessage1TypeSendRNDISPacketComplete)
	{
		/* Get the send context */
		nvscPacket = (struct hv_netvsc_packet *)(unsigned long)Packet->TransactionId;
		ASSERT(nvscPacket);

		/* Notify the layer above us */
		nvscPacket->Completion.Send.OnSendCompletion(nvscPacket->Completion.Send.SendCompletionContext);

		atomic_dec(&netDevice->NumOutstandingSends);
	}
	else
	{
		DPRINT_ERR(NETVSC, "Unknown send completion packet type - %d received!!", nvspPacket->Header.MessageType);
	}

	PutNetDevice(Device);
	DPRINT_EXIT(NETVSC);
}



static int
NetVscOnSend(
	struct hv_device *Device,
	struct hv_netvsc_packet *Packet
	)
{
	struct NETVSC_DEVICE *netDevice;
	int ret=0;

	NVSP_MESSAGE sendMessage;

	DPRINT_ENTER(NETVSC);

	netDevice = GetOutboundNetDevice(Device);
	if (!netDevice)
	{
		DPRINT_ERR(NETVSC, "net device (%p) shutting down...ignoring outbound packets", netDevice);
		DPRINT_EXIT(NETVSC);
		return -2;
	}

	sendMessage.Header.MessageType = NvspMessage1TypeSendRNDISPacket;
	if (Packet->IsDataPacket)
	    sendMessage.Messages.Version1Messages.SendRNDISPacket.ChannelType = 0;/* 0 is RMC_DATA; */
	else
		sendMessage.Messages.Version1Messages.SendRNDISPacket.ChannelType = 1;/* 1 is RMC_CONTROL; */

	/* Not using send buffer section */
    sendMessage.Messages.Version1Messages.SendRNDISPacket.SendBufferSectionIndex = 0xFFFFFFFF;
    sendMessage.Messages.Version1Messages.SendRNDISPacket.SendBufferSectionSize = 0;

	if (Packet->PageBufferCount)
	{
		ret = Device->Driver->VmbusChannelInterface.SendPacketPageBuffer(Device,
																			Packet->PageBuffers,
																			Packet->PageBufferCount,
																			&sendMessage,
																			sizeof(NVSP_MESSAGE),
																			(unsigned long)Packet);
	}
	else
	{
		ret = Device->Driver->VmbusChannelInterface.SendPacket(Device,
																&sendMessage,
																sizeof(NVSP_MESSAGE),
																(unsigned long)Packet,
																VmbusPacketTypeDataInBand,
																VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);

	}

	if (ret != 0)
	{
		DPRINT_ERR(NETVSC, "Unable to send packet %p ret %d", Packet, ret);
	}

	atomic_inc(&netDevice->NumOutstandingSends);
	PutNetDevice(Device);

	DPRINT_EXIT(NETVSC);
	return ret;
}


static void
NetVscOnReceive(
	struct hv_device *Device,
	VMPACKET_DESCRIPTOR *Packet
	)
{
	struct NETVSC_DEVICE *netDevice;
	VMTRANSFER_PAGE_PACKET_HEADER *vmxferpagePacket;
	NVSP_MESSAGE *nvspPacket;
	struct hv_netvsc_packet *netvscPacket=NULL;
	LIST_ENTRY* entry;
	unsigned long start;
	unsigned long end, endVirtual;
	/* NETVSC_DRIVER_OBJECT *netvscDriver; */
	XFERPAGE_PACKET *xferpagePacket=NULL;
	LIST_ENTRY listHead;

	int i=0, j=0;
	int count=0, bytesRemain=0;
	unsigned long flags;

	DPRINT_ENTER(NETVSC);

	netDevice = GetInboundNetDevice(Device);
	if (!netDevice)
	{
		DPRINT_ERR(NETVSC, "unable to get net device...device being destroyed?");
		DPRINT_EXIT(NETVSC);
		return;
	}

	/* All inbound packets other than send completion should be xfer page packet */
	if (Packet->Type != VmbusPacketTypeDataUsingTransferPages)
	{
		DPRINT_ERR(NETVSC, "Unknown packet type received - %d", Packet->Type);
		PutNetDevice(Device);
		return;
	}

	nvspPacket = (NVSP_MESSAGE*)((unsigned long)Packet + (Packet->DataOffset8 << 3));

	/* Make sure this is a valid nvsp packet */
	if (nvspPacket->Header.MessageType != NvspMessage1TypeSendRNDISPacket )
	{
		DPRINT_ERR(NETVSC, "Unknown nvsp packet type received - %d", nvspPacket->Header.MessageType);
		PutNetDevice(Device);
		return;
	}

	DPRINT_DBG(NETVSC, "NVSP packet received - type %d", nvspPacket->Header.MessageType);

	vmxferpagePacket = (VMTRANSFER_PAGE_PACKET_HEADER*)Packet;

	if (vmxferpagePacket->TransferPageSetId != NETVSC_RECEIVE_BUFFER_ID)
	{
		DPRINT_ERR(NETVSC, "Invalid xfer page set id - expecting %x got %x", NETVSC_RECEIVE_BUFFER_ID, vmxferpagePacket->TransferPageSetId);
		PutNetDevice(Device);
		return;
	}

	DPRINT_DBG(NETVSC, "xfer page - range count %d", vmxferpagePacket->RangeCount);

	INITIALIZE_LIST_HEAD(&listHead);

	/*
	 * Grab free packets (range count + 1) to represent this xfer
	 * page packet. +1 to represent the xfer page packet itself.
	 * We grab it here so that we know exactly how many we can
	 * fulfil
	 */
	spin_lock_irqsave(&netDevice->receive_packet_list_lock, flags);
	while (!IsListEmpty(&netDevice->ReceivePacketList))
	{
		entry = REMOVE_HEAD_LIST(&netDevice->ReceivePacketList);
		netvscPacket = CONTAINING_RECORD(entry, struct hv_netvsc_packet, ListEntry);

		INSERT_TAIL_LIST(&listHead, &netvscPacket->ListEntry);

		if (++count == vmxferpagePacket->RangeCount + 1)
			break;
	}
	spin_unlock_irqrestore(&netDevice->receive_packet_list_lock, flags);

	/*
	 * We need at least 2 netvsc pkts (1 to represent the xfer
	 * page and at least 1 for the range) i.e. we can handled
	 * some of the xfer page packet ranges...
	 */
	if (count < 2)
	{
		DPRINT_ERR(NETVSC, "Got only %d netvsc pkt...needed %d pkts. Dropping this xfer page packet completely!", count, vmxferpagePacket->RangeCount+1);

		/* Return it to the freelist */
		spin_lock_irqsave(&netDevice->receive_packet_list_lock, flags);
		for (i=count; i != 0; i--)
		{
			entry = REMOVE_HEAD_LIST(&listHead);
			netvscPacket = CONTAINING_RECORD(entry, struct hv_netvsc_packet, ListEntry);

			INSERT_TAIL_LIST(&netDevice->ReceivePacketList, &netvscPacket->ListEntry);
		}
		spin_unlock_irqrestore(&netDevice->receive_packet_list_lock, flags);

		NetVscSendReceiveCompletion(Device, vmxferpagePacket->d.TransactionId);

		PutNetDevice(Device);
		return;
	}

	/* Remove the 1st packet to represent the xfer page packet itself */
	entry = REMOVE_HEAD_LIST(&listHead);
	xferpagePacket = CONTAINING_RECORD(entry, XFERPAGE_PACKET, ListEntry);
	xferpagePacket->Count = count - 1; /* This is how much we can satisfy */
	ASSERT(xferpagePacket->Count > 0 && xferpagePacket->Count <= vmxferpagePacket->RangeCount);

	if (xferpagePacket->Count != vmxferpagePacket->RangeCount)
	{
		DPRINT_INFO(NETVSC, "Needed %d netvsc pkts to satisy this xfer page...got %d", vmxferpagePacket->RangeCount, xferpagePacket->Count);
	}

	/* Each range represents 1 RNDIS pkt that contains 1 ethernet frame */
	for (i=0; i < (count - 1); i++)
	{
		entry = REMOVE_HEAD_LIST(&listHead);
		netvscPacket = CONTAINING_RECORD(entry, struct hv_netvsc_packet, ListEntry);

		/* Initialize the netvsc packet */
		netvscPacket->XferPagePacket = xferpagePacket;
		netvscPacket->Completion.Recv.OnReceiveCompletion = NetVscOnReceiveCompletion;
		netvscPacket->Completion.Recv.ReceiveCompletionContext = netvscPacket;
		netvscPacket->Device = Device;
		netvscPacket->Completion.Recv.ReceiveCompletionTid = vmxferpagePacket->d.TransactionId; /* Save this so that we can send it back */

		netvscPacket->TotalDataBufferLength = vmxferpagePacket->Ranges[i].ByteCount;
		netvscPacket->PageBufferCount = 1;

		ASSERT(vmxferpagePacket->Ranges[i].ByteOffset + vmxferpagePacket->Ranges[i].ByteCount < netDevice->ReceiveBufferSize);

		netvscPacket->PageBuffers[0].Length = vmxferpagePacket->Ranges[i].ByteCount;

		start = virt_to_phys((void*)((unsigned long)netDevice->ReceiveBuffer + vmxferpagePacket->Ranges[i].ByteOffset));

		netvscPacket->PageBuffers[0].Pfn = start >> PAGE_SHIFT;
		endVirtual = (unsigned long)netDevice->ReceiveBuffer
		    + vmxferpagePacket->Ranges[i].ByteOffset
		    + vmxferpagePacket->Ranges[i].ByteCount -1;
		end = virt_to_phys((void*)endVirtual);

		/* Calculate the page relative offset */
		netvscPacket->PageBuffers[0].Offset = vmxferpagePacket->Ranges[i].ByteOffset & (PAGE_SIZE -1);
		if ((end >> PAGE_SHIFT) != (start>>PAGE_SHIFT)) {
		    /* Handle frame across multiple pages: */
		    netvscPacket->PageBuffers[0].Length =
			(netvscPacket->PageBuffers[0].Pfn <<PAGE_SHIFT) + PAGE_SIZE - start;
		    bytesRemain = netvscPacket->TotalDataBufferLength - netvscPacket->PageBuffers[0].Length;
		    for (j=1; j<NETVSC_PACKET_MAXPAGE; j++) {
			netvscPacket->PageBuffers[j].Offset = 0;
			if (bytesRemain <= PAGE_SIZE) {
			    netvscPacket->PageBuffers[j].Length = bytesRemain;
			    bytesRemain = 0;
			} else {
			    netvscPacket->PageBuffers[j].Length = PAGE_SIZE;
			    bytesRemain -= PAGE_SIZE;
			}
			netvscPacket->PageBuffers[j].Pfn =
			    virt_to_phys((void*)(endVirtual - bytesRemain)) >> PAGE_SHIFT;
			netvscPacket->PageBufferCount++;
			if (bytesRemain == 0)
			    break;
		    }
		    ASSERT(bytesRemain == 0);
		}
		DPRINT_DBG(NETVSC, "[%d] - (abs offset %u len %u) => (pfn %llx, offset %u, len %u)",
			i,
			vmxferpagePacket->Ranges[i].ByteOffset,
			vmxferpagePacket->Ranges[i].ByteCount,
			netvscPacket->PageBuffers[0].Pfn,
			netvscPacket->PageBuffers[0].Offset,
			netvscPacket->PageBuffers[0].Length);

		/* Pass it to the upper layer */
		((NETVSC_DRIVER_OBJECT*)Device->Driver)->OnReceiveCallback(Device, netvscPacket);

		NetVscOnReceiveCompletion(netvscPacket->Completion.Recv.ReceiveCompletionContext);
	}

	ASSERT(IsListEmpty(&listHead));

	PutNetDevice(Device);
	DPRINT_EXIT(NETVSC);
}


static void
NetVscSendReceiveCompletion(
	struct hv_device *Device,
	u64			TransactionId
	)
{
	NVSP_MESSAGE recvcompMessage;
	int retries=0;
	int ret=0;

	DPRINT_DBG(NETVSC, "Sending receive completion pkt - %llx", TransactionId);

	recvcompMessage.Header.MessageType = NvspMessage1TypeSendRNDISPacketComplete;

	/* FIXME: Pass in the status */
	recvcompMessage.Messages.Version1Messages.SendRNDISPacketComplete.Status = NvspStatusSuccess;

retry_send_cmplt:
	/* Send the completion */
	ret = Device->Driver->VmbusChannelInterface.SendPacket(Device,
															&recvcompMessage,
															sizeof(NVSP_MESSAGE),
															TransactionId,
															VmbusPacketTypeCompletion,
															0);
	if (ret == 0) /* success */
	{
		/* no-op */
	}
	else if (ret == -1) /* no more room...wait a bit and attempt to retry 3 times */
	{
		retries++;
		DPRINT_ERR(NETVSC, "unable to send receive completion pkt (tid %llx)...retrying %d", TransactionId, retries);

		if (retries < 4)
		{
			udelay(100);
			goto retry_send_cmplt;
		}
		else
		{
			DPRINT_ERR(NETVSC, "unable to send receive completion pkt (tid %llx)...give up retrying", TransactionId);
		}
	}
	else
	{
		DPRINT_ERR(NETVSC, "unable to send receive completion pkt - %llx", TransactionId);
	}
}

/* Send a receive completion packet to RNDIS device (ie NetVsp) */
static void
NetVscOnReceiveCompletion(
	void * Context)
{
	struct hv_netvsc_packet *packet = (struct hv_netvsc_packet*)Context;
	struct hv_device *device = (struct hv_device*)packet->Device;
	struct NETVSC_DEVICE *netDevice;
	u64	transactionId=0;
	bool fSendReceiveComp = false;
	unsigned long flags;

	DPRINT_ENTER(NETVSC);

	ASSERT(packet->XferPagePacket);

	/* Even though it seems logical to do a GetOutboundNetDevice() here to send out receive completion, */
	/* we are using GetInboundNetDevice() since we may have disable outbound traffic already. */
	netDevice = GetInboundNetDevice(device);
	if (!netDevice)
	{
		DPRINT_ERR(NETVSC, "unable to get net device...device being destroyed?");
		DPRINT_EXIT(NETVSC);
		return;
	}

	/* Overloading use of the lock. */
	spin_lock_irqsave(&netDevice->receive_packet_list_lock, flags);

	ASSERT(packet->XferPagePacket->Count > 0);
	packet->XferPagePacket->Count--;

	/* Last one in the line that represent 1 xfer page packet. */
	/* Return the xfer page packet itself to the freelist */
	if (packet->XferPagePacket->Count == 0)
	{
		fSendReceiveComp = true;
		transactionId = packet->Completion.Recv.ReceiveCompletionTid;

		INSERT_TAIL_LIST(&netDevice->ReceivePacketList, &packet->XferPagePacket->ListEntry);
	}

	/* Put the packet back */
	INSERT_TAIL_LIST(&netDevice->ReceivePacketList, &packet->ListEntry);
	spin_unlock_irqrestore(&netDevice->receive_packet_list_lock, flags);

	/* Send a receive completion for the xfer page packet */
	if (fSendReceiveComp)
	{
		NetVscSendReceiveCompletion(device, transactionId);
	}

	PutNetDevice(device);
	DPRINT_EXIT(NETVSC);
}



void
NetVscOnChannelCallback(
	void * Context
	)
{
	const int netPacketSize=2048;
	int ret=0;
	struct hv_device *device=(struct hv_device*)Context;
	struct NETVSC_DEVICE *netDevice;

	u32 bytesRecvd;
	u64 requestId;
	unsigned char packet[netPacketSize];
	VMPACKET_DESCRIPTOR *desc;
	unsigned char	*buffer=packet;
	int		bufferlen=netPacketSize;


	DPRINT_ENTER(NETVSC);

	ASSERT(device);

	netDevice = GetInboundNetDevice(device);
	if (!netDevice)
	{
		DPRINT_ERR(NETVSC, "net device (%p) shutting down...ignoring inbound packets", netDevice);
		DPRINT_EXIT(NETVSC);
		return;
	}

	do
	{
		ret = device->Driver->VmbusChannelInterface.RecvPacketRaw(device,
																	buffer,
																	bufferlen,
																	&bytesRecvd,
																	&requestId);

		if (ret == 0)
		{
			if (bytesRecvd > 0)
			{
				DPRINT_DBG(NETVSC, "receive %d bytes, tid %llx", bytesRecvd, requestId);

				desc = (VMPACKET_DESCRIPTOR*)buffer;
				switch (desc->Type)
				{
					case VmbusPacketTypeCompletion:
						NetVscOnSendCompletion(device, desc);
						break;

					case VmbusPacketTypeDataUsingTransferPages:
						NetVscOnReceive(device, desc);
						break;

					default:
						DPRINT_ERR(NETVSC, "unhandled packet type %d, tid %llx len %d\n", desc->Type, requestId, bytesRecvd);
						break;
				}

				/* reset */
				if (bufferlen > netPacketSize)
				{
					kfree(buffer);

					buffer = packet;
					bufferlen = netPacketSize;
				}
			}
			else
			{
				/* DPRINT_DBG(NETVSC, "nothing else to read..."); */

				/* reset */
				if (bufferlen > netPacketSize)
				{
					kfree(buffer);

					buffer = packet;
					bufferlen = netPacketSize;
				}

				break;
			}
		}
		else if (ret == -2) /* Handle large packet */
		{
			buffer = kmalloc(bytesRecvd, GFP_ATOMIC);
			if (buffer == NULL)
			{
				/* Try again next time around */
				DPRINT_ERR(NETVSC, "unable to allocate buffer of size (%d)!!", bytesRecvd);
				break;
			}

			bufferlen = bytesRecvd;
		}
		else
		{
			ASSERT(0);
		}
	} while (1);

	PutNetDevice(device);
	DPRINT_EXIT(NETVSC);
	return;
}
