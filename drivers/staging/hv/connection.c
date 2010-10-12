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
 *   Haiyang Zhang <haiyangz@microsoft.com>
 *   Hank Janssen  <hjanssen@microsoft.com>
 *
 */
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include "osd.h"
#include "logging.h"
#include "vmbus_private.h"


struct VMBUS_CONNECTION gVmbusConnection = {
	.ConnectState		= Disconnected,
	.NextGpadlHandle	= ATOMIC_INIT(0xE1E10),
};

/*
 * VmbusConnect - Sends a connect request on the partition service connection
 */
int VmbusConnect(void)
{
	int ret = 0;
	struct vmbus_channel_msginfo *msgInfo = NULL;
	struct vmbus_channel_initiate_contact *msg;
	unsigned long flags;

	/* Make sure we are not connecting or connected */
	if (gVmbusConnection.ConnectState != Disconnected)
		return -1;

	/* Initialize the vmbus connection */
	gVmbusConnection.ConnectState = Connecting;
	gVmbusConnection.WorkQueue = create_workqueue("hv_vmbus_con");
	if (!gVmbusConnection.WorkQueue) {
		ret = -1;
		goto Cleanup;
	}

	INIT_LIST_HEAD(&gVmbusConnection.ChannelMsgList);
	spin_lock_init(&gVmbusConnection.channelmsg_lock);

	INIT_LIST_HEAD(&gVmbusConnection.ChannelList);
	spin_lock_init(&gVmbusConnection.channel_lock);

	/*
	 * Setup the vmbus event connection for channel interrupt
	 * abstraction stuff
	 */
	gVmbusConnection.InterruptPage = osd_PageAlloc(1);
	if (gVmbusConnection.InterruptPage == NULL) {
		ret = -1;
		goto Cleanup;
	}

	gVmbusConnection.RecvInterruptPage = gVmbusConnection.InterruptPage;
	gVmbusConnection.SendInterruptPage =
		(void *)((unsigned long)gVmbusConnection.InterruptPage +
			(PAGE_SIZE >> 1));

	/*
	 * Setup the monitor notification facility. The 1st page for
	 * parent->child and the 2nd page for child->parent
	 */
	gVmbusConnection.MonitorPages = osd_PageAlloc(2);
	if (gVmbusConnection.MonitorPages == NULL) {
		ret = -1;
		goto Cleanup;
	}

	msgInfo = kzalloc(sizeof(*msgInfo) +
			  sizeof(struct vmbus_channel_initiate_contact),
			  GFP_KERNEL);
	if (msgInfo == NULL) {
		ret = -ENOMEM;
		goto Cleanup;
	}

	msgInfo->WaitEvent = osd_WaitEventCreate();
	if (!msgInfo->WaitEvent) {
		ret = -ENOMEM;
		goto Cleanup;
	}

	msg = (struct vmbus_channel_initiate_contact *)msgInfo->Msg;

	msg->Header.MessageType = ChannelMessageInitiateContact;
	msg->VMBusVersionRequested = VMBUS_REVISION_NUMBER;
	msg->InterruptPage = virt_to_phys(gVmbusConnection.InterruptPage);
	msg->MonitorPage1 = virt_to_phys(gVmbusConnection.MonitorPages);
	msg->MonitorPage2 = virt_to_phys(
			(void *)((unsigned long)gVmbusConnection.MonitorPages +
				 PAGE_SIZE));

	/*
	 * Add to list before we send the request since we may
	 * receive the response before returning from this routine
	 */
	spin_lock_irqsave(&gVmbusConnection.channelmsg_lock, flags);
	list_add_tail(&msgInfo->MsgListEntry,
		      &gVmbusConnection.ChannelMsgList);

	spin_unlock_irqrestore(&gVmbusConnection.channelmsg_lock, flags);

	DPRINT_DBG(VMBUS, "Vmbus connection - interrupt pfn %llx, "
		   "monitor1 pfn %llx,, monitor2 pfn %llx",
		   msg->InterruptPage, msg->MonitorPage1, msg->MonitorPage2);

	DPRINT_DBG(VMBUS, "Sending channel initiate msg...");
	ret = VmbusPostMessage(msg,
			       sizeof(struct vmbus_channel_initiate_contact));
	if (ret != 0) {
		list_del(&msgInfo->MsgListEntry);
		goto Cleanup;
	}

	/* Wait for the connection response */
	osd_WaitEventWait(msgInfo->WaitEvent);

	list_del(&msgInfo->MsgListEntry);

	/* Check if successful */
	if (msgInfo->Response.VersionResponse.VersionSupported) {
		DPRINT_INFO(VMBUS, "Vmbus connected!!");
		gVmbusConnection.ConnectState = Connected;

	} else {
		DPRINT_ERR(VMBUS, "Vmbus connection failed!!..."
			   "current version (%d) not supported",
			   VMBUS_REVISION_NUMBER);
		ret = -1;
		goto Cleanup;
	}

	kfree(msgInfo->WaitEvent);
	kfree(msgInfo);
	return 0;

Cleanup:
	gVmbusConnection.ConnectState = Disconnected;

	if (gVmbusConnection.WorkQueue)
		destroy_workqueue(gVmbusConnection.WorkQueue);

	if (gVmbusConnection.InterruptPage) {
		osd_PageFree(gVmbusConnection.InterruptPage, 1);
		gVmbusConnection.InterruptPage = NULL;
	}

	if (gVmbusConnection.MonitorPages) {
		osd_PageFree(gVmbusConnection.MonitorPages, 2);
		gVmbusConnection.MonitorPages = NULL;
	}

	if (msgInfo) {
		kfree(msgInfo->WaitEvent);
		kfree(msgInfo);
	}

	return ret;
}

