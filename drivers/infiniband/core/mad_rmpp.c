/*
 * Copyright (c) 2005 Intel Inc. All rights reserved.
 * Copyright (c) 2005-2006 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2014 Intel Corporation.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/slab.h>

#include "mad_priv.h"
#include "mad_rmpp.h"

enum rmpp_state {
	RMPP_STATE_ACTIVE,
	RMPP_STATE_TIMEOUT,
	RMPP_STATE_COMPLETE,
	RMPP_STATE_CANCELING
};

struct mad_rmpp_recv {
	struct ib_mad_agent_private *agent;
	struct list_head list;
	struct delayed_work timeout_work;
	struct delayed_work cleanup_work;
	struct completion comp;
	enum rmpp_state state;
	spinlock_t lock;
	atomic_t refcount;

	struct ib_ah *ah;
	struct ib_mad_recv_wc *rmpp_wc;
	struct ib_mad_recv_buf *cur_seg_buf;
	int last_ack;
	int seg_num;
	int newwin;
	int repwin;

	__be64 tid;
	u32 src_qp;
	u16 slid;
	u8 mgmt_class;
	u8 class_version;
	u8 method;
	u8 base_version;
};

static inline void deref_rmpp_recv(struct mad_rmpp_recv *rmpp_recv)
{
	if (atomic_dec_and_test(&rmpp_recv->refcount))
		complete(&rmpp_recv->comp);
}

static void destroy_rmpp_recv(struct mad_rmpp_recv *rmpp_recv)
{
	deref_rmpp_recv(rmpp_recv);
	wait_for_completion(&rmpp_recv->comp);
	ib_destroy_ah(rmpp_recv->ah);
	kfree(rmpp_recv);
}

void ib_cancel_rmpp_recvs(struct ib_mad_agent_private *agent)
{
	struct mad_rmpp_recv *rmpp_recv, *temp_rmpp_recv;
	unsigned long flags;

	spin_lock_irqsave(&agent->lock, flags);
	list_for_each_entry(rmpp_recv, &agent->rmpp_list, list) {
		if (rmpp_recv->state != RMPP_STATE_COMPLETE)
			ib_free_recv_mad(rmpp_recv->rmpp_wc);
		rmpp_recv->state = RMPP_STATE_CANCELING;
	}
	spin_unlock_irqrestore(&agent->lock, flags);

	list_for_each_entry(rmpp_recv, &agent->rmpp_list, list) {
		cancel_delayed_work(&rmpp_recv->timeout_work);
		cancel_delayed_work(&rmpp_recv->cleanup_work);
	}

	flush_workqueue(agent->qp_info->port_priv->wq);

	list_for_each_entry_safe(rmpp_recv, temp_rmpp_recv,
				 &agent->rmpp_list, list) {
		list_del(&rmpp_recv->list);
		destroy_rmpp_recv(rmpp_recv);
	}
}

static void format_ack(struct ib_mad_send_buf *msg,
		       struct ib_rmpp_mad *data,
		       struct mad_rmpp_recv *rmpp_recv)
{
	struct ib_rmpp_mad *ack = msg->mad;
	unsigned long flags;

	memcpy(ack, &data->mad_hdr, msg->hdr_len);

	ack->mad_hdr.method ^= IB_MGMT_METHOD_RESP;
	ack->rmpp_hdr.rmpp_type = IB_MGMT_RMPP_TYPE_ACK;
	ib_set_rmpp_flags(&ack->rmpp_hdr, IB_MGMT_RMPP_FLAG_ACTIVE);

	spin_lock_irqsave(&rmpp_recv->lock, flags);
	rmpp_recv->last_ack = rmpp_recv->seg_num;
	ack->rmpp_hdr.seg_num = cpu_to_be32(rmpp_recv->seg_num);
	ack->rmpp_hdr.paylen_newwin = cpu_to_be32(rmpp_recv->newwin);
	spin_unlock_irqrestore(&rmpp_recv->lock, flags);
}

static void ack_recv(struct mad_rmpp_recv *rmpp_recv,
		     struct ib_mad_recv_wc *recv_wc)
{
	struct ib_mad_send_buf *msg;
	int ret, hdr_len;

	hdr_len = ib_get_mad_data_offset(recv_wc->recv_buf.mad->mad_hdr.mgmt_class);
	msg = ib_create_send_mad(&rmpp_recv->agent->agent, recv_wc->wc->src_qp,
				 recv_wc->wc->pkey_index, 1, hdr_len,
				 0, GFP_KERNEL,
				 IB_MGMT_BASE_VERSION);
	if (IS_ERR(msg))
		return;

	format_ack(msg, (struct ib_rmpp_mad *) recv_wc->recv_buf.mad, rmpp_recv);
	msg->ah = rmpp_recv->ah;
	ret = ib_post_send_mad(msg, NULL);
	if (ret)
		ib_free_send_mad(msg);
}

static struct ib_mad_send_buf *alloc_response_msg(struct ib_mad_agent *agent,
						  struct ib_mad_recv_wc *recv_wc)
{
	struct ib_mad_send_buf *msg;
	struct ib_ah *ah;
	int hdr_len;

	ah = ib_create_ah_from_wc(agent->qp->pd, recv_wc->wc,
				  recv_wc->recv_buf.grh, agent->port_num);
	if (IS_ERR(ah))
		return (void *) ah;

	hdr_len = ib_get_mad_data_offset(recv_wc->recv_buf.mad->mad_hdr.mgmt_class);
	msg = ib_create_send_mad(agent, recv_wc->wc->src_qp,
				 recv_wc->wc->pkey_index, 1,
				 hdr_len, 0, GFP_KERNEL,
				 IB_MGMT_BASE_VERSION);
	if (IS_ERR(msg))
		ib_destroy_ah(ah);
	else {
		msg->ah = ah;
		msg->context[0] = ah;
	}

	return msg;
}

static void ack_ds_ack(struct ib_mad_agent_private *agent,
		       struct ib_mad_recv_wc *recv_wc)
{
	struct ib_mad_send_buf *msg;
	struct ib_rmpp_mad *rmpp_mad;
	int ret;

	msg = alloc_response_msg(&agent->agent, recv_wc);
	if (IS_ERR(msg))
		return;

	rmpp_mad = msg->mad;
	memcpy(rmpp_mad, recv_wc->recv_buf.mad, msg->hdr_len);

	rmpp_mad->mad_hdr.method ^= IB_MGMT_METHOD_RESP;
	ib_set_rmpp_flags(&rmpp_mad->rmpp_hdr, IB_MGMT_RMPP_FLAG_ACTIVE);
	rmpp_mad->rmpp_hdr.seg_num = 0;
	rmpp_mad->rmpp_hdr.paylen_newwin = cpu_to_be32(1);

	ret = ib_post_send_mad(msg, NULL);
	if (ret) {
		ib_destroy_ah(msg->ah);
		ib_free_send_mad(msg);
	}
}

void ib_rmpp_send_handler(struct ib_mad_send_wc *mad_send_wc)
{
	if (mad_send_wc->send_buf->context[0] == mad_send_wc->send_buf->ah)
		ib_destroy_ah(mad_send_wc->send_buf->ah);
	ib_free_send_mad(mad_send_wc->send_buf);
}

static void nack_recv(struct ib_mad_agent_private *agent,
		      struct ib_mad_recv_wc *recv_wc, u8 rmpp_status)
{
	struct ib_mad_send_buf *msg;
	struct ib_rmpp_mad *rmpp_mad;
	int ret;

	msg = alloc_response_msg(&agent->agent, recv_wc);
	if (IS_ERR(msg))
		return;

	rmpp_mad = msg->mad;
	memcpy(rmpp_mad, recv_wc->recv_buf.mad, msg->hdr_len);

	rmpp_mad->mad_hdr.method ^= IB_MGMT_METHOD_RESP;
	rmpp_mad->rmpp_hdr.rmpp_version = IB_MGMT_RMPP_VERSION;
	rmpp_mad->rmpp_hdr.rmpp_type = IB_MGMT_RMPP_TYPE_ABORT;
	ib_set_rmpp_flags(&rmpp_mad->rmpp_hdr, IB_MGMT_RMPP_FLAG_ACTIVE);
	rmpp_mad->rmpp_hdr.rmpp_status = rmpp_status;
	rmpp_mad->rmpp_hdr.seg_num = 0;
	rmpp_mad->rmpp_hdr.paylen_newwin = 0;

	ret = ib_post_send_mad(msg, NULL);
	if (ret) {
		ib_destroy_ah(msg->ah);
		ib_free_send_mad(msg);
	}
}

static void recv_timeout_handler(struct work_struct *work)
{
	struct mad_rmpp_recv *rmpp_recv =
		container_of(work, struct mad_rmpp_recv, timeout_work.work);
	struct ib_mad_recv_wc *rmpp_wc;
	unsigned long flags;

	spin_lock_irqsave(&rmpp_recv->agent->lock, flags);
	if (rmpp_recv->state != RMPP_STATE_ACTIVE) {
		spin_unlock_irqrestore(&rmpp_recv->agent->lock, flags);
		return;
	}
	rmpp_recv->state = RMPP_STATE_TIMEOUT;
	list_del(&rmpp_recv->list);
	spin_unlock_irqrestore(&rmpp_recv->agent->lock, flags);

	rmpp_wc = rmpp_recv->rmpp_wc;
	nack_recv(rmpp_recv->agent, rmpp_wc, IB_MGMT_RMPP_STATUS_T2L);
	destroy_rmpp_recv(rmpp_recv);
	ib_free_recv_mad(rmpp_wc);
}

static void recv_cleanup_handler(struct work_struct *work)
{
	struct mad_rmpp_recv *rmpp_recv =
		container_of(work, struct mad_rmpp_recv, cleanup_work.work);
	unsigned long flags;

	spin_lock_irqsave(&rmpp_recv->agent->lock, flags);
	if (rmpp_recv->state == RMPP_STATE_CANCELING) {
		spin_unlock_irqrestore(&rmpp_recv->agent->lock, flags);
		return;
	}
	list_del(&rmpp_recv->list);
	spin_unlock_irqrestore(&rmpp_recv->agent->lock, flags);
	destroy_rmpp_recv(rmpp_recv);
}

static struct mad_rmpp_recv *
create_rmpp_recv(struct ib_mad_agent_private *agent,
		 struct ib_mad_recv_wc *mad_recv_wc)
{
	struct mad_rmpp_recv *rmpp_recv;
	struct ib_mad_hdr *mad_hdr;

	rmpp_recv = kmalloc(sizeof *rmpp_recv, GFP_KERNEL);
	if (!rmpp_recv)
		return NULL;

	rmpp_recv->ah = ib_create_ah_from_wc(agent->agent.qp->pd,
					     mad_recv_wc->wc,
					     mad_recv_wc->recv_buf.grh,
					     agent->agent.port_num);
	if (IS_ERR(rmpp_recv->ah))
		goto error;

	rmpp_recv->agent = agent;
	init_completion(&rmpp_recv->comp);
	INIT_DELAYED_WORK(&rmpp_recv->timeout_work, recv_timeout_handler);
	INIT_DELAYED_WORK(&rmpp_recv->cleanup_work, recv_cleanup_handler);
	spin_lock_init(&rmpp_recv->lock);
	rmpp_recv->state = RMPP_STATE_ACTIVE;
	atomic_set(&rmpp_recv->refcount, 1);

	rmpp_recv->rmpp_wc = mad_recv_wc;
	rmpp_recv->cur_seg_buf = &mad_recv_wc->recv_buf;
	rmpp_recv->newwin = 1;
	rmpp_recv->seg_num = 1;
	rmpp_recv->last_ack = 0;
	rmpp_recv->repwin = 1;

	mad_hdr = &mad_recv_wc->recv_buf.mad->mad_hdr;
	rmpp_recv->tid = mad_hdr->tid;
	rmpp_recv->src_qp = mad_recv_wc->wc->src_qp;
	rmpp_recv->slid = mad_recv_wc->wc->slid;
	rmpp_recv->mgmt_class = mad_hdr->mgmt_class;
	rmpp_recv->class_version = mad_hdr->class_version;
	rmpp_recv->method  = mad_hdr->method;
	rmpp_recv->base_version  = mad_hdr->base_version;
	return rmpp_recv;

error:	kfree(rmpp_recv);
	return NULL;
}

static struct mad_rmpp_recv *
find_rmpp_recv(struct ib_mad_agent_private *agent,
	       struct ib_mad_recv_wc *mad_recv_wc)
{
	struct mad_rmpp_recv *rmpp_recv;
	struct ib_mad_hdr *mad_hdr = &mad_recv_wc->recv_buf.mad->mad_hdr;

	list_for_each_entry(rmpp_recv, &agent->rmpp_list, list) {
		if (rmpp_recv->tid == mad_hdr->tid &&
		    rmpp_recv->src_qp == mad_recv_wc->wc->src_qp &&
		    rmpp_recv->slid == mad_recv_wc->wc->slid &&
		    rmpp_recv->mgmt_class == mad_hdr->mgmt_class &&
		    rmpp_recv->class_version == mad_hdr->class_version &&
		    rmpp_recv->method == mad_hdr->method)
			return rmpp_recv;
	}
	return NULL;
}

static struct mad_rmpp_recv *
acquire_rmpp_recv(struct ib_mad_agent_private *agent,
		  struct ib_mad_recv_wc *mad_recv_wc)
{
	struct mad_rmpp_recv *rmpp_recv;
	unsigned long flags;

	spin_lock_irqsave(&agent->lock, flags);
	rmpp_recv = find_rmpp_recv(agent, mad_recv_wc);
	if (rmpp_recv)
		atomic_inc(&rmpp_recv->refcount);
	spin_unlock_irqrestore(&agent->lock, flags);
	return rmpp_recv;
}

static struct mad_rmpp_recv *
insert_rmpp_recv(struct ib_mad_agent_private *agent,
		 struct mad_rmpp_recv *rmpp_recv)
{
	struct mad_rmpp_recv *cur_rmpp_recv;

	cur_rmpp_recv = find_rmpp_recv(agent, rmpp_recv->rmpp_wc);
	if (!cur_rmpp_recv)
		list_add_tail(&rmpp_recv->list, &agent->rmpp_list);

	return cur_rmpp_recv;
}

static inline int get_last_flag(struct ib_mad_recv_buf *seg)
{
	struct ib_rmpp_mad *rmpp_mad;

	rmpp_mad = (struct ib_rmpp_mad *) seg->mad;
	return ib_get_rmpp_flags(&rmpp_mad->rmpp_hdr) & IB_MGMT_RMPP_FLAG_LAST;
}

static inline int get_seg_num(struct ib_mad_recv_buf *seg)
{
	struct ib_rmpp_mad *rmpp_mad;

	rmpp_mad = (struct ib_rmpp_mad *) seg->mad;
	return be32_to_cpu(rmpp_mad->rmpp_hdr.seg_num);
}

static inline struct ib_mad_recv_buf * get_next_seg(struct list_head *rmpp_list,
						    struct ib_mad_recv_buf *seg)
{
	if (seg->list.next == rmpp_list)
		return NULL;

	return container_of(seg->list.next, struct ib_mad_recv_buf, list);
}

static inline int window_size(struct ib_mad_agent_private *agent)
{
	return max(agent->qp_info->recv_queue.max_active >> 3, 1);
}

static struct ib_mad_recv_buf * find_seg_location(struct list_head *rmpp_list,
						  int seg_num)
{
	struct ib_mad_recv_buf *seg_buf;
	int cur_seg_num;

	list_for_each_entry_reverse(seg_buf, rmpp_list, list) {
		cur_seg_num = get_seg_num(seg_buf);
		if (seg_num > cur_seg_num)
			return seg_buf;
		if (seg_num == cur_seg_num)
			break;
	}
	return NULL;
}

static void update_seg_num(struct mad_rmpp_recv *rmpp_recv,
			   struct ib_mad_recv_buf *new_buf)
{
	struct list_head *rmpp_list = &rmpp_recv->rmpp_wc->rmpp_list;

	while (new_buf && (get_seg_num(new_buf) == rmpp_recv->seg_num + 1)) {
		rmpp_recv->cur_seg_buf = new_buf;
		rmpp_recv->seg_num++;
		new_buf = get_next_seg(rmpp_list, new_buf);
	}
}

static inline int get_mad_len(struct mad_rmpp_recv *rmpp_recv)
{
	struct ib_rmpp_mad *rmpp_mad;
	int hdr_size, data_size, pad;
	bool opa = rdma_cap_opa_mad(rmpp_recv->agent->qp_info->port_priv->device,
				    rmpp_recv->agent->qp_info->port_priv->port_num);

	rmpp_mad = (struct ib_rmpp_mad *)rmpp_recv->cur_seg_buf->mad;

	hdr_size = ib_get_mad_data_offset(rmpp_mad->mad_hdr.mgmt_class);
	if (opa && rmpp_recv->base_version == OPA_MGMT_BASE_VERSION) {
		data_size = sizeof(struct opa_rmpp_mad) - hdr_size;
		pad = OPA_MGMT_RMPP_DATA - be32_to_cpu(rmpp_mad->rmpp_hdr.paylen_newwin);
		if (pad > OPA_MGMT_RMPP_DATA || pad < 0)
			pad = 0;
	} else {
		data_size = sizeof(struct ib_rmpp_mad) - hdr_size;
		pad = IB_MGMT_RMPP_DATA - be32_to_cpu(rmpp_mad->rmpp_hdr.paylen_newwin);
		if (pad > IB_MGMT_RMPP_DATA || pad < 0)
			pad = 0;
	}

	return hdr_size + rmpp_recv->seg_num * data_size - pad;
}

static struct ib_mad_recv_wc * complete_rmpp(struct mad_rmpp_recv *rmpp_recv)
{
	struct ib_mad_recv_wc *rmpp_wc;

	ack_recv(rmpp_recv, rmpp_recv->rmpp_wc);
	if (rmpp_recv->seg_num > 1)
		cancel_delayed_work(&rmpp_recv->timeout_work);

	rmpp_wc = rmpp_recv->rmpp_wc;
	rmpp_wc->mad_len = get_mad_len(rmpp_recv);
	/* 10 seconds until we can find the packet lifetime */
	queue_delayed_work(rmpp_recv->agent->qp_info->port_priv->wq,
			   &rmpp_recv->cleanup_work, msecs_to_jiffies(10000));
	return rmpp_wc;
}

