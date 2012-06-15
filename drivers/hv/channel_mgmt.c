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
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/completion.h>
#include <linux/hyperv.h>

#include "hyperv_vmbus.h"

struct vmbus_channel_message_table_entry {
	enum vmbus_channel_message_type message_type;
	void (*message_handler)(struct vmbus_channel_message_header *msg);
};


/**
 * vmbus_prep_negotiate_resp() - Create default response for Hyper-V Negotiate message
 * @icmsghdrp: Pointer to msg header structure
 * @icmsg_negotiate: Pointer to negotiate message structure
 * @buf: Raw buffer channel data
 *
 * @icmsghdrp is of type &struct icmsg_hdr.
 * @negop is of type &struct icmsg_negotiate.
 * Set up and fill in default negotiate response message.
 *
 * The max_fw_version specifies the maximum framework version that
 * we can support and max _srv_version specifies the maximum service
 * version we can support. A special value MAX_SRV_VER can be
 * specified to indicate that we can handle the maximum version
 * exposed by the host.
 *
 * Mainly used by Hyper-V drivers.
 */
void vmbus_prep_negotiate_resp(struct icmsg_hdr *icmsghdrp,
				struct icmsg_negotiate *negop, u8 *buf,
				int max_fw_version, int max_srv_version)
{
	int icframe_vercnt;
	int icmsg_vercnt;
	int i;

	icmsghdrp->icmsgsize = 0x10;

	negop = (struct icmsg_negotiate *)&buf[
		sizeof(struct vmbuspipe_hdr) +
		sizeof(struct icmsg_hdr)];

	icframe_vercnt = negop->icframe_vercnt;
	icmsg_vercnt = negop->icmsg_vercnt;

	/*
	 * Select the framework version number we will
	 * support.
	 */

	for (i = 0; i < negop->icframe_vercnt; i++) {
		if (negop->icversion_data[i].major <= max_fw_version)
			icframe_vercnt = negop->icversion_data[i].major;
	}

	for (i = negop->icframe_vercnt;
		 (i < negop->icframe_vercnt + negop->icmsg_vercnt); i++) {
		if (negop->icversion_data[i].major <= max_srv_version)
			icmsg_vercnt = negop->icversion_data[i].major;
	}

	/*
	 * Respond with the maximum framework and service
	 * version numbers we can support.
	 */
	negop->icframe_vercnt = 1;
	negop->icmsg_vercnt = 1;
	negop->icversion_data[0].major = icframe_vercnt;
	negop->icversion_data[0].minor = 0;
	negop->icversion_data[1].major = icmsg_vercnt;
	negop->icversion_data[1].minor = 0;
}

EXPORT_SYMBOL_GPL(vmbus_prep_negotiate_resp);

/*
 * alloc_channel - Allocate and initialize a vmbus channel object
 */
static struct vmbus_channel *alloc_channel(void)
{
	struct vmbus_channel *channel;

	channel = kzalloc(sizeof(*channel), GFP_ATOMIC);
	if (!channel)
		return NULL;

	spin_lock_init(&channel->inbound_lock);

	channel->controlwq = create_workqueue("hv_vmbus_ctl");
	if (!channel->controlwq) {
		kfree(channel);
		return NULL;
	}

	return channel;
}

/*
 * release_hannel - Release the vmbus channel object itself
 */
static void release_channel(struct work_struct *work)
{
	struct vmbus_channel *channel = container_of(work,
						     struct vmbus_channel,
						     work);

	destroy_workqueue(channel->controlwq);

	kfree(channel);
}

/*
 * free_channel - Release the resources used by the vmbus channel object
 */
static void free_channel(struct vmbus_channel *channel)
{

	/*
	 * We have to release the channel's workqueue/thread in the vmbus's
	 * workqueue/thread context
	 * ie we can't destroy ourselves.
	 */
	INIT_WORK(&channel->work, release_channel);
	queue_work(vmbus_connection.work_queue, &channel->work);
}



/*
 * vmbus_process_rescind_offer -
 * Rescind the offer by initiating a device removal
 */
static void vmbus_process_rescind_offer(struct work_struct *work)
{
	struct vmbus_channel *channel = container_of(work,
						     struct vmbus_channel,
						     work);

	vmbus_device_unregister(channel->device_obj);
}

void vmbus_free_channels(void)
{
	struct vmbus_channel *channel;

	list_for_each_entry(channel, &vmbus_connection.chn_list, listentry) {
		vmbus_device_unregister(channel->device_obj);
		kfree(channel->device_obj);
		free_channel(channel);
	}
}

