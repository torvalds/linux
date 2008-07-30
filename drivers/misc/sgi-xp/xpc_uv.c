/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2008 Silicon Graphics, Inc.  All Rights Reserved.
 */

/*
 * Cross Partition Communication (XPC) uv-based functions.
 *
 *     Architecture specific implementation of common functions.
 *
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <asm/uv/uv_hub.h>
#include "../sgi-gru/gru.h"
#include "../sgi-gru/grukservices.h"
#include "xpc.h"

static atomic64_t xpc_heartbeat_uv;
static DECLARE_BITMAP(xpc_heartbeating_to_mask_uv, XP_MAX_NPARTITIONS_UV);

#define XPC_ACTIVATE_MSG_SIZE_UV	(1 * GRU_CACHE_LINE_BYTES)
#define XPC_NOTIFY_MSG_SIZE_UV		(2 * GRU_CACHE_LINE_BYTES)

#define XPC_ACTIVATE_MQ_SIZE_UV	(4 * XP_MAX_NPARTITIONS_UV * \
				 XPC_ACTIVATE_MSG_SIZE_UV)
#define XPC_NOTIFY_MQ_SIZE_UV	(4 * XP_MAX_NPARTITIONS_UV * \
				 XPC_NOTIFY_MSG_SIZE_UV)

static void *xpc_activate_mq_uv;
static void *xpc_notify_mq_uv;

static int
xpc_setup_partitions_sn_uv(void)
{
	short partid;
	struct xpc_partition_uv *part_uv;

	for (partid = 0; partid < XP_MAX_NPARTITIONS_UV; partid++) {
		part_uv = &xpc_partitions[partid].sn.uv;

		spin_lock_init(&part_uv->flags_lock);
		part_uv->remote_act_state = XPC_P_AS_INACTIVE;
	}
	return 0;
}

static void *
xpc_create_gru_mq_uv(unsigned int mq_size, int cpuid, unsigned int irq,
		     irq_handler_t irq_handler)
{
	int ret;
	int nid;
	int mq_order;
	struct page *page;
	void *mq;

	nid = cpu_to_node(cpuid);
	mq_order = get_order(mq_size);
	page = alloc_pages_node(nid, GFP_KERNEL | __GFP_ZERO | GFP_THISNODE,
				mq_order);
	if (page == NULL)
		return NULL;

	mq = page_address(page);
	ret = gru_create_message_queue(mq, mq_size);
	if (ret != 0) {
		dev_err(xpc_part, "gru_create_message_queue() returned "
			"error=%d\n", ret);
		free_pages((unsigned long)mq, mq_order);
		return NULL;
	}

	/* !!! Need to do some other things to set up IRQ */

	ret = request_irq(irq, irq_handler, 0, "xpc", NULL);
	if (ret != 0) {
		dev_err(xpc_part, "request_irq(irq=%d) returned error=%d\n",
			irq, ret);
		free_pages((unsigned long)mq, mq_order);
		return NULL;
	}

	/* !!! enable generation of irq when GRU mq op occurs to this mq */

	/* ??? allow other partitions to access GRU mq? */

	return mq;
}

static void
xpc_destroy_gru_mq_uv(void *mq, unsigned int mq_size, unsigned int irq)
{
	/* ??? disallow other partitions to access GRU mq? */

	/* !!! disable generation of irq when GRU mq op occurs to this mq */

	free_irq(irq, NULL);

	free_pages((unsigned long)mq, get_order(mq_size));
}

static enum xp_retval
xpc_send_gru_msg(unsigned long mq_gpa, void *msg, size_t msg_size)
{
	enum xp_retval xp_ret;
	int ret;

	while (1) {
		ret = gru_send_message_gpa(mq_gpa, msg, msg_size);
		if (ret == MQE_OK) {
			xp_ret = xpSuccess;
			break;
		}

		if (ret == MQE_QUEUE_FULL) {
			dev_dbg(xpc_chan, "gru_send_message_gpa() returned "
				"error=MQE_QUEUE_FULL\n");
			/* !!! handle QLimit reached; delay & try again */
			/* ??? Do we add a limit to the number of retries? */
			(void)msleep_interruptible(10);
		} else if (ret == MQE_CONGESTION) {
			dev_dbg(xpc_chan, "gru_send_message_gpa() returned "
				"error=MQE_CONGESTION\n");
			/* !!! handle LB Overflow; simply try again */
			/* ??? Do we add a limit to the number of retries? */
		} else {
			/* !!! Currently this is MQE_UNEXPECTED_CB_ERR */
			dev_err(xpc_chan, "gru_send_message_gpa() returned "
				"error=%d\n", ret);
			xp_ret = xpGruSendMqError;
			break;
		}
	}
	return xp_ret;
}

