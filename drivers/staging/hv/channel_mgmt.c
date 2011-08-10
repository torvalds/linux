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

#include "hyperv.h"
#include "hyperv_vmbus.h"

struct vmbus_channel_message_table_entry {
	enum vmbus_channel_message_type message_type;
	void (*message_handler)(struct vmbus_channel_message_header *msg);
};

#define MAX_MSG_TYPES                    4
#define MAX_NUM_DEVICE_CLASSES_SUPPORTED 8

static const struct hv_guid
	supported_device_classes[MAX_NUM_DEVICE_CLASSES_SUPPORTED] = {
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
	/* {A9A0F4E7-5A45-4d96-B827-8A841E8C03E6} */
	/* KVP */
	{
		.data = {
			0xe7, 0xf4, 0xa0, 0xa9, 0x45, 0x5a, 0x96, 0x4d,
			0xb8, 0x27, 0x8a, 0x84, 0x1e, 0x8c, 0x3,  0xe6
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
 * driver handles requests like graceful shutdown, heartbeats etc.
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

	if (channel->util_index >= 0) {
		/*
		 * This is a properly initialized util channel.
		 * Route this callback appropriately and setup state
		 * so that we don't need to reroute again.
		 */
		if (hv_cb_utils[channel->util_index].callback != NULL) {
			/*
			 * The util driver has established a handler for
			 * this service; do the magic.
			 */
			channel->onchannel_callback =
			hv_cb_utils[channel->util_index].callback;
			(hv_cb_utils[channel->util_index].callback)(channel);
			return;
		}
	}

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
				       VM_PKT_DATA_INBAND, 0);
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
		.log_msg = "Heartbeat channel functionality initialized"
	},
	/* {A9A0F4E7-5A45-4d96-B827-8A841E8C03E6} */
	/* KVP */
	{
		.data = {
			0xe7, 0xf4, 0xa0, 0xa9, 0x45, 0x5a, 0x96, 0x4d,
			0xb8, 0x27, 0x8a, 0x84, 0x1e, 0x8c, 0x3,  0xe6
		},
		.log_msg = "KVP channel functionality initialized"
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
void free_channel(struct vmbus_channel *channel)
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
	spin_lock_irqsave(&vmbus_connection.channel_lock, flags);

	list_for_each_entry(channel, &vmbus_connection.chn_list, listentry) {
		if (!memcmp(&channel->offermsg.offer.if_type,
			    &newchannel->offermsg.offer.if_type,
			    sizeof(struct hv_guid)) &&
		    !memcmp(&channel->offermsg.offer.if_instance,
			    &newchannel->offermsg.offer.if_instance,
			    sizeof(struct hv_guid))) {
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
	newchannel->device_obj = vmbus_child_device_create(
		&newchannel->offermsg.offer.if_type,
		&newchannel->offermsg.offer.if_instance,
		newchannel);

	/*
	 * Add the new device to the bus. This will kick off device-driver
	 * binding which eventually invokes the device driver's AddDevice()
	 * method.
	 */
	ret = vmbus_child_device_register(newchannel->device_obj);
	if (ret != 0) {
		pr_err("unable to add child device object (relid %d)\n",
			   newchannel->offermsg.child_relid);

		spin_lock_irqsave(&vmbus_connection.channel_lock, flags);
		list_del(&newchannel->listentry);
		spin_unlock_irqrestore(&vmbus_connection.channel_lock, flags);

		free_channel(newchannel);
	} else {
		/*
		 * This state is used to indicate a successful open
		 * so that when we do close the channel normally, we
		 * can cleanup properly
		 */
		newchannel->state = CHANNEL_OPEN_STATE;
		newchannel->util_index = -1; /* Invalid index */

		/* Open IC channels */
		for (cnt = 0; cnt < MAX_MSG_TYPES; cnt++) {
			if (memcmp(&newchannel->offermsg.offer.if_type,
				   &hv_cb_utils[cnt].data,
				   sizeof(struct hv_guid)) == 0 &&
				vmbus_open(newchannel, 2 * PAGE_SIZE,
						 2 * PAGE_SIZE, NULL, 0,
						 chn_cb_negotiate,
						 newchannel) == 0) {
				hv_cb_utils[cnt].channel = newchannel;
				newchannel->util_index = cnt;

				pr_info("%s\n", hv_cb_utils[cnt].log_msg);

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
		if (memcmp(&offer->offer.if_type,
			&supported_device_classes[i],
			sizeof(struct hv_guid)) == 0) {
			fsupported = 1;
			break;
		}
	}

	if (!fsupported)
		return;

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
