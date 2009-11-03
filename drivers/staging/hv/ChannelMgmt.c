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
#include <linux/list.h>
#include "osd.h"
#include "logging.h"
#include "VmbusPrivate.h"

struct vmbus_channel_message_table_entry {
	enum vmbus_channel_message_type messageType;
	void (*messageHandler)(struct vmbus_channel_message_header *msg);
};

#define MAX_NUM_DEVICE_CLASSES_SUPPORTED 4
static const struct hv_guid
		gSupportedDeviceClasses[MAX_NUM_DEVICE_CLASSES_SUPPORTED] = {
	/* {ba6163d9-04a1-4d29-b605-72e2ffb1dc7f} */
	/* Storage - SCSI */
	{
		.data  = {
			0xd9, 0x63, 0x61, 0xba, 0xa1, 0x04, 0x29, 0x4d,
			0xb6, 0x05, 0x72, 0xe2, 0xff, 0xb1, 0xdc, 0x7f
		}
	},

	/* {F8615163-DF3E-46c5-913F-F2D2F965ED0E} */
	/* Network */
	{
		.data = {
			0x63, 0x51, 0x61, 0xF8, 0x3E, 0xDF, 0xc5, 0x46,
			0x91, 0x3F, 0xF2, 0xD2, 0xF9, 0x65, 0xED, 0x0E
		}
	},

	/* {CFA8B69E-5B4A-4cc0-B98B-8BA1A1F3F95A} */
	/* Input */
	{
		.data = {
			0x9E, 0xB6, 0xA8, 0xCF, 0x4A, 0x5B, 0xc0, 0x4c,
			0xB9, 0x8B, 0x8B, 0xA1, 0xA1, 0xF3, 0xF9, 0x5A
		}
	},

	/* {32412632-86cb-44a2-9b5c-50d1417354f5} */
	/* IDE */
	{
		.data = {
			0x32, 0x26, 0x41, 0x32, 0xcb, 0x86, 0xa2, 0x44,
			0x9b, 0x5c, 0x50, 0xd1, 0x41, 0x73, 0x54, 0xf5
		}
	},
};

/**
 * AllocVmbusChannel - Allocate and initialize a vmbus channel object
 */
struct vmbus_channel *AllocVmbusChannel(void)
{
	struct vmbus_channel *channel;

	channel = kzalloc(sizeof(*channel), GFP_ATOMIC);
	if (!channel)
		return NULL;

	spin_lock_init(&channel->inbound_lock);

	init_timer(&channel->poll_timer);
	channel->poll_timer.data = (unsigned long)channel;
	channel->poll_timer.function = VmbusChannelOnTimer;

	channel->ControlWQ = create_workqueue("hv_vmbus_ctl");
	if (!channel->ControlWQ) {
		kfree(channel);
		return NULL;
	}

	return channel;
}

/**
 * ReleaseVmbusChannel - Release the vmbus channel object itself
 */
static inline void ReleaseVmbusChannel(void *context)
{
	struct vmbus_channel *channel = context;

	DPRINT_ENTER(VMBUS);

	DPRINT_DBG(VMBUS, "releasing channel (%p)", channel);
	destroy_workqueue(channel->ControlWQ);
	DPRINT_DBG(VMBUS, "channel released (%p)", channel);

	kfree(channel);

	DPRINT_EXIT(VMBUS);
}

/**
 * FreeVmbusChannel - Release the resources used by the vmbus channel object
 */
void FreeVmbusChannel(struct vmbus_channel *Channel)
{
	del_timer(&Channel->poll_timer);

	/*
	 * We have to release the channel's workqueue/thread in the vmbus's
	 * workqueue/thread context
	 * ie we can't destroy ourselves.
	 */
	osd_schedule_callback(gVmbusConnection.WorkQueue, ReleaseVmbusChannel,
			      Channel);
}

/**
 * VmbusChannelProcessOffer - Process the offer by creating a channel/device associated with this offer
 */
