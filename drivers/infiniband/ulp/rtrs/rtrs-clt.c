// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * RDMA Transport Layer
 *
 * Copyright (c) 2014 - 2018 ProfitBricks GmbH. All rights reserved.
 * Copyright (c) 2018 - 2019 1&1 IONOS Cloud GmbH. All rights reserved.
 * Copyright (c) 2019 - 2020 1&1 IONOS SE. All rights reserved.
 */

#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME " L" __stringify(__LINE__) ": " fmt

#include <linux/module.h>
#include <linux/rculist.h>
#include <linux/random.h>

#include "rtrs-clt.h"
#include "rtrs-log.h"
#include "rtrs-clt-trace.h"

#define RTRS_CONNECT_TIMEOUT_MS 30000
/*
 * Wait a bit before trying to reconnect after a failure
 * in order to give server time to finish clean up which
 * leads to "false positives" failed reconnect attempts
 */
#define RTRS_RECONNECT_BACKOFF 1000
/*
 * Wait for additional random time between 0 and 8 seconds
 * before starting to reconnect to avoid clients reconnecting
 * all at once in case of a major network outage
 */
#define RTRS_RECONNECT_SEED 8

#define FIRST_CONN 0x01
/* limit to 128 * 4k = 512k max IO */
#define RTRS_MAX_SEGMENTS          128

MODULE_DESCRIPTION("RDMA Transport Client");
MODULE_LICENSE("GPL");

static const struct rtrs_rdma_dev_pd_ops dev_pd_ops;
static struct rtrs_rdma_dev_pd dev_pd = {
	.ops = &dev_pd_ops
};

static struct workqueue_struct *rtrs_wq;
static const struct class rtrs_clt_dev_class = {
	.name = "rtrs-client",
};

static inline bool rtrs_clt_is_connected(const struct rtrs_clt_sess *clt)
{
	struct rtrs_clt_path *clt_path;
	bool connected = false;

	rcu_read_lock();
	list_for_each_entry_rcu(clt_path, &clt->paths_list, s.entry)
		if (READ_ONCE(clt_path->state) == RTRS_CLT_CONNECTED) {
			connected = true;
			break;
		}
	rcu_read_unlock();

	return connected;
}

static struct rtrs_permit *
__rtrs_get_permit(struct rtrs_clt_sess *clt, enum rtrs_clt_con_type con_type)
{
	size_t max_depth = clt->queue_depth;
	struct rtrs_permit *permit;
	int bit;

	/*
	 * Adapted from null_blk get_tag(). Callers from different cpus may
	 * grab the same bit, since find_first_zero_bit is not atomic.
	 * But then the test_and_set_bit_lock will fail for all the
	 * callers but one, so that they will loop again.
	 * This way an explicit spinlock is not required.
	 */
	do {
		bit = find_first_zero_bit(clt->permits_map, max_depth);
		if (bit >= max_depth)
			return NULL;
	} while (test_and_set_bit_lock(bit, clt->permits_map));

	permit = get_permit(clt, bit);
	WARN_ON(permit->mem_id != bit);
	permit->cpu_id = raw_smp_processor_id();
	permit->con_type = con_type;

	return permit;
}

static inline void __rtrs_put_permit(struct rtrs_clt_sess *clt,
				      struct rtrs_permit *permit)
{
	clear_bit_unlock(permit->mem_id, clt->permits_map);
}

/**
 * rtrs_clt_get_permit() - allocates permit for future RDMA operation
 * @clt:	Current session
 * @con_type:	Type of connection to use with the permit
 * @can_wait:	Wait type
 *
 * Description:
 *    Allocates permit for the following RDMA operation.  Permit is used
 *    to preallocate all resources and to propagate memory pressure
 *    up earlier.
 *
 * Context:
 *    Can sleep if @wait == RTRS_PERMIT_WAIT
 */
struct rtrs_permit *rtrs_clt_get_permit(struct rtrs_clt_sess *clt,
					  enum rtrs_clt_con_type con_type,
					  enum wait_type can_wait)
{
	struct rtrs_permit *permit;
	DEFINE_WAIT(wait);

	permit = __rtrs_get_permit(clt, con_type);
	if (permit || !can_wait)
		return permit;

	do {
		prepare_to_wait(&clt->permits_wait, &wait,
				TASK_UNINTERRUPTIBLE);
		permit = __rtrs_get_permit(clt, con_type);
		if (permit)
			break;

		io_schedule();
	} while (1);

	finish_wait(&clt->permits_wait, &wait);

	return permit;
}
EXPORT_SYMBOL(rtrs_clt_get_permit);

/**
 * rtrs_clt_put_permit() - puts allocated permit
 * @clt:	Current session
 * @permit:	Permit to be freed
 *
 * Context:
 *    Does not matter
 */
void rtrs_clt_put_permit(struct rtrs_clt_sess *clt,
			 struct rtrs_permit *permit)
{
	if (WARN_ON(!test_bit(permit->mem_id, clt->permits_map)))
		return;

	__rtrs_put_permit(clt, permit);

	/*
	 * rtrs_clt_get_permit() adds itself to the &clt->permits_wait list
	 * before calling schedule(). So if rtrs_clt_get_permit() is sleeping
	 * it must have added itself to &clt->permits_wait before
	 * __rtrs_put_permit() finished.
	 * Hence it is safe to guard wake_up() with a waitqueue_active() test.
	 */
	if (waitqueue_active(&clt->permits_wait))
		wake_up(&clt->permits_wait);
}
EXPORT_SYMBOL(rtrs_clt_put_permit);

/**
 * rtrs_permit_to_clt_con() - returns RDMA connection pointer by the permit
 * @clt_path: client path pointer
 * @permit: permit for the allocation of the RDMA buffer
 * Note:
 *     IO connection starts from 1.
 *     0 connection is for user messages.
 */
static
struct rtrs_clt_con *rtrs_permit_to_clt_con(struct rtrs_clt_path *clt_path,
					    struct rtrs_permit *permit)
{
	int id = 0;

	if (permit->con_type == RTRS_IO_CON)
		id = (permit->cpu_id % (clt_path->s.irq_con_num - 1)) + 1;

	return to_clt_con(clt_path->s.con[id]);
}

/**
 * rtrs_clt_change_state() - change the session state through session state
 * machine.
 *
 * @clt_path: client path to change the state of.
 * @new_state: state to change to.
 *
 * returns true if sess's state is changed to new state, otherwise return false.
 *
 * Locks:
 * state_wq lock must be hold.
 */
static bool rtrs_clt_change_state(struct rtrs_clt_path *clt_path,
				     enum rtrs_clt_state new_state)
{
	enum rtrs_clt_state old_state;
	bool changed = false;

	lockdep_assert_held(&clt_path->state_wq.lock);

	old_state = clt_path->state;
	switch (new_state) {
	case RTRS_CLT_CONNECTING:
		switch (old_state) {
		case RTRS_CLT_RECONNECTING:
			changed = true;
			fallthrough;
		default:
			break;
		}
		break;
	case RTRS_CLT_RECONNECTING:
		switch (old_state) {
		case RTRS_CLT_CONNECTED:
		case RTRS_CLT_CONNECTING_ERR:
		case RTRS_CLT_CLOSED:
			changed = true;
			fallthrough;
		default:
			break;
		}
		break;
	case RTRS_CLT_CONNECTED:
		switch (old_state) {
		case RTRS_CLT_CONNECTING:
			changed = true;
			fallthrough;
		default:
			break;
		}
		break;
	case RTRS_CLT_CONNECTING_ERR:
		switch (old_state) {
		case RTRS_CLT_CONNECTING:
			changed = true;
			fallthrough;
		default:
			break;
		}
		break;
	case RTRS_CLT_CLOSING:
		switch (old_state) {
		case RTRS_CLT_CONNECTING:
		case RTRS_CLT_CONNECTING_ERR:
		case RTRS_CLT_RECONNECTING:
		case RTRS_CLT_CONNECTED:
			changed = true;
			fallthrough;
		default:
			break;
		}
		break;
	case RTRS_CLT_CLOSED:
		switch (old_state) {
		case RTRS_CLT_CLOSING:
			changed = true;
			fallthrough;
		default:
			break;
		}
		break;
	case RTRS_CLT_DEAD:
		switch (old_state) {
		case RTRS_CLT_CLOSED:
			changed = true;
			fallthrough;
		default:
			break;
		}
		break;
	default:
		break;
	}
	if (changed) {
		clt_path->state = new_state;
		wake_up_locked(&clt_path->state_wq);
	}

	return changed;
}

static bool rtrs_clt_change_state_from_to(struct rtrs_clt_path *clt_path,
					   enum rtrs_clt_state old_state,
					   enum rtrs_clt_state new_state)
{
	bool changed = false;

	spin_lock_irq(&clt_path->state_wq.lock);
	if (clt_path->state == old_state)
		changed = rtrs_clt_change_state(clt_path, new_state);
	spin_unlock_irq(&clt_path->state_wq.lock);

	return changed;
}

static void rtrs_clt_stop_and_destroy_conns(struct rtrs_clt_path *clt_path);
static void rtrs_rdma_error_recovery(struct rtrs_clt_con *con)
{
	struct rtrs_clt_path *clt_path = to_clt_path(con->c.path);

	trace_rtrs_rdma_error_recovery(clt_path);

	if (rtrs_clt_change_state_from_to(clt_path,
					   RTRS_CLT_CONNECTED,
					   RTRS_CLT_RECONNECTING)) {
		queue_work(rtrs_wq, &clt_path->err_recovery_work);
	} else {
		/*
		 * Error can happen just on establishing new connection,
		 * so notify waiter with error state, waiter is responsible
		 * for cleaning the rest and reconnect if needed.
		 */
		rtrs_clt_change_state_from_to(clt_path,
					       RTRS_CLT_CONNECTING,
					       RTRS_CLT_CONNECTING_ERR);
	}
}

static void rtrs_clt_fast_reg_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct rtrs_clt_con *con = to_clt_con(wc->qp->qp_context);

	if (wc->status != IB_WC_SUCCESS) {
		rtrs_err_rl(con->c.path, "Failed IB_WR_REG_MR: %s\n",
			  ib_wc_status_msg(wc->status));
		rtrs_rdma_error_recovery(con);
	}
}

static struct ib_cqe fast_reg_cqe = {
	.done = rtrs_clt_fast_reg_done
};

static void complete_rdma_req(struct rtrs_clt_io_req *req, int errno,
			      bool notify, bool can_wait);

static void rtrs_clt_inv_rkey_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct rtrs_clt_io_req *req =
		container_of(wc->wr_cqe, typeof(*req), inv_cqe);
	struct rtrs_clt_con *con = to_clt_con(wc->qp->qp_context);

	if (wc->status != IB_WC_SUCCESS) {
		rtrs_err_rl(con->c.path, "Failed IB_WR_LOCAL_INV: %s\n",
			  ib_wc_status_msg(wc->status));
		rtrs_rdma_error_recovery(con);
	}
	req->mr->need_inval = false;
	if (req->need_inv_comp)
		complete(&req->inv_comp);
	else
		/* Complete request from INV callback */
		complete_rdma_req(req, req->inv_errno, true, false);
}

static int rtrs_inv_rkey(struct rtrs_clt_io_req *req)
{
	struct rtrs_clt_con *con = req->con;
	struct ib_send_wr wr = {
		.opcode		    = IB_WR_LOCAL_INV,
		.wr_cqe		    = &req->inv_cqe,
		.send_flags	    = IB_SEND_SIGNALED,
		.ex.invalidate_rkey = req->mr->rkey,
	};
	req->inv_cqe.done = rtrs_clt_inv_rkey_done;

	return ib_post_send(con->c.qp, &wr, NULL);
}

static void complete_rdma_req(struct rtrs_clt_io_req *req, int errno,
			      bool notify, bool can_wait)
{
	struct rtrs_clt_con *con = req->con;
	struct rtrs_clt_path *clt_path;
	int err;

	if (!req->in_use)
		return;
	if (WARN_ON(!req->con))
		return;
	clt_path = to_clt_path(con->c.path);

	if (req->sg_cnt) {
		if (req->mr->need_inval) {
			/*
			 * We are here to invalidate read/write requests
			 * ourselves.  In normal scenario server should
			 * send INV for all read requests, we do local
			 * invalidate for write requests ourselves, but
			 * we are here, thus three things could happen:
			 *
			 *    1.  this is failover, when errno != 0
			 *        and can_wait == 1,
			 *
			 *    2.  something totally bad happened and
			 *        server forgot to send INV, so we
			 *        should do that ourselves.
			 *
			 *    3.  write request finishes, we need to do local
			 *        invalidate
			 */

			if (can_wait) {
				req->need_inv_comp = true;
			} else {
				/* This should be IO path, so always notify */
				WARN_ON(!notify);
				/* Save errno for INV callback */
				req->inv_errno = errno;
			}

			refcount_inc(&req->ref);
			err = rtrs_inv_rkey(req);
			if (err) {
				rtrs_err_rl(con->c.path, "Send INV WR key=%#x: %d\n",
					  req->mr->rkey, err);
			} else if (can_wait) {
				wait_for_completion(&req->inv_comp);
			}
			if (!refcount_dec_and_test(&req->ref))
				return;
		}
		ib_dma_unmap_sg(clt_path->s.dev->ib_dev, req->sglist,
				req->sg_cnt, req->dir);
	}
	if (!refcount_dec_and_test(&req->ref))
		return;
	if (req->mp_policy == MP_POLICY_MIN_INFLIGHT)
		atomic_dec(&clt_path->stats->inflight);

	req->in_use = false;
	req->con = NULL;

	if (errno) {
		rtrs_err_rl(con->c.path,
			    "IO %s request failed: error=%d path=%s [%s:%u] notify=%d\n",
			    req->dir == DMA_TO_DEVICE ? "write" : "read", errno,
			    kobject_name(&clt_path->kobj), clt_path->hca_name,
			    clt_path->hca_port, notify);
	}

	if (notify)
		req->conf(req->priv, errno);
}