static void
xpc_process_activate_IRQ_rcvd_uv(void)
{
	unsigned long irq_flags;
	short partid;
	struct xpc_partition *part;
	u8 act_state_req;

	DBUG_ON(xpc_activate_IRQ_rcvd == 0);

	spin_lock_irqsave(&xpc_activate_IRQ_rcvd_lock, irq_flags);
	for (partid = 0; partid < XP_MAX_NPARTITIONS_UV; partid++) {
		part = &xpc_partitions[partid];

		if (part->sn.uv.act_state_req == 0)
			continue;

		xpc_activate_IRQ_rcvd--;
		BUG_ON(xpc_activate_IRQ_rcvd < 0);

		act_state_req = part->sn.uv.act_state_req;
		part->sn.uv.act_state_req = 0;
		spin_unlock_irqrestore(&xpc_activate_IRQ_rcvd_lock, irq_flags);

		if (act_state_req == XPC_P_ASR_ACTIVATE_UV) {
			if (part->act_state == XPC_P_AS_INACTIVE)
				xpc_activate_partition(part);
			else if (part->act_state == XPC_P_AS_DEACTIVATING)
				XPC_DEACTIVATE_PARTITION(part, xpReactivating);

		} else if (act_state_req == XPC_P_ASR_REACTIVATE_UV) {
			if (part->act_state == XPC_P_AS_INACTIVE)
				xpc_activate_partition(part);
			else
				XPC_DEACTIVATE_PARTITION(part, xpReactivating);

		} else if (act_state_req == XPC_P_ASR_DEACTIVATE_UV) {
			XPC_DEACTIVATE_PARTITION(part, part->sn.uv.reason);

		} else {
			BUG();
		}

		spin_lock_irqsave(&xpc_activate_IRQ_rcvd_lock, irq_flags);
		if (xpc_activate_IRQ_rcvd == 0)
			break;
	}
	spin_unlock_irqrestore(&xpc_activate_IRQ_rcvd_lock, irq_flags);

}