static void VmbusChannelProcessOffer(void *context)
{
	struct vmbus_channel *newChannel = context;
	struct vmbus_channel *channel;
	bool fNew = true;
	int ret;
	unsigned long flags;

	DPRINT_ENTER(VMBUS);

	/* Make sure this is a new offer */
	spin_lock_irqsave(&gVmbusConnection.channel_lock, flags);

	list_for_each_entry(channel, &gVmbusConnection.ChannelList, ListEntry) {
		if (!memcmp(&channel->OfferMsg.Offer.InterfaceType,
			    &newChannel->OfferMsg.Offer.InterfaceType,
			    sizeof(struct hv_guid)) &&
		    !memcmp(&channel->OfferMsg.Offer.InterfaceInstance,
			    &newChannel->OfferMsg.Offer.InterfaceInstance,
			    sizeof(struct hv_guid))) {
			fNew = false;
			break;
		}
	}

	if (fNew)
		list_add_tail(&newChannel->ListEntry,
			      &gVmbusConnection.ChannelList);

	spin_unlock_irqrestore(&gVmbusConnection.channel_lock, flags);

	if (!fNew) {
		DPRINT_DBG(VMBUS, "Ignoring duplicate offer for relid (%d)",
			   newChannel->OfferMsg.ChildRelId);
		FreeVmbusChannel(newChannel);
		DPRINT_EXIT(VMBUS);
		return;
	}

	/*
	 * Start the process of binding this offer to the driver
	 * We need to set the DeviceObject field before calling
	 * VmbusChildDeviceAdd()
	 */
	newChannel->DeviceObject = VmbusChildDeviceCreate(
		&newChannel->OfferMsg.Offer.InterfaceType,
		&newChannel->OfferMsg.Offer.InterfaceInstance,
		newChannel);

	DPRINT_DBG(VMBUS, "child device object allocated - %p",
		   newChannel->DeviceObject);

	/*
	 * Add the new device to the bus. This will kick off device-driver
	 * binding which eventually invokes the device driver's AddDevice()
	 * method.
	 */
	ret = VmbusChildDeviceAdd(newChannel->DeviceObject);
	if (ret != 0) {
		DPRINT_ERR(VMBUS,
			   "unable to add child device object (relid %d)",
			   newChannel->OfferMsg.ChildRelId);

		spin_lock_irqsave(&gVmbusConnection.channel_lock, flags);
		list_del(&newChannel->ListEntry);
		spin_unlock_irqrestore(&gVmbusConnection.channel_lock, flags);

		FreeVmbusChannel(newChannel);
	} else {
		/*
		 * This state is used to indicate a successful open
		 * so that when we do close the channel normally, we
		 * can cleanup properly
		 */
		newChannel->State = CHANNEL_OPEN_STATE;
	}
	DPRINT_EXIT(VMBUS);
}

/**
 * VmbusChannelProcessRescindOffer - Rescind the offer by initiating a device removal
 */
static void VmbusChannelProcessRescindOffer(void *context)
{
	struct vmbus_channel *channel = context;

	DPRINT_ENTER(VMBUS);
	VmbusChildDeviceRemove(channel->DeviceObject);
	DPRINT_EXIT(VMBUS);
}

/**
 * VmbusChannelOnOffer - Handler for channel offers from vmbus in parent partition.
 *
 * We ignore all offers except network and storage offers. For each network and
 * storage offers, we create a channel object and queue a work item to the
 * channel object to process the offer synchronously
 */