static struct ib_mad_recv_wc *
continue_rmpp(struct ib_mad_agent_private *agent,
	      struct ib_mad_recv_wc *mad_recv_wc)
{
	struct mad_rmpp_recv *rmpp_recv;
	struct ib_mad_recv_buf *prev_buf;
	struct ib_mad_recv_wc *done_wc;
	int seg_num;
	unsigned long flags;

	rmpp_recv = acquire_rmpp_recv(agent, mad_recv_wc);
	if (!rmpp_recv)
		goto drop1;

	seg_num = get_seg_num(&mad_recv_wc->recv_buf);

	spin_lock_irqsave(&rmpp_recv->lock, flags);
	if ((rmpp_recv->state == RMPP_STATE_TIMEOUT) ||
	    (seg_num > rmpp_recv->newwin))
		goto drop3;

	if ((seg_num <= rmpp_recv->last_ack) ||
	    (rmpp_recv->state == RMPP_STATE_COMPLETE)) {
		spin_unlock_irqrestore(&rmpp_recv->lock, flags);
		ack_recv(rmpp_recv, mad_recv_wc);
		goto drop2;
	}

	prev_buf = find_seg_location(&rmpp_recv->rmpp_wc->rmpp_list, seg_num);
	if (!prev_buf)
		goto drop3;