static irqreturn_t
xpc_handle_activate_IRQ_uv(int irq, void *dev_id)
{
	unsigned long irq_flags;
	struct xpc_activate_mq_msghdr_uv *msg_hdr;
	short partid;
	struct xpc_partition *part;
	struct xpc_partition_uv *part_uv;
	struct xpc_openclose_args *args;
	int wakeup_hb_checker = 0;

	while ((msg_hdr = gru_get_next_message(xpc_activate_mq_uv)) != NULL) {

		partid = msg_hdr->partid;
		if (partid < 0 || partid >= XP_MAX_NPARTITIONS_UV) {
			dev_err(xpc_part, "xpc_handle_activate_IRQ_uv() invalid"
				"partid=0x%x passed in message\n", partid);
			gru_free_message(xpc_activate_mq_uv, msg_hdr);
			continue;
		}
		part = &xpc_partitions[partid];
		part_uv = &part->sn.uv;

		part_uv->remote_act_state = msg_hdr->act_state;

		switch (msg_hdr->type) {
		case XPC_ACTIVATE_MQ_MSG_SYNC_ACT_STATE_UV:
			/* syncing of remote_act_state was just done above */
			break;

		case XPC_ACTIVATE_MQ_MSG_INC_HEARTBEAT_UV: {
			struct xpc_activate_mq_msg_heartbeat_req_uv *msg;

			msg = (struct xpc_activate_mq_msg_heartbeat_req_uv *)
			    msg_hdr;
			part_uv->heartbeat = msg->heartbeat;
			break;
		}
		case XPC_ACTIVATE_MQ_MSG_OFFLINE_HEARTBEAT_UV: {
			struct xpc_activate_mq_msg_heartbeat_req_uv *msg;

			msg = (struct xpc_activate_mq_msg_heartbeat_req_uv *)
			    msg_hdr;
			part_uv->heartbeat = msg->heartbeat;
			spin_lock_irqsave(&part_uv->flags_lock, irq_flags);
			part_uv->flags |= XPC_P_HEARTBEAT_OFFLINE_UV;
			spin_unlock_irqrestore(&part_uv->flags_lock, irq_flags);
			break;
		}
		case XPC_ACTIVATE_MQ_MSG_ONLINE_HEARTBEAT_UV: {
			struct xpc_activate_mq_msg_heartbeat_req_uv *msg;

			msg = (struct xpc_activate_mq_msg_heartbeat_req_uv *)
			    msg_hdr;
			part_uv->heartbeat = msg->heartbeat;
			spin_lock_irqsave(&part_uv->flags_lock, irq_flags);
			part_uv->flags &= ~XPC_P_HEARTBEAT_OFFLINE_UV;
			spin_unlock_irqrestore(&part_uv->flags_lock, irq_flags);
			break;
		}
		case XPC_ACTIVATE_MQ_MSG_ACTIVATE_REQ_UV: {
			struct xpc_activate_mq_msg_activate_req_uv *msg;

			/*
			 * ??? Do we deal here with ts_jiffies being different
			 * ??? if act_state != XPC_P_AS_INACTIVE instead of
			 * ??? below?
			 */
			msg = (struct xpc_activate_mq_msg_activate_req_uv *)
			    msg_hdr;
			spin_lock_irqsave(&xpc_activate_IRQ_rcvd_lock,
					  irq_flags);
			if (part_uv->act_state_req == 0)
				xpc_activate_IRQ_rcvd++;
			part_uv->act_state_req = XPC_P_ASR_ACTIVATE_UV;
			part->remote_rp_pa = msg->rp_gpa; /* !!! _pa is _gpa */
			part->remote_rp_ts_jiffies = msg_hdr->rp_ts_jiffies;
			part_uv->remote_activate_mq_gpa = msg->activate_mq_gpa;
			spin_unlock_irqrestore(&xpc_activate_IRQ_rcvd_lock,
					       irq_flags);
			wakeup_hb_checker++;
			break;
		}
		case XPC_ACTIVATE_MQ_MSG_DEACTIVATE_REQ_UV: {
			struct xpc_activate_mq_msg_deactivate_req_uv *msg;

			msg = (struct xpc_activate_mq_msg_deactivate_req_uv *)
			    msg_hdr;
			spin_lock_irqsave(&xpc_activate_IRQ_rcvd_lock,
					  irq_flags);
			if (part_uv->act_state_req == 0)
				xpc_activate_IRQ_rcvd++;
			part_uv->act_state_req = XPC_P_ASR_DEACTIVATE_UV;
			part_uv->reason = msg->reason;
			spin_unlock_irqrestore(&xpc_activate_IRQ_rcvd_lock,
					       irq_flags);
			wakeup_hb_checker++;
			break;
		}
		case XPC_ACTIVATE_MQ_MSG_CHCTL_CLOSEREQUEST_UV: {
			struct xpc_activate_mq_msg_chctl_closerequest_uv *msg;

			msg = (struct xpc_activate_mq_msg_chctl_closerequest_uv
			    *)msg_hdr;
			args = &part->remote_openclose_args[msg->ch_number];
			args->reason = msg->reason;

			spin_lock_irqsave(&part->chctl_lock, irq_flags);
			part->chctl.flags[msg->ch_number] |=
			    XPC_CHCTL_CLOSEREQUEST;
			spin_unlock_irqrestore(&part->chctl_lock, irq_flags);

			xpc_wakeup_channel_mgr(part);
			break;
		}
		case XPC_ACTIVATE_MQ_MSG_CHCTL_CLOSEREPLY_UV: {
			struct xpc_activate_mq_msg_chctl_closereply_uv *msg;

			msg = (struct xpc_activate_mq_msg_chctl_closereply_uv *)
			    msg_hdr;

			spin_lock_irqsave(&part->chctl_lock, irq_flags);
			part->chctl.flags[msg->ch_number] |=
			    XPC_CHCTL_CLOSEREPLY;
			spin_unlock_irqrestore(&part->chctl_lock, irq_flags);

			xpc_wakeup_channel_mgr(part);
			break;
		}
		case XPC_ACTIVATE_MQ_MSG_CHCTL_OPENREQUEST_UV: {
			struct xpc_activate_mq_msg_chctl_openrequest_uv *msg;

			msg = (struct xpc_activate_mq_msg_chctl_openrequest_uv
			    *)msg_hdr;
			args = &part->remote_openclose_args[msg->ch_number];
			args->msg_size = msg->msg_size;
			args->local_nentries = msg->local_nentries;

			spin_lock_irqsave(&part->chctl_lock, irq_flags);
			part->chctl.flags[msg->ch_number] |=
			    XPC_CHCTL_OPENREQUEST;
			spin_unlock_irqrestore(&part->chctl_lock, irq_flags);

			xpc_wakeup_channel_mgr(part);
			break;
		}
		case XPC_ACTIVATE_MQ_MSG_CHCTL_OPENREPLY_UV: {
			struct xpc_activate_mq_msg_chctl_openreply_uv *msg;

			msg = (struct xpc_activate_mq_msg_chctl_openreply_uv *)
			    msg_hdr;
			args = &part->remote_openclose_args[msg->ch_number];
			args->remote_nentries = msg->remote_nentries;
			args->local_nentries = msg->local_nentries;
			args->local_msgqueue_pa = msg->local_notify_mq_gpa;

			spin_lock_irqsave(&part->chctl_lock, irq_flags);
			part->chctl.flags[msg->ch_number] |=
			    XPC_CHCTL_OPENREPLY;
			spin_unlock_irqrestore(&part->chctl_lock, irq_flags);

			xpc_wakeup_channel_mgr(part);
			break;
		}
		case XPC_ACTIVATE_MQ_MSG_MARK_ENGAGED_UV:
			spin_lock_irqsave(&part_uv->flags_lock, irq_flags);
			part_uv->flags |= XPC_P_ENGAGED_UV;
			spin_unlock_irqrestore(&part_uv->flags_lock, irq_flags);
			break;

		case XPC_ACTIVATE_MQ_MSG_MARK_DISENGAGED_UV:
			spin_lock_irqsave(&part_uv->flags_lock, irq_flags);
			part_uv->flags &= ~XPC_P_ENGAGED_UV;
			spin_unlock_irqrestore(&part_uv->flags_lock, irq_flags);
			break;

		default:
			dev_err(xpc_part, "received unknown activate_mq msg "
				"type=%d from partition=%d\n", msg_hdr->type,
				partid);
		}

		if (msg_hdr->rp_ts_jiffies != part->remote_rp_ts_jiffies &&
		    part->remote_rp_ts_jiffies != 0) {
			/*
			 * ??? Does what we do here need to be sensitive to
			 * ??? act_state or remote_act_state?
			 */
			spin_lock_irqsave(&xpc_activate_IRQ_rcvd_lock,
					  irq_flags);
			if (part_uv->act_state_req == 0)
				xpc_activate_IRQ_rcvd++;
			part_uv->act_state_req = XPC_P_ASR_REACTIVATE_UV;
			spin_unlock_irqrestore(&xpc_activate_IRQ_rcvd_lock,
					       irq_flags);
			wakeup_hb_checker++;
		}

		gru_free_message(xpc_activate_mq_uv, msg_hdr);
	}

	if (wakeup_hb_checker)
		wake_up_interruptible(&xpc_activate_IRQ_wq);

	return IRQ_HANDLED;
}