static void VmbusChannelOnOffer(struct vmbus_channel_message_header *hdr)
{
	struct vmbus_channel_offer_channel *offer;
	struct vmbus_channel *newChannel;
	struct hv_guid *guidType;
	struct hv_guid *guidInstance;
	int i;
	int fSupported = 0;

	DPRINT_ENTER(VMBUS);

	offer = (struct vmbus_channel_offer_channel *)hdr;
	for (i = 0; i < MAX_NUM_DEVICE_CLASSES_SUPPORTED; i++) {
		if (memcmp(&offer->Offer.InterfaceType,
		    &gSupportedDeviceClasses[i], sizeof(struct hv_guid)) == 0) {
			fSupported = 1;
			break;
		}
	}

	if (!fSupported) {
		DPRINT_DBG(VMBUS, "Ignoring channel offer notification for "
			   "child relid %d", offer->ChildRelId);
		DPRINT_EXIT(VMBUS);
		return;
	}

	guidType = &offer->Offer.InterfaceType;
	guidInstance = &offer->Offer.InterfaceInstance;

	DPRINT_INFO(VMBUS, "Channel offer notification - "
		    "child relid %d monitor id %d allocated %d, "
		    "type {%02x%02x%02x%02x-%02x%02x-%02x%02x-"
		    "%02x%02x%02x%02x%02x%02x%02x%02x} "
		    "instance {%02x%02x%02x%02x-%02x%02x-%02x%02x-"
		    "%02x%02x%02x%02x%02x%02x%02x%02x}",
		    offer->ChildRelId, offer->MonitorId,
		    offer->MonitorAllocated,
		    guidType->data[3], guidType->data[2],
		    guidType->data[1], guidType->data[0],
		    guidType->data[5], guidType->data[4],
		    guidType->data[7], guidType->data[6],
		    guidType->data[8], guidType->data[9],
		    guidType->data[10], guidType->data[11],
		    guidType->data[12], guidType->data[13],
		    guidType->data[14], guidType->data[15],
		    guidInstance->data[3], guidInstance->data[2],
		    guidInstance->data[1], guidInstance->data[0],
		    guidInstance->data[5], guidInstance->data[4],
		    guidInstance->data[7], guidInstance->data[6],
		    guidInstance->data[8], guidInstance->data[9],
		    guidInstance->data[10], guidInstance->data[11],
		    guidInstance->data[12], guidInstance->data[13],
		    guidInstance->data[14], guidInstance->data[15]);

	/* Allocate the channel object and save this offer. */
	newChannel = AllocVmbusChannel();
	if (!newChannel) {
		DPRINT_ERR(VMBUS, "unable to allocate channel object");
		return;
	}

	DPRINT_DBG(VMBUS, "channel object allocated - %p", newChannel);

	memcpy(&newChannel->OfferMsg, offer,
	       sizeof(struct vmbus_channel_offer_channel));
	newChannel->MonitorGroup = (u8)offer->MonitorId / 32;
	newChannel->MonitorBit = (u8)offer->MonitorId % 32;

	/* TODO: Make sure the offer comes from our parent partition */
	osd_schedule_callback(newChannel->ControlWQ, VmbusChannelProcessOffer,
			      newChannel);

	DPRINT_EXIT(VMBUS);
}

/**
 * VmbusChannelOnOfferRescind - Rescind offer handler.
 *
 * We queue a work item to process this offer synchronously
 */
static void VmbusChannelOnOfferRescind(struct vmbus_channel_message_header *hdr)
{
	struct vmbus_channel_rescind_offer *rescind;
	struct vmbus_channel *channel;

	DPRINT_ENTER(VMBUS);

	rescind = (struct vmbus_channel_rescind_offer *)hdr;
	channel = GetChannelFromRelId(rescind->ChildRelId);
	if (channel == NULL) {
		DPRINT_DBG(VMBUS, "channel not found for relId %d",
			   rescind->ChildRelId);
		return;
	}

	osd_schedule_callback(channel->ControlWQ,
			      VmbusChannelProcessRescindOffer,
			      channel);

	DPRINT_EXIT(VMBUS);
}

/**
 * VmbusChannelOnOffersDelivered - This is invoked when all offers have been delivered.
 *
 * Nothing to do here.
 */
static void VmbusChannelOnOffersDelivered(
			struct vmbus_channel_message_header *hdr)
{
	DPRINT_ENTER(VMBUS);
	DPRINT_EXIT(VMBUS);
}

/**
 * VmbusChannelOnOpenResult - Open result handler.
 *
 * This is invoked when we received a response to our channel open request.
 * Find the matching request, copy the response and signal the requesting
 * thread.
 */