static int rtrs_post_send_rdma(struct rtrs_clt_con *con,
				struct rtrs_clt_io_req *req,
				struct rtrs_rbuf *rbuf, u32 off,
				u32 imm, struct ib_send_wr *wr)
{
	struct rtrs_clt_path *clt_path = to_clt_path(con->c.path);
	enum ib_send_flags flags;
	struct ib_sge sge;

	if (!req->sg_size) {
		rtrs_wrn(con->c.path,
			 "Doing RDMA Write failed, no data supplied\n");
		return -EINVAL;
	}

	/* user data and user message in the first list element */
	sge.addr   = req->iu->dma_addr;
	sge.length = req->sg_size;
	sge.lkey   = clt_path->s.dev->ib_pd->local_dma_lkey;

	/*
	 * From time to time we have to post signalled sends,
	 * or send queue will fill up and only QP reset can help.
	 */
	flags = atomic_inc_return(&con->c.wr_cnt) % clt_path->s.signal_interval ?
			0 : IB_SEND_SIGNALED;

	ib_dma_sync_single_for_device(clt_path->s.dev->ib_dev,
				      req->iu->dma_addr,
				      req->sg_size, DMA_TO_DEVICE);

	return rtrs_iu_post_rdma_write_imm(&con->c, req->iu, &sge, 1,
					    rbuf->rkey, rbuf->addr + off,
					    imm, flags, wr, NULL);
}

static void process_io_rsp(struct rtrs_clt_path *clt_path, u32 msg_id,
			   s16 errno, bool w_inval)
{
	struct rtrs_clt_io_req *req;

	if (WARN_ON(msg_id >= clt_path->queue_depth))
		return;

	req = &clt_path->reqs[msg_id];
	/* Drop need_inv if server responded with send with invalidation */
	req->mr->need_inval &= !w_inval;
	complete_rdma_req(req, errno, true, false);
}

static void rtrs_clt_recv_done(struct rtrs_clt_con *con, struct ib_wc *wc)
{
	struct rtrs_iu *iu;
	int err;
	struct rtrs_clt_path *clt_path = to_clt_path(con->c.path);

	WARN_ON((clt_path->flags & RTRS_MSG_NEW_RKEY_F) == 0);
	iu = container_of(wc->wr_cqe, struct rtrs_iu,
			  cqe);
	err = rtrs_iu_post_recv(&con->c, iu);
	if (err) {
		rtrs_err(con->c.path, "post iu failed %d\n", err);
		rtrs_rdma_error_recovery(con);
	}
}

static void rtrs_clt_rkey_rsp_done(struct rtrs_clt_con *con, struct ib_wc *wc)
{
	struct rtrs_clt_path *clt_path = to_clt_path(con->c.path);
	struct rtrs_msg_rkey_rsp *msg;
	u32 imm_type, imm_payload;
	bool w_inval = false;
	struct rtrs_iu *iu;
	u32 buf_id;
	int err;

	WARN_ON((clt_path->flags & RTRS_MSG_NEW_RKEY_F) == 0);

	iu = container_of(wc->wr_cqe, struct rtrs_iu, cqe);

	if (wc->byte_len < sizeof(*msg)) {
		rtrs_err(con->c.path, "rkey response is malformed: size %d\n",
			  wc->byte_len);
		goto out;
	}
	ib_dma_sync_single_for_cpu(clt_path->s.dev->ib_dev, iu->dma_addr,
				   iu->size, DMA_FROM_DEVICE);
	msg = iu->buf;
	if (le16_to_cpu(msg->type) != RTRS_MSG_RKEY_RSP) {
		rtrs_err(clt_path->clt,
			  "rkey response is malformed: type %d\n",
			  le16_to_cpu(msg->type));
		goto out;
	}
	buf_id = le16_to_cpu(msg->buf_id);
	if (WARN_ON(buf_id >= clt_path->queue_depth))
		goto out;

	rtrs_from_imm(be32_to_cpu(wc->ex.imm_data), &imm_type, &imm_payload);
	if (imm_type == RTRS_IO_RSP_IMM ||
	    imm_type == RTRS_IO_RSP_W_INV_IMM) {
		u32 msg_id;

		w_inval = (imm_type == RTRS_IO_RSP_W_INV_IMM);
		rtrs_from_io_rsp_imm(imm_payload, &msg_id, &err);

		if (WARN_ON(buf_id != msg_id))
			goto out;
		clt_path->rbufs[buf_id].rkey = le32_to_cpu(msg->rkey);
		process_io_rsp(clt_path, msg_id, err, w_inval);
	}
	ib_dma_sync_single_for_device(clt_path->s.dev->ib_dev, iu->dma_addr,
				      iu->size, DMA_FROM_DEVICE);
	return rtrs_clt_recv_done(con, wc);
out:
	rtrs_rdma_error_recovery(con);
}

static void rtrs_clt_rdma_done(struct ib_cq *cq, struct ib_wc *wc);

static struct ib_cqe io_comp_cqe = {
	.done = rtrs_clt_rdma_done
};

/*
 * Post x2 empty WRs: first is for this RDMA with IMM,
 * second is for RECV with INV, which happened earlier.
 */
static int rtrs_post_recv_empty_x2(struct rtrs_con *con, struct ib_cqe *cqe)
{
	struct ib_recv_wr wr_arr[2], *wr;
	int i;

	memset(wr_arr, 0, sizeof(wr_arr));
	for (i = 0; i < ARRAY_SIZE(wr_arr); i++) {
		wr = &wr_arr[i];
		wr->wr_cqe  = cqe;
		if (i)
			/* Chain backwards */
			wr->next = &wr_arr[i - 1];
	}

	return ib_post_recv(con->qp, wr, NULL);
}

static void rtrs_clt_rdma_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct rtrs_clt_con *con = to_clt_con(wc->qp->qp_context);
	struct rtrs_clt_path *clt_path = to_clt_path(con->c.path);
	u32 imm_type, imm_payload;
	bool w_inval = false;
	int err;

	if (wc->status != IB_WC_SUCCESS) {
		if (wc->status != IB_WC_WR_FLUSH_ERR) {
			rtrs_err(clt_path->clt, "RDMA failed: %s\n",
				  ib_wc_status_msg(wc->status));
			rtrs_rdma_error_recovery(con);
		}
		return;
	}
	rtrs_clt_update_wc_stats(con);

	switch (wc->opcode) {
	case IB_WC_RECV_RDMA_WITH_IMM:
		/*
		 * post_recv() RDMA write completions of IO reqs (read/write)
		 * and hb
		 */
		if (WARN_ON(wc->wr_cqe->done != rtrs_clt_rdma_done))
			return;
		clt_path->s.hb_missed_cnt = 0;
		rtrs_from_imm(be32_to_cpu(wc->ex.imm_data),
			       &imm_type, &imm_payload);
		if (imm_type == RTRS_IO_RSP_IMM ||
		    imm_type == RTRS_IO_RSP_W_INV_IMM) {
			u32 msg_id;

			w_inval = (imm_type == RTRS_IO_RSP_W_INV_IMM);
			rtrs_from_io_rsp_imm(imm_payload, &msg_id, &err);

			process_io_rsp(clt_path, msg_id, err, w_inval);
		} else if (imm_type == RTRS_HB_MSG_IMM) {
			WARN_ON(con->c.cid);
			rtrs_send_hb_ack(&clt_path->s);
			if (clt_path->flags & RTRS_MSG_NEW_RKEY_F)
				return  rtrs_clt_recv_done(con, wc);
		} else if (imm_type == RTRS_HB_ACK_IMM) {
			WARN_ON(con->c.cid);
			clt_path->s.hb_cur_latency =
				ktime_sub(ktime_get(), clt_path->s.hb_last_sent);
			if (clt_path->flags & RTRS_MSG_NEW_RKEY_F)
				return  rtrs_clt_recv_done(con, wc);
		} else {
			rtrs_wrn(con->c.path, "Unknown IMM type %u\n",
				  imm_type);
		}
		if (w_inval)
			/*
			 * Post x2 empty WRs: first is for this RDMA with IMM,
			 * second is for RECV with INV, which happened earlier.
			 */
			err = rtrs_post_recv_empty_x2(&con->c, &io_comp_cqe);
		else
			err = rtrs_post_recv_empty(&con->c, &io_comp_cqe);
		if (err) {
			rtrs_err(con->c.path, "rtrs_post_recv_empty(): %d\n",
				  err);
			rtrs_rdma_error_recovery(con);
		}
		break;
	case IB_WC_RECV:
		/*
		 * Key invalidations from server side
		 */
		clt_path->s.hb_missed_cnt = 0;
		WARN_ON(!(wc->wc_flags & IB_WC_WITH_INVALIDATE ||
			  wc->wc_flags & IB_WC_WITH_IMM));
		WARN_ON(wc->wr_cqe->done != rtrs_clt_rdma_done);
		if (clt_path->flags & RTRS_MSG_NEW_RKEY_F) {
			if (wc->wc_flags & IB_WC_WITH_INVALIDATE)
				return  rtrs_clt_recv_done(con, wc);

			return  rtrs_clt_rkey_rsp_done(con, wc);
		}
		break;
	case IB_WC_RDMA_WRITE:
		/*
		 * post_send() RDMA write completions of IO reqs (read/write)
		 * and hb.
		 */
		break;

	default:
		rtrs_wrn(clt_path->clt, "Unexpected WC type: %d\n", wc->opcode);
		return;
	}
}

static int post_recv_io(struct rtrs_clt_con *con, size_t q_size)
{
	int err, i;
	struct rtrs_clt_path *clt_path = to_clt_path(con->c.path);

	for (i = 0; i < q_size; i++) {
		if (clt_path->flags & RTRS_MSG_NEW_RKEY_F) {
			struct rtrs_iu *iu = &con->rsp_ius[i];

			err = rtrs_iu_post_recv(&con->c, iu);
		} else {
			err = rtrs_post_recv_empty(&con->c, &io_comp_cqe);
		}
		if (err)
			return err;
	}

	return 0;
}

static int post_recv_path(struct rtrs_clt_path *clt_path)
{
	size_t q_size = 0;
	int err, cid;

	for (cid = 0; cid < clt_path->s.con_num; cid++) {
		if (cid == 0)
			q_size = SERVICE_CON_QUEUE_DEPTH;
		else
			q_size = clt_path->queue_depth;

		/*
		 * x2 for RDMA read responses + FR key invalidations,
		 * RDMA writes do not require any FR registrations.
		 */
		q_size *= 2;

		err = post_recv_io(to_clt_con(clt_path->s.con[cid]), q_size);
		if (err) {
			rtrs_err(clt_path->clt, "post_recv_io(), err: %d\n",
				 err);
			return err;
		}
	}

	return 0;
}

struct path_it {
	int i;
	struct list_head skip_list;
	struct rtrs_clt_sess *clt;
	struct rtrs_clt_path *(*next_path)(struct path_it *it);
};

/*
 * rtrs_clt_get_next_path_or_null - get clt path from the list or return NULL
 * @head:	the head for the list.
 * @clt_path:	The element to take the next clt_path from.
 *
 * Next clt path returned in round-robin fashion, i.e. head will be skipped,
 * but if list is observed as empty, NULL will be returned.
 *
 * This function may safely run concurrently with the _rcu list-mutation
 * primitives such as list_add_rcu() as long as it's guarded by rcu_read_lock().
 */
static inline struct rtrs_clt_path *
rtrs_clt_get_next_path_or_null(struct list_head *head, struct rtrs_clt_path *clt_path)
{
	return list_next_or_null_rcu(head, &clt_path->s.entry, typeof(*clt_path), s.entry) ?:
				     list_next_or_null_rcu(head,
							   READ_ONCE((&clt_path->s.entry)->next),
							   typeof(*clt_path), s.entry);
}

/**
 * get_next_path_rr() - Returns path in round-robin fashion.
 * @it:	the path pointer
 *
 * Related to @MP_POLICY_RR
 *
 * Locks:
 *    rcu_read_lock() must be held.
 */
static struct rtrs_clt_path *get_next_path_rr(struct path_it *it)
{
	struct rtrs_clt_path __rcu **ppcpu_path;
	struct rtrs_clt_path *path;
	struct rtrs_clt_sess *clt;

	/*
	 * Assert that rcu lock must be held
	 */
	RCU_LOCKDEP_WARN(!rcu_read_lock_held(), "no rcu read lock held");

	clt = it->clt;

	/*
	 * Here we use two RCU objects: @paths_list and @pcpu_path
	 * pointer.  See rtrs_clt_remove_path_from_arr() for details
	 * how that is handled.
	 */

	ppcpu_path = this_cpu_ptr(clt->pcpu_path);
	path = rcu_dereference(*ppcpu_path);
	if (!path)
		path = list_first_or_null_rcu(&clt->paths_list,
					      typeof(*path), s.entry);
	else
		path = rtrs_clt_get_next_path_or_null(&clt->paths_list, path);

	rcu_assign_pointer(*ppcpu_path, path);

	return path;
}

/**
 * get_next_path_min_inflight() - Returns path with minimal inflight count.
 * @it:	the path pointer
 *
 * Related to @MP_POLICY_MIN_INFLIGHT
 *
 * Locks:
 *    rcu_read_lock() must be hold.
 */
static struct rtrs_clt_path *get_next_path_min_inflight(struct path_it *it)
{
	struct rtrs_clt_path *min_path = NULL;
	struct rtrs_clt_sess *clt = it->clt;
	struct rtrs_clt_path *clt_path;
	int min_inflight = INT_MAX;
	int inflight;

	list_for_each_entry_rcu(clt_path, &clt->paths_list, s.entry) {
		if (READ_ONCE(clt_path->state) != RTRS_CLT_CONNECTED)
			continue;

		if (!list_empty(raw_cpu_ptr(clt_path->mp_skip_entry)))
			continue;

		inflight = atomic_read(&clt_path->stats->inflight);

		if (inflight < min_inflight) {
			min_inflight = inflight;
			min_path = clt_path;
		}
	}

