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
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "hyperv.h"
#include "hyperv_vmbus.h"


struct vmbus_connection vmbus_connection = {
	.conn_state		= DISCONNECTED,
	.next_gpadl_handle	= ATOMIC_INIT(0xE1E10),
};

/*
 * vmbus_connect - Sends a connect request on the partition service connection
 */
int vmbus_connect(void)
{
	int ret = 0;
	int t;
	struct vmbus_channel_msginfo *msginfo = NULL;
	struct vmbus_channel_initiate_contact *msg;
	unsigned long flags;

	/* Make sure we are not connecting or connected */
	if (vmbus_connection.conn_state != DISCONNECTED)
		return -1;

	/* Initialize the vmbus connection */
	vmbus_connection.conn_state = CONNECTING;
	vmbus_connection.work_queue = create_workqueue("hv_vmbus_con");
	if (!vmbus_connection.work_queue) {
		ret = -1;
		goto cleanup;
	}

	INIT_LIST_HEAD(&vmbus_connection.chn_msg_list);
	spin_lock_init(&vmbus_connection.channelmsg_lock);

	INIT_LIST_HEAD(&vmbus_connection.chn_list);
	spin_lock_init(&vmbus_connection.channel_lock);

	/*
	 * Setup the vmbus event connection for channel interrupt
	 * abstraction stuff
	 */
	vmbus_connection.int_page =
	(void *)__get_free_pages(GFP_KERNEL|__GFP_ZERO, 0);
	if (vmbus_connection.int_page == NULL) {
		ret = -1;
		goto cleanup;
	}

	vmbus_connection.recv_int_page = vmbus_connection.int_page;
	vmbus_connection.send_int_page =
		(void *)((unsigned long)vmbus_connection.int_page +
			(PAGE_SIZE >> 1));

	/*
	 * Setup the monitor notification facility. The 1st page for
	 * parent->child and the 2nd page for child->parent
	 */
	vmbus_connection.monitor_pages =
	(void *)__get_free_pages((GFP_KERNEL|__GFP_ZERO), 1);
	if (vmbus_connection.monitor_pages == NULL) {
		ret = -1;
		goto cleanup;
	}

	msginfo = kzalloc(sizeof(*msginfo) +
			  sizeof(struct vmbus_channel_initiate_contact),
			  GFP_KERNEL);
	if (msginfo == NULL) {
		ret = -ENOMEM;
		goto cleanup;
	}

	init_completion(&msginfo->waitevent);

	msg = (struct vmbus_channel_initiate_contact *)msginfo->msg;

	msg->header.msgtype = CHANNELMSG_INITIATE_CONTACT;
	msg->vmbus_version_requested = VMBUS_REVISION_NUMBER;
	msg->interrupt_page = virt_to_phys(vmbus_connection.int_page);
	msg->monitor_page1 = virt_to_phys(vmbus_connection.monitor_pages);
	msg->monitor_page2 = virt_to_phys(
			(void *)((unsigned long)vmbus_connection.monitor_pages +
				 PAGE_SIZE));

	/*
	 * Add to list before we send the request since we may
	 * receive the response before returning from this routine
	 */
	spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);
	list_add_tail(&msginfo->msglistentry,
		      &vmbus_connection.chn_msg_list);

	spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock, flags);

	ret = vmbus_post_msg(msg,
			       sizeof(struct vmbus_channel_initiate_contact));
	if (ret != 0) {
		spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);
		list_del(&msginfo->msglistentry);
		spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock,
					flags);
		goto cleanup;
	}

	/* Wait for the connection response */
	t =  wait_for_completion_timeout(&msginfo->waitevent, 5*HZ);
	if (t == 0) {
		spin_lock_irqsave(&vmbus_connection.channelmsg_lock,
				flags);
		list_del(&msginfo->msglistentry);
		spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock,
					flags);
		ret = -ETIMEDOUT;
		goto cleanup;
	}

	spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);
	list_del(&msginfo->msglistentry);
	spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock, flags);

	/* Check if successful */
	if (msginfo->response.version_response.version_supported) {
		vmbus_connection.conn_state = CONNECTED;
	} else {
		pr_err("Unable to connect, "
			"Version %d not supported by Hyper-V\n",
			VMBUS_REVISION_NUMBER);
		ret = -1;
		goto cleanup;
	}

	kfree(msginfo);
	return 0;