static enum xp_retval
xpc_send_activate_IRQ_uv(struct xpc_partition *part, void *msg, size_t msg_size,
			 int msg_type)
{
	struct xpc_activate_mq_msghdr_uv *msg_hdr = msg;

	DBUG_ON(msg_size > XPC_ACTIVATE_MSG_SIZE_UV);

	msg_hdr->type = msg_type;
	msg_hdr->partid = XPC_PARTID(part);
	msg_hdr->act_state = part->act_state;
	msg_hdr->rp_ts_jiffies = xpc_rsvd_page->ts_jiffies;

	/* ??? Is holding a spin_lock (ch->lock) during this call a bad idea? */
	return xpc_send_gru_msg(part->sn.uv.remote_activate_mq_gpa, msg,
				msg_size);
}

static void
xpc_send_activate_IRQ_part_uv(struct xpc_partition *part, void *msg,
			      size_t msg_size, int msg_type)
{
	enum xp_retval ret;

	ret = xpc_send_activate_IRQ_uv(part, msg, msg_size, msg_type);
	if (unlikely(ret != xpSuccess))
		XPC_DEACTIVATE_PARTITION(part, ret);
}

static void
xpc_send_activate_IRQ_ch_uv(struct xpc_channel *ch, unsigned long *irq_flags,
			 void *msg, size_t msg_size, int msg_type)
{
	struct xpc_partition *part = &xpc_partitions[ch->number];
	enum xp_retval ret;

	ret = xpc_send_activate_IRQ_uv(part, msg, msg_size, msg_type);
	if (unlikely(ret != xpSuccess)) {
		if (irq_flags != NULL)
			spin_unlock_irqrestore(&ch->lock, *irq_flags);

		XPC_DEACTIVATE_PARTITION(part, ret);

		if (irq_flags != NULL)
			spin_lock_irqsave(&ch->lock, *irq_flags);
	}
}