	/*
	 * add the path to the skip list, so that next time we can get
	 * a different one
	 */
	if (min_path)
		list_add(raw_cpu_ptr(min_path->mp_skip_entry), &it->skip_list);

	return min_path;
}

/**
 * get_next_path_min_latency() - Returns path with minimal latency.
 * @it:	the path pointer
 *
 * Return: a path with the lowest latency or NULL if all paths are tried
 *
 * Locks:
 *    rcu_read_lock() must be hold.
 *
 * Related to @MP_POLICY_MIN_LATENCY
 *
 * This DOES skip an already-tried path.
 * There is a skip-list to skip a path if the path has tried but failed.
 * It will try the minimum latency path and then the second minimum latency
 * path and so on. Finally it will return NULL if all paths are tried.
 * Therefore the caller MUST check the returned
 * path is NULL and trigger the IO error.
 */
static struct rtrs_clt_path *get_next_path_min_latency(struct path_it *it)
{
	struct rtrs_clt_path *min_path = NULL;
	struct rtrs_clt_sess *clt = it->clt;
	struct rtrs_clt_path *clt_path;
	ktime_t min_latency = KTIME_MAX;
	ktime_t latency;

	list_for_each_entry_rcu(clt_path, &clt->paths_list, s.entry) {
		if (READ_ONCE(clt_path->state) != RTRS_CLT_CONNECTED)
			continue;

		if (!list_empty(raw_cpu_ptr(clt_path->mp_skip_entry)))
			continue;

		latency = clt_path->s.hb_cur_latency;

		if (latency < min_latency) {
			min_latency = latency;
			min_path = clt_path;
		}
	}

	/*
	 * add the path to the skip list, so that next time we can get
	 * a different one
	 */
	if (min_path)
		list_add(raw_cpu_ptr(min_path->mp_skip_entry), &it->skip_list);

	return min_path;
}

static inline void path_it_init(struct path_it *it, struct rtrs_clt_sess *clt)
{
	INIT_LIST_HEAD(&it->skip_list);
	it->clt = clt;
	it->i = 0;

	if (clt->mp_policy == MP_POLICY_RR)
		it->next_path = get_next_path_rr;
	else if (clt->mp_policy == MP_POLICY_MIN_INFLIGHT)
		it->next_path = get_next_path_min_inflight;
	else
		it->next_path = get_next_path_min_latency;
}

static inline void path_it_deinit(struct path_it *it)
{
	struct list_head *skip, *tmp;
	/*
	 * The skip_list is used only for the MIN_INFLIGHT and MIN_LATENCY policies.
	 * We need to remove paths from it, so that next IO can insert
	 * paths (->mp_skip_entry) into a skip_list again.
	 */
	list_for_each_safe(skip, tmp, &it->skip_list)
		list_del_init(skip);
}

/**
 * rtrs_clt_init_req() - Initialize an rtrs_clt_io_req holding information
 * about an inflight IO.
 * The user buffer holding user control message (not data) is copied into
 * the corresponding buffer of rtrs_iu (req->iu->buf), which later on will
 * also hold the control message of rtrs.
 * @req: an io request holding information about IO.
 * @clt_path: client path
 * @conf: conformation callback function to notify upper layer.
 * @permit: permit for allocation of RDMA remote buffer
 * @priv: private pointer
 * @vec: kernel vector containing control message
 * @usr_len: length of the user message
 * @sg: scater list for IO data
 * @sg_cnt: number of scater list entries
 * @data_len: length of the IO data
 * @dir: direction of the IO.
 */
static void rtrs_clt_init_req(struct rtrs_clt_io_req *req,
			      struct rtrs_clt_path *clt_path,
			      void (*conf)(void *priv, int errno),
			      struct rtrs_permit *permit, void *priv,
			      const struct kvec *vec, size_t usr_len,
			      struct scatterlist *sg, size_t sg_cnt,
			      size_t data_len, int dir)
{
	struct iov_iter iter;
	size_t len;

	req->permit = permit;
	req->in_use = true;
	req->usr_len = usr_len;
	req->data_len = data_len;
	req->sglist = sg;
	req->sg_cnt = sg_cnt;
	req->priv = priv;
	req->dir = dir;
	req->con = rtrs_permit_to_clt_con(clt_path, permit);
	req->conf = conf;
	req->mr->need_inval = false;
	req->need_inv_comp = false;
	req->inv_errno = 0;
	refcount_set(&req->ref, 1);
	req->mp_policy = clt_path->clt->mp_policy;

	iov_iter_kvec(&iter, ITER_SOURCE, vec, 1, usr_len);
	len = _copy_from_iter(req->iu->buf, usr_len, &iter);
	WARN_ON(len != usr_len);

	reinit_completion(&req->inv_comp);
}

static struct rtrs_clt_io_req *
rtrs_clt_get_req(struct rtrs_clt_path *clt_path,
		 void (*conf)(void *priv, int errno),
		 struct rtrs_permit *permit, void *priv,
		 const struct kvec *vec, size_t usr_len,
		 struct scatterlist *sg, size_t sg_cnt,
		 size_t data_len, int dir)
{
	struct rtrs_clt_io_req *req;

	req = &clt_path->reqs[permit->mem_id];
	rtrs_clt_init_req(req, clt_path, conf, permit, priv, vec, usr_len,
			   sg, sg_cnt, data_len, dir);
	return req;
}

static struct rtrs_clt_io_req *
rtrs_clt_get_copy_req(struct rtrs_clt_path *alive_path,
		       struct rtrs_clt_io_req *fail_req)
{
	struct rtrs_clt_io_req *req;
	struct kvec vec = {
		.iov_base = fail_req->iu->buf,
		.iov_len  = fail_req->usr_len
	};

	req = &alive_path->reqs[fail_req->permit->mem_id];
	rtrs_clt_init_req(req, alive_path, fail_req->conf, fail_req->permit,
			   fail_req->priv, &vec, fail_req->usr_len,
			   fail_req->sglist, fail_req->sg_cnt,
			   fail_req->data_len, fail_req->dir);
	return req;
}

static int rtrs_post_rdma_write_sg(struct rtrs_clt_con *con,
				   struct rtrs_clt_io_req *req,
				   struct rtrs_rbuf *rbuf, bool fr_en,
				   u32 count, u32 size, u32 imm,
				   struct ib_send_wr *wr,
				   struct ib_send_wr *tail)
{
	struct rtrs_clt_path *clt_path = to_clt_path(con->c.path);
	struct ib_sge *sge = req->sge;
	enum ib_send_flags flags;
	struct scatterlist *sg;
	size_t num_sge;
	int i;
	struct ib_send_wr *ptail = NULL;

	if (fr_en) {
		i = 0;
		sge[i].addr   = req->mr->iova;
		sge[i].length = req->mr->length;
		sge[i].lkey   = req->mr->lkey;
		i++;
		num_sge = 2;
		ptail = tail;
	} else {
		for_each_sg(req->sglist, sg, count, i) {
			sge[i].addr   = sg_dma_address(sg);
			sge[i].length = sg_dma_len(sg);
			sge[i].lkey   = clt_path->s.dev->ib_pd->local_dma_lkey;
		}
		num_sge = 1 + count;
	}
	sge[i].addr   = req->iu->dma_addr;
	sge[i].length = size;
	sge[i].lkey   = clt_path->s.dev->ib_pd->local_dma_lkey;

	/*
	 * From time to time we have to post signalled sends,
	 * or send queue will fill up and only QP reset can help.
	 */
	flags = atomic_inc_return(&con->c.wr_cnt) % clt_path->s.signal_interval ?
			0 : IB_SEND_SIGNALED;

	ib_dma_sync_single_for_device(clt_path->s.dev->ib_dev,
				      req->iu->dma_addr,
				      size, DMA_TO_DEVICE);

	return rtrs_iu_post_rdma_write_imm(&con->c, req->iu, sge, num_sge,
					    rbuf->rkey, rbuf->addr, imm,
					    flags, wr, ptail);
}

static int rtrs_map_sg_fr(struct rtrs_clt_io_req *req, size_t count)
{
	int nr;

	/* Align the MR to a 4K page size to match the block virt boundary */
	nr = ib_map_mr_sg(req->mr, req->sglist, count, NULL, SZ_4K);
	if (nr != count)
		return nr < 0 ? nr : -EINVAL;
	ib_update_fast_reg_key(req->mr, ib_inc_rkey(req->mr->rkey));

	return nr;
}

static int rtrs_clt_write_req(struct rtrs_clt_io_req *req)
{
	struct rtrs_clt_con *con = req->con;
	struct rtrs_path *s = con->c.path;
	struct rtrs_clt_path *clt_path = to_clt_path(s);
	struct rtrs_msg_rdma_write *msg;

	struct rtrs_rbuf *rbuf;
	int ret, count = 0;
	u32 imm, buf_id;
	struct ib_reg_wr rwr;
	struct ib_send_wr *wr = NULL;
	bool fr_en = false;

	const size_t tsize = sizeof(*msg) + req->data_len + req->usr_len;

	if (tsize > clt_path->chunk_size) {
		rtrs_wrn(s, "Write request failed, size too big %zu > %d\n",
			  tsize, clt_path->chunk_size);
		return -EMSGSIZE;
	}
	if (req->sg_cnt) {
		count = ib_dma_map_sg(clt_path->s.dev->ib_dev, req->sglist,
				      req->sg_cnt, req->dir);
		if (!count) {
			rtrs_wrn(s, "Write request failed, map failed\n");
			return -EINVAL;
		}
	}
	/* put rtrs msg after sg and user message */
	msg = req->iu->buf + req->usr_len;
	msg->type = cpu_to_le16(RTRS_MSG_WRITE);
	msg->usr_len = cpu_to_le16(req->usr_len);

	/* rtrs message on server side will be after user data and message */
	imm = req->permit->mem_off + req->data_len + req->usr_len;
	imm = rtrs_to_io_req_imm(imm);
	buf_id = req->permit->mem_id;
	req->sg_size = tsize;
	rbuf = &clt_path->rbufs[buf_id];

	if (count) {
		ret = rtrs_map_sg_fr(req, count);
		if (ret < 0) {
			rtrs_err_rl(s,
				    "Write request failed, failed to map fast reg. data, err: %d\n",
				    ret);
			ib_dma_unmap_sg(clt_path->s.dev->ib_dev, req->sglist,
					req->sg_cnt, req->dir);
			return ret;
		}
		rwr = (struct ib_reg_wr) {
			.wr.opcode = IB_WR_REG_MR,
			.wr.wr_cqe = &fast_reg_cqe,
			.mr = req->mr,
			.key = req->mr->rkey,
			.access = (IB_ACCESS_LOCAL_WRITE),
		};
		wr = &rwr.wr;
		fr_en = true;
		req->mr->need_inval = true;
	}
	/*
	 * Update stats now, after request is successfully sent it is not
	 * safe anymore to touch it.
	 */
	rtrs_clt_update_all_stats(req, WRITE);

	ret = rtrs_post_rdma_write_sg(req->con, req, rbuf, fr_en, count,
				      req->usr_len + sizeof(*msg),
				      imm, wr, NULL);
	if (ret) {
		rtrs_err_rl(s,
			    "Write request failed: error=%d path=%s [%s:%u]\n",
			    ret, kobject_name(&clt_path->kobj), clt_path->hca_name,
			    clt_path->hca_port);
		if (req->mp_policy == MP_POLICY_MIN_INFLIGHT)
			atomic_dec(&clt_path->stats->inflight);
		if (req->mr->need_inval) {
			req->mr->need_inval = false;
			refcount_dec(&req->ref);
		}
		if (req->sg_cnt)
			ib_dma_unmap_sg(clt_path->s.dev->ib_dev, req->sglist,
					req->sg_cnt, req->dir);
	}

	return ret;
}

static int rtrs_clt_read_req(struct rtrs_clt_io_req *req)
{
	struct rtrs_clt_con *con = req->con;
	struct rtrs_path *s = con->c.path;
	struct rtrs_clt_path *clt_path = to_clt_path(s);
	struct rtrs_msg_rdma_read *msg;
	struct rtrs_ib_dev *dev = clt_path->s.dev;

	struct ib_reg_wr rwr;
	struct ib_send_wr *wr = NULL;

	int ret, count = 0;
	u32 imm, buf_id;

	const size_t tsize = sizeof(*msg) + req->data_len + req->usr_len;

	if (tsize > clt_path->chunk_size) {
		rtrs_wrn(s,
			  "Read request failed, message size is %zu, bigger than CHUNK_SIZE %d\n",
			  tsize, clt_path->chunk_size);
		return -EMSGSIZE;
	}

	if (req->sg_cnt) {
		count = ib_dma_map_sg(dev->ib_dev, req->sglist, req->sg_cnt,
				      req->dir);
		if (!count) {
			rtrs_wrn(s,
				  "Read request failed, dma map failed\n");
			return -EINVAL;
		}
	}
	/* put our message into req->buf after user message*/
	msg = req->iu->buf + req->usr_len;
	msg->type = cpu_to_le16(RTRS_MSG_READ);
	msg->usr_len = cpu_to_le16(req->usr_len);

	if (count) {
		ret = rtrs_map_sg_fr(req, count);
		if (ret < 0) {
			rtrs_err_rl(s,
				     "Read request failed, failed to map fast reg. data, err: %d\n",
				     ret);
			ib_dma_unmap_sg(dev->ib_dev, req->sglist, req->sg_cnt,
					req->dir);
			return ret;
		}
		rwr = (struct ib_reg_wr) {
			.wr.opcode = IB_WR_REG_MR,
			.wr.wr_cqe = &fast_reg_cqe,
			.mr = req->mr,
			.key = req->mr->rkey,
			.access = (IB_ACCESS_LOCAL_WRITE |
				   IB_ACCESS_REMOTE_WRITE),
		};
		wr = &rwr.wr;

		msg->sg_cnt = cpu_to_le16(1);
		msg->flags = cpu_to_le16(RTRS_MSG_NEED_INVAL_F);

		msg->desc[0].addr = cpu_to_le64(req->mr->iova);
		msg->desc[0].key = cpu_to_le32(req->mr->rkey);
		msg->desc[0].len = cpu_to_le32(req->mr->length);

		/* Further invalidation is required */
		req->mr->need_inval = !!RTRS_MSG_NEED_INVAL_F;

	} else {
		msg->sg_cnt = 0;
		msg->flags = 0;
	}
	/*
	 * rtrs message will be after the space reserved for disk data and
	 * user message
	 */
	imm = req->permit->mem_off + req->data_len + req->usr_len;
	imm = rtrs_to_io_req_imm(imm);
	buf_id = req->permit->mem_id;

	req->sg_size  = sizeof(*msg);
	req->sg_size += le16_to_cpu(msg->sg_cnt) * sizeof(struct rtrs_sg_desc);
	req->sg_size += req->usr_len;

	/*
	 * Update stats now, after request is successfully sent it is not
	 * safe anymore to touch it.
	 */
	rtrs_clt_update_all_stats(req, READ);

	ret = rtrs_post_send_rdma(req->con, req, &clt_path->rbufs[buf_id],
				   req->data_len, imm, wr);
	if (ret) {
		rtrs_err_rl(s,
			    "Read request failed: error=%d path=%s [%s:%u]\n",
			    ret, kobject_name(&clt_path->kobj), clt_path->hca_name,
			    clt_path->hca_port);
		if (req->mp_policy == MP_POLICY_MIN_INFLIGHT)
			atomic_dec(&clt_path->stats->inflight);
		req->mr->need_inval = false;
		if (req->sg_cnt)
			ib_dma_unmap_sg(dev->ib_dev, req->sglist,
					req->sg_cnt, req->dir);
	}

	return ret;
}