/*
 * vmbus_process_offer - Process the offer by creating a channel/device
 * associated with this offer
 */
static void vmbus_process_offer(struct work_struct *work)
{
	struct vmbus_channel *newchannel = container_of(work,
							struct vmbus_channel,
							work);
	struct vmbus_channel *channel;
	bool fnew = true;
	int ret;
	unsigned long flags;

	/* The next possible work is rescind handling */
	INIT_WORK(&newchannel->work, vmbus_process_rescind_offer);

	/* Make sure this is a new offer */
	spin_lock_irqsave(&vmbus_connection.channel_lock, flags);

	list_for_each_entry(channel, &vmbus_connection.chn_list, listentry) {
		if (!uuid_le_cmp(channel->offermsg.offer.if_type,
			newchannel->offermsg.offer.if_type) &&
			!uuid_le_cmp(channel->offermsg.offer.if_instance,
				newchannel->offermsg.offer.if_instance)) {
			fnew = false;
			break;
		}
	}

	if (fnew)
		list_add_tail(&newchannel->listentry,
			      &vmbus_connection.chn_list);

	spin_unlock_irqrestore(&vmbus_connection.channel_lock, flags);

	if (!fnew) {
		free_channel(newchannel);
		return;
	}

	/*
	 * Start the process of binding this offer to the driver
	 * We need to set the DeviceObject field before calling
	 * vmbus_child_dev_add()
	 */
	newchannel->device_obj = vmbus_device_create(
		&newchannel->offermsg.offer.if_type,
		&newchannel->offermsg.offer.if_instance,
		newchannel);

	/*
	 * Add the new device to the bus. This will kick off device-driver
	 * binding which eventually invokes the device driver's AddDevice()
	 * method.
	 */
	ret = vmbus_device_register(newchannel->device_obj);
	if (ret != 0) {
		pr_err("unable to add child device object (relid %d)\n",
			   newchannel->offermsg.child_relid);

		spin_lock_irqsave(&vmbus_connection.channel_lock, flags);
		list_del(&newchannel->listentry);
		spin_unlock_irqrestore(&vmbus_connection.channel_lock, flags);
		kfree(newchannel->device_obj);

		free_channel(newchannel);
	} else {
		/*
		 * This state is used to indicate a successful open
		 * so that when we do close the channel normally, we
		 * can cleanup properly
		 */
		newchannel->state = CHANNEL_OPEN_STATE;
	}
}

/*
 * vmbus_onoffer - Handler for channel offers from vmbus in parent partition.
 *
 */
static void vmbus_onoffer(struct vmbus_channel_message_header *hdr)
{
	struct vmbus_channel_offer_channel *offer;
	struct vmbus_channel *newchannel;
	uuid_le *guidtype;
	uuid_le *guidinstance;

	offer = (struct vmbus_channel_offer_channel *)hdr;

	guidtype = &offer->offer.if_type;
	guidinstance = &offer->offer.if_instance;

	/* Allocate the channel object and save this offer. */
	newchannel = alloc_channel();
	if (!newchannel) {
		pr_err("Unable to allocate channel object\n");
		return;
	}

	memcpy(&newchannel->offermsg, offer,
	       sizeof(struct vmbus_channel_offer_channel));
	newchannel->monitor_grp = (u8)offer->monitorid / 32;
	newchannel->monitor_bit = (u8)offer->monitorid % 32;

	INIT_WORK(&newchannel->work, vmbus_process_offer);
	queue_work(newchannel->controlwq, &newchannel->work);
}

/*
 * vmbus_onoffer_rescind - Rescind offer handler.
 *
 * We queue a work item to process this offer synchronously
 */
static void vmbus_onoffer_rescind(struct vmbus_channel_message_header *hdr)
{
	struct vmbus_channel_rescind_offer *rescind;
	struct vmbus_channel *channel;

	rescind = (struct vmbus_channel_rescind_offer *)hdr;
	channel = relid2channel(rescind->child_relid);

	if (channel == NULL)
		/* Just return here, no channel found */
		return;

	/* work is initialized for vmbus_process_rescind_offer() from
	 * vmbus_process_offer() where the channel got created */
	queue_work(channel->controlwq, &channel->work);
}

/*
 * vmbus_onoffers_delivered -
 * This is invoked when all offers have been delivered.
 *
 * Nothing to do here.
 */
static void vmbus_onoffers_delivered(
			struct vmbus_channel_message_header *hdr)
{
}

