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
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/completion.h>
#include "osd.h"
#include "logging.h"
#include "vmbus_private.h"
#include "utils.h"

struct vmbus_channel_message_table_entry {
	enum vmbus_channel_message_type messageType;
	void (*messageHandler)(struct vmbus_channel_message_header *msg);
};

#define MAX_MSG_TYPES                    3
#define MAX_NUM_DEVICE_CLASSES_SUPPORTED 7

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
	/* 0E0B6031-5213-4934-818B-38D90CED39DB */
	/* Shutdown */
	{
		.data = {
			0x31, 0x60, 0x0B, 0X0E, 0x13, 0x52, 0x34, 0x49,
			0x81, 0x8B, 0x38, 0XD9, 0x0C, 0xED, 0x39, 0xDB
		}
	},
	/* {9527E630-D0AE-497b-ADCE-E80AB0175CAF} */
	/* TimeSync */
	{
		.data = {
			0x30, 0xe6, 0x27, 0x95, 0xae, 0xd0, 0x7b, 0x49,
			0xad, 0xce, 0xe8, 0x0a, 0xb0, 0x17, 0x5c, 0xaf
		}
	},
	/* {57164f39-9115-4e78-ab55-382f3bd5422d} */
	/* Heartbeat */
	{
		.data = {
			0x39, 0x4f, 0x16, 0x57, 0x15, 0x91, 0x78, 0x4e,
			0xab, 0x55, 0x38, 0x2f, 0x3b, 0xd5, 0x42, 0x2d
		}
	},
};


/**
 * prep_negotiate_resp() - Create default response for Hyper-V Negotiate message
 * @icmsghdrp: Pointer to msg header structure
 * @icmsg_negotiate: Pointer to negotiate message structure
 * @buf: Raw buffer channel data
 *
 * @icmsghdrp is of type &struct icmsg_hdr.
 * @negop is of type &struct icmsg_negotiate.
 * Set up and fill in default negotiate response message. This response can
 * come from both the vmbus driver and the hv_utils driver. The current api
 * will respond properly to both Windows 2008 and Windows 2008-R2 operating
 * systems.
 *
 * Mainly used by Hyper-V drivers.
 */
void prep_negotiate_resp(struct icmsg_hdr *icmsghdrp,
			     struct icmsg_negotiate *negop,
			     u8 *buf)
{
	if (icmsghdrp->icmsgtype == ICMSGTYPE_NEGOTIATE) {
		icmsghdrp->icmsgsize = 0x10;

		negop = (struct icmsg_negotiate *)&buf[
			sizeof(struct vmbuspipe_hdr) +
			sizeof(struct icmsg_hdr)];

		if (negop->icframe_vercnt == 2 &&
		   negop->icversion_data[1].major == 3) {
			negop->icversion_data[0].major = 3;
			negop->icversion_data[0].minor = 0;
			negop->icversion_data[1].major = 3;
			negop->icversion_data[1].minor = 0;
		} else {
			negop->icversion_data[0].major = 1;
			negop->icversion_data[0].minor = 0;
			negop->icversion_data[1].major = 1;
			negop->icversion_data[1].minor = 0;
		}

		negop->icframe_vercnt = 1;
		negop->icmsg_vercnt = 1;
	}
}
EXPORT_SYMBOL(prep_negotiate_resp);

/**
 * chn_cb_negotiate() - Default handler for non IDE/SCSI/NETWORK
 * Hyper-V requests
 * @context: Pointer to argument structure.
 *
 * Set up the default handler for non device driver specific requests
 * from Hyper-V. This stub responds to the default negotiate messages
 * that come in for every non IDE/SCSI/Network request.
 * This behavior is normally overwritten in the hv_utils driver. That
 * driver handles requests like gracefull shutdown, heartbeats etc.
 *
 * Mainly used by Hyper-V drivers.
 */
void chn_cb_negotiate(void *context)
{
	struct vmbus_channel *channel = context;
	u8 *buf;
	u32 buflen, recvlen;
	u64 requestid;

	struct icmsg_hdr *icmsghdrp;
	struct icmsg_negotiate *negop = NULL;

	buflen = PAGE_SIZE;
	buf = kmalloc(buflen, GFP_ATOMIC);

	vmbus_recvpacket(channel, buf, buflen, &recvlen, &requestid);

	if (recvlen > 0) {
		icmsghdrp = (struct icmsg_hdr *)&buf[
			sizeof(struct vmbuspipe_hdr)];

		prep_negotiate_resp(icmsghdrp, negop, buf);

		icmsghdrp->icflags = ICMSGHDRFLAG_TRANSACTION
			| ICMSGHDRFLAG_RESPONSE;

		vmbus_sendpacket(channel, buf,
				       recvlen, requestid,
				       VmbusPacketTypeDataInBand, 0);
	}

	kfree(buf);
}
EXPORT_SYMBOL(chn_cb_negotiate);