static void
xpc_send_local_activate_IRQ_uv(struct xpc_partition *part, int act_state_req)
{
	unsigned long irq_flags;
	struct xpc_partition_uv *part_uv = &part->sn.uv;

	/*
	 * !!! Make our side think that the remote parition sent an activate
	 * !!! message our way by doing what the activate IRQ handler would
	 * !!! do had one really been sent.
	 */

	spin_lock_irqsave(&xpc_activate_IRQ_rcvd_lock, irq_flags);
	if (part_uv->act_state_req == 0)
		xpc_activate_IRQ_rcvd++;
	part_uv->act_state_req = act_state_req;
	spin_unlock_irqrestore(&xpc_activate_IRQ_rcvd_lock, irq_flags);

	wake_up_interruptible(&xpc_activate_IRQ_wq);
}

static enum xp_retval
xpc_get_partition_rsvd_page_pa_uv(void *buf, u64 *cookie, unsigned long *rp_pa,
				  size_t *len)
{
	/* !!! call the UV version of sn_partition_reserved_page_pa() */
	return xpUnsupported;
}

static int
xpc_setup_rsvd_page_sn_uv(struct xpc_rsvd_page *rp)
{
	rp->sn.activate_mq_gpa = uv_gpa(xpc_activate_mq_uv);
	return 0;
}

static void
xpc_send_heartbeat_uv(int msg_type)
{
	short partid;
	struct xpc_partition *part;
	struct xpc_activate_mq_msg_heartbeat_req_uv msg;

	/*
	 * !!! On uv we're broadcasting a heartbeat message every 5 seconds.
	 * !!! Whereas on sn2 we're bte_copy'ng the heartbeat info every 20
	 * !!! seconds. This is an increase in numalink traffic.
	 * ??? Is this good?
	 */

	msg.heartbeat = atomic64_inc_return(&xpc_heartbeat_uv);

	partid = find_first_bit(xpc_heartbeating_to_mask_uv,
				XP_MAX_NPARTITIONS_UV);

	while (partid < XP_MAX_NPARTITIONS_UV) {
		part = &xpc_partitions[partid];

		xpc_send_activate_IRQ_part_uv(part, &msg, sizeof(msg),
					      msg_type);

		partid = find_next_bit(xpc_heartbeating_to_mask_uv,
				       XP_MAX_NPARTITIONS_UV, partid + 1);
	}
}

static void
xpc_increment_heartbeat_uv(void)
{
	xpc_send_heartbeat_uv(XPC_ACTIVATE_MQ_MSG_INC_HEARTBEAT_UV);
}

static void
xpc_offline_heartbeat_uv(void)
{
	xpc_send_heartbeat_uv(XPC_ACTIVATE_MQ_MSG_OFFLINE_HEARTBEAT_UV);
}

static void
xpc_online_heartbeat_uv(void)
{
	xpc_send_heartbeat_uv(XPC_ACTIVATE_MQ_MSG_ONLINE_HEARTBEAT_UV);
}

static void
xpc_heartbeat_init_uv(void)
{
	atomic64_set(&xpc_heartbeat_uv, 0);
	bitmap_zero(xpc_heartbeating_to_mask_uv, XP_MAX_NPARTITIONS_UV);
	xpc_heartbeating_to_mask = &xpc_heartbeating_to_mask_uv[0];
}