/**
 * rtrs_clt_failover_req() - Try to find an active path for a failed request
 * @clt: clt context
 * @fail_req: a failed io request.
 */
static int rtrs_clt_failover_req(struct rtrs_clt_sess *clt,
				 struct rtrs_clt_io_req *fail_req)
{
	struct rtrs_clt_path *alive_path;
	struct rtrs_clt_io_req *req;
	int err = -ECONNABORTED;
	struct path_it it;

	rcu_read_lock();
	for (path_it_init(&it, clt);
	     (alive_path = it.next_path(&it)) && it.i < it.clt->paths_num;
	     it.i++) {
		if (READ_ONCE(alive_path->state) != RTRS_CLT_CONNECTED)
			continue;
		req = rtrs_clt_get_copy_req(alive_path, fail_req);
		if (req->dir == DMA_TO_DEVICE)
			err = rtrs_clt_write_req(req);
		else
			err = rtrs_clt_read_req(req);
		if (err) {
			req->in_use = false;
			continue;
		}
		/* Success path */
		rtrs_clt_inc_failover_cnt(alive_path->stats);
		break;
	}
	path_it_deinit(&it);
	rcu_read_unlock();

	return err;
}

static void fail_all_outstanding_reqs(struct rtrs_clt_path *clt_path)
{
	struct rtrs_clt_sess *clt = clt_path->clt;
	struct rtrs_clt_io_req *req;
	int i, err;

	if (!clt_path->reqs)
		return;
	for (i = 0; i < clt_path->queue_depth; ++i) {
		req = &clt_path->reqs[i];
		if (!req->in_use)
			continue;

		/*
		 * Safely (without notification) complete failed request.
		 * After completion this request is still useble and can
		 * be failovered to another path.
		 */
		complete_rdma_req(req, -ECONNABORTED, false, true);

		err = rtrs_clt_failover_req(clt, req);
		if (err)
			/* Failover failed, notify anyway */
			req->conf(req->priv, err);
	}
}

static void free_path_reqs(struct rtrs_clt_path *clt_path)
{
	struct rtrs_clt_io_req *req;
	int i;

	if (!clt_path->reqs)
		return;
	for (i = 0; i < clt_path->queue_depth; ++i) {
		req = &clt_path->reqs[i];
		if (req->mr)
			ib_dereg_mr(req->mr);
		kfree(req->sge);
		rtrs_iu_free(req->iu, clt_path->s.dev->ib_dev, 1);
	}
	kfree(clt_path->reqs);
	clt_path->reqs = NULL;
}

static int alloc_path_reqs(struct rtrs_clt_path *clt_path)
{
	struct rtrs_clt_io_req *req;
	int i, err = -ENOMEM;

	clt_path->reqs = kcalloc(clt_path->queue_depth,
				 sizeof(*clt_path->reqs),
				 GFP_KERNEL);
	if (!clt_path->reqs)
		return -ENOMEM;

	for (i = 0; i < clt_path->queue_depth; ++i) {
		req = &clt_path->reqs[i];
		req->iu = rtrs_iu_alloc(1, clt_path->max_hdr_size, GFP_KERNEL,
					 clt_path->s.dev->ib_dev,
					 DMA_TO_DEVICE,
					 rtrs_clt_rdma_done);
		if (!req->iu)
			goto out;

		req->sge = kcalloc(2, sizeof(*req->sge), GFP_KERNEL);
		if (!req->sge)
			goto out;

		req->mr = ib_alloc_mr(clt_path->s.dev->ib_pd,
				      IB_MR_TYPE_MEM_REG,
				      clt_path->max_pages_per_mr);
		if (IS_ERR(req->mr)) {
			err = PTR_ERR(req->mr);
			pr_err("Failed to alloc clt_path->max_pages_per_mr %d: %pe\n",
			       clt_path->max_pages_per_mr, req->mr);
			req->mr = NULL;
			goto out;
		}

		init_completion(&req->inv_comp);
	}

	return 0;

out:
	free_path_reqs(clt_path);

	return err;
}

static int alloc_permits(struct rtrs_clt_sess *clt)
{
	unsigned int chunk_bits;
	int err, i;

	clt->permits_map = bitmap_zalloc(clt->queue_depth, GFP_KERNEL);
	if (!clt->permits_map) {
		err = -ENOMEM;
		goto out_err;
	}
	clt->permits = kcalloc(clt->queue_depth, permit_size(clt), GFP_KERNEL);
	if (!clt->permits) {
		err = -ENOMEM;
		goto err_map;
	}
	chunk_bits = ilog2(clt->queue_depth - 1) + 1;
	for (i = 0; i < clt->queue_depth; i++) {
		struct rtrs_permit *permit;

		permit = get_permit(clt, i);
		permit->mem_id = i;
		permit->mem_off = i << (MAX_IMM_PAYL_BITS - chunk_bits);
	}

	return 0;

err_map:
	bitmap_free(clt->permits_map);
	clt->permits_map = NULL;
out_err:
	return err;
}

static void free_permits(struct rtrs_clt_sess *clt)
{
	if (clt->permits_map)
		wait_event(clt->permits_wait,
			   bitmap_empty(clt->permits_map, clt->queue_depth));

	bitmap_free(clt->permits_map);
	clt->permits_map = NULL;
	kfree(clt->permits);
	clt->permits = NULL;
}

static void query_fast_reg_mode(struct rtrs_clt_path *clt_path)
{
	struct ib_device *ib_dev;
	u64 max_pages_per_mr;
	int mr_page_shift;

	ib_dev = clt_path->s.dev->ib_dev;

	/*
	 * Use the smallest page size supported by the HCA, down to a
	 * minimum of 4096 bytes. We're unlikely to build large sglists
	 * out of smaller entries.
	 */
	mr_page_shift      = max(12, ffs(ib_dev->attrs.page_size_cap) - 1);
	max_pages_per_mr   = ib_dev->attrs.max_mr_size;
	do_div(max_pages_per_mr, (1ull << mr_page_shift));
	clt_path->max_pages_per_mr =
		min3(clt_path->max_pages_per_mr, (u32)max_pages_per_mr,
		     ib_dev->attrs.max_fast_reg_page_list_len);
	clt_path->clt->max_segments =
		min(clt_path->max_pages_per_mr, clt_path->clt->max_segments);
}

static bool rtrs_clt_change_state_get_old(struct rtrs_clt_path *clt_path,
					   enum rtrs_clt_state new_state,
					   enum rtrs_clt_state *old_state)
{
	bool changed;

	spin_lock_irq(&clt_path->state_wq.lock);
	if (old_state)
		*old_state = clt_path->state;
	changed = rtrs_clt_change_state(clt_path, new_state);
	spin_unlock_irq(&clt_path->state_wq.lock);

	return changed;
}

static void rtrs_clt_hb_err_handler(struct rtrs_con *c)
{
	struct rtrs_clt_con *con = container_of(c, typeof(*con), c);
	struct rtrs_clt_path *clt_path = to_clt_path(con->c.path);

	rtrs_err(con->c.path, "HB err handler for path=%s\n", kobject_name(&clt_path->kobj));
	rtrs_rdma_error_recovery(con);
}

static void rtrs_clt_init_hb(struct rtrs_clt_path *clt_path)
{
	rtrs_init_hb(&clt_path->s, &io_comp_cqe,
		      RTRS_HB_INTERVAL_MS,
		      RTRS_HB_MISSED_MAX,
		      rtrs_clt_hb_err_handler,
		      rtrs_wq);
}

static void rtrs_clt_reconnect_work(struct work_struct *work);
static void rtrs_clt_close_work(struct work_struct *work);

static void rtrs_clt_err_recovery_work(struct work_struct *work)
{
	struct rtrs_clt_path *clt_path;
	struct rtrs_clt_sess *clt;
	int delay_ms;

	clt_path = container_of(work, struct rtrs_clt_path, err_recovery_work);
	clt = clt_path->clt;
	delay_ms = clt->reconnect_delay_sec * 1000;
	rtrs_clt_stop_and_destroy_conns(clt_path);
	queue_delayed_work(rtrs_wq, &clt_path->reconnect_dwork,
			   msecs_to_jiffies(delay_ms +
					    get_random_u32_below(RTRS_RECONNECT_SEED)));
}

static struct rtrs_clt_path *alloc_path(struct rtrs_clt_sess *clt,
					const struct rtrs_addr *path,
					size_t con_num, u32 nr_poll_queues)
{
	struct rtrs_clt_path *clt_path;
	int err = -ENOMEM;
	int cpu;
	size_t total_con;

	clt_path = kzalloc(sizeof(*clt_path), GFP_KERNEL);
	if (!clt_path)
		goto err;

	/*
	 * irqmode and poll
	 * +1: Extra connection for user messages
	 */
	total_con = con_num + nr_poll_queues + 1;
	clt_path->s.con = kcalloc(total_con, sizeof(*clt_path->s.con),
				  GFP_KERNEL);
	if (!clt_path->s.con)
		goto err_free_path;

	clt_path->s.con_num = total_con;
	clt_path->s.irq_con_num = con_num + 1;

	clt_path->stats = kzalloc(sizeof(*clt_path->stats), GFP_KERNEL);
	if (!clt_path->stats)
		goto err_free_con;

	mutex_init(&clt_path->init_mutex);
	uuid_gen(&clt_path->s.uuid);
	memcpy(&clt_path->s.dst_addr, path->dst,
	       rdma_addr_size((struct sockaddr *)path->dst));

	/*
	 * rdma_resolve_addr() passes src_addr to cma_bind_addr, which
	 * checks the sa_family to be non-zero. If user passed src_addr=NULL
	 * the sess->src_addr will contain only zeros, which is then fine.
	 */
	if (path->src)
		memcpy(&clt_path->s.src_addr, path->src,
		       rdma_addr_size((struct sockaddr *)path->src));
	strscpy(clt_path->s.sessname, clt->sessname,
		sizeof(clt_path->s.sessname));
	clt_path->clt = clt;
	clt_path->max_pages_per_mr = RTRS_MAX_SEGMENTS;
	init_waitqueue_head(&clt_path->state_wq);
	clt_path->state = RTRS_CLT_CONNECTING;
	atomic_set(&clt_path->connected_cnt, 0);
	INIT_WORK(&clt_path->close_work, rtrs_clt_close_work);
	INIT_WORK(&clt_path->err_recovery_work, rtrs_clt_err_recovery_work);
	INIT_DELAYED_WORK(&clt_path->reconnect_dwork, rtrs_clt_reconnect_work);
	rtrs_clt_init_hb(clt_path);

	clt_path->mp_skip_entry = alloc_percpu(typeof(*clt_path->mp_skip_entry));
	if (!clt_path->mp_skip_entry)
		goto err_free_stats;

	for_each_possible_cpu(cpu)
		INIT_LIST_HEAD(per_cpu_ptr(clt_path->mp_skip_entry, cpu));

	err = rtrs_clt_init_stats(clt_path->stats);
	if (err)
		goto err_free_percpu;

	return clt_path;

err_free_percpu:
	free_percpu(clt_path->mp_skip_entry);
err_free_stats:
	kfree(clt_path->stats);
err_free_con:
	kfree(clt_path->s.con);
err_free_path:
	kfree(clt_path);
err:
	return ERR_PTR(err);
}

void free_path(struct rtrs_clt_path *clt_path)
{
	free_percpu(clt_path->mp_skip_entry);
	mutex_destroy(&clt_path->init_mutex);
	kfree(clt_path->s.con);
	kfree(clt_path->rbufs);
	kfree(clt_path);
}

static int create_con(struct rtrs_clt_path *clt_path, unsigned int cid)
{
	struct rtrs_clt_con *con;

	con = kzalloc(sizeof(*con), GFP_KERNEL);
	if (!con)
		return -ENOMEM;

	/* Map first two connections to the first CPU */
	con->cpu  = (cid ? cid - 1 : 0) % nr_cpu_ids;
	con->c.cid = cid;
	con->c.path = &clt_path->s;
	/* Align with srv, init as 1 */
	atomic_set(&con->c.wr_cnt, 1);
	mutex_init(&con->con_mutex);

	clt_path->s.con[cid] = &con->c;

	return 0;
}

static void destroy_con(struct rtrs_clt_con *con)
{
	struct rtrs_clt_path *clt_path = to_clt_path(con->c.path);

	clt_path->s.con[con->c.cid] = NULL;
	mutex_destroy(&con->con_mutex);
	kfree(con);
}