	done_wc = NULL;
	list_add(&mad_recv_wc->recv_buf.list, &prev_buf->list);
	if (rmpp_recv->cur_seg_buf == prev_buf) {
		update_seg_num(rmpp_recv, &mad_recv_wc->recv_buf);
		if (get_last_flag(rmpp_recv->cur_seg_buf)) {
			rmpp_recv->state = RMPP_STATE_COMPLETE;
			spin_unlock_irqrestore(&rmpp_recv->lock, flags);
			done_wc = complete_rmpp(rmpp_recv);
			goto out;
		} else if (rmpp_recv->seg_num == rmpp_recv->newwin) {
			rmpp_recv->newwin += window_size(agent);
			spin_unlock_irqrestore(&rmpp_recv->lock, flags);
			ack_recv(rmpp_recv, mad_recv_wc);
			goto out;
		}
	}
	spin_unlock_irqrestore(&rmpp_recv->lock, flags);
out:
	deref_rmpp_recv(rmpp_recv);
	return done_wc;

drop3:	spin_unlock_irqrestore(&rmpp_recv->lock, flags);
drop2:	deref_rmpp_recv(rmpp_recv);
drop1:	ib_free_recv_mad(mad_recv_wc);
	return NULL;
}

static struct ib_mad_recv_wc *
start_rmpp(struct ib_mad_agent_private *agent,
	   struct ib_mad_recv_wc *mad_recv_wc)
{
	struct mad_rmpp_recv *rmpp_recv;
	unsigned long flags;