static void
xpc_heartbeat_exit_uv(void)
{
	xpc_send_heartbeat_uv(XPC_ACTIVATE_MQ_MSG_OFFLINE_HEARTBEAT_UV);
}

static enum xp_retval
xpc_get_remote_heartbeat_uv(struct xpc_partition *part)
{
	struct xpc_partition_uv *part_uv = &part->sn.uv;
	enum xp_retval ret = xpNoHeartbeat;

	if (part_uv->remote_act_state != XPC_P_AS_INACTIVE &&
	    part_uv->remote_act_state != XPC_P_AS_DEACTIVATING) {

		if (part_uv->heartbeat != part->last_heartbeat ||
		    (part_uv->flags & XPC_P_HEARTBEAT_OFFLINE_UV)) {

			part->last_heartbeat = part_uv->heartbeat;
			ret = xpSuccess;
		}
	}
	return ret;
}

static void
xpc_request_partition_activation_uv(struct xpc_rsvd_page *remote_rp,
				    unsigned long remote_rp_gpa, int nasid)
{
	short partid = remote_rp->SAL_partid;
	struct xpc_partition *part = &xpc_partitions[partid];
	struct xpc_activate_mq_msg_activate_req_uv msg;

	part->remote_rp_pa = remote_rp_gpa; /* !!! _pa here is really _gpa */
	part->remote_rp_ts_jiffies = remote_rp->ts_jiffies;
	part->sn.uv.remote_activate_mq_gpa = remote_rp->sn.activate_mq_gpa;

	/*
	 * ??? Is it a good idea to make this conditional on what is
	 * ??? potentially stale state information?
	 */
	if (part->sn.uv.remote_act_state == XPC_P_AS_INACTIVE) {
		msg.rp_gpa = uv_gpa(xpc_rsvd_page);
		msg.activate_mq_gpa = xpc_rsvd_page->sn.activate_mq_gpa;
		xpc_send_activate_IRQ_part_uv(part, &msg, sizeof(msg),
					   XPC_ACTIVATE_MQ_MSG_ACTIVATE_REQ_UV);
	}

	if (part->act_state == XPC_P_AS_INACTIVE)
		xpc_send_local_activate_IRQ_uv(part, XPC_P_ASR_ACTIVATE_UV);
}

static void
xpc_request_partition_reactivation_uv(struct xpc_partition *part)
{
	xpc_send_local_activate_IRQ_uv(part, XPC_P_ASR_ACTIVATE_UV);
}

static void
xpc_request_partition_deactivation_uv(struct xpc_partition *part)
{
	struct xpc_activate_mq_msg_deactivate_req_uv msg;

	/*
	 * ??? Is it a good idea to make this conditional on what is
	 * ??? potentially stale state information?
	 */
	if (part->sn.uv.remote_act_state != XPC_P_AS_DEACTIVATING &&
	    part->sn.uv.remote_act_state != XPC_P_AS_INACTIVE) {

		msg.reason = part->reason;
		xpc_send_activate_IRQ_part_uv(part, &msg, sizeof(msg),
					 XPC_ACTIVATE_MQ_MSG_DEACTIVATE_REQ_UV);
	}
}

/*
 * Setup the channel structures that are uv specific.
 */
static enum xp_retval
xpc_setup_ch_structures_sn_uv(struct xpc_partition *part)
{
	/* !!! this function needs fleshing out */
	return xpUnsupported;
}

/*
 * Teardown the channel structures that are uv specific.
 */
static void
xpc_teardown_ch_structures_sn_uv(struct xpc_partition *part)
{
	/* !!! this function needs fleshing out */
	return;
}

static enum xp_retval
xpc_make_first_contact_uv(struct xpc_partition *part)
{
	struct xpc_activate_mq_msg_uv msg;

	/*
	 * We send a sync msg to get the remote partition's remote_act_state
	 * updated to our current act_state which at this point should
	 * be XPC_P_AS_ACTIVATING.
	 */
	xpc_send_activate_IRQ_part_uv(part, &msg, sizeof(msg),
				      XPC_ACTIVATE_MQ_MSG_SYNC_ACT_STATE_UV);

	while (part->sn.uv.remote_act_state != XPC_P_AS_ACTIVATING) {

		dev_dbg(xpc_part, "waiting to make first contact with "
			"partition %d\n", XPC_PARTID(part));

		/* wait a 1/4 of a second or so */
		(void)msleep_interruptible(250);

		if (part->act_state == XPC_P_AS_DEACTIVATING)
			return part->reason;
	}

	return xpSuccess;
}