static void VmbusChannelOnOpenResult(struct vmbus_channel_message_header *hdr)
{
	struct vmbus_channel_open_result *result;
	struct list_head *curr;
	struct vmbus_channel_msginfo *msgInfo;
	struct vmbus_channel_message_header *requestHeader;
	struct vmbus_channel_open_channel *openMsg;
	unsigned long flags;

	DPRINT_ENTER(VMBUS);

	result = (struct vmbus_channel_open_result *)hdr;
	DPRINT_DBG(VMBUS, "vmbus open result - %d", result->Status);

	/*
	 * Find the open msg, copy the result and signal/unblock the wait event
	 */
	spin_lock_irqsave(&gVmbusConnection.channelmsg_lock, flags);

	list_for_each(curr, &gVmbusConnection.ChannelMsgList) {
/* FIXME: this should probably use list_entry() instead */
		msgInfo = (struct vmbus_channel_msginfo *)curr;
		requestHeader = (struct vmbus_channel_message_header *)msgInfo->Msg;

		if (requestHeader->MessageType == ChannelMessageOpenChannel) {
			openMsg = (struct vmbus_channel_open_channel *)msgInfo->Msg;
			if (openMsg->ChildRelId == result->ChildRelId &&
			    openMsg->OpenId == result->OpenId) {
				memcpy(&msgInfo->Response.OpenResult,
				       result,
				       sizeof(struct vmbus_channel_open_result));
				osd_WaitEventSet(msgInfo->WaitEvent);
				break;
			}
		}
	}
	spin_unlock_irqrestore(&gVmbusConnection.channelmsg_lock, flags);

	DPRINT_EXIT(VMBUS);
}

/**
 * VmbusChannelOnGpadlCreated - GPADL created handler.
 *
 * This is invoked when we received a response to our gpadl create request.
 * Find the matching request, copy the response and signal the requesting
 * thread.
 */
static void VmbusChannelOnGpadlCreated(struct vmbus_channel_message_header *hdr)
{
	struct vmbus_channel_gpadl_created *gpadlCreated;
	struct list_head *curr;
	struct vmbus_channel_msginfo *msgInfo;
	struct vmbus_channel_message_header *requestHeader;
	struct vmbus_channel_gpadl_header *gpadlHeader;
	unsigned long flags;

	DPRINT_ENTER(VMBUS);

	gpadlCreated = (struct vmbus_channel_gpadl_created *)hdr;
	DPRINT_DBG(VMBUS, "vmbus gpadl created result - %d",
		   gpadlCreated->CreationStatus);

	/*
	 * Find the establish msg, copy the result and signal/unblock the wait
	 * event
	 */
	spin_lock_irqsave(&gVmbusConnection.channelmsg_lock, flags);

	list_for_each(curr, &gVmbusConnection.ChannelMsgList) {
/* FIXME: this should probably use list_entry() instead */
		msgInfo = (struct vmbus_channel_msginfo *)curr;
		requestHeader = (struct vmbus_channel_message_header *)msgInfo->Msg;

		if (requestHeader->MessageType == ChannelMessageGpadlHeader) {
			gpadlHeader = (struct vmbus_channel_gpadl_header *)requestHeader;

			if ((gpadlCreated->ChildRelId ==
			     gpadlHeader->ChildRelId) &&
			    (gpadlCreated->Gpadl == gpadlHeader->Gpadl)) {
				memcpy(&msgInfo->Response.GpadlCreated,
				       gpadlCreated,
				       sizeof(struct vmbus_channel_gpadl_created));
				osd_WaitEventSet(msgInfo->WaitEvent);
				break;
			}
		}
	}
	spin_unlock_irqrestore(&gVmbusConnection.channelmsg_lock, flags);

	DPRINT_EXIT(VMBUS);
}

/**
 * VmbusChannelOnGpadlTorndown - GPADL torndown handler.
 *
 * This is invoked when we received a response to our gpadl teardown request.
 * Find the matching request, copy the response and signal the requesting
 * thread.
 */
static void VmbusChannelOnGpadlTorndown(
			struct vmbus_channel_message_header *hdr)
{
	struct vmbus_channel_gpadl_torndown *gpadlTorndown;
	struct list_head *curr;
	struct vmbus_channel_msginfo *msgInfo;
	struct vmbus_channel_message_header *requestHeader;
	struct vmbus_channel_gpadl_teardown *gpadlTeardown;
	unsigned long flags;

	DPRINT_ENTER(VMBUS);

	gpadlTorndown = (struct vmbus_channel_gpadl_torndown *)hdr;