	rmpp_recv = create_rmpp_recv(agent, mad_recv_wc);
	if (!rmpp_recv) {
		ib_free_recv_mad(mad_recv_wc);
		return NULL;
	}

	spin_lock_irqsave(&agent->lock, flags);
	if (insert_rmpp_recv(agent, rmpp_recv)) {
		spin_unlock_irqrestore(&agent->lock, flags);
		/* duplicate first MAD */
		destroy_rmpp_recv(rmpp_recv);
		return continue_rmpp(agent, mad_recv_wc);
	}
	atomic_inc(&rmpp_recv->refcount);

	if (get_last_flag(&mad_recv_wc->recv_buf)) {
		rmpp_recv->state = RMPP_STATE_COMPLETE;
		spin_unlock_irqrestore(&agent->lock, flags);
		complete_rmpp(rmpp_recv);
	} else {
		spin_unlock_irqrestore(&agent->lock, flags);
		/* 40 seconds until we can find the packet lifetimes */
		queue_delayed_work(agent->qp_info->port_priv->wq,
				   &rmpp_recv->timeout_work,
				   msecs_to_jiffies(40000));
		rmpp_recv->newwin += window_size(agent);
		ack_recv(rmpp_recv, mad_recv_wc);
		mad_recv_wc = NULL;
	}
	deref_rmpp_recv(rmpp_recv);
	return mad_recv_wc;
}