cleanup:
	vmbus_connection.conn_state = DISCONNECTED;

	if (vmbus_connection.work_queue)
		destroy_workqueue(vmbus_connection.work_queue);

	if (vmbus_connection.int_page) {
		free_pages((unsigned long)vmbus_connection.int_page, 0);
		vmbus_connection.int_page = NULL;
	}

	if (vmbus_connection.monitor_pages) {
		free_pages((unsigned long)vmbus_connection.monitor_pages, 1);
		vmbus_connection.monitor_pages = NULL;
	}

	kfree(msginfo);

	return ret;
}

/*
 * vmbus_disconnect -
 * Sends a disconnect request on the partition service connection
 */
int vmbus_disconnect(void)
{
	int ret = 0;
	struct vmbus_channel_message_header *msg;

	/* Make sure we are connected */
	if (vmbus_connection.conn_state != CONNECTED)
		return -1;

	msg = kzalloc(sizeof(struct vmbus_channel_message_header), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	msg->msgtype = CHANNELMSG_UNLOAD;

	ret = vmbus_post_msg(msg,
			       sizeof(struct vmbus_channel_message_header));
	if (ret != 0)
		goto cleanup;

	free_pages((unsigned long)vmbus_connection.int_page, 0);
	free_pages((unsigned long)vmbus_connection.monitor_pages, 1);

	/* TODO: iterate thru the msg list and free up */
	destroy_workqueue(vmbus_connection.work_queue);

	vmbus_connection.conn_state = DISCONNECTED;

	pr_info("hv_vmbus disconnected\n");

cleanup:
	kfree(msg);
	return ret;
}

/*
 * relid2channel - Get the channel object given its
 * child relative id (ie channel id)
 */
struct vmbus_channel *relid2channel(u32 relid)
{
	struct vmbus_channel *channel;
	struct vmbus_channel *found_channel  = NULL;
	unsigned long flags;

	spin_lock_irqsave(&vmbus_connection.channel_lock, flags);
	list_for_each_entry(channel, &vmbus_connection.chn_list, listentry) {
		if (channel->offermsg.child_relid == relid) {
			found_channel = channel;
			break;
		}
	}
	spin_unlock_irqrestore(&vmbus_connection.channel_lock, flags);

	return found_channel;
}

/*
 * process_chn_event - Process a channel event notification
 */
static void process_chn_event(u32 relid)
{
	struct vmbus_channel *channel;

	/* ASSERT(relId > 0); */

	/*
	 * Find the channel based on this relid and invokes the
	 * channel callback to process the event
	 */
	channel = relid2channel(relid);

	if (channel) {
		vmbus_onchannel_event(channel);
	} else {
		pr_err("channel not found for relid - %u\n", relid);
	}
}

/*
 * vmbus_on_event - Handler for events
 */
void vmbus_on_event(unsigned long data)
{
	u32 dword;
	u32 maxdword = MAX_NUM_CHANNELS_SUPPORTED >> 5;
	int bit;
	u32 relid;
	u32 *recv_int_page = vmbus_connection.recv_int_page;

	/* Check events */
	if (!recv_int_page)
		return;
	for (dword = 0; dword < maxdword; dword++) {
		if (!recv_int_page[dword])
			continue;
		for (bit = 0; bit < 32; bit++) {
			if (sync_test_and_clear_bit(bit, (unsigned long *)&recv_int_page[dword])) {
				relid = (dword << 5) + bit;

				if (relid == 0) {
					/*
					 * Special case - vmbus
					 * channel protocol msg
					 */
					continue;
				}
				process_chn_event(relid);
			}
		}
	}
}

/*
 * vmbus_post_msg - Send a msg on the vmbus's message connection
 */
int vmbus_post_msg(void *buffer, size_t buflen)
{
	union hv_connection_id conn_id;

	conn_id.asu32 = 0;
	conn_id.u.id = VMBUS_MESSAGE_CONNECTION_ID;
	return hv_post_message(conn_id, 1, buffer, buflen);
}

/*
 * vmbus_set_event - Send an event notification to the parent
 */
int vmbus_set_event(u32 child_relid)
{
	/* Each u32 represents 32 channels */
	sync_set_bit(child_relid & 31,
		(unsigned long *)vmbus_connection.send_int_page +
		(child_relid >> 5));

	return hv_signal_event();
}