/*
 * vmbus_onopen_result - Open result handler.
 *
 * This is invoked when we received a response to our channel open request.
 * Find the matching request, copy the response and signal the requesting
 * thread.
 */
static void vmbus_onopen_result(struct vmbus_channel_message_header *hdr)
{
	struct vmbus_channel_open_result *result;
	struct vmbus_channel_msginfo *msginfo;
	struct vmbus_channel_message_header *requestheader;
	struct vmbus_channel_open_channel *openmsg;
	unsigned long flags;

	result = (struct vmbus_channel_open_result *)hdr;

	/*
	 * Find the open msg, copy the result and signal/unblock the wait event
	 */
	spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);

	list_for_each_entry(msginfo, &vmbus_connection.chn_msg_list,
				msglistentry) {
		requestheader =
			(struct vmbus_channel_message_header *)msginfo->msg;

		if (requestheader->msgtype == CHANNELMSG_OPENCHANNEL) {
			openmsg =
			(struct vmbus_channel_open_channel *)msginfo->msg;
			if (openmsg->child_relid == result->child_relid &&
			    openmsg->openid == result->openid) {
				memcpy(&msginfo->response.open_result,
				       result,
				       sizeof(
					struct vmbus_channel_open_result));
				complete(&msginfo->waitevent);
				break;
			}
		}
	}
	spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock, flags);
}

/*
 * vmbus_ongpadl_created - GPADL created handler.
 *
 * This is invoked when we received a response to our gpadl create request.
 * Find the matching request, copy the response and signal the requesting
 * thread.
 */
static void vmbus_ongpadl_created(struct vmbus_channel_message_header *hdr)
{
	struct vmbus_channel_gpadl_created *gpadlcreated;
	struct vmbus_channel_msginfo *msginfo;
	struct vmbus_channel_message_header *requestheader;
	struct vmbus_channel_gpadl_header *gpadlheader;
	unsigned long flags;

	gpadlcreated = (struct vmbus_channel_gpadl_created *)hdr;

	/*
	 * Find the establish msg, copy the result and signal/unblock the wait
	 * event
	 */
	spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);

	list_for_each_entry(msginfo, &vmbus_connection.chn_msg_list,
				msglistentry) {
		requestheader =
			(struct vmbus_channel_message_header *)msginfo->msg;

		if (requestheader->msgtype == CHANNELMSG_GPADL_HEADER) {
			gpadlheader =
			(struct vmbus_channel_gpadl_header *)requestheader;

			if ((gpadlcreated->child_relid ==
			     gpadlheader->child_relid) &&
			    (gpadlcreated->gpadl == gpadlheader->gpadl)) {
				memcpy(&msginfo->response.gpadl_created,
				       gpadlcreated,
				       sizeof(
					struct vmbus_channel_gpadl_created));
				complete(&msginfo->waitevent);
				break;
			}
		}
	}
	spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock, flags);
}

/*
 * vmbus_ongpadl_torndown - GPADL torndown handler.
 *
 * This is invoked when we received a response to our gpadl teardown request.
 * Find the matching request, copy the response and signal the requesting
 * thread.
 */
static void vmbus_ongpadl_torndown(
			struct vmbus_channel_message_header *hdr)
{
	struct vmbus_channel_gpadl_torndown *gpadl_torndown;
	struct vmbus_channel_msginfo *msginfo;
	struct vmbus_channel_message_header *requestheader;
	struct vmbus_channel_gpadl_teardown *gpadl_teardown;
	unsigned long flags;

	gpadl_torndown = (struct vmbus_channel_gpadl_torndown *)hdr;

	/*
	 * Find the open msg, copy the result and signal/unblock the wait event
	 */
	spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);

	list_for_each_entry(msginfo, &vmbus_connection.chn_msg_list,
				msglistentry) {
		requestheader =
			(struct vmbus_channel_message_header *)msginfo->msg;

		if (requestheader->msgtype == CHANNELMSG_GPADL_TEARDOWN) {
			gpadl_teardown =
			(struct vmbus_channel_gpadl_teardown *)requestheader;

			if (gpadl_torndown->gpadl == gpadl_teardown->gpadl) {
				memcpy(&msginfo->response.gpadl_torndown,
				       gpadl_torndown,
				       sizeof(
					struct vmbus_channel_gpadl_torndown));
				complete(&msginfo->waitevent);
				break;
			}
		}
	}
	spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock, flags);
}

/*
 * vmbus_onversion_response - Version response handler
 *
 * This is invoked when we received a response to our initiate contact request.
 * Find the matching request, copy the response and signal the requesting
 * thread.
 */