static int send_next_seg(struct ib_mad_send_wr_private *mad_send_wr)
{
	struct ib_rmpp_mad *rmpp_mad;
	int timeout;
	u32 paylen = 0;

	rmpp_mad = mad_send_wr->send_buf.mad;
	ib_set_rmpp_flags(&rmpp_mad->rmpp_hdr, IB_MGMT_RMPP_FLAG_ACTIVE);
	rmpp_mad->rmpp_hdr.seg_num = cpu_to_be32(++mad_send_wr->seg_num);

	if (mad_send_wr->seg_num == 1) {
		rmpp_mad->rmpp_hdr.rmpp_rtime_flags |= IB_MGMT_RMPP_FLAG_FIRST;
		paylen = (mad_send_wr->send_buf.seg_count *
			  mad_send_wr->send_buf.seg_rmpp_size) -
			  mad_send_wr->pad;
	}

	if (mad_send_wr->seg_num == mad_send_wr->send_buf.seg_count) {
		rmpp_mad->rmpp_hdr.rmpp_rtime_flags |= IB_MGMT_RMPP_FLAG_LAST;
		paylen = mad_send_wr->send_buf.seg_rmpp_size - mad_send_wr->pad;
	}
	rmpp_mad->rmpp_hdr.paylen_newwin = cpu_to_be32(paylen);

	/* 2 seconds for an ACK until we can find the packet lifetime */
	timeout = mad_send_wr->send_buf.timeout_ms;
	if (!timeout || timeout > 2000)
		mad_send_wr->timeout = msecs_to_jiffies(2000);

	return ib_send_mad(mad_send_wr);
}

static void abort_send(struct ib_mad_agent_private *agent,
		       struct ib_mad_recv_wc *mad_recv_wc, u8 rmpp_status)
{
	struct ib_mad_send_wr_private *mad_send_wr;
	struct ib_mad_send_wc wc;
	unsigned long flags;

	spin_lock_irqsave(&agent->lock, flags);
	mad_send_wr = ib_find_send_mad(agent, mad_recv_wc);
	if (!mad_send_wr)
		goto out;	/* Unmatched send */

	if ((mad_send_wr->last_ack == mad_send_wr->send_buf.seg_count) ||
	    (!mad_send_wr->timeout) || (mad_send_wr->status != IB_WC_SUCCESS))
		goto out;	/* Send is already done */

	ib_mark_mad_done(mad_send_wr);
	spin_unlock_irqrestore(&agent->lock, flags);

	wc.status = IB_WC_REM_ABORT_ERR;
	wc.vendor_err = rmpp_status;
	wc.send_buf = &mad_send_wr->send_buf;
	ib_mad_complete_send_wr(mad_send_wr, &wc);
	return;
out:
	spin_unlock_irqrestore(&agent->lock, flags);
}