/*
 * Function table used for message responses for non IDE/SCSI/Network type
 * messages. (Such as KVP/Shutdown etc)
 */
struct hyperv_service_callback hv_cb_utils[MAX_MSG_TYPES] = {
	/* 0E0B6031-5213-4934-818B-38D90CED39DB */
	/* Shutdown */
	{
		.msg_type = HV_SHUTDOWN_MSG,
		.data = {
			0x31, 0x60, 0x0B, 0X0E, 0x13, 0x52, 0x34, 0x49,
			0x81, 0x8B, 0x38, 0XD9, 0x0C, 0xED, 0x39, 0xDB
		},
		.callback = chn_cb_negotiate,
		.log_msg = "Shutdown channel functionality initialized"
	},

	/* {9527E630-D0AE-497b-ADCE-E80AB0175CAF} */
	/* TimeSync */
	{
		.msg_type = HV_TIMESYNC_MSG,
		.data = {
			0x30, 0xe6, 0x27, 0x95, 0xae, 0xd0, 0x7b, 0x49,
			0xad, 0xce, 0xe8, 0x0a, 0xb0, 0x17, 0x5c, 0xaf
		},
		.callback = chn_cb_negotiate,
		.log_msg = "Timesync channel functionality initialized"
	},
	/* {57164f39-9115-4e78-ab55-382f3bd5422d} */
	/* Heartbeat */
	{
		.msg_type = HV_HEARTBEAT_MSG,
		.data = {
			0x39, 0x4f, 0x16, 0x57, 0x15, 0x91, 0x78, 0x4e,
			0xab, 0x55, 0x38, 0x2f, 0x3b, 0xd5, 0x42, 0x2d
		},
		.callback = chn_cb_negotiate,
		.log_msg = "Heartbeat channel functionality initialized"
	},
};
EXPORT_SYMBOL(hv_cb_utils);

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

	init_timer(&channel->poll_timer);
	channel->poll_timer.data = (unsigned long)channel;
	channel->poll_timer.function = vmbus_ontimer;

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

	DPRINT_DBG(VMBUS, "releasing channel (%p)", channel);
	destroy_workqueue(channel->controlwq);
	DPRINT_DBG(VMBUS, "channel released (%p)", channel);

	kfree(channel);
}

/*
 * free_channel - Release the resources used by the vmbus channel object
 */
void free_channel(struct vmbus_channel *channel)
{
	del_timer_sync(&channel->poll_timer);

	/*
	 * We have to release the channel's workqueue/thread in the vmbus's
	 * workqueue/thread context
	 * ie we can't destroy ourselves.
	 */
	INIT_WORK(&channel->work, release_channel);
	queue_work(gVmbusConnection.WorkQueue, &channel->work);
}


DECLARE_COMPLETION(hv_channel_ready);

/*
 * Count initialized channels, and ensure all channels are ready when hv_vmbus
 * module loading completes.
 */