	/*
	 * Find the open msg, copy the result and signal/unblock the wait event
	 */
	spin_lock_irqsave(&gVmbusConnection.channelmsg_lock, flags);

	list_for_each(curr, &gVmbusConnection.ChannelMsgList) {
/* FIXME: this should probably use list_entry() instead */
		msgInfo = (struct vmbus_channel_msginfo *)curr;
		requestHeader = (struct vmbus_channel_message_header *)msgInfo->Msg;

		if (requestHeader->MessageType == ChannelMessageGpadlTeardown) {
			gpadlTeardown = (struct vmbus_channel_gpadl_teardown *)requestHeader;

			if (gpadlTorndown->Gpadl == gpadlTeardown->Gpadl) {
				memcpy(&msgInfo->Response.GpadlTorndown,
				       gpadlTorndown,
				       sizeof(struct vmbus_channel_gpadl_torndown));
				osd_WaitEventSet(msgInfo->WaitEvent);
				break;
			}
		}
	}
	spin_unlock_irqrestore(&gVmbusConnection.channelmsg_lock, flags);

	DPRINT_EXIT(VMBUS);
}

/**
 * VmbusChannelOnVersionResponse - Version response handler
 *
 * This is invoked when we received a response to our initiate contact request.
 * Find the matching request, copy the response and signal the requesting
 * thread.
 */
static void VmbusChannelOnVersionResponse(
		struct vmbus_channel_message_header *hdr)
{
	struct list_head *curr;
	struct vmbus_channel_msginfo *msgInfo;
	struct vmbus_channel_message_header *requestHeader;
	struct vmbus_channel_initiate_contact *initiate;
	struct vmbus_channel_version_response *versionResponse;
	unsigned long flags;

	DPRINT_ENTER(VMBUS);

	versionResponse = (struct vmbus_channel_version_response *)hdr;
	spin_lock_irqsave(&gVmbusConnection.channelmsg_lock, flags);

	list_for_each(curr, &gVmbusConnection.ChannelMsgList) {
/* FIXME: this should probably use list_entry() instead */
		msgInfo = (struct vmbus_channel_msginfo *)curr;
		requestHeader = (struct vmbus_channel_message_header *)msgInfo->Msg;

		if (requestHeader->MessageType ==
		    ChannelMessageInitiateContact) {
			initiate = (struct vmbus_channel_initiate_contact *)requestHeader;
			memcpy(&msgInfo->Response.VersionResponse,
			      versionResponse,
			      sizeof(struct vmbus_channel_version_response));
			osd_WaitEventSet(msgInfo->WaitEvent);
		}
	}
	spin_unlock_irqrestore(&gVmbusConnection.channelmsg_lock, flags);

	DPRINT_EXIT(VMBUS);
}

/* Channel message dispatch table */
static struct vmbus_channel_message_table_entry
	gChannelMessageTable[ChannelMessageCount] = {
	{ChannelMessageInvalid,			NULL},
	{ChannelMessageOfferChannel,		VmbusChannelOnOffer},
	{ChannelMessageRescindChannelOffer,	VmbusChannelOnOfferRescind},
	{ChannelMessageRequestOffers,		NULL},
	{ChannelMessageAllOffersDelivered,	VmbusChannelOnOffersDelivered},
	{ChannelMessageOpenChannel,		NULL},
	{ChannelMessageOpenChannelResult,	VmbusChannelOnOpenResult},
	{ChannelMessageCloseChannel,		NULL},
	{ChannelMessageGpadlHeader,		NULL},
	{ChannelMessageGpadlBody,		NULL},
	{ChannelMessageGpadlCreated,		VmbusChannelOnGpadlCreated},
	{ChannelMessageGpadlTeardown,		NULL},
	{ChannelMessageGpadlTorndown,		VmbusChannelOnGpadlTorndown},
	{ChannelMessageRelIdReleased,		NULL},
	{ChannelMessageInitiateContact,		NULL},
	{ChannelMessageVersionResponse,		VmbusChannelOnVersionResponse},
	{ChannelMessageUnload,			NULL},
};

/**
 * VmbusOnChannelMessage - Handler for channel protocol messages.
 *
 * This is invoked in the vmbus worker thread context.
 */