static u64
xpc_get_chctl_all_flags_uv(struct xpc_partition *part)
{
	unsigned long irq_flags;
	union xpc_channel_ctl_flags chctl;

	spin_lock_irqsave(&part->chctl_lock, irq_flags);
	chctl = part->chctl;
	if (chctl.all_flags != 0)
		part->chctl.all_flags = 0;

	spin_unlock_irqrestore(&part->chctl_lock, irq_flags);
	return chctl.all_flags;
}

static enum xp_retval
xpc_setup_msg_structures_uv(struct xpc_channel *ch)
{
	/* !!! this function needs fleshing out */
	return xpUnsupported;
}

static void
xpc_teardown_msg_structures_uv(struct xpc_channel *ch)
{
	struct xpc_channel_uv *ch_uv = &ch->sn.uv;

	ch_uv->remote_notify_mq_gpa = 0;

	/* !!! this function needs fleshing out */
}

static void
xpc_send_chctl_closerequest_uv(struct xpc_channel *ch, unsigned long *irq_flags)
{
	struct xpc_activate_mq_msg_chctl_closerequest_uv msg;

	msg.ch_number = ch->number;
	msg.reason = ch->reason;
	xpc_send_activate_IRQ_ch_uv(ch, irq_flags, &msg, sizeof(msg),
				    XPC_ACTIVATE_MQ_MSG_CHCTL_CLOSEREQUEST_UV);
}

static void
xpc_send_chctl_closereply_uv(struct xpc_channel *ch, unsigned long *irq_flags)
{
	struct xpc_activate_mq_msg_chctl_closereply_uv msg;

	msg.ch_number = ch->number;
	xpc_send_activate_IRQ_ch_uv(ch, irq_flags, &msg, sizeof(msg),
				    XPC_ACTIVATE_MQ_MSG_CHCTL_CLOSEREPLY_UV);
}

static void
xpc_send_chctl_openrequest_uv(struct xpc_channel *ch, unsigned long *irq_flags)
{
	struct xpc_activate_mq_msg_chctl_openrequest_uv msg;

	msg.ch_number = ch->number;
	msg.msg_size = ch->msg_size;
	msg.local_nentries = ch->local_nentries;
	xpc_send_activate_IRQ_ch_uv(ch, irq_flags, &msg, sizeof(msg),
				    XPC_ACTIVATE_MQ_MSG_CHCTL_OPENREQUEST_UV);
}

static void
xpc_send_chctl_openreply_uv(struct xpc_channel *ch, unsigned long *irq_flags)
{
	struct xpc_activate_mq_msg_chctl_openreply_uv msg;

	msg.ch_number = ch->number;
	msg.local_nentries = ch->local_nentries;
	msg.remote_nentries = ch->remote_nentries;
	msg.local_notify_mq_gpa = uv_gpa(xpc_notify_mq_uv);
	xpc_send_activate_IRQ_ch_uv(ch, irq_flags, &msg, sizeof(msg),
				    XPC_ACTIVATE_MQ_MSG_CHCTL_OPENREPLY_UV);
}

static void
xpc_save_remote_msgqueue_pa_uv(struct xpc_channel *ch,
			       unsigned long msgqueue_pa)
{
	ch->sn.uv.remote_notify_mq_gpa = msgqueue_pa;
}

static void
xpc_indicate_partition_engaged_uv(struct xpc_partition *part)
{
	struct xpc_activate_mq_msg_uv msg;

	xpc_send_activate_IRQ_part_uv(part, &msg, sizeof(msg),
				      XPC_ACTIVATE_MQ_MSG_MARK_ENGAGED_UV);
}

static void
xpc_indicate_partition_disengaged_uv(struct xpc_partition *part)
{
	struct xpc_activate_mq_msg_uv msg;

	xpc_send_activate_IRQ_part_uv(part, &msg, sizeof(msg),
				      XPC_ACTIVATE_MQ_MSG_MARK_DISENGAGED_UV);
}