static void count_hv_channel(void)
{
	static int counter;
	unsigned long flags;

	spin_lock_irqsave(&gVmbusConnection.channel_lock, flags);
	if (++counter == MAX_MSG_TYPES)
		complete(&hv_channel_ready);
	spin_unlock_irqrestore(&gVmbusConnection.channel_lock, flags);
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

	vmbus_child_device_unregister(channel->device_obj);
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
	int cnt;
	unsigned long flags;

	/* The next possible work is rescind handling */
	INIT_WORK(&newchannel->work, vmbus_process_rescind_offer);

	/* Make sure this is a new offer */
	spin_lock_irqsave(&gVmbusConnection.channel_lock, flags);

	list_for_each_entry(channel, &gVmbusConnection.ChannelList, listentry) {
		if (!memcmp(&channel->offermsg.offer.InterfaceType,
			    &newchannel->offermsg.offer.InterfaceType,
			    sizeof(struct hv_guid)) &&
		    !memcmp(&channel->offermsg.offer.InterfaceInstance,
			    &newchannel->offermsg.offer.InterfaceInstance,
			    sizeof(struct hv_guid))) {
			fnew = false;
			break;
		}
	}

	if (fnew)
		list_add_tail(&newchannel->listentry,
			      &gVmbusConnection.ChannelList);

	spin_unlock_irqrestore(&gVmbusConnection.channel_lock, flags);

	if (!fnew) {
		DPRINT_DBG(VMBUS, "Ignoring duplicate offer for relid (%d)",
			   newchannel->offermsg.child_relid);
		free_channel(newchannel);
		return;
	}

	/*
	 * Start the process of binding this offer to the driver
	 * We need to set the DeviceObject field before calling
	 * VmbusChildDeviceAdd()
	 */
	newchannel->device_obj = vmbus_child_device_create(
		&newchannel->offermsg.offer.InterfaceType,
		&newchannel->offermsg.offer.InterfaceInstance,
		newchannel);

	DPRINT_DBG(VMBUS, "child device object allocated - %p",
		   newchannel->device_obj);

	/*
	 * Add the new device to the bus. This will kick off device-driver
	 * binding which eventually invokes the device driver's AddDevice()
	 * method.
	 */
	ret = VmbusChildDeviceAdd(newchannel->device_obj);
	if (ret != 0) {
		DPRINT_ERR(VMBUS,
			   "unable to add child device object (relid %d)",
			   newchannel->offermsg.child_relid);

		spin_lock_irqsave(&gVmbusConnection.channel_lock, flags);
		list_del(&newchannel->listentry);
		spin_unlock_irqrestore(&gVmbusConnection.channel_lock, flags);

		free_channel(newchannel);
	} else {
		/*
		 * This state is used to indicate a successful open
		 * so that when we do close the channel normally, we
		 * can cleanup properly
		 */
		newchannel->state = CHANNEL_OPEN_STATE;

		/* Open IC channels */
		for (cnt = 0; cnt < MAX_MSG_TYPES; cnt++) {
			if (memcmp(&newchannel->offermsg.offer.InterfaceType,
				   &hv_cb_utils[cnt].data,
				   sizeof(struct hv_guid)) == 0 &&
				vmbus_open(newchannel, 2 * PAGE_SIZE,
						 2 * PAGE_SIZE, NULL, 0,
						 hv_cb_utils[cnt].callback,
						 newchannel) == 0) {
				hv_cb_utils[cnt].channel = newchannel;
				DPRINT_INFO(VMBUS, "%s",
						hv_cb_utils[cnt].log_msg);
				count_hv_channel();
			}
		}
	}
}

/*
 * vmbus_onoffer - Handler for channel offers from vmbus in parent partition.
 *
 * We ignore all offers except network and storage offers. For each network and
 * storage offers, we create a channel object and queue a work item to the
 * channel object to process the offer synchronously
 */
