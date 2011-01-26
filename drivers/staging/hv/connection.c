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


struct VMBUS_CONNECTION vmbus_connection = {
	.ConnectState		= Disconnected,
	.NextGpadlHandle	= ATOMIC_INIT(0xE1E10),
};

/*
 * VmbusConnect - Sends a connect request on the partition service connection
 */
int VmbusConnect(void)
{
	int ret = 0;
	struct vmbus_channel_msginfo *msginfo = NULL;
	struct vmbus_channel_initiate_contact *msg;
	unsigned long flags;

	/* Make sure we are not connecting or connected */
	if (vmbus_connection.ConnectState != Disconnected)
		return -1;

	/* Initialize the vmbus connection */
	vmbus_connection.ConnectState = Connecting;
	vmbus_connection.WorkQueue = create_workqueue("hv_vmbus_con");
	if (!vmbus_connection.WorkQueue) {
		ret = -1;
		goto Cleanup;
	}

	INIT_LIST_HEAD(&vmbus_connection.ChannelMsgList);
	spin_lock_init(&vmbus_connection.channelmsg_lock);

	INIT_LIST_HEAD(&vmbus_connection.ChannelList);
	spin_lock_init(&vmbus_connection.channel_lock);

	/*
	 * Setup the vmbus event connection for channel interrupt
	 * abstraction stuff
	 */
	vmbus_connection.InterruptPage = osd_page_alloc(1);
	if (vmbus_connection.InterruptPage == NULL) {
		ret = -1;
		goto Cleanup;
	}

	vmbus_connection.RecvInterruptPage = vmbus_connection.InterruptPage;
	vmbus_connection.SendInterruptPage =
		(void *)((unsigned long)vmbus_connection.InterruptPage +
			(PAGE_SIZE >> 1));

	/*
	 * Setup the monitor notification facility. The 1st page for
	 * parent->child and the 2nd page for child->parent
	 */
	vmbus_connection.MonitorPages = osd_page_alloc(2);
	if (vmbus_connection.MonitorPages == NULL) {
		ret = -1;
		goto Cleanup;
	}

	msginfo = kzalloc(sizeof(*msginfo) +
			  sizeof(struct vmbus_channel_initiate_contact),
			  GFP_KERNEL);
	if (msginfo == NULL) {
		ret = -ENOMEM;
		goto Cleanup;
	}

	msginfo->waitevent = osd_waitevent_create();
	if (!msginfo->waitevent) {
		ret = -ENOMEM;
		goto Cleanup;
	}

	msg = (struct vmbus_channel_initiate_contact *)msginfo->msg;

	msg->header.msgtype = CHANNELMSG_INITIATE_CONTACT;
	msg->vmbus_version_requested = VMBUS_REVISION_NUMBER;
	msg->interrupt_page = virt_to_phys(vmbus_connection.InterruptPage);
	msg->monitor_page1 = virt_to_phys(vmbus_connection.MonitorPages);
	msg->monitor_page2 = virt_to_phys(
			(void *)((unsigned long)vmbus_connection.MonitorPages +
				 PAGE_SIZE));

	/*
	 * Add to list before we send the request since we may
	 * receive the response before returning from this routine
	 */
	spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);
	list_add_tail(&msginfo->msglistentry,
		      &vmbus_connection.ChannelMsgList);

	spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock, flags);

	DPRINT_DBG(VMBUS, "Vmbus connection - interrupt pfn %llx, "
		   "monitor1 pfn %llx,, monitor2 pfn %llx",
		   msg->interrupt_page, msg->monitor_page1, msg->monitor_page2);

	DPRINT_DBG(VMBUS, "Sending channel initiate msg...");
	ret = VmbusPostMessage(msg,
			       sizeof(struct vmbus_channel_initiate_contact));
	if (ret != 0) {
		list_del(&msginfo->msglistentry);
		goto Cleanup;
	}

	/* Wait for the connection response */
	osd_waitevent_wait(msginfo->waitevent);

	list_del(&msginfo->msglistentry);

	/* Check if successful */
	if (msginfo->response.version_response.version_supported) {
		DPRINT_INFO(VMBUS, "Vmbus connected!!");
		vmbus_connection.ConnectState = Connected;

	} else {
		DPRINT_ERR(VMBUS, "Vmbus connection failed!!..."
			   "current version (%d) not supported",
			   VMBUS_REVISION_NUMBER);
		ret = -1;
		goto Cleanup;
	}

	kfree(msginfo->waitevent);
	kfree(msginfo);
	return 0;