/*
 * VmbusDisconnect - Sends a disconnect request on the partition service connection
 */
int VmbusDisconnect(void)
{
	int ret = 0;
	struct vmbus_channel_message_header *msg;

	/* Make sure we are connected */
	if (gVmbusConnection.ConnectState != Connected)
		return -1;

	msg = kzalloc(sizeof(struct vmbus_channel_message_header), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	msg->MessageType = ChannelMessageUnload;

	ret = VmbusPostMessage(msg,
			       sizeof(struct vmbus_channel_message_header));
	if (ret != 0)
		goto Cleanup;

	osd_PageFree(gVmbusConnection.InterruptPage, 1);

	/* TODO: iterate thru the msg list and free up */
	destroy_workqueue(gVmbusConnection.WorkQueue);

	gVmbusConnection.ConnectState = Disconnected;

	DPRINT_INFO(VMBUS, "Vmbus disconnected!!");

Cleanup:
	kfree(msg);
	return ret;
}

/*
 * GetChannelFromRelId - Get the channel object given its child relative id (ie channel id)
 */
struct vmbus_channel *GetChannelFromRelId(u32 relId)
{
	struct vmbus_channel *channel;
	struct vmbus_channel *foundChannel  = NULL;
	unsigned long flags;

	spin_lock_irqsave(&gVmbusConnection.channel_lock, flags);
	list_for_each_entry(channel, &gVmbusConnection.ChannelList, ListEntry) {
		if (channel->OfferMsg.ChildRelId == relId) {
			foundChannel = channel;
			break;
		}
	}
	spin_unlock_irqrestore(&gVmbusConnection.channel_lock, flags);

	return foundChannel;
}

/*
 * VmbusProcessChannelEvent - Process a channel event notification
 */
static void VmbusProcessChannelEvent(void *context)
{
	struct vmbus_channel *channel;
	u32 relId = (u32)(unsigned long)context;

	/* ASSERT(relId > 0); */

	/*
	 * Find the channel based on this relid and invokes the
	 * channel callback to process the event
	 */
	channel = GetChannelFromRelId(relId);

	if (channel) {
		VmbusChannelOnChannelEvent(channel);
		/*
		 * WorkQueueQueueWorkItem(channel->dataWorkQueue,
		 *			  VmbusChannelOnChannelEvent,
		 *			  (void*)channel);
		 */
	} else {
		DPRINT_ERR(VMBUS, "channel not found for relid - %d.", relId);
	}
}

/*
 * VmbusOnEvents - Handler for events
 */
void VmbusOnEvents(void)
{
	int dword;
	int maxdword = MAX_NUM_CHANNELS_SUPPORTED >> 5;
	int bit;
	int relid;
	u32 *recvInterruptPage = gVmbusConnection.RecvInterruptPage;

	/* Check events */
	if (recvInterruptPage) {
		for (dword = 0; dword < maxdword; dword++) {
			if (recvInterruptPage[dword]) {
				for (bit = 0; bit < 32; bit++) {
					if (test_and_clear_bit(bit, (unsigned long *)&recvInterruptPage[dword])) {
						relid = (dword << 5) + bit;
						DPRINT_DBG(VMBUS, "event detected for relid - %d", relid);

						if (relid == 0) {
							/* special case - vmbus channel protocol msg */
							DPRINT_DBG(VMBUS, "invalid relid - %d", relid);
							continue;
						} else {
							/* QueueWorkItem(VmbusProcessEvent, (void*)relid); */
							/* ret = WorkQueueQueueWorkItem(gVmbusConnection.workQueue, VmbusProcessChannelEvent, (void*)relid); */
							VmbusProcessChannelEvent((void *)(unsigned long)relid);
						}
					}
				}
			}
		 }
	}
	return;
}

/*
 * VmbusPostMessage - Send a msg on the vmbus's message connection
 */
int VmbusPostMessage(void *buffer, size_t bufferLen)
{
	union hv_connection_id connId;

	connId.Asu32 = 0;
	connId.u.Id = VMBUS_MESSAGE_CONNECTION_ID;
	return HvPostMessage(connId, 1, buffer, bufferLen);
}

/*
 * VmbusSetEvent - Send an event notification to the parent
 */
int VmbusSetEvent(u32 childRelId)
{
	/* Each u32 represents 32 channels */
	set_bit(childRelId & 31,
		(unsigned long *)gVmbusConnection.SendInterruptPage +
		(childRelId >> 5));

	return HvSignalEvent();
}