static inline void adjust_last_ack(struct ib_mad_send_wr_private *wr,
				   int seg_num)
{
	struct list_head *list;

	wr->last_ack = seg_num;
	list = &wr->last_ack_seg->list;
	list_for_each_entry(wr->last_ack_seg, list, list)
		if (wr->last_ack_seg->num == seg_num)
			break;
}

static void process_ds_ack(struct ib_mad_agent_private *agent,
			   struct ib_mad_recv_wc *mad_recv_wc, int newwin)
{
	struct mad_rmpp_recv *rmpp_recv;

	rmpp_recv = find_rmpp_recv(agent, mad_recv_wc);
	if (rmpp_recv && rmpp_recv->state == RMPP_STATE_COMPLETE)
		rmpp_recv->repwin = newwin;
}

static void process_rmpp_ack(struct ib_mad_agent_private *agent,
			     struct ib_mad_recv_wc *mad_recv_wc)
{
	struct ib_mad_send_wr_private *mad_send_wr;
	struct ib_rmpp_mad *rmpp_mad;
	unsigned long flags;
	int seg_num, newwin, ret;

	rmpp_mad = (struct ib_rmpp_mad *)mad_recv_wc->recv_buf.mad;
	if (rmpp_mad->rmpp_hdr.rmpp_status) {
		abort_send(agent, mad_recv_wc, IB_MGMT_RMPP_STATUS_BAD_STATUS);
		nack_recv(agent, mad_recv_wc, IB_MGMT_RMPP_STATUS_BAD_STATUS);
		return;
	}

	seg_num = be32_to_cpu(rmpp_mad->rmpp_hdr.seg_num);
	newwin = be32_to_cpu(rmpp_mad->rmpp_hdr.paylen_newwin);
	if (newwin < seg_num) {
		abort_send(agent, mad_recv_wc, IB_MGMT_RMPP_STATUS_W2S);
		nack_recv(agent, mad_recv_wc, IB_MGMT_RMPP_STATUS_W2S);
		return;
	}

	spin_lock_irqsave(&agent->lock, flags);
	mad_send_wr = ib_find_send_mad(agent, mad_recv_wc);
	if (!mad_send_wr) {
		if (!seg_num)
			process_ds_ack(agent, mad_recv_wc, newwin);
		goto out;	/* Unmatched or DS RMPP ACK */
	}

	if ((mad_send_wr->last_ack == mad_send_wr->send_buf.seg_count) &&
	    (mad_send_wr->timeout)) {
		spin_unlock_irqrestore(&agent->lock, flags);
		ack_ds_ack(agent, mad_recv_wc);
		return;		/* Repeated ACK for DS RMPP transaction */
	}

	if ((mad_send_wr->last_ack == mad_send_wr->send_buf.seg_count) ||
	    (!mad_send_wr->timeout) || (mad_send_wr->status != IB_WC_SUCCESS))
		goto out;	/* Send is already done */

	if (seg_num > mad_send_wr->send_buf.seg_count ||
	    seg_num > mad_send_wr->newwin) {
		spin_unlock_irqrestore(&agent->lock, flags);
		abort_send(agent, mad_recv_wc, IB_MGMT_RMPP_STATUS_S2B);
		nack_recv(agent, mad_recv_wc, IB_MGMT_RMPP_STATUS_S2B);
		return;
	}

	if (newwin < mad_send_wr->newwin || seg_num < mad_send_wr->last_ack)
		goto out;	/* Old ACK */

	if (seg_num > mad_send_wr->last_ack) {
		adjust_last_ack(mad_send_wr, seg_num);
		mad_send_wr->retries_left = mad_send_wr->max_retries;
	}
	mad_send_wr->newwin = newwin;
	if (mad_send_wr->last_ack == mad_send_wr->send_buf.seg_count) {
		/* If no response is expected, the ACK completes the send */
		if (!mad_send_wr->send_buf.timeout_ms) {
			struct ib_mad_send_wc wc;

			ib_mark_mad_done(mad_send_wr);
			spin_unlock_irqrestore(&agent->lock, flags);

			wc.status = IB_WC_SUCCESS;
			wc.vendor_err = 0;
			wc.send_buf = &mad_send_wr->send_buf;
			ib_mad_complete_send_wr(mad_send_wr, &wc);
			return;
		}
		if (mad_send_wr->refcount == 1)
			ib_reset_mad_timeout(mad_send_wr,
					     mad_send_wr->send_buf.timeout_ms);
		spin_unlock_irqrestore(&agent->lock, flags);
		ack_ds_ack(agent, mad_recv_wc);
		return;
	} else if (mad_send_wr->refcount == 1 &&
		   mad_send_wr->seg_num < mad_send_wr->newwin &&
		   mad_send_wr->seg_num < mad_send_wr->send_buf.seg_count) {
		/* Send failure will just result in a timeout/retry */
		ret = send_next_seg(mad_send_wr);
		if (ret)
			goto out;

		mad_send_wr->refcount++;
		list_move_tail(&mad_send_wr->agent_list,
			      &mad_send_wr->mad_agent_priv->send_list);
	}
out:
	spin_unlock_irqrestore(&agent->lock, flags);
}