Cleanup:
	vmbus_connection.ConnectState = Disconnected;

	if (vmbus_connection.WorkQueue)
		destroy_workqueue(vmbus_connection.WorkQueue);

	if (vmbus_connection.InterruptPage) {
		osd_page_free(vmbus_connection.InterruptPage, 1);
		vmbus_connection.InterruptPage = NULL;
	}

	if (vmbus_connection.MonitorPages) {
		osd_page_free(vmbus_connection.MonitorPages, 2);
		vmbus_connection.MonitorPages = NULL;
	}

	if (msginfo) {
		kfree(msginfo->waitevent);
		kfree(msginfo);
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
	if (vmbus_connection.ConnectState != Connected)
		return -1;

	msg = kzalloc(sizeof(struct vmbus_channel_message_header), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	msg->msgtype = CHANNELMSG_UNLOAD;

	ret = VmbusPostMessage(msg,
			       sizeof(struct vmbus_channel_message_header));
	if (ret != 0)
		goto Cleanup;

	osd_page_free(vmbus_connection.InterruptPage, 1);

	/* TODO: iterate thru the msg list and free up */
	destroy_workqueue(vmbus_connection.WorkQueue);

	vmbus_connection.ConnectState = Disconnected;

	DPRINT_INFO(VMBUS, "Vmbus disconnected!!");

Cleanup:
	kfree(msg);
	return ret;
}

/*
 * GetChannelFromRelId - Get the channel object given its child relative id (ie channel id)
 */
struct vmbus_channel *GetChannelFromRelId(u32 relid)
{
	struct vmbus_channel *channel;
	struct vmbus_channel *found_channel  = NULL;
	unsigned long flags;

	spin_lock_irqsave(&vmbus_connection.channel_lock, flags);
	list_for_each_entry(channel, &vmbus_connection.ChannelList, listentry) {
		if (channel->offermsg.child_relid == relid) {
			found_channel = channel;
			break;
		}
	}
	spin_unlock_irqrestore(&vmbus_connection.channel_lock, flags);

	return found_channel;
}

/*
 * VmbusProcessChannelEvent - Process a channel event notification
 */
static void VmbusProcessChannelEvent(void *context)
{
	struct vmbus_channel *channel;
	u32 relid = (u32)(unsigned long)context;

	/* ASSERT(relId > 0); */

	/*
	 * Find the channel based on this relid and invokes the
	 * channel callback to process the event
	 */
	channel = GetChannelFromRelId(relid);

	if (channel) {
		vmbus_onchannel_event(channel);
		/*
		 * WorkQueueQueueWorkItem(channel->dataWorkQueue,
		 *			  vmbus_onchannel_event,
		 *			  (void*)channel);
		 */
	} else {
		DPRINT_ERR(VMBUS, "channel not found for relid - %d.", relid);
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
	u32 *recv_int_page = vmbus_connection.RecvInterruptPage;

	/* Check events */
	if (recv_int_page) {
		for (dword = 0; dword < maxdword; dword++) {
			if (recv_int_page[dword]) {
				for (bit = 0; bit < 32; bit++) {
					if (test_and_clear_bit(bit,
						(unsigned long *)
						&recv_int_page[dword])) {
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
int VmbusPostMessage(void *buffer, size_t buflen)
{
	union hv_connection_id conn_id;

	conn_id.asu32 = 0;
	conn_id.u.id = VMBUS_MESSAGE_CONNECTION_ID;
	return hv_post_message(conn_id, 1, buffer, buflen);
}

/*
 * VmbusSetEvent - Send an event notification to the parent
 */
int VmbusSetEvent(u32 child_relid)
{
	/* Each u32 represents 32 channels */
	set_bit(child_relid & 31,
		(unsigned long *)vmbus_connection.SendInterruptPage +
		(child_relid >> 5));

	return hv_signal_event();
}