static int create_con_cq_qp(struct rtrs_clt_con *con)
{
	struct rtrs_clt_path *clt_path = to_clt_path(con->c.path);
	u32 max_send_wr, max_recv_wr, cq_num, max_send_sge, wr_limit;
	int err, cq_vector;
	struct rtrs_msg_rkey_rsp *rsp;

	lockdep_assert_held(&con->con_mutex);
	if (con->c.cid == 0) {
		max_send_sge = 1;
		/* We must be the first here */
		if (WARN_ON(clt_path->s.dev))
			return -EINVAL;

		/*
		 * The whole session uses device from user connection.
		 * Be careful not to close user connection before ib dev
		 * is gracefully put.
		 */
		clt_path->s.dev = rtrs_ib_dev_find_or_add(con->c.cm_id->device,
						       &dev_pd);
		if (!clt_path->s.dev) {
			rtrs_wrn(clt_path->clt,
				  "rtrs_ib_dev_find_get_or_add(): no memory\n");
			return -ENOMEM;
		}
		clt_path->s.dev_ref = 1;
		query_fast_reg_mode(clt_path);
		wr_limit = clt_path->s.dev->ib_dev->attrs.max_qp_wr;
		/*
		 * Two (request + registration) completion for send
		 * Two for recv if always_invalidate is set on server
		 * or one for recv.
		 * + 2 for drain and heartbeat
		 * in case qp gets into error state.
		 */
		max_send_wr =
			min_t(int, wr_limit, SERVICE_CON_QUEUE_DEPTH * 2 + 2);
		max_recv_wr = max_send_wr;
	} else {
		/*
		 * Here we assume that session members are correctly set.
		 * This is always true if user connection (cid == 0) is
		 * established first.
		 */
		if (WARN_ON(!clt_path->s.dev))
			return -EINVAL;
		if (WARN_ON(!clt_path->queue_depth))
			return -EINVAL;

		wr_limit = clt_path->s.dev->ib_dev->attrs.max_qp_wr;
		/* Shared between connections */
		clt_path->s.dev_ref++;
		max_send_wr = min_t(int, wr_limit,
			      /* QD * (REQ + RSP + FR REGS or INVS) + drain */
			      clt_path->queue_depth * 4 + 1);
		max_recv_wr = min_t(int, wr_limit,
			      clt_path->queue_depth * 3 + 1);
		max_send_sge = 2;
	}
	atomic_set(&con->c.sq_wr_avail, max_send_wr);
	cq_num = max_send_wr + max_recv_wr;
	/* alloc iu to recv new rkey reply when server reports flags set */
	if (clt_path->flags & RTRS_MSG_NEW_RKEY_F || con->c.cid == 0) {
		con->rsp_ius = rtrs_iu_alloc(cq_num, sizeof(*rsp),
					      GFP_KERNEL,
					      clt_path->s.dev->ib_dev,
					      DMA_FROM_DEVICE,
					      rtrs_clt_rdma_done);
		if (!con->rsp_ius)
			return -ENOMEM;
		con->queue_num = cq_num;
	}
	cq_vector = con->cpu % clt_path->s.dev->ib_dev->num_comp_vectors;
	if (con->c.cid >= clt_path->s.irq_con_num)
		err = rtrs_cq_qp_create(&clt_path->s, &con->c, max_send_sge,
					cq_vector, cq_num, max_send_wr,
					max_recv_wr, IB_POLL_DIRECT);
	else
		err = rtrs_cq_qp_create(&clt_path->s, &con->c, max_send_sge,
					cq_vector, cq_num, max_send_wr,
					max_recv_wr, IB_POLL_SOFTIRQ);
	/*
	 * In case of error we do not bother to clean previous allocations,
	 * since destroy_con_cq_qp() must be called.
	 */
	return err;
}

static void destroy_con_cq_qp(struct rtrs_clt_con *con)
{
	struct rtrs_clt_path *clt_path = to_clt_path(con->c.path);

	/*
	 * Be careful here: destroy_con_cq_qp() can be called even
	 * create_con_cq_qp() failed, see comments there.
	 */
	lockdep_assert_held(&con->con_mutex);
	rtrs_cq_qp_destroy(&con->c);
	if (con->rsp_ius) {
		rtrs_iu_free(con->rsp_ius, clt_path->s.dev->ib_dev,
			     con->queue_num);
		con->rsp_ius = NULL;
		con->queue_num = 0;
	}
	if (clt_path->s.dev_ref && !--clt_path->s.dev_ref) {
		rtrs_ib_dev_put(clt_path->s.dev);
		clt_path->s.dev = NULL;
	}
}

static void stop_cm(struct rtrs_clt_con *con)
{
	rdma_disconnect(con->c.cm_id);
	if (con->c.qp)
		ib_drain_qp(con->c.qp);
}

static void destroy_cm(struct rtrs_clt_con *con)
{
	rdma_destroy_id(con->c.cm_id);
	con->c.cm_id = NULL;
}

static int rtrs_rdma_addr_resolved(struct rtrs_clt_con *con)
{
	struct rtrs_path *s = con->c.path;
	int err;

	mutex_lock(&con->con_mutex);
	err = create_con_cq_qp(con);
	mutex_unlock(&con->con_mutex);
	if (err) {
		rtrs_err(s, "create_con_cq_qp(), err: %d\n", err);
		return err;
	}
	err = rdma_resolve_route(con->c.cm_id, RTRS_CONNECT_TIMEOUT_MS);
	if (err)
		rtrs_err(s, "Resolving route failed, err: %d\n", err);

	return err;
}

static int rtrs_rdma_route_resolved(struct rtrs_clt_con *con)
{
	struct rtrs_clt_path *clt_path = to_clt_path(con->c.path);
	struct rtrs_clt_sess *clt = clt_path->clt;
	struct rtrs_msg_conn_req msg;
	struct rdma_conn_param param;

	int err;

	param = (struct rdma_conn_param) {
		.retry_count = 7,
		.rnr_retry_count = 7,
		.private_data = &msg,
		.private_data_len = sizeof(msg),
	};

	msg = (struct rtrs_msg_conn_req) {
		.magic = cpu_to_le16(RTRS_MAGIC),
		.version = cpu_to_le16(RTRS_PROTO_VER),
		.cid = cpu_to_le16(con->c.cid),
		.cid_num = cpu_to_le16(clt_path->s.con_num),
		.recon_cnt = cpu_to_le16(clt_path->s.recon_cnt),
	};
	msg.first_conn = clt_path->for_new_clt ? FIRST_CONN : 0;
	uuid_copy(&msg.sess_uuid, &clt_path->s.uuid);
	uuid_copy(&msg.paths_uuid, &clt->paths_uuid);

	err = rdma_connect_locked(con->c.cm_id, &param);
	if (err)
		rtrs_err(clt, "rdma_connect_locked(): %d\n", err);

	return err;
}

static int rtrs_rdma_conn_established(struct rtrs_clt_con *con,
				       struct rdma_cm_event *ev)
{
	struct rtrs_clt_path *clt_path = to_clt_path(con->c.path);
	struct rtrs_clt_sess *clt = clt_path->clt;
	const struct rtrs_msg_conn_rsp *msg;
	u16 version, queue_depth;
	int errno;
	u8 len;

	msg = ev->param.conn.private_data;
	len = ev->param.conn.private_data_len;
	if (len < sizeof(*msg)) {
		rtrs_err(clt, "Invalid RTRS connection response\n");
		return -ECONNRESET;
	}
	if (le16_to_cpu(msg->magic) != RTRS_MAGIC) {
		rtrs_err(clt, "Invalid RTRS magic\n");
		return -ECONNRESET;
	}
	version = le16_to_cpu(msg->version);
	if (version >> 8 != RTRS_PROTO_VER_MAJOR) {
		rtrs_err(clt, "Unsupported major RTRS version: %d, expected %d\n",
			  version >> 8, RTRS_PROTO_VER_MAJOR);
		return -ECONNRESET;
	}
	errno = le16_to_cpu(msg->errno);
	if (errno) {
		rtrs_err(clt, "Invalid RTRS message: errno %d\n",
			  errno);
		return -ECONNRESET;
	}
	if (con->c.cid == 0) {
		queue_depth = le16_to_cpu(msg->queue_depth);

		if (clt_path->queue_depth > 0 && queue_depth != clt_path->queue_depth) {
			rtrs_err(clt, "Error: queue depth changed\n");

			/*
			 * Stop any more reconnection attempts
			 */
			clt_path->reconnect_attempts = -1;
			rtrs_err(clt,
				"Disabling auto-reconnect. Trigger a manual reconnect after issue is resolved\n");
			return -ECONNRESET;
		}

		if (!clt_path->rbufs) {
			clt_path->rbufs = kcalloc(queue_depth,
						  sizeof(*clt_path->rbufs),
						  GFP_KERNEL);
			if (!clt_path->rbufs)
				return -ENOMEM;
		}
		clt_path->queue_depth = queue_depth;
		clt_path->s.signal_interval = min_not_zero(queue_depth,
						(unsigned short) SERVICE_CON_QUEUE_DEPTH);
		clt_path->max_hdr_size = le32_to_cpu(msg->max_hdr_size);
		clt_path->max_io_size = le32_to_cpu(msg->max_io_size);
		clt_path->flags = le32_to_cpu(msg->flags);
		clt_path->chunk_size = clt_path->max_io_size + clt_path->max_hdr_size;

		/*
		 * Global IO size is always a minimum.
		 * If while a reconnection server sends us a value a bit
		 * higher - client does not care and uses cached minimum.
		 *
		 * Since we can have several sessions (paths) restablishing
		 * connections in parallel, use lock.
		 */
		mutex_lock(&clt->paths_mutex);
		clt->queue_depth = clt_path->queue_depth;
		clt->max_io_size = min_not_zero(clt_path->max_io_size,
						clt->max_io_size);
		mutex_unlock(&clt->paths_mutex);

		/*
		 * Cache the hca_port and hca_name for sysfs
		 */
		clt_path->hca_port = con->c.cm_id->port_num;
		scnprintf(clt_path->hca_name, sizeof(clt_path->hca_name),
			  clt_path->s.dev->ib_dev->name);
		clt_path->s.src_addr = con->c.cm_id->route.addr.src_addr;
		/* set for_new_clt, to allow future reconnect on any path */
		clt_path->for_new_clt = 1;
	}

	return 0;
}

static inline void flag_success_on_conn(struct rtrs_clt_con *con)
{
	struct rtrs_clt_path *clt_path = to_clt_path(con->c.path);

	atomic_inc(&clt_path->connected_cnt);
	con->cm_err = 1;
}

static int rtrs_rdma_conn_rejected(struct rtrs_clt_con *con,
				    struct rdma_cm_event *ev)
{
	struct rtrs_path *s = con->c.path;
	const struct rtrs_msg_conn_rsp *msg;
	const char *rej_msg;
	int status, errno;
	u8 data_len;

	status = ev->status;
	rej_msg = rdma_reject_msg(con->c.cm_id, status);
	msg = rdma_consumer_reject_data(con->c.cm_id, ev, &data_len);

	if (msg && data_len >= sizeof(*msg)) {
		errno = (int16_t)le16_to_cpu(msg->errno);
		if (errno == -EBUSY)
			rtrs_err(s,
				  "Previous session is still exists on the server, please reconnect later\n");
		else
			rtrs_err(s,
				  "Connect rejected: status %d (%s), rtrs errno %d\n",
				  status, rej_msg, errno);
	} else {
		rtrs_err(s,
			  "Connect rejected but with malformed message: status %d (%s)\n",
			  status, rej_msg);
	}

	return -ECONNRESET;
}

void rtrs_clt_close_conns(struct rtrs_clt_path *clt_path, bool wait)
{
	trace_rtrs_clt_close_conns(clt_path);

	if (rtrs_clt_change_state_get_old(clt_path, RTRS_CLT_CLOSING, NULL))
		queue_work(rtrs_wq, &clt_path->close_work);
	if (wait)
		flush_work(&clt_path->close_work);
}

static inline void flag_error_on_conn(struct rtrs_clt_con *con, int cm_err)
{
	if (con->cm_err == 1) {
		struct rtrs_clt_path *clt_path;

		clt_path = to_clt_path(con->c.path);
		if (atomic_dec_and_test(&clt_path->connected_cnt))

			wake_up(&clt_path->state_wq);
	}
	con->cm_err = cm_err;
}

static int rtrs_clt_rdma_cm_handler(struct rdma_cm_id *cm_id,
				     struct rdma_cm_event *ev)
{
	struct rtrs_clt_con *con = cm_id->context;
	struct rtrs_path *s = con->c.path;
	struct rtrs_clt_path *clt_path = to_clt_path(s);
	int cm_err = 0;

	switch (ev->event) {
	case RDMA_CM_EVENT_ADDR_RESOLVED:
		cm_err = rtrs_rdma_addr_resolved(con);
		break;
	case RDMA_CM_EVENT_ROUTE_RESOLVED:
		cm_err = rtrs_rdma_route_resolved(con);
		break;
	case RDMA_CM_EVENT_ESTABLISHED:
		cm_err = rtrs_rdma_conn_established(con, ev);
		if (!cm_err) {
			/*
			 * Report success and wake up. Here we abuse state_wq,
			 * i.e. wake up without state change, but we set cm_err.
			 */
			flag_success_on_conn(con);
			wake_up(&clt_path->state_wq);
			return 0;
		}
		break;
	case RDMA_CM_EVENT_REJECTED:
		cm_err = rtrs_rdma_conn_rejected(con, ev);
		break;
	case RDMA_CM_EVENT_DISCONNECTED:
		/* No message for disconnecting */
		cm_err = -ECONNRESET;
		break;
	case RDMA_CM_EVENT_CONNECT_ERROR:
	case RDMA_CM_EVENT_UNREACHABLE:
	case RDMA_CM_EVENT_ADDR_CHANGE:
	case RDMA_CM_EVENT_TIMEWAIT_EXIT:
		rtrs_wrn(s, "CM error (CM event: %s, err: %d)\n",
			 rdma_event_msg(ev->event), ev->status);
		cm_err = -ECONNRESET;
		break;
	case RDMA_CM_EVENT_ADDR_ERROR:
	case RDMA_CM_EVENT_ROUTE_ERROR:
		rtrs_wrn(s, "CM error (CM event: %s, err: %d)\n",
			 rdma_event_msg(ev->event), ev->status);
		cm_err = -EHOSTUNREACH;
		break;
	case RDMA_CM_EVENT_DEVICE_REMOVAL:
		/*
		 * Device removal is a special case.  Queue close and return 0.
		 */
		rtrs_wrn_rl(s, "CM event: %s, status: %d\n", rdma_event_msg(ev->event),
			    ev->status);
		rtrs_clt_close_conns(clt_path, false);
		return 0;
	default:
		rtrs_err(s, "Unexpected RDMA CM error (CM event: %s, err: %d)\n",
			 rdma_event_msg(ev->event), ev->status);
		cm_err = -ECONNRESET;
		break;
	}