static void vmbus_onoffer(struct vmbus_channel_message_header *hdr)
{
	struct vmbus_channel_offer_channel *offer;
	struct vmbus_channel *newchannel;
	struct hv_guid *guidtype;
	struct hv_guid *guidinstance;
	int i;
	int fsupported = 0;

	offer = (struct vmbus_channel_offer_channel *)hdr;
	for (i = 0; i < MAX_NUM_DEVICE_CLASSES_SUPPORTED; i++) {
		if (memcmp(&offer->offer.InterfaceType,
		    &gSupportedDeviceClasses[i], sizeof(struct hv_guid)) == 0) {
			fsupported = 1;
			break;
		}
	}

	if (!fsupported) {
		DPRINT_DBG(VMBUS, "Ignoring channel offer notification for "
			   "child relid %d", offer->child_relid);
		return;
	}

	guidtype = &offer->offer.InterfaceType;
	guidinstance = &offer->offer.InterfaceInstance;

	DPRINT_INFO(VMBUS, "Channel offer notification - "
		    "child relid %d monitor id %d allocated %d, "
		    "type {%02x%02x%02x%02x-%02x%02x-%02x%02x-"
		    "%02x%02x%02x%02x%02x%02x%02x%02x} "
		    "instance {%02x%02x%02x%02x-%02x%02x-%02x%02x-"
		    "%02x%02x%02x%02x%02x%02x%02x%02x}",
		    offer->child_relid, offer->monitorid,
		    offer->monitor_allocated,
		    guidtype->data[3], guidtype->data[2],
		    guidtype->data[1], guidtype->data[0],
		    guidtype->data[5], guidtype->data[4],
		    guidtype->data[7], guidtype->data[6],
		    guidtype->data[8], guidtype->data[9],
		    guidtype->data[10], guidtype->data[11],
		    guidtype->data[12], guidtype->data[13],
		    guidtype->data[14], guidtype->data[15],
		    guidinstance->data[3], guidinstance->data[2],
		    guidinstance->data[1], guidinstance->data[0],
		    guidinstance->data[5], guidinstance->data[4],
		    guidinstance->data[7], guidinstance->data[6],
		    guidinstance->data[8], guidinstance->data[9],
		    guidinstance->data[10], guidinstance->data[11],
		    guidinstance->data[12], guidinstance->data[13],
		    guidinstance->data[14], guidinstance->data[15]);

	/* Allocate the channel object and save this offer. */
	newchannel = alloc_channel();
	if (!newchannel) {
		DPRINT_ERR(VMBUS, "unable to allocate channel object");
		return;
	}

	DPRINT_DBG(VMBUS, "channel object allocated - %p", newchannel);

	memcpy(&newchannel->offermsg, offer,
	       sizeof(struct vmbus_channel_offer_channel));
	newchannel->monitor_grp = (u8)offer->monitorid / 32;
	newchannel->monitor_bit = (u8)offer->monitorid % 32;

	/* TODO: Make sure the offer comes from our parent partition */
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
	channel = GetChannelFromRelId(rescind->child_relid);
	if (channel == NULL) {
		DPRINT_DBG(VMBUS, "channel not found for relId %d",
			   rescind->child_relid);
		return;
	}

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
	struct list_head *curr;
	struct vmbus_channel_msginfo *msginfo;
	struct vmbus_channel_message_header *requestheader;
	struct vmbus_channel_open_channel *openmsg;
	unsigned long flags;

	result = (struct vmbus_channel_open_result *)hdr;
	DPRINT_DBG(VMBUS, "vmbus open result - %d", result->status);

	/*
	 * Find the open msg, copy the result and signal/unblock the wait event
	 */
	spin_lock_irqsave(&gVmbusConnection.channelmsg_lock, flags);

	list_for_each(curr, &gVmbusConnection.ChannelMsgList) {
/* FIXME: this should probably use list_entry() instead */
		msginfo = (struct vmbus_channel_msginfo *)curr;
		requestheader =
			(struct vmbus_channel_message_header *)msginfo->msg;

		if (requestheader->msgtype == CHANNELMSG_OPENCHANNEL) {
			openmsg =
			(struct vmbus_channel_open_channel *)msginfo->msg;
			if (openmsg->child_relid == result->child_relid &&
			    openmsg->openid == result->openid) {
				memcpy(&msginfo->response.open_result,
				       result,
				       sizeof(struct vmbus_channel_open_result));
				osd_waitevent_set(msginfo->waitevent);
				break;
			}
		}
	}
	spin_unlock_irqrestore(&gVmbusConnection.channelmsg_lock, flags);
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
	struct list_head *curr;
	struct vmbus_channel_msginfo *msginfo;
	struct vmbus_channel_message_header *requestheader;
	struct vmbus_channel_gpadl_header *gpadlheader;
	unsigned long flags;

	gpadlcreated = (struct vmbus_channel_gpadl_created *)hdr;
	DPRINT_DBG(VMBUS, "vmbus gpadl created result - %d",
		   gpadlcreated->creation_status);

	/*
	 * Find the establish msg, copy the result and signal/unblock the wait
	 * event
	 */
	spin_lock_irqsave(&gVmbusConnection.channelmsg_lock, flags);

	list_for_each(curr, &gVmbusConnection.ChannelMsgList) {
/* FIXME: this should probably use list_entry() instead */
		msginfo = (struct vmbus_channel_msginfo *)curr;
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
				       sizeof(struct vmbus_channel_gpadl_created));
				osd_waitevent_set(msginfo->waitevent);
				break;
			}
		}
	}
	spin_unlock_irqrestore(&gVmbusConnection.channelmsg_lock, flags);
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
	struct list_head *curr;
	struct vmbus_channel_msginfo *msginfo;
	struct vmbus_channel_message_header *requestheader;
	struct vmbus_channel_gpadl_teardown *gpadl_teardown;
	unsigned long flags;

	gpadl_torndown = (struct vmbus_channel_gpadl_torndown *)hdr;

	/*
	 * Find the open msg, copy the result and signal/unblock the wait event
	 */
	spin_lock_irqsave(&gVmbusConnection.channelmsg_lock, flags);

	list_for_each(curr, &gVmbusConnection.ChannelMsgList) {
/* FIXME: this should probably use list_entry() instead */
		msginfo = (struct vmbus_channel_msginfo *)curr;
		requestheader =
			(struct vmbus_channel_message_header *)msginfo->msg;

		if (requestheader->msgtype == CHANNELMSG_GPADL_TEARDOWN) {
			gpadl_teardown =
			(struct vmbus_channel_gpadl_teardown *)requestheader;

			if (gpadl_torndown->gpadl == gpadl_teardown->gpadl) {
				memcpy(&msginfo->response.gpadl_torndown,
				       gpadl_torndown,
				       sizeof(struct vmbus_channel_gpadl_torndown));
				osd_waitevent_set(msginfo->waitevent);
				break;
			}
		}
	}
	spin_unlock_irqrestore(&gVmbusConnection.channelmsg_lock, flags);
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
	struct list_head *curr;
	struct vmbus_channel_msginfo *msginfo;
	struct vmbus_channel_message_header *requestheader;
	struct vmbus_channel_initiate_contact *initiate;
	struct vmbus_channel_version_response *version_response;
	unsigned long flags;

	version_response = (struct vmbus_channel_version_response *)hdr;
	spin_lock_irqsave(&gVmbusConnection.channelmsg_lock, flags);

	list_for_each(curr, &gVmbusConnection.ChannelMsgList) {
/* FIXME: this should probably use list_entry() instead */
		msginfo = (struct vmbus_channel_msginfo *)curr;
		requestheader =
			(struct vmbus_channel_message_header *)msginfo->msg;

		if (requestheader->msgtype ==
		    CHANNELMSG_INITIATE_CONTACT) {
			initiate =
			(struct vmbus_channel_initiate_contact *)requestheader;
			memcpy(&msginfo->response.version_response,
			      version_response,
			      sizeof(struct vmbus_channel_version_response));
			osd_waitevent_set(msginfo->waitevent);
		}
	}
	spin_unlock_irqrestore(&gVmbusConnection.channelmsg_lock, flags);
}