void VmbusOnChannelMessage(void *Context)
{
	struct hv_message *msg = Context;
	struct vmbus_channel_message_header *hdr;
	int size;

	DPRINT_ENTER(VMBUS);

	hdr = (struct vmbus_channel_message_header *)msg->u.Payload;
	size = msg->Header.PayloadSize;

	DPRINT_DBG(VMBUS, "message type %d size %d", hdr->MessageType, size);

	if (hdr->MessageType >= ChannelMessageCount) {
		DPRINT_ERR(VMBUS,
			   "Received invalid channel message type %d size %d",
			   hdr->MessageType, size);
		print_hex_dump_bytes("", DUMP_PREFIX_NONE,
				     (unsigned char *)msg->u.Payload, size);
		kfree(msg);
		return;
	}

	if (gChannelMessageTable[hdr->MessageType].messageHandler)
		gChannelMessageTable[hdr->MessageType].messageHandler(hdr);
	else
		DPRINT_ERR(VMBUS, "Unhandled channel message type %d",
			   hdr->MessageType);

	/* Free the msg that was allocated in VmbusOnMsgDPC() */
	kfree(msg);
	DPRINT_EXIT(VMBUS);
}

/**
 * VmbusChannelRequestOffers - Send a request to get all our pending offers.
 */
int VmbusChannelRequestOffers(void)
{
	struct vmbus_channel_message_header *msg;
	struct vmbus_channel_msginfo *msgInfo;
	int ret;

	DPRINT_ENTER(VMBUS);

	msgInfo = kmalloc(sizeof(*msgInfo) +
			  sizeof(struct vmbus_channel_message_header),
			  GFP_KERNEL);
	ASSERT(msgInfo != NULL);

	msgInfo->WaitEvent = osd_WaitEventCreate();
	msg = (struct vmbus_channel_message_header *)msgInfo->Msg;

	msg->MessageType = ChannelMessageRequestOffers;

	/*SpinlockAcquire(gVmbusConnection.channelMsgLock);
	INSERT_TAIL_LIST(&gVmbusConnection.channelMsgList,
			 &msgInfo->msgListEntry);
	SpinlockRelease(gVmbusConnection.channelMsgLock);*/

	ret = VmbusPostMessage(msg,
			       sizeof(struct vmbus_channel_message_header));
	if (ret != 0) {
		DPRINT_ERR(VMBUS, "Unable to request offers - %d", ret);

		/*SpinlockAcquire(gVmbusConnection.channelMsgLock);
		REMOVE_ENTRY_LIST(&msgInfo->msgListEntry);
		SpinlockRelease(gVmbusConnection.channelMsgLock);*/

		goto Cleanup;
	}
	/* osd_WaitEventWait(msgInfo->waitEvent); */

	/*SpinlockAcquire(gVmbusConnection.channelMsgLock);
	REMOVE_ENTRY_LIST(&msgInfo->msgListEntry);
	SpinlockRelease(gVmbusConnection.channelMsgLock);*/


Cleanup:
	if (msgInfo) {
		kfree(msgInfo->WaitEvent);
		kfree(msgInfo);
	}

	DPRINT_EXIT(VMBUS);
	return ret;
}

/**
 * VmbusChannelReleaseUnattachedChannels - Release channels that are unattached/unconnected ie (no drivers associated)
 */
void VmbusChannelReleaseUnattachedChannels(void)
{
	struct vmbus_channel *channel, *pos;
	struct vmbus_channel *start = NULL;
	unsigned long flags;

	spin_lock_irqsave(&gVmbusConnection.channel_lock, flags);

	list_for_each_entry_safe(channel, pos, &gVmbusConnection.ChannelList,
				 ListEntry) {
		if (channel == start)
			break;

		if (!channel->DeviceObject->Driver) {
			list_del(&channel->ListEntry);
			DPRINT_INFO(VMBUS,
				    "Releasing unattached device object %p",
				    channel->DeviceObject);

			VmbusChildDeviceRemove(channel->DeviceObject);
			FreeVmbusChannel(channel);
		} else {
			if (!start)
				start = channel;
		}
	}

	spin_unlock_irqrestore(&gVmbusConnection.channel_lock, flags);
}

/* eof */