static struct ib_mad_recv_wc *
process_rmpp_data(struct ib_mad_agent_private *agent,
		  struct ib_mad_recv_wc *mad_recv_wc)
{
	struct ib_rmpp_hdr *rmpp_hdr;
	u8 rmpp_status;

	rmpp_hdr = &((struct ib_rmpp_mad *)mad_recv_wc->recv_buf.mad)->rmpp_hdr;

	if (rmpp_hdr->rmpp_status) {
		rmpp_status = IB_MGMT_RMPP_STATUS_BAD_STATUS;
		goto bad;
	}

	if (rmpp_hdr->seg_num == cpu_to_be32(1)) {
		if (!(ib_get_rmpp_flags(rmpp_hdr) & IB_MGMT_RMPP_FLAG_FIRST)) {
			rmpp_status = IB_MGMT_RMPP_STATUS_BAD_SEG;
			goto bad;
		}
		return start_rmpp(agent, mad_recv_wc);
	} else {
		if (ib_get_rmpp_flags(rmpp_hdr) & IB_MGMT_RMPP_FLAG_FIRST) {
			rmpp_status = IB_MGMT_RMPP_STATUS_BAD_SEG;
			goto bad;
		}
		return continue_rmpp(agent, mad_recv_wc);
	}
bad:
	nack_recv(agent, mad_recv_wc, rmpp_status);
	ib_free_recv_mad(mad_recv_wc);
	return NULL;
}

static void process_rmpp_stop(struct ib_mad_agent_private *agent,
			      struct ib_mad_recv_wc *mad_recv_wc)
{
	struct ib_rmpp_mad *rmpp_mad;

	rmpp_mad = (struct ib_rmpp_mad *)mad_recv_wc->recv_buf.mad;

	if (rmpp_mad->rmpp_hdr.rmpp_status != IB_MGMT_RMPP_STATUS_RESX) {
		abort_send(agent, mad_recv_wc, IB_MGMT_RMPP_STATUS_BAD_STATUS);
		nack_recv(agent, mad_recv_wc, IB_MGMT_RMPP_STATUS_BAD_STATUS);
	} else
		abort_send(agent, mad_recv_wc, rmpp_mad->rmpp_hdr.rmpp_status);
}

static void process_rmpp_abort(struct ib_mad_agent_private *agent,
			       struct ib_mad_recv_wc *mad_recv_wc)
{
	struct ib_rmpp_mad *rmpp_mad;

	rmpp_mad = (struct ib_rmpp_mad *)mad_recv_wc->recv_buf.mad;

	if (rmpp_mad->rmpp_hdr.rmpp_status < IB_MGMT_RMPP_STATUS_ABORT_MIN ||
	    rmpp_mad->rmpp_hdr.rmpp_status > IB_MGMT_RMPP_STATUS_ABORT_MAX) {
		abort_send(agent, mad_recv_wc, IB_MGMT_RMPP_STATUS_BAD_STATUS);
		nack_recv(agent, mad_recv_wc, IB_MGMT_RMPP_STATUS_BAD_STATUS);
	} else
		abort_send(agent, mad_recv_wc, rmpp_mad->rmpp_hdr.rmpp_status);
}

struct ib_mad_recv_wc *
ib_process_rmpp_recv_wc(struct ib_mad_agent_private *agent,
			struct ib_mad_recv_wc *mad_recv_wc)
{
	struct ib_rmpp_mad *rmpp_mad;

	rmpp_mad = (struct ib_rmpp_mad *)mad_recv_wc->recv_buf.mad;
	if (!(rmpp_mad->rmpp_hdr.rmpp_rtime_flags & IB_MGMT_RMPP_FLAG_ACTIVE))
		return mad_recv_wc;

	if (rmpp_mad->rmpp_hdr.rmpp_version != IB_MGMT_RMPP_VERSION) {
		abort_send(agent, mad_recv_wc, IB_MGMT_RMPP_STATUS_UNV);
		nack_recv(agent, mad_recv_wc, IB_MGMT_RMPP_STATUS_UNV);
		goto out;
	}

	switch (rmpp_mad->rmpp_hdr.rmpp_type) {
	case IB_MGMT_RMPP_TYPE_DATA:
		return process_rmpp_data(agent, mad_recv_wc);
	case IB_MGMT_RMPP_TYPE_ACK:
		process_rmpp_ack(agent, mad_recv_wc);
		break;
	case IB_MGMT_RMPP_TYPE_STOP:
		process_rmpp_stop(agent, mad_recv_wc);
		break;
	case IB_MGMT_RMPP_TYPE_ABORT:
		process_rmpp_abort(agent, mad_recv_wc);
		break;
	default:
		abort_send(agent, mad_recv_wc, IB_MGMT_RMPP_STATUS_BADT);
		nack_recv(agent, mad_recv_wc, IB_MGMT_RMPP_STATUS_BADT);
		break;
	}
out:
	ib_free_recv_mad(mad_recv_wc);
	return NULL;
}