/* Channel message dispatch table */
static struct vmbus_channel_message_table_entry
	gChannelMessageTable[CHANNELMSG_COUNT] = {
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

	DPRINT_DBG(VMBUS, "message type %d size %d", hdr->msgtype, size);

	if (hdr->msgtype >= CHANNELMSG_COUNT) {
		DPRINT_ERR(VMBUS,
			   "Received invalid channel message type %d size %d",
			   hdr->msgtype, size);
		print_hex_dump_bytes("", DUMP_PREFIX_NONE,
				     (unsigned char *)msg->u.payload, size);
		return;
	}

	if (gChannelMessageTable[hdr->msgtype].messageHandler)
		gChannelMessageTable[hdr->msgtype].messageHandler(hdr);
	else
		DPRINT_ERR(VMBUS, "Unhandled channel message type %d",
			   hdr->msgtype);
}

/*
 * vmbus_request_offers - Send a request to get all our pending offers.
 */
int vmbus_request_offers(void)
{
	struct vmbus_channel_message_header *msg;
	struct vmbus_channel_msginfo *msginfo;
	int ret;

	msginfo = kmalloc(sizeof(*msginfo) +
			  sizeof(struct vmbus_channel_message_header),
			  GFP_KERNEL);
	if (!msginfo)
		return -ENOMEM;

	msginfo->waitevent = osd_waitevent_create();
	if (!msginfo->waitevent) {
		kfree(msginfo);
		return -ENOMEM;
	}

	msg = (struct vmbus_channel_message_header *)msginfo->msg;

	msg->msgtype = CHANNELMSG_REQUESTOFFERS;

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
	/* osd_waitevent_wait(msgInfo->waitEvent); */

	/*SpinlockAcquire(gVmbusConnection.channelMsgLock);
	REMOVE_ENTRY_LIST(&msgInfo->msgListEntry);
	SpinlockRelease(gVmbusConnection.channelMsgLock);*/


Cleanup:
	if (msginfo) {
		kfree(msginfo->waitevent);
		kfree(msginfo);
	}

	return ret;
}

/*
 * vmbus_release_unattached_channels - Release channels that are
 * unattached/unconnected ie (no drivers associated)
 */
void vmbus_release_unattached_channels(void)
{
	struct vmbus_channel *channel, *pos;
	struct vmbus_channel *start = NULL;
	unsigned long flags;

	spin_lock_irqsave(&gVmbusConnection.channel_lock, flags);

	list_for_each_entry_safe(channel, pos, &gVmbusConnection.ChannelList,
				 listentry) {
		if (channel == start)
			break;

		if (!channel->device_obj->Driver) {
			list_del(&channel->listentry);
			DPRINT_INFO(VMBUS,
				    "Releasing unattached device object %p",
				    channel->device_obj);

			vmbus_child_device_unregister(channel->device_obj);
			free_channel(channel);
		} else {
			if (!start)
				start = channel;
		}
	}

	spin_unlock_irqrestore(&gVmbusConnection.channel_lock, flags);
}

/* eof */