	if (cm_err) {
		/*
		 * cm error makes sense only on connection establishing,
		 * in other cases we rely on normal procedure of reconnecting.
		 */
		flag_error_on_conn(con, cm_err);
		rtrs_rdma_error_recovery(con);
	}

	return 0;
}

/* The caller should do the cleanup in case of error */
static int create_cm(struct rtrs_clt_con *con)
{
	struct rtrs_path *s = con->c.path;
	struct rtrs_clt_path *clt_path = to_clt_path(s);
	struct rdma_cm_id *cm_id;
	int err;

	cm_id = rdma_create_id(&init_net, rtrs_clt_rdma_cm_handler, con,
			       clt_path->s.dst_addr.ss_family == AF_IB ?
			       RDMA_PS_IB : RDMA_PS_TCP, IB_QPT_RC);
	if (IS_ERR(cm_id)) {
		rtrs_err(s, "Failed to create CM ID, err: %pe\n", cm_id);
		return PTR_ERR(cm_id);
	}
	con->c.cm_id = cm_id;
	con->cm_err = 0;
	/* allow the port to be reused */
	err = rdma_set_reuseaddr(cm_id, 1);
	if (err != 0) {
		rtrs_err(s, "Set address reuse failed, err: %d\n", err);
		return err;
	}
	err = rdma_resolve_addr(cm_id, (struct sockaddr *)&clt_path->s.src_addr,
				(struct sockaddr *)&clt_path->s.dst_addr,
				RTRS_CONNECT_TIMEOUT_MS);
	if (err) {
		rtrs_err(s, "Failed to resolve address, err: %d\n", err);
		return err;
	}
	/*
	 * Combine connection status and session events. This is needed
	 * for waiting two possible cases: cm_err has something meaningful
	 * or session state was really changed to error by device removal.
	 */
	err = wait_event_interruptible_timeout(
			clt_path->state_wq,
			con->cm_err || clt_path->state != RTRS_CLT_CONNECTING,
			msecs_to_jiffies(RTRS_CONNECT_TIMEOUT_MS));
	if (err == 0 || err == -ERESTARTSYS) {
		if (err == 0)
			err = -ETIMEDOUT;
		/* Timedout or interrupted */
		return err;
	}
	if (con->cm_err < 0)
		return con->cm_err;
	if (READ_ONCE(clt_path->state) != RTRS_CLT_CONNECTING)
		/* Device removal */
		return -ECONNABORTED;

	return 0;
}

static void rtrs_clt_path_up(struct rtrs_clt_path *clt_path)
{
	struct rtrs_clt_sess *clt = clt_path->clt;
	int up;

	/*
	 * We can fire RECONNECTED event only when all paths were
	 * connected on rtrs_clt_open(), then each was disconnected
	 * and the first one connected again.  That's why this nasty
	 * game with counter value.
	 */

	mutex_lock(&clt->paths_ev_mutex);
	up = ++clt->paths_up;
	/*
	 * Here it is safe to access paths num directly since up counter
	 * is greater than MAX_PATHS_NUM only while rtrs_clt_open() is
	 * in progress, thus paths removals are impossible.
	 */
	if (up > MAX_PATHS_NUM && up == MAX_PATHS_NUM + clt->paths_num)
		clt->paths_up = clt->paths_num;
	else if (up == 1)
		clt->link_ev(clt->priv, RTRS_CLT_LINK_EV_RECONNECTED);
	mutex_unlock(&clt->paths_ev_mutex);

	/* Mark session as established */
	clt_path->established = true;
	clt_path->reconnect_attempts = 0;
	clt_path->stats->reconnects.successful_cnt++;
}

static void rtrs_clt_path_down(struct rtrs_clt_path *clt_path)
{
	struct rtrs_clt_sess *clt = clt_path->clt;

	if (!clt_path->established)
		return;

	clt_path->established = false;
	mutex_lock(&clt->paths_ev_mutex);
	WARN_ON(!clt->paths_up);
	if (--clt->paths_up == 0)
		clt->link_ev(clt->priv, RTRS_CLT_LINK_EV_DISCONNECTED);
	mutex_unlock(&clt->paths_ev_mutex);
}

static void rtrs_clt_stop_and_destroy_conns(struct rtrs_clt_path *clt_path)
{
	struct rtrs_clt_con *con;
	unsigned int cid;

	WARN_ON(READ_ONCE(clt_path->state) == RTRS_CLT_CONNECTED);

	/*
	 * Possible race with rtrs_clt_open(), when DEVICE_REMOVAL comes
	 * exactly in between.  Start destroying after it finishes.
	 */
	mutex_lock(&clt_path->init_mutex);
	mutex_unlock(&clt_path->init_mutex);

	/*
	 * All IO paths must observe !CONNECTED state before we
	 * free everything.
	 */
	synchronize_rcu();

	rtrs_stop_hb(&clt_path->s);

	/*
	 * The order it utterly crucial: firstly disconnect and complete all
	 * rdma requests with error (thus set in_use=false for requests),
	 * then fail outstanding requests checking in_use for each, and
	 * eventually notify upper layer about session disconnection.
	 */

	for (cid = 0; cid < clt_path->s.con_num; cid++) {
		if (!clt_path->s.con[cid])
			break;
		con = to_clt_con(clt_path->s.con[cid]);
		stop_cm(con);
	}
	fail_all_outstanding_reqs(clt_path);
	free_path_reqs(clt_path);
	rtrs_clt_path_down(clt_path);

	/*
	 * Wait for graceful shutdown, namely when peer side invokes
	 * rdma_disconnect(). 'connected_cnt' is decremented only on
	 * CM events, thus if other side had crashed and hb has detected
	 * something is wrong, here we will stuck for exactly timeout ms,
	 * since CM does not fire anything.  That is fine, we are not in
	 * hurry.
	 */
	wait_event_timeout(clt_path->state_wq,
			   !atomic_read(&clt_path->connected_cnt),
			   msecs_to_jiffies(RTRS_CONNECT_TIMEOUT_MS));

	for (cid = 0; cid < clt_path->s.con_num; cid++) {
		if (!clt_path->s.con[cid])
			break;
		con = to_clt_con(clt_path->s.con[cid]);
		mutex_lock(&con->con_mutex);
		destroy_con_cq_qp(con);
		mutex_unlock(&con->con_mutex);
		destroy_cm(con);
		destroy_con(con);
	}
}

static void rtrs_clt_remove_path_from_arr(struct rtrs_clt_path *clt_path)
{
	struct rtrs_clt_sess *clt = clt_path->clt;
	struct rtrs_clt_path *next;
	bool wait_for_grace = false;
	int cpu;

	mutex_lock(&clt->paths_mutex);
	list_del_rcu(&clt_path->s.entry);

	/* Make sure everybody observes path removal. */
	synchronize_rcu();

	/*
	 * At this point nobody sees @sess in the list, but still we have
	 * dangling pointer @pcpu_path which _can_ point to @sess.  Since
	 * nobody can observe @sess in the list, we guarantee that IO path
	 * will not assign @sess to @pcpu_path, i.e. @pcpu_path can be equal
	 * to @sess, but can never again become @sess.
	 */

	/*
	 * Decrement paths number only after grace period, because
	 * caller of do_each_path() must firstly observe list without
	 * path and only then decremented paths number.
	 *
	 * Otherwise there can be the following situation:
	 *    o Two paths exist and IO is coming.
	 *    o One path is removed:
	 *      CPU#0                          CPU#1
	 *      do_each_path():                rtrs_clt_remove_path_from_arr():
	 *          path = get_next_path()
	 *          ^^^                            list_del_rcu(path)
	 *          [!CONNECTED path]              clt->paths_num--
	 *                                              ^^^^^^^^^
	 *          load clt->paths_num                 from 2 to 1
	 *                    ^^^^^^^^^
	 *                    sees 1
	 *
	 *      path is observed as !CONNECTED, but do_each_path() loop
	 *      ends, because expression i < clt->paths_num is false.
	 */
	clt->paths_num--;

	/*
	 * Get @next connection from current @sess which is going to be
	 * removed.  If @sess is the last element, then @next is NULL.
	 */
	rcu_read_lock();
	next = rtrs_clt_get_next_path_or_null(&clt->paths_list, clt_path);
	rcu_read_unlock();

	/*
	 * @pcpu paths can still point to the path which is going to be
	 * removed, so change the pointer manually.
	 */
	for_each_possible_cpu(cpu) {
		struct rtrs_clt_path __rcu **ppcpu_path;

		ppcpu_path = per_cpu_ptr(clt->pcpu_path, cpu);
		if (rcu_dereference_protected(*ppcpu_path,
			lockdep_is_held(&clt->paths_mutex)) != clt_path)
			/*
			 * synchronize_rcu() was called just after deleting
			 * entry from the list, thus IO code path cannot
			 * change pointer back to the pointer which is going
			 * to be removed, we are safe here.
			 */
			continue;

		/*
		 * We race with IO code path, which also changes pointer,
		 * thus we have to be careful not to overwrite it.
		 */
		if (try_cmpxchg((struct rtrs_clt_path **)ppcpu_path, &clt_path,
				next))
			/*
			 * @ppcpu_path was successfully replaced with @next,
			 * that means that someone could also pick up the
			 * @sess and dereferencing it right now, so wait for
			 * a grace period is required.
			 */
			wait_for_grace = true;
	}
	if (wait_for_grace)
		synchronize_rcu();

	mutex_unlock(&clt->paths_mutex);
}

static void rtrs_clt_add_path_to_arr(struct rtrs_clt_path *clt_path)
{
	struct rtrs_clt_sess *clt = clt_path->clt;

	mutex_lock(&clt->paths_mutex);
	clt->paths_num++;

	list_add_tail_rcu(&clt_path->s.entry, &clt->paths_list);
	mutex_unlock(&clt->paths_mutex);
}

static void rtrs_clt_close_work(struct work_struct *work)
{
	struct rtrs_clt_path *clt_path;

	clt_path = container_of(work, struct rtrs_clt_path, close_work);

	cancel_work_sync(&clt_path->err_recovery_work);
	cancel_delayed_work_sync(&clt_path->reconnect_dwork);
	rtrs_clt_stop_and_destroy_conns(clt_path);
	rtrs_clt_change_state_get_old(clt_path, RTRS_CLT_CLOSED, NULL);
}

static int init_conns(struct rtrs_clt_path *clt_path)
{
	unsigned int cid;
	int err, i;

	/*
	 * On every new session connections increase reconnect counter
	 * to avoid clashes with previous sessions not yet closed
	 * sessions on a server side.
	 */
	clt_path->s.recon_cnt++;

	/* Establish all RDMA connections  */
	for (cid = 0; cid < clt_path->s.con_num; cid++) {
		err = create_con(clt_path, cid);
		if (err)
			goto destroy;

		err = create_cm(to_clt_con(clt_path->s.con[cid]));
		if (err)
			goto destroy;
	}

	/*
	 * Set the cid to con_num - 1, since if we fail later, we want to stay in bounds.
	 */
	cid = clt_path->s.con_num - 1;

	err = alloc_path_reqs(clt_path);
	if (err)
		goto destroy;

	return 0;

destroy:
	/* Make sure we do the cleanup in the order they are created */
	for (i = 0; i <= cid; i++) {
		struct rtrs_clt_con *con;

		if (!clt_path->s.con[i])
			break;

		con = to_clt_con(clt_path->s.con[i]);
		if (con->c.cm_id) {
			stop_cm(con);
			mutex_lock(&con->con_mutex);
			destroy_con_cq_qp(con);
			mutex_unlock(&con->con_mutex);
			destroy_cm(con);
		}
		destroy_con(con);
	}
	/*
	 * If we've never taken async path and got an error, say,
	 * doing rdma_resolve_addr(), switch to CONNECTION_ERR state
	 * manually to keep reconnecting.
	 */
	rtrs_clt_change_state_get_old(clt_path, RTRS_CLT_CONNECTING_ERR, NULL);

	return err;
}

static void rtrs_clt_info_req_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct rtrs_clt_con *con = to_clt_con(wc->qp->qp_context);
	struct rtrs_clt_path *clt_path = to_clt_path(con->c.path);
	struct rtrs_iu *iu;

	iu = container_of(wc->wr_cqe, struct rtrs_iu, cqe);
	rtrs_iu_free(iu, clt_path->s.dev->ib_dev, 1);

	if (wc->status != IB_WC_SUCCESS) {
		rtrs_err(clt_path->clt, "Path info request send failed: %s\n",
			  ib_wc_status_msg(wc->status));
		rtrs_clt_change_state_get_old(clt_path, RTRS_CLT_CONNECTING_ERR, NULL);
		return;
	}

	rtrs_clt_update_wc_stats(con);
}