static int init_newwin(struct ib_mad_send_wr_private *mad_send_wr)
{
	struct ib_mad_agent_private *agent = mad_send_wr->mad_agent_priv;
	struct ib_mad_hdr *mad_hdr = mad_send_wr->send_buf.mad;
	struct mad_rmpp_recv *rmpp_recv;
	struct ib_ah_attr ah_attr;
	unsigned long flags;
	int newwin = 1;

	if (!(mad_hdr->method & IB_MGMT_METHOD_RESP))
		goto out;

	spin_lock_irqsave(&agent->lock, flags);
	list_for_each_entry(rmpp_recv, &agent->rmpp_list, list) {
		if (rmpp_recv->tid != mad_hdr->tid ||
		    rmpp_recv->mgmt_class != mad_hdr->mgmt_class ||
		    rmpp_recv->class_version != mad_hdr->class_version ||
		    (rmpp_recv->method & IB_MGMT_METHOD_RESP))
			continue;

		if (ib_query_ah(mad_send_wr->send_buf.ah, &ah_attr))
			continue;

		if (rmpp_recv->slid == ah_attr.dlid) {
			newwin = rmpp_recv->repwin;
			break;
		}
	}
	spin_unlock_irqrestore(&agent->lock, flags);
out:
	return newwin;
}

int ib_send_rmpp_mad(struct ib_mad_send_wr_private *mad_send_wr)
{
	struct ib_rmpp_mad *rmpp_mad;
	int ret;

	rmpp_mad = mad_send_wr->send_buf.mad;
	if (!(ib_get_rmpp_flags(&rmpp_mad->rmpp_hdr) &
	      IB_MGMT_RMPP_FLAG_ACTIVE))
		return IB_RMPP_RESULT_UNHANDLED;

	if (rmpp_mad->rmpp_hdr.rmpp_type != IB_MGMT_RMPP_TYPE_DATA) {
		mad_send_wr->seg_num = 1;
		return IB_RMPP_RESULT_INTERNAL;
	}

	mad_send_wr->newwin = init_newwin(mad_send_wr);

	/* We need to wait for the final ACK even if there isn't a response */
	mad_send_wr->refcount += (mad_send_wr->timeout == 0);
	ret = send_next_seg(mad_send_wr);
	if (!ret)
		return IB_RMPP_RESULT_CONSUMED;
	return ret;
}

int ib_process_rmpp_send_wc(struct ib_mad_send_wr_private *mad_send_wr,
			    struct ib_mad_send_wc *mad_send_wc)
{
	struct ib_rmpp_mad *rmpp_mad;
	int ret;

	rmpp_mad = mad_send_wr->send_buf.mad;
	if (!(ib_get_rmpp_flags(&rmpp_mad->rmpp_hdr) &
	      IB_MGMT_RMPP_FLAG_ACTIVE))
		return IB_RMPP_RESULT_UNHANDLED; /* RMPP not active */

	if (rmpp_mad->rmpp_hdr.rmpp_type != IB_MGMT_RMPP_TYPE_DATA)
		return IB_RMPP_RESULT_INTERNAL;	 /* ACK, STOP, or ABORT */

	if (mad_send_wc->status != IB_WC_SUCCESS ||
	    mad_send_wr->status != IB_WC_SUCCESS)
		return IB_RMPP_RESULT_PROCESSED; /* Canceled or send error */

	if (!mad_send_wr->timeout)
		return IB_RMPP_RESULT_PROCESSED; /* Response received */

	if (mad_send_wr->last_ack == mad_send_wr->send_buf.seg_count) {
		mad_send_wr->timeout =
			msecs_to_jiffies(mad_send_wr->send_buf.timeout_ms);
		return IB_RMPP_RESULT_PROCESSED; /* Send done */
	}

	if (mad_send_wr->seg_num == mad_send_wr->newwin ||
	    mad_send_wr->seg_num == mad_send_wr->send_buf.seg_count)
		return IB_RMPP_RESULT_PROCESSED; /* Wait for ACK */

	ret = send_next_seg(mad_send_wr);
	if (ret) {
		mad_send_wc->status = IB_WC_GENERAL_ERR;
		return IB_RMPP_RESULT_PROCESSED;
	}
	return IB_RMPP_RESULT_CONSUMED;
}

int ib_retry_rmpp(struct ib_mad_send_wr_private *mad_send_wr)
{
	struct ib_rmpp_mad *rmpp_mad;
	int ret;

	rmpp_mad = mad_send_wr->send_buf.mad;
	if (!(ib_get_rmpp_flags(&rmpp_mad->rmpp_hdr) &
	      IB_MGMT_RMPP_FLAG_ACTIVE))
		return IB_RMPP_RESULT_UNHANDLED; /* RMPP not active */

	if (mad_send_wr->last_ack == mad_send_wr->send_buf.seg_count)
		return IB_RMPP_RESULT_PROCESSED;

	mad_send_wr->seg_num = mad_send_wr->last_ack;
	mad_send_wr->cur_seg = mad_send_wr->last_ack_seg;

	ret = send_next_seg(mad_send_wr);
	if (ret)
		return IB_RMPP_RESULT_PROCESSED;

	return IB_RMPP_RESULT_CONSUMED;
}
