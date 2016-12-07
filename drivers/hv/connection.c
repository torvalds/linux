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
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/hyperv.h>
#include <linux/export.h>
#include <asm/hyperv.h>
#include "hyperv_vmbus.h"


struct vmbus_connection vmbus_connection = {
	.conn_state		= DISCONNECTED,
	.next_gpadl_handle	= ATOMIC_INIT(0xE1E10),
};
EXPORT_SYMBOL_GPL(vmbus_connection);

/*
 * Negotiated protocol version with the host.
 */
__u32 vmbus_proto_version;
EXPORT_SYMBOL_GPL(vmbus_proto_version);

static __u32 vmbus_get_next_version(__u32 current_version)
{
	switch (current_version) {
	case (VERSION_WIN7):
		return VERSION_WS2008;

	case (VERSION_WIN8):
		return VERSION_WIN7;

	case (VERSION_WIN8_1):
		return VERSION_WIN8;

	case (VERSION_WIN10):
		return VERSION_WIN8_1;

	case (VERSION_WS2008):
	default:
		return VERSION_INVAL;
	}
}

static int vmbus_negotiate_version(struct vmbus_channel_msginfo *msginfo,
					__u32 version)
{
	int ret = 0;
	struct vmbus_channel_initiate_contact *msg;
	unsigned long flags;

	init_completion(&msginfo->waitevent);

	msg = (struct vmbus_channel_initiate_contact *)msginfo->msg;

	msg->header.msgtype = CHANNELMSG_INITIATE_CONTACT;
	msg->vmbus_version_requested = version;
	msg->interrupt_page = virt_to_phys(vmbus_connection.int_page);
	msg->monitor_page1 = virt_to_phys(vmbus_connection.monitor_pages[0]);
	msg->monitor_page2 = virt_to_phys(vmbus_connection.monitor_pages[1]);
	/*
	 * We want all channel messages to be delivered on CPU 0.
	 * This has been the behavior pre-win8. This is not
	 * perf issue and having all channel messages delivered on CPU 0
	 * would be ok.
	 * For post win8 hosts, we support receiving channel messagges on
	 * all the CPUs. This is needed for kexec to work correctly where
	 * the CPU attempting to connect may not be CPU 0.
	 */
	if (version >= VERSION_WIN8_1) {
		msg->target_vcpu = hv_context.vp_index[get_cpu()];
		put_cpu();
	} else {
		msg->target_vcpu = 0;
	}

	/*
	 * Add to list before we send the request since we may
	 * receive the response before returning from this routine
	 */
	spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);
	list_add_tail(&msginfo->msglistentry,
		      &vmbus_connection.chn_msg_list);

	spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock, flags);

	ret = vmbus_post_msg(msg,
			     sizeof(struct vmbus_channel_initiate_contact),
			     true);
	if (ret != 0) {
		spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);
		list_del(&msginfo->msglistentry);
		spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock,
					flags);
		return ret;
	}

	/* Wait for the connection response */
	wait_for_completion(&msginfo->waitevent);

	spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);
	list_del(&msginfo->msglistentry);
	spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock, flags);

	/* Check if successful */
	if (msginfo->response.version_response.version_supported) {
		vmbus_connection.conn_state = CONNECTED;
	} else {
		return -ECONNREFUSED;
	}

	return ret;
}

/*
 * vmbus_connect - Sends a connect request on the partition service connection
 */