static int process_info_rsp(struct rtrs_clt_path *clt_path,
			    const struct rtrs_msg_info_rsp *msg)
{
	unsigned int sg_cnt, total_len;
	int i, sgi;

	sg_cnt = le16_to_cpu(msg->sg_cnt);
	if (!sg_cnt || (clt_path->queue_depth % sg_cnt)) {
		rtrs_err(clt_path->clt,
			  "Incorrect sg_cnt %d, is not multiple\n",
			  sg_cnt);
		return -EINVAL;
	}

	/*
	 * Check if IB immediate data size is enough to hold the mem_id and
	 * the offset inside the memory chunk.
	 */
	if ((ilog2(sg_cnt - 1) + 1) + (ilog2(clt_path->chunk_size - 1) + 1) >
	    MAX_IMM_PAYL_BITS) {
		rtrs_err(clt_path->clt,
			  "RDMA immediate size (%db) not enough to encode %d buffers of size %dB\n",
			  MAX_IMM_PAYL_BITS, sg_cnt, clt_path->chunk_size);
		return -EINVAL;
	}
	total_len = 0;
	for (sgi = 0, i = 0; sgi < sg_cnt && i < clt_path->queue_depth; sgi++) {
		const struct rtrs_sg_desc *desc = &msg->desc[sgi];
		u32 len, rkey;
		u64 addr;

		addr = le64_to_cpu(desc->addr);
		rkey = le32_to_cpu(desc->key);
		len  = le32_to_cpu(desc->len);

		total_len += len;

		if (!len || (len % clt_path->chunk_size)) {
			rtrs_err(clt_path->clt, "Incorrect [%d].len %d\n",
				  sgi,
				  len);
			return -EINVAL;
		}
		for ( ; len && i < clt_path->queue_depth; i++) {
			clt_path->rbufs[i].addr = addr;
			clt_path->rbufs[i].rkey = rkey;

			len  -= clt_path->chunk_size;
			addr += clt_path->chunk_size;
		}
	}
	/* Sanity check */
	if (sgi != sg_cnt || i != clt_path->queue_depth) {
		rtrs_err(clt_path->clt,
			 "Incorrect sg vector, not fully mapped\n");
		return -EINVAL;
	}
	if (total_len != clt_path->chunk_size * clt_path->queue_depth) {
		rtrs_err(clt_path->clt, "Incorrect total_len %d\n", total_len);
		return -EINVAL;
	}

	return 0;
}

static void rtrs_clt_info_rsp_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct rtrs_clt_con *con = to_clt_con(wc->qp->qp_context);
	struct rtrs_clt_path *clt_path = to_clt_path(con->c.path);
	struct rtrs_msg_info_rsp *msg;
	enum rtrs_clt_state state;
	struct rtrs_iu *iu;
	size_t rx_sz;
	int err;

	state = RTRS_CLT_CONNECTING_ERR;

	WARN_ON(con->c.cid);
	iu = container_of(wc->wr_cqe, struct rtrs_iu, cqe);
	if (wc->status != IB_WC_SUCCESS) {
		rtrs_err(clt_path->clt, "Path info response recv failed: %s\n",
			  ib_wc_status_msg(wc->status));
		goto out;
	}
	WARN_ON(wc->opcode != IB_WC_RECV);

	if (wc->byte_len < sizeof(*msg)) {
		rtrs_err(clt_path->clt, "Path info response is malformed: size %d\n",
			  wc->byte_len);
		goto out;
	}
	ib_dma_sync_single_for_cpu(clt_path->s.dev->ib_dev, iu->dma_addr,
				   iu->size, DMA_FROM_DEVICE);
	msg = iu->buf;
	if (le16_to_cpu(msg->type) != RTRS_MSG_INFO_RSP) {
		rtrs_err(clt_path->clt, "Path info response is malformed: type %d\n",
			  le16_to_cpu(msg->type));
		goto out;
	}
	rx_sz  = sizeof(*msg);
	rx_sz += sizeof(msg->desc[0]) * le16_to_cpu(msg->sg_cnt);
	if (wc->byte_len < rx_sz) {
		rtrs_err(clt_path->clt, "Path info response is malformed: size %d\n",
			  wc->byte_len);
		goto out;
	}
	err = process_info_rsp(clt_path, msg);
	if (err)
		goto out;

	err = post_recv_path(clt_path);
	if (err)
		goto out;

	state = RTRS_CLT_CONNECTED;

out:
	rtrs_clt_update_wc_stats(con);
	rtrs_iu_free(iu, clt_path->s.dev->ib_dev, 1);
	rtrs_clt_change_state_get_old(clt_path, state, NULL);
}

static int rtrs_send_path_info(struct rtrs_clt_path *clt_path)
{
	struct rtrs_clt_con *usr_con = to_clt_con(clt_path->s.con[0]);
	struct rtrs_msg_info_req *msg;
	struct rtrs_iu *tx_iu, *rx_iu;
	size_t rx_sz;
	int err;

	rx_sz  = sizeof(struct rtrs_msg_info_rsp);
	rx_sz += sizeof(struct rtrs_sg_desc) * clt_path->queue_depth;

	tx_iu = rtrs_iu_alloc(1, sizeof(struct rtrs_msg_info_req), GFP_KERNEL,
			       clt_path->s.dev->ib_dev, DMA_TO_DEVICE,
			       rtrs_clt_info_req_done);
	rx_iu = rtrs_iu_alloc(1, rx_sz, GFP_KERNEL, clt_path->s.dev->ib_dev,
			       DMA_FROM_DEVICE, rtrs_clt_info_rsp_done);
	if (!tx_iu || !rx_iu) {
		err = -ENOMEM;
		goto out;
	}
	/* Prepare for getting info response */
	err = rtrs_iu_post_recv(&usr_con->c, rx_iu);
	if (err) {
		rtrs_err(clt_path->clt, "rtrs_iu_post_recv(), err: %d\n", err);
		goto out;
	}
	rx_iu = NULL;

	msg = tx_iu->buf;
	msg->type = cpu_to_le16(RTRS_MSG_INFO_REQ);
	memcpy(msg->pathname, clt_path->s.sessname, sizeof(msg->pathname));

	ib_dma_sync_single_for_device(clt_path->s.dev->ib_dev,
				      tx_iu->dma_addr,
				      tx_iu->size, DMA_TO_DEVICE);

	/* Send info request */
	err = rtrs_iu_post_send(&usr_con->c, tx_iu, sizeof(*msg), NULL);
	if (err) {
		rtrs_err(clt_path->clt, "rtrs_iu_post_send(), err: %d\n", err);
		goto out;
	}
	tx_iu = NULL;

	/* Wait for state change */
	wait_event_interruptible_timeout(clt_path->state_wq,
					 clt_path->state != RTRS_CLT_CONNECTING,
					 msecs_to_jiffies(
						 RTRS_CONNECT_TIMEOUT_MS));
	if (READ_ONCE(clt_path->state) != RTRS_CLT_CONNECTED) {
		if (READ_ONCE(clt_path->state) == RTRS_CLT_CONNECTING_ERR)
			err = -ECONNRESET;
		else
			err = -ETIMEDOUT;
	}

out:
	if (tx_iu)
		rtrs_iu_free(tx_iu, clt_path->s.dev->ib_dev, 1);
	if (rx_iu)
		rtrs_iu_free(rx_iu, clt_path->s.dev->ib_dev, 1);
	if (err)
		/* If we've never taken async path because of malloc problems */
		rtrs_clt_change_state_get_old(clt_path,
					      RTRS_CLT_CONNECTING_ERR, NULL);

	return err;
}

/**
 * init_path() - establishes all path connections and does handshake
 * @clt_path: client path.
 * In case of error full close or reconnect procedure should be taken,
 * because reconnect or close async works can be started.
 */
static int init_path(struct rtrs_clt_path *clt_path)
{
	int err;
	char str[NAME_MAX];
	struct rtrs_addr path = {
		.src = &clt_path->s.src_addr,
		.dst = &clt_path->s.dst_addr,
	};

	rtrs_addr_to_str(&path, str, sizeof(str));

	mutex_lock(&clt_path->init_mutex);
	err = init_conns(clt_path);
	if (err) {
		rtrs_err(clt_path->clt,
			 "init_conns() failed: err=%d path=%s [%s:%u]\n", err,
			 str, clt_path->hca_name, clt_path->hca_port);
		goto out;
	}
	err = rtrs_send_path_info(clt_path);
	if (err) {
		rtrs_err(clt_path->clt,
			 "rtrs_send_path_info() failed: err=%d path=%s [%s:%u]\n",
			 err, str, clt_path->hca_name, clt_path->hca_port);
		goto out;
	}
	rtrs_clt_path_up(clt_path);
	rtrs_start_hb(&clt_path->s);
out:
	mutex_unlock(&clt_path->init_mutex);

	return err;
}

static void rtrs_clt_reconnect_work(struct work_struct *work)
{
	struct rtrs_clt_path *clt_path;
	struct rtrs_clt_sess *clt;
	int err;

	clt_path = container_of(to_delayed_work(work), struct rtrs_clt_path,
				reconnect_dwork);
	clt = clt_path->clt;

	trace_rtrs_clt_reconnect_work(clt_path);

	if (READ_ONCE(clt_path->state) != RTRS_CLT_RECONNECTING)
		return;

	if (clt_path->reconnect_attempts >= clt->max_reconnect_attempts) {
		/* Close a path completely if max attempts is reached */
		rtrs_clt_close_conns(clt_path, false);
		return;
	}
	clt_path->reconnect_attempts++;

	msleep(RTRS_RECONNECT_BACKOFF);
	if (rtrs_clt_change_state_get_old(clt_path, RTRS_CLT_CONNECTING, NULL)) {
		err = init_path(clt_path);
		if (err)
			goto reconnect_again;
	}

	return;

reconnect_again:
	if (rtrs_clt_change_state_get_old(clt_path, RTRS_CLT_RECONNECTING, NULL)) {
		clt_path->stats->reconnects.fail_cnt++;
		queue_work(rtrs_wq, &clt_path->err_recovery_work);
	}
}

static void rtrs_clt_dev_release(struct device *dev)
{
	struct rtrs_clt_sess *clt = container_of(dev, struct rtrs_clt_sess,
						 dev);

	mutex_destroy(&clt->paths_ev_mutex);
	mutex_destroy(&clt->paths_mutex);
	kfree(clt);
}

static struct rtrs_clt_sess *alloc_clt(const char *sessname, size_t paths_num,
				  u16 port, size_t pdu_sz, void *priv,
				  void	(*link_ev)(void *priv,
						   enum rtrs_clt_link_ev ev),
				  unsigned int reconnect_delay_sec,
				  unsigned int max_reconnect_attempts)
{
	struct rtrs_clt_sess *clt;
	int err;

	if (!paths_num || paths_num > MAX_PATHS_NUM)
		return ERR_PTR(-EINVAL);

	if (strlen(sessname) >= sizeof(clt->sessname))
		return ERR_PTR(-EINVAL);

	clt = kzalloc(sizeof(*clt), GFP_KERNEL);
	if (!clt)
		return ERR_PTR(-ENOMEM);

	clt->pcpu_path = alloc_percpu(typeof(*clt->pcpu_path));
	if (!clt->pcpu_path) {
		kfree(clt);
		return ERR_PTR(-ENOMEM);
	}

	clt->dev.class = &rtrs_clt_dev_class;
	clt->dev.release = rtrs_clt_dev_release;
	uuid_gen(&clt->paths_uuid);
	INIT_LIST_HEAD_RCU(&clt->paths_list);
	clt->paths_num = paths_num;
	clt->paths_up = MAX_PATHS_NUM;
	clt->port = port;
	clt->pdu_sz = pdu_sz;
	clt->max_segments = RTRS_MAX_SEGMENTS;
	clt->reconnect_delay_sec = reconnect_delay_sec;
	clt->max_reconnect_attempts = max_reconnect_attempts;
	clt->priv = priv;
	clt->link_ev = link_ev;
	clt->mp_policy = MP_POLICY_MIN_INFLIGHT;
	strscpy(clt->sessname, sessname, sizeof(clt->sessname));
	init_waitqueue_head(&clt->permits_wait);
	mutex_init(&clt->paths_ev_mutex);
	mutex_init(&clt->paths_mutex);
	device_initialize(&clt->dev);

	err = dev_set_name(&clt->dev, "%s", sessname);
	if (err)
		goto err_put;

	/*
	 * Suppress user space notification until
	 * sysfs files are created
	 */
	dev_set_uevent_suppress(&clt->dev, true);
	err = device_add(&clt->dev);
	if (err)
		goto err_put;

	clt->kobj_paths = kobject_create_and_add("paths", &clt->dev.kobj);
	if (!clt->kobj_paths) {
		err = -ENOMEM;
		goto err_del;
	}
	err = rtrs_clt_create_sysfs_root_files(clt);
	if (err) {
		kobject_del(clt->kobj_paths);
		kobject_put(clt->kobj_paths);
		goto err_del;
	}
	dev_set_uevent_suppress(&clt->dev, false);
	kobject_uevent(&clt->dev.kobj, KOBJ_ADD);

	return clt;
err_del:
	device_del(&clt->dev);
err_put:
	free_percpu(clt->pcpu_path);
	put_device(&clt->dev);
	return ERR_PTR(err);
}

static void free_clt(struct rtrs_clt_sess *clt)
{
	free_percpu(clt->pcpu_path);

	/*
	 * release callback will free clt and destroy mutexes in last put
	 */
	device_unregister(&clt->dev);
}

/**
 * rtrs_clt_open() - Open a path to an RTRS server
 * @ops: holds the link event callback and the private pointer.
 * @pathname: name of the path to an RTRS server
 * @paths: Paths to be established defined by their src and dst addresses
 * @paths_num: Number of elements in the @paths array
 * @port: port to be used by the RTRS session
 * @pdu_sz: Size of extra payload which can be accessed after permit allocation.
 * @reconnect_delay_sec: time between reconnect tries
 * @max_reconnect_attempts: Number of times to reconnect on error before giving
 *			    up, 0 for * disabled, -1 for forever
 * @nr_poll_queues: number of polling mode connection using IB_POLL_DIRECT flag
 *
 * Starts session establishment with the rtrs_server. The function can block
 * up to ~2000ms before it returns.
 *
 * Return a valid pointer on success otherwise PTR_ERR.
 */