static void
xpc_assume_partition_disengaged_uv(short partid)
{
	struct xpc_partition_uv *part_uv = &xpc_partitions[partid].sn.uv;
	unsigned long irq_flags;

	spin_lock_irqsave(&part_uv->flags_lock, irq_flags);
	part_uv->flags &= ~XPC_P_ENGAGED_UV;
	spin_unlock_irqrestore(&part_uv->flags_lock, irq_flags);
}

static int
xpc_partition_engaged_uv(short partid)
{
	return (xpc_partitions[partid].sn.uv.flags & XPC_P_ENGAGED_UV) != 0;
}

static int
xpc_any_partition_engaged_uv(void)
{
	struct xpc_partition_uv *part_uv;
	short partid;

	for (partid = 0; partid < XP_MAX_NPARTITIONS_UV; partid++) {
		part_uv = &xpc_partitions[partid].sn.uv;
		if ((part_uv->flags & XPC_P_ENGAGED_UV) != 0)
			return 1;
	}
	return 0;
}

static struct xpc_msg *
xpc_get_deliverable_msg_uv(struct xpc_channel *ch)
{
	/* !!! this function needs fleshing out */
	return NULL;
}

int
xpc_init_uv(void)
{
	xpc_setup_partitions_sn = xpc_setup_partitions_sn_uv;
	xpc_process_activate_IRQ_rcvd = xpc_process_activate_IRQ_rcvd_uv;
	xpc_get_partition_rsvd_page_pa = xpc_get_partition_rsvd_page_pa_uv;
	xpc_setup_rsvd_page_sn = xpc_setup_rsvd_page_sn_uv;
	xpc_increment_heartbeat = xpc_increment_heartbeat_uv;
	xpc_offline_heartbeat = xpc_offline_heartbeat_uv;
	xpc_online_heartbeat = xpc_online_heartbeat_uv;
	xpc_heartbeat_init = xpc_heartbeat_init_uv;
	xpc_heartbeat_exit = xpc_heartbeat_exit_uv;
	xpc_get_remote_heartbeat = xpc_get_remote_heartbeat_uv;

	xpc_request_partition_activation = xpc_request_partition_activation_uv;
	xpc_request_partition_reactivation =
	    xpc_request_partition_reactivation_uv;
	xpc_request_partition_deactivation =
	    xpc_request_partition_deactivation_uv;

	xpc_setup_ch_structures_sn = xpc_setup_ch_structures_sn_uv;
	xpc_teardown_ch_structures_sn = xpc_teardown_ch_structures_sn_uv;

	xpc_make_first_contact = xpc_make_first_contact_uv;

	xpc_get_chctl_all_flags = xpc_get_chctl_all_flags_uv;
	xpc_send_chctl_closerequest = xpc_send_chctl_closerequest_uv;
	xpc_send_chctl_closereply = xpc_send_chctl_closereply_uv;
	xpc_send_chctl_openrequest = xpc_send_chctl_openrequest_uv;
	xpc_send_chctl_openreply = xpc_send_chctl_openreply_uv;

	xpc_save_remote_msgqueue_pa = xpc_save_remote_msgqueue_pa_uv;

	xpc_setup_msg_structures = xpc_setup_msg_structures_uv;
	xpc_teardown_msg_structures = xpc_teardown_msg_structures_uv;

	xpc_indicate_partition_engaged = xpc_indicate_partition_engaged_uv;
	xpc_indicate_partition_disengaged =
	    xpc_indicate_partition_disengaged_uv;
	xpc_assume_partition_disengaged = xpc_assume_partition_disengaged_uv;
	xpc_partition_engaged = xpc_partition_engaged_uv;
	xpc_any_partition_engaged = xpc_any_partition_engaged_uv;

	xpc_get_deliverable_msg = xpc_get_deliverable_msg_uv;

	/* ??? The cpuid argument's value is 0, is that what we want? */
	/* !!! The irq argument's value isn't correct. */
	xpc_activate_mq_uv = xpc_create_gru_mq_uv(XPC_ACTIVATE_MQ_SIZE_UV, 0, 0,
						  xpc_handle_activate_IRQ_uv);
	if (xpc_activate_mq_uv == NULL)
		return -ENOMEM;

	return 0;
}

void
xpc_exit_uv(void)
{
	/* !!! The irq argument's value isn't correct. */
	xpc_destroy_gru_mq_uv(xpc_activate_mq_uv, XPC_ACTIVATE_MQ_SIZE_UV, 0);
}