static void vmbus_onversion_response(
		struct vmbus_channel_message_header *hdr)
{
	struct vmbus_channel_msginfo *msginfo;
	struct vmbus_channel_message_header *requestheader;
	struct vmbus_channel_initiate_contact *initiate;
	struct vmbus_channel_version_response *version_response;
	unsigned long flags;

	version_response = (struct vmbus_channel_version_response *)hdr;
	spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);

	list_for_each_entry(msginfo, &vmbus_connection.chn_msg_list,
				msglistentry) {
		requestheader =
			(struct vmbus_channel_message_header *)msginfo->msg;

		if (requestheader->msgtype ==
		    CHANNELMSG_INITIATE_CONTACT) {
			initiate =
			(struct vmbus_channel_initiate_contact *)requestheader;
			memcpy(&msginfo->response.version_response,
			      version_response,
			      sizeof(struct vmbus_channel_version_response));
			complete(&msginfo->waitevent);
		}
	}
	spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock, flags);
}

/* Channel message dispatch table */
static struct vmbus_channel_message_table_entry
	channel_message_table[CHANNELMSG_COUNT] = {
	{CHANNELMSG_INVALID,			NULL},
	{CHANNELMSG_OFFERCHANNEL,		vmbus_onoffer},
	{CHANNELMSG_RESCIND_CHANNELOFFER,	vmbus_onoffer_rescind},
	{CHANNELMSG_REQUESTOFFERS,		NULL},
	{CHANNELMSG_ALLOFFERS_DELIVERED,	vmbus_onoffers_delivered},
	{CHANNELMSG_OPENCHANNEL,		NULL},
	{CHANNELMSG_OPENCHANNEL_RESULT,	vmbus_onopen_result},
	{CHANNELMSG_CLOSECHANNEL,		NULL},
	{CHANNELMSG_GPADL_HEADER,		NULL},
	{CHANNELMSG_GPADL_BODY,		NULL},
	{CHANNELMSG_GPADL_CREATED,		vmbus_ongpadl_created},
	{CHANNELMSG_GPADL_TEARDOWN,		NULL},
	{CHANNELMSG_GPADL_TORNDOWN,		vmbus_ongpadl_torndown},
	{CHANNELMSG_RELID_RELEASED,		NULL},
	{CHANNELMSG_INITIATE_CONTACT,		NULL},
	{CHANNELMSG_VERSION_RESPONSE,		vmbus_onversion_response},
	{CHANNELMSG_UNLOAD,			NULL},
};

/*
 * vmbus_onmessage - Handler for channel protocol messages.
 *
 * This is invoked in the vmbus worker thread context.
 */
void vmbus_onmessage(void *context)
{
	struct hv_message *msg = context;
	struct vmbus_channel_message_header *hdr;
	int size;

	hdr = (struct vmbus_channel_message_header *)msg->u.payload;
	size = msg->header.payload_size;

	if (hdr->msgtype >= CHANNELMSG_COUNT) {
		pr_err("Received invalid channel message type %d size %d\n",
			   hdr->msgtype, size);
		print_hex_dump_bytes("", DUMP_PREFIX_NONE,
				     (unsigned char *)msg->u.payload, size);
		return;
	}

	if (channel_message_table[hdr->msgtype].message_handler)
		channel_message_table[hdr->msgtype].message_handler(hdr);
	else
		pr_err("Unhandled channel message type %d\n", hdr->msgtype);
}

/*
 * vmbus_request_offers - Send a request to get all our pending offers.
 */
int vmbus_request_offers(void)
{
	struct vmbus_channel_message_header *msg;
	struct vmbus_channel_msginfo *msginfo;
	int ret, t;

	msginfo = kmalloc(sizeof(*msginfo) +
			  sizeof(struct vmbus_channel_message_header),
			  GFP_KERNEL);
	if (!msginfo)
		return -ENOMEM;

	init_completion(&msginfo->waitevent);

	msg = (struct vmbus_channel_message_header *)msginfo->msg;

	msg->msgtype = CHANNELMSG_REQUESTOFFERS;


	ret = vmbus_post_msg(msg,
			       sizeof(struct vmbus_channel_message_header));
	if (ret != 0) {
		pr_err("Unable to request offers - %d\n", ret);

		goto cleanup;
	}

	t = wait_for_completion_timeout(&msginfo->waitevent, 5*HZ);
	if (t == 0) {
		ret = -ETIMEDOUT;
		goto cleanup;
	}



cleanup:
	kfree(msginfo);

	return ret;
}

/* eof */