struct rtrs_clt_sess *rtrs_clt_open(struct rtrs_clt_ops *ops,
				 const char *pathname,
				 const struct rtrs_addr *paths,
				 size_t paths_num, u16 port,
				 size_t pdu_sz, u8 reconnect_delay_sec,
				 s16 max_reconnect_attempts, u32 nr_poll_queues)
{
	struct rtrs_clt_path *clt_path, *tmp;
	struct rtrs_clt_sess *clt;
	int err, i;

	if (strchr(pathname, '/') || strchr(pathname, '.')) {
		pr_err("pathname cannot contain / and .\n");
		err = -EINVAL;
		goto out;
	}

	clt = alloc_clt(pathname, paths_num, port, pdu_sz, ops->priv,
			ops->link_ev,
			reconnect_delay_sec,
			max_reconnect_attempts);
	if (IS_ERR(clt)) {
		err = PTR_ERR(clt);
		goto out;
	}
	for (i = 0; i < paths_num; i++) {
		struct rtrs_clt_path *clt_path;

		clt_path = alloc_path(clt, &paths[i], nr_cpu_ids,
				  nr_poll_queues);
		if (IS_ERR(clt_path)) {
			err = PTR_ERR(clt_path);
			goto close_all_path;
		}
		if (!i)
			clt_path->for_new_clt = 1;
		list_add_tail_rcu(&clt_path->s.entry, &clt->paths_list);

		err = init_path(clt_path);
		if (err) {
			list_del_rcu(&clt_path->s.entry);
			rtrs_clt_close_conns(clt_path, true);
			free_percpu(clt_path->stats->pcpu_stats);
			kfree(clt_path->stats);
			free_path(clt_path);
			goto close_all_path;
		}

		err = rtrs_clt_create_path_files(clt_path);
		if (err) {
			list_del_rcu(&clt_path->s.entry);
			rtrs_clt_close_conns(clt_path, true);
			free_percpu(clt_path->stats->pcpu_stats);
			kfree(clt_path->stats);
			free_path(clt_path);
			goto close_all_path;
		}
	}
	err = alloc_permits(clt);
	if (err)
		goto close_all_path;

	return clt;

close_all_path:
	list_for_each_entry_safe(clt_path, tmp, &clt->paths_list, s.entry) {
		rtrs_clt_destroy_path_files(clt_path, NULL);
		rtrs_clt_close_conns(clt_path, true);
		kobject_put(&clt_path->kobj);
	}
	rtrs_clt_destroy_sysfs_root(clt);
	free_clt(clt);

out:
	return ERR_PTR(err);
}
EXPORT_SYMBOL(rtrs_clt_open);

/**
 * rtrs_clt_close() - Close a path
 * @clt: Session handle. Session is freed upon return.
 */
void rtrs_clt_close(struct rtrs_clt_sess *clt)
{
	struct rtrs_clt_path *clt_path, *tmp;

	/* Firstly forbid sysfs access */
	rtrs_clt_destroy_sysfs_root(clt);

	/* Now it is safe to iterate over all paths without locks */
	list_for_each_entry_safe(clt_path, tmp, &clt->paths_list, s.entry) {
		rtrs_clt_close_conns(clt_path, true);
		rtrs_clt_destroy_path_files(clt_path, NULL);
		kobject_put(&clt_path->kobj);
	}
	free_permits(clt);
	free_clt(clt);
}
EXPORT_SYMBOL(rtrs_clt_close);

int rtrs_clt_reconnect_from_sysfs(struct rtrs_clt_path *clt_path)
{
	enum rtrs_clt_state old_state;
	int err = -EBUSY;
	bool changed;

	changed = rtrs_clt_change_state_get_old(clt_path,
						 RTRS_CLT_RECONNECTING,
						 &old_state);
	if (changed) {
		clt_path->reconnect_attempts = 0;
		rtrs_clt_stop_and_destroy_conns(clt_path);
		queue_delayed_work(rtrs_wq, &clt_path->reconnect_dwork, 0);
	}
	if (changed || old_state == RTRS_CLT_RECONNECTING) {
		/*
		 * flush_delayed_work() queues pending work for immediate
		 * execution, so do the flush if we have queued something
		 * right now or work is pending.
		 */
		flush_delayed_work(&clt_path->reconnect_dwork);
		err = (READ_ONCE(clt_path->state) ==
		       RTRS_CLT_CONNECTED ? 0 : -ENOTCONN);
	}

	return err;
}

int rtrs_clt_remove_path_from_sysfs(struct rtrs_clt_path *clt_path,
				     const struct attribute *sysfs_self)
{
	enum rtrs_clt_state old_state;
	bool changed;

	/*
	 * Continue stopping path till state was changed to DEAD or
	 * state was observed as DEAD:
	 * 1. State was changed to DEAD - we were fast and nobody
	 *    invoked rtrs_clt_reconnect(), which can again start
	 *    reconnecting.
	 * 2. State was observed as DEAD - we have someone in parallel
	 *    removing the path.
	 */
	do {
		rtrs_clt_close_conns(clt_path, true);
		changed = rtrs_clt_change_state_get_old(clt_path,
							RTRS_CLT_DEAD,
							&old_state);
	} while (!changed && old_state != RTRS_CLT_DEAD);

	if (changed) {
		rtrs_clt_remove_path_from_arr(clt_path);
		rtrs_clt_destroy_path_files(clt_path, sysfs_self);
		kobject_put(&clt_path->kobj);
	}

	return 0;
}

void rtrs_clt_set_max_reconnect_attempts(struct rtrs_clt_sess *clt, int value)
{
	clt->max_reconnect_attempts = (unsigned int)value;
}

int rtrs_clt_get_max_reconnect_attempts(const struct rtrs_clt_sess *clt)
{
	return (int)clt->max_reconnect_attempts;
}

/**
 * rtrs_clt_request() - Request data transfer to/from server via RDMA.
 *
 * @dir:	READ/WRITE
 * @ops:	callback function to be called as confirmation, and the pointer.
 * @clt:	Session
 * @permit:	Preallocated permit
 * @vec:	Message that is sent to server together with the request.
 *		Sum of len of all @vec elements limited to <= IO_MSG_SIZE.
 *		Since the msg is copied internally it can be allocated on stack.
 * @nr:		Number of elements in @vec.
 * @data_len:	length of data sent to/from server
 * @sg:		Pages to be sent/received to/from server.
 * @sg_cnt:	Number of elements in the @sg
 *
 * Return:
 * 0:		Success
 * <0:		Error
 *
 * On dir=READ rtrs client will request a data transfer from Server to client.
 * The data that the server will respond with will be stored in @sg when
 * the user receives an %RTRS_CLT_RDMA_EV_RDMA_REQUEST_WRITE_COMPL event.
 * On dir=WRITE rtrs client will rdma write data in sg to server side.
 */
int rtrs_clt_request(int dir, struct rtrs_clt_req_ops *ops,
		     struct rtrs_clt_sess *clt, struct rtrs_permit *permit,
		     const struct kvec *vec, size_t nr, size_t data_len,
		     struct scatterlist *sg, unsigned int sg_cnt)
{
	struct rtrs_clt_io_req *req;
	struct rtrs_clt_path *clt_path;

	enum dma_data_direction dma_dir;
	int err = -ECONNABORTED, i;
	size_t usr_len, hdr_len;
	struct path_it it;

	/* Get kvec length */
	for (i = 0, usr_len = 0; i < nr; i++)
		usr_len += vec[i].iov_len;

	if (dir == READ) {
		hdr_len = sizeof(struct rtrs_msg_rdma_read) +
			  sg_cnt * sizeof(struct rtrs_sg_desc);
		dma_dir = DMA_FROM_DEVICE;
	} else {
		hdr_len = sizeof(struct rtrs_msg_rdma_write);
		dma_dir = DMA_TO_DEVICE;
	}

	rcu_read_lock();
	for (path_it_init(&it, clt);
	     (clt_path = it.next_path(&it)) && it.i < it.clt->paths_num; it.i++) {
		if (READ_ONCE(clt_path->state) != RTRS_CLT_CONNECTED)
			continue;

		if (usr_len + hdr_len > clt_path->max_hdr_size) {
			rtrs_wrn_rl(clt_path->clt,
				     "%s request failed, user message size is %zu and header length %zu, but max size is %u\n",
				     dir == READ ? "Read" : "Write",
				     usr_len, hdr_len, clt_path->max_hdr_size);
			err = -EMSGSIZE;
			break;
		}
		req = rtrs_clt_get_req(clt_path, ops->conf_fn, permit, ops->priv,
				       vec, usr_len, sg, sg_cnt, data_len,
				       dma_dir);
		if (dir == READ)
			err = rtrs_clt_read_req(req);
		else
			err = rtrs_clt_write_req(req);
		if (err) {
			req->in_use = false;
			continue;
		}
		/* Success path */
		break;
	}
	path_it_deinit(&it);
	rcu_read_unlock();

	return err;
}
EXPORT_SYMBOL(rtrs_clt_request);

int rtrs_clt_rdma_cq_direct(struct rtrs_clt_sess *clt, unsigned int index)
{
	/* If no path, return -1 for block layer not to try again */
	int cnt = -1;
	struct rtrs_con *con;
	struct rtrs_clt_path *clt_path;
	struct path_it it;

	rcu_read_lock();
	for (path_it_init(&it, clt);
	     (clt_path = it.next_path(&it)) && it.i < it.clt->paths_num; it.i++) {
		if (READ_ONCE(clt_path->state) != RTRS_CLT_CONNECTED)
			continue;

		con = clt_path->s.con[index + 1];
		cnt = ib_process_cq_direct(con->cq, -1);
		if (cnt)
			break;
	}
	path_it_deinit(&it);
	rcu_read_unlock();

	return cnt;
}
EXPORT_SYMBOL(rtrs_clt_rdma_cq_direct);

/**
 * rtrs_clt_query() - queries RTRS session attributes
 *@clt: session pointer
 *@attr: query results for session attributes.
 * Returns:
 *    0 on success
 *    -ECOMM		no connection to the server
 */
int rtrs_clt_query(struct rtrs_clt_sess *clt, struct rtrs_attrs *attr)
{
	if (!rtrs_clt_is_connected(clt))
		return -ECOMM;

	attr->queue_depth      = clt->queue_depth;
	attr->max_segments     = clt->max_segments;
	/* Cap max_io_size to min of remote buffer size and the fr pages */
	attr->max_io_size = min_t(int, clt->max_io_size,
				  clt->max_segments * SZ_4K);

	return 0;
}
EXPORT_SYMBOL(rtrs_clt_query);

int rtrs_clt_create_path_from_sysfs(struct rtrs_clt_sess *clt,
				     struct rtrs_addr *addr)
{
	struct rtrs_clt_path *clt_path;
	int err;

	clt_path = alloc_path(clt, addr, nr_cpu_ids, 0);
	if (IS_ERR(clt_path))
		return PTR_ERR(clt_path);

	mutex_lock(&clt->paths_mutex);
	if (clt->paths_num == 0) {
		/*
		 * When all the paths are removed for a session,
		 * the addition of the first path is like a new session for
		 * the storage server
		 */
		clt_path->for_new_clt = 1;
	}

	mutex_unlock(&clt->paths_mutex);

	/*
	 * It is totally safe to add path in CONNECTING state: coming
	 * IO will never grab it.  Also it is very important to add
	 * path before init, since init fires LINK_CONNECTED event.
	 */
	rtrs_clt_add_path_to_arr(clt_path);

	err = init_path(clt_path);
	if (err)
		goto close_path;

	err = rtrs_clt_create_path_files(clt_path);
	if (err)
		goto close_path;

	return 0;

close_path:
	rtrs_clt_remove_path_from_arr(clt_path);
	rtrs_clt_close_conns(clt_path, true);
	free_percpu(clt_path->stats->pcpu_stats);
	kfree(clt_path->stats);
	free_path(clt_path);

	return err;
}

void rtrs_clt_ib_event_handler(struct ib_event_handler *handler,
			       struct ib_event *ibevent)
{
	pr_info("Handling event: %s (%d).\n", ib_event_msg(ibevent->event),
		ibevent->event);
}


static int rtrs_clt_ib_dev_init(struct rtrs_ib_dev *dev)
{
	INIT_IB_EVENT_HANDLER(&dev->event_handler, dev->ib_dev,
			      rtrs_clt_ib_event_handler);
	ib_register_event_handler(&dev->event_handler);

	if (!(dev->ib_dev->attrs.device_cap_flags &
	      IB_DEVICE_MEM_MGT_EXTENSIONS)) {
		pr_err("Memory registrations not supported.\n");
		return -ENOTSUPP;
	}

	return 0;
}

static void rtrs_clt_ib_dev_deinit(struct rtrs_ib_dev *dev)
{
	ib_unregister_event_handler(&dev->event_handler);
}


static const struct rtrs_rdma_dev_pd_ops dev_pd_ops = {
	.init = rtrs_clt_ib_dev_init,
	.deinit = rtrs_clt_ib_dev_deinit
};

static int __init rtrs_client_init(void)
{
	int ret = 0;

	rtrs_rdma_dev_pd_init(0, &dev_pd);
	ret = class_register(&rtrs_clt_dev_class);
	if (ret) {
		pr_err("Failed to create rtrs-client dev class\n");
		return ret;
	}
	rtrs_wq = alloc_workqueue("rtrs_client_wq", 0, 0);
	if (!rtrs_wq) {
		class_unregister(&rtrs_clt_dev_class);
		return -ENOMEM;
	}

	return 0;
}

static void __exit rtrs_client_exit(void)
{
	destroy_workqueue(rtrs_wq);
	class_unregister(&rtrs_clt_dev_class);
	rtrs_rdma_dev_pd_deinit(&dev_pd);
}

module_init(rtrs_client_init);
module_exit(rtrs_client_exit);