int vmbus_connect(void)
{
	int ret = 0;
	struct vmbus_channel_msginfo *msginfo = NULL;
	__u32 version;

	/* Initialize the vmbus connection */
	vmbus_connection.conn_state = CONNECTING;
	vmbus_connection.work_queue = create_workqueue("hv_vmbus_con");
	if (!vmbus_connection.work_queue) {
		ret = -ENOMEM;
		goto cleanup;
	}

	INIT_LIST_HEAD(&vmbus_connection.chn_msg_list);
	spin_lock_init(&vmbus_connection.channelmsg_lock);

	INIT_LIST_HEAD(&vmbus_connection.chn_list);
	mutex_init(&vmbus_connection.channel_mutex);

	/*
	 * Setup the vmbus event connection for channel interrupt
	 * abstraction stuff
	 */
	vmbus_connection.int_page =
	(void *)__get_free_pages(GFP_KERNEL|__GFP_ZERO, 0);
	if (vmbus_connection.int_page == NULL) {
		ret = -ENOMEM;
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
	vmbus_connection.monitor_pages[0] = (void *)__get_free_pages((GFP_KERNEL|__GFP_ZERO), 0);
	vmbus_connection.monitor_pages[1] = (void *)__get_free_pages((GFP_KERNEL|__GFP_ZERO), 0);
	if ((vmbus_connection.monitor_pages[0] == NULL) ||
	    (vmbus_connection.monitor_pages[1] == NULL)) {
		ret = -ENOMEM;
		goto cleanup;
	}

	msginfo = kzalloc(sizeof(*msginfo) +
			  sizeof(struct vmbus_channel_initiate_contact),
			  GFP_KERNEL);
	if (msginfo == NULL) {
		ret = -ENOMEM;
		goto cleanup;
	}

	/*
	 * Negotiate a compatible VMBUS version number with the
	 * host. We start with the highest number we can support
	 * and work our way down until we negotiate a compatible
	 * version.
	 */

	version = VERSION_CURRENT;

	do {
		ret = vmbus_negotiate_version(msginfo, version);
		if (ret == -ETIMEDOUT)
			goto cleanup;

		if (vmbus_connection.conn_state == CONNECTED)
			break;

		version = vmbus_get_next_version(version);
	} while (version != VERSION_INVAL);

	if (version == VERSION_INVAL)
		goto cleanup;

	vmbus_proto_version = version;
	pr_info("Hyper-V Host Build:%d-%d.%d-%d-%d.%d; Vmbus version:%d.%d\n",
		    host_info_eax, host_info_ebx >> 16,
		    host_info_ebx & 0xFFFF, host_info_ecx,
		    host_info_edx >> 24, host_info_edx & 0xFFFFFF,
		    version >> 16, version & 0xFFFF);

	kfree(msginfo);
	return 0;

cleanup:
	pr_err("Unable to connect to host\n");

	vmbus_connection.conn_state = DISCONNECTED;
	vmbus_disconnect();

	kfree(msginfo);

	return ret;
}

void vmbus_disconnect(void)
{
	/*
	 * First send the unload request to the host.
	 */
	vmbus_initiate_unload(false);

	if (vmbus_connection.work_queue) {
		drain_workqueue(vmbus_connection.work_queue);
		destroy_workqueue(vmbus_connection.work_queue);
	}

	if (vmbus_connection.int_page) {
		free_pages((unsigned long)vmbus_connection.int_page, 0);
		vmbus_connection.int_page = NULL;
	}

	free_pages((unsigned long)vmbus_connection.monitor_pages[0], 0);
	free_pages((unsigned long)vmbus_connection.monitor_pages[1], 0);
	vmbus_connection.monitor_pages[0] = NULL;
	vmbus_connection.monitor_pages[1] = NULL;
}

/*
 * Map the given relid to the corresponding channel based on the
 * per-cpu list of channels that have been affinitized to this CPU.
 * This will be used in the channel callback path as we can do this
 * mapping in a lock-free fashion.
 */
static struct vmbus_channel *pcpu_relid2channel(u32 relid)
{
	struct vmbus_channel *channel;
	struct vmbus_channel *found_channel  = NULL;
	int cpu = smp_processor_id();
	struct list_head *pcpu_head = &hv_context.percpu_list[cpu];

	list_for_each_entry(channel, pcpu_head, percpu_list) {
		if (channel->offermsg.child_relid == relid) {
			found_channel = channel;
			break;
		}
	}

	return found_channel;
}

/*
 * relid2channel - Get the channel object given its
 * child relative id (ie channel id)
 */
struct vmbus_channel *relid2channel(u32 relid)
{
	struct vmbus_channel *channel;
	struct vmbus_channel *found_channel  = NULL;
	struct list_head *cur, *tmp;
	struct vmbus_channel *cur_sc;

	BUG_ON(!mutex_is_locked(&vmbus_connection.channel_mutex));

	list_for_each_entry(channel, &vmbus_connection.chn_list, listentry) {
		if (channel->offermsg.child_relid == relid) {
			found_channel = channel;
			break;
		} else if (!list_empty(&channel->sc_list)) {
			/*
			 * Deal with sub-channels.
			 */
			list_for_each_safe(cur, tmp, &channel->sc_list) {
				cur_sc = list_entry(cur, struct vmbus_channel,
							sc_list);
				if (cur_sc->offermsg.child_relid == relid) {
					found_channel = cur_sc;
					break;
				}
			}
		}
	}

	return found_channel;
}

/*
 * process_chn_event - Process a channel event notification
 */
static void process_chn_event(u32 relid)
{
	struct vmbus_channel *channel;
	void *arg;
	bool read_state;
	u32 bytes_to_read;

	/*
	 * Find the channel based on this relid and invokes the
	 * channel callback to process the event
	 */
	channel = pcpu_relid2channel(relid);

	if (!channel)
		return;

	/*
	 * A channel once created is persistent even when there
	 * is no driver handling the device. An unloading driver
	 * sets the onchannel_callback to NULL on the same CPU
	 * as where this interrupt is handled (in an interrupt context).
	 * Thus, checking and invoking the driver specific callback takes
	 * care of orderly unloading of the driver.
	 */

	if (channel->onchannel_callback != NULL) {
		arg = channel->channel_callback_context;
		read_state = channel->batched_reading;
		/*
		 * This callback reads the messages sent by the host.
		 * We can optimize host to guest signaling by ensuring:
		 * 1. While reading the channel, we disable interrupts from
		 *    host.
		 * 2. Ensure that we process all posted messages from the host
		 *    before returning from this callback.
		 * 3. Once we return, enable signaling from the host. Once this
		 *    state is set we check to see if additional packets are
		 *    available to read. In this case we repeat the process.
		 */

		do {
			if (read_state)
				hv_begin_read(&channel->inbound);
			channel->onchannel_callback(arg);
			if (read_state)
				bytes_to_read = hv_end_read(&channel->inbound);
			else
				bytes_to_read = 0;
		} while (read_state && (bytes_to_read != 0));
	}
}

/*
 * vmbus_on_event - Handler for events
 */
void vmbus_on_event(unsigned long data)
{
	u32 dword;
	u32 maxdword;
	int bit;
	u32 relid;
	u32 *recv_int_page = NULL;
	void *page_addr;
	int cpu = smp_processor_id();
	union hv_synic_event_flags *event;

	if (vmbus_proto_version < VERSION_WIN8) {
		maxdword = MAX_NUM_CHANNELS_SUPPORTED >> 5;
		recv_int_page = vmbus_connection.recv_int_page;
	} else {
		/*
		 * When the host is win8 and beyond, the event page
		 * can be directly checked to get the id of the channel
		 * that has the interrupt pending.
		 */
		maxdword = HV_EVENT_FLAGS_DWORD_COUNT;
		page_addr = hv_context.synic_event_page[cpu];
		event = (union hv_synic_event_flags *)page_addr +
						 VMBUS_MESSAGE_SINT;
		recv_int_page = event->flags32;
	}



	/* Check events */
	if (!recv_int_page)
		return;
	for (dword = 0; dword < maxdword; dword++) {
		if (!recv_int_page[dword])
			continue;
		for (bit = 0; bit < 32; bit++) {
			if (sync_test_and_clear_bit(bit,
				(unsigned long *)&recv_int_page[dword])) {
				relid = (dword << 5) + bit;

				if (relid == 0)
					/*
					 * Special case - vmbus
					 * channel protocol msg
					 */
					continue;

				process_chn_event(relid);
			}
		}
	}
}

/*
 * vmbus_post_msg - Send a msg on the vmbus's message connection
 */
int vmbus_post_msg(void *buffer, size_t buflen, bool can_sleep)
{
	union hv_connection_id conn_id;
	int ret = 0;
	int retries = 0;
	u32 usec = 1;

	conn_id.asu32 = 0;
	conn_id.u.id = VMBUS_MESSAGE_CONNECTION_ID;

	/*
	 * hv_post_message() can have transient failures because of
	 * insufficient resources. Retry the operation a couple of
	 * times before giving up.
	 */
	while (retries < 100) {
		ret = hv_post_message(conn_id, 1, buffer, buflen);

		switch (ret) {
		case HV_STATUS_INVALID_CONNECTION_ID:
			/*
			 * We could get this if we send messages too
			 * frequently.
			 */
			ret = -EAGAIN;
			break;
		case HV_STATUS_INSUFFICIENT_MEMORY:
		case HV_STATUS_INSUFFICIENT_BUFFERS:
			ret = -ENOMEM;
			break;
		case HV_STATUS_SUCCESS:
			return ret;
		default:
			pr_err("hv_post_msg() failed; error code:%d\n", ret);
			return -EINVAL;
		}

		retries++;
		if (can_sleep && usec > 1000)
			msleep(usec / 1000);
		else if (usec < MAX_UDELAY_MS * 1000)
			udelay(usec);
		else
			mdelay(usec / 1000);

		if (usec < 256000)
			usec *= 2;
	}
	return ret;
}

/*
 * vmbus_set_event - Send an event notification to the parent
 */
void vmbus_set_event(struct vmbus_channel *channel)
{
	u32 child_relid = channel->offermsg.child_relid;

	if (!channel->is_dedicated_interrupt) {
		/* Each u32 represents 32 channels */
		sync_set_bit(child_relid & 31,
			(unsigned long *)vmbus_connection.send_int_page +
			(child_relid >> 5));
	}

	hv_do_hypercall(HVCALL_SIGNAL_EVENT, channel->sig_event, NULL);
}
EXPORT_SYMBOL_GPL(vmbus_set_event);
