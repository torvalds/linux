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

#include "rtrs-srv.h"
#include "rtrs-log.h"
#include <rdma/ib_cm.h>
#include <rdma/ib_verbs.h>
#include "rtrs-srv-trace.h"

MODULE_DESCRIPTION("RDMA Transport Server");
MODULE_LICENSE("GPL");

/* Must be power of 2, see mask from mr->page_size in ib_sg_to_pages() */
#define DEFAULT_MAX_CHUNK_SIZE (128 << 10)
#define DEFAULT_SESS_QUEUE_DEPTH 512
#define MAX_HDR_SIZE PAGE_SIZE

static struct rtrs_rdma_dev_pd dev_pd;
struct class *rtrs_dev_class;
static struct rtrs_srv_ib_ctx ib_ctx;

static int __read_mostly max_chunk_size = DEFAULT_MAX_CHUNK_SIZE;
static int __read_mostly sess_queue_depth = DEFAULT_SESS_QUEUE_DEPTH;

static bool always_invalidate = true;
module_param(always_invalidate, bool, 0444);
MODULE_PARM_DESC(always_invalidate,
		 "Invalidate memory registration for contiguous memory regions before accessing.");

module_param_named(max_chunk_size, max_chunk_size, int, 0444);
MODULE_PARM_DESC(max_chunk_size,
		 "Max size for each IO request, when change the unit is in byte (default: "
		 __stringify(DEFAULT_MAX_CHUNK_SIZE) "KB)");

module_param_named(sess_queue_depth, sess_queue_depth, int, 0444);
MODULE_PARM_DESC(sess_queue_depth,
		 "Number of buffers for pending I/O requests to allocate per session. Maximum: "
		 __stringify(MAX_SESS_QUEUE_DEPTH) " (default: "
		 __stringify(DEFAULT_SESS_QUEUE_DEPTH) ")");

static cpumask_t cq_affinity_mask = { CPU_BITS_ALL };

static struct workqueue_struct *rtrs_wq;

static inline struct rtrs_srv_con *to_srv_con(struct rtrs_con *c)
{
	return container_of(c, struct rtrs_srv_con, c);
}

static bool rtrs_srv_change_state(struct rtrs_srv_path *srv_path,
				  enum rtrs_srv_state new_state)
{
	enum rtrs_srv_state old_state;
	bool changed = false;
	unsigned long flags;

	spin_lock_irqsave(&srv_path->state_lock, flags);
	old_state = srv_path->state;
	switch (new_state) {
	case RTRS_SRV_CONNECTED:
		if (old_state == RTRS_SRV_CONNECTING)
			changed = true;
		break;
	case RTRS_SRV_CLOSING:
		if (old_state == RTRS_SRV_CONNECTING ||
		    old_state == RTRS_SRV_CONNECTED)
			changed = true;
		break;
	case RTRS_SRV_CLOSED:
		if (old_state == RTRS_SRV_CLOSING)
			changed = true;
		break;
	default:
		break;
	}
	if (changed)
		srv_path->state = new_state;
	spin_unlock_irqrestore(&srv_path->state_lock, flags);

	return changed;
}

static void free_id(struct rtrs_srv_op *id)
{
	if (!id)
		return;
	kfree(id);
}

static void rtrs_srv_free_ops_ids(struct rtrs_srv_path *srv_path)
{
	struct rtrs_srv_sess *srv = srv_path->srv;
	int i;

	if (srv_path->ops_ids) {
		for (i = 0; i < srv->queue_depth; i++)
			free_id(srv_path->ops_ids[i]);
		kfree(srv_path->ops_ids);
		srv_path->ops_ids = NULL;
	}
}

static void rtrs_srv_rdma_done(struct ib_cq *cq, struct ib_wc *wc);

static struct ib_cqe io_comp_cqe = {
	.done = rtrs_srv_rdma_done
};

static inline void rtrs_srv_inflight_ref_release(struct percpu_ref *ref)
{
	struct rtrs_srv_path *srv_path = container_of(ref,
						      struct rtrs_srv_path,
						      ids_inflight_ref);

	percpu_ref_exit(&srv_path->ids_inflight_ref);
	complete(&srv_path->complete_done);
}

static int rtrs_srv_alloc_ops_ids(struct rtrs_srv_path *srv_path)
{
	struct rtrs_srv_sess *srv = srv_path->srv;
	struct rtrs_srv_op *id;
	int i, ret;

	srv_path->ops_ids = kcalloc(srv->queue_depth,
				    sizeof(*srv_path->ops_ids),
				    GFP_KERNEL);
	if (!srv_path->ops_ids)
		goto err;

	for (i = 0; i < srv->queue_depth; ++i) {
		id = kzalloc(sizeof(*id), GFP_KERNEL);
		if (!id)
			goto err;

		srv_path->ops_ids[i] = id;
	}

	ret = percpu_ref_init(&srv_path->ids_inflight_ref,
			      rtrs_srv_inflight_ref_release, 0, GFP_KERNEL);
	if (ret) {
		pr_err("Percpu reference init failed\n");
		goto err;
	}
	init_completion(&srv_path->complete_done);

	return 0;

err:
	rtrs_srv_free_ops_ids(srv_path);
	return -ENOMEM;
}

static inline void rtrs_srv_get_ops_ids(struct rtrs_srv_path *srv_path)
{
	percpu_ref_get(&srv_path->ids_inflight_ref);
}

static inline void rtrs_srv_put_ops_ids(struct rtrs_srv_path *srv_path)
{
	percpu_ref_put(&srv_path->ids_inflight_ref);
}

static void rtrs_srv_reg_mr_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct rtrs_srv_con *con = to_srv_con(wc->qp->qp_context);
	struct rtrs_path *s = con->c.path;
	struct rtrs_srv_path *srv_path = to_srv_path(s);

	if (wc->status != IB_WC_SUCCESS) {
		rtrs_err(s, "REG MR failed: %s\n",
			  ib_wc_status_msg(wc->status));
		close_path(srv_path);
		return;
	}
}

static struct ib_cqe local_reg_cqe = {
	.done = rtrs_srv_reg_mr_done
};

static int rdma_write_sg(struct rtrs_srv_op *id)
{
	struct rtrs_path *s = id->con->c.path;
	struct rtrs_srv_path *srv_path = to_srv_path(s);
	dma_addr_t dma_addr = srv_path->dma_addr[id->msg_id];
	struct rtrs_srv_mr *srv_mr;
	struct ib_send_wr inv_wr;
	struct ib_rdma_wr imm_wr;
	struct ib_rdma_wr *wr = NULL;
	enum ib_send_flags flags;
	size_t sg_cnt;
	int err, offset;
	bool need_inval;
	u32 rkey = 0;
	struct ib_reg_wr rwr;
	struct ib_sge *plist;
	struct ib_sge list;

	sg_cnt = le16_to_cpu(id->rd_msg->sg_cnt);
	need_inval = le16_to_cpu(id->rd_msg->flags) & RTRS_MSG_NEED_INVAL_F;
	if (sg_cnt != 1)
		return -EINVAL;

	offset = 0;

	wr		= &id->tx_wr;
	plist		= &id->tx_sg;
	plist->addr	= dma_addr + offset;
	plist->length	= le32_to_cpu(id->rd_msg->desc[0].len);

	/* WR will fail with length error
	 * if this is 0
	 */
	if (plist->length == 0) {
		rtrs_err(s, "Invalid RDMA-Write sg list length 0\n");
		return -EINVAL;
	}

	plist->lkey = srv_path->s.dev->ib_pd->local_dma_lkey;
	offset += plist->length;

	wr->wr.sg_list	= plist;
	wr->wr.num_sge	= 1;
	wr->remote_addr	= le64_to_cpu(id->rd_msg->desc[0].addr);
	wr->rkey	= le32_to_cpu(id->rd_msg->desc[0].key);
	if (rkey == 0)
		rkey = wr->rkey;
	else
		/* Only one key is actually used */
		WARN_ON_ONCE(rkey != wr->rkey);

	wr->wr.opcode = IB_WR_RDMA_WRITE;
	wr->wr.wr_cqe   = &io_comp_cqe;
	wr->wr.ex.imm_data = 0;
	wr->wr.send_flags  = 0;

	if (need_inval && always_invalidate) {
		wr->wr.next = &rwr.wr;
		rwr.wr.next = &inv_wr;
		inv_wr.next = &imm_wr.wr;
	} else if (always_invalidate) {
		wr->wr.next = &rwr.wr;
		rwr.wr.next = &imm_wr.wr;
	} else if (need_inval) {
		wr->wr.next = &inv_wr;
		inv_wr.next = &imm_wr.wr;
	} else {
		wr->wr.next = &imm_wr.wr;
	}
	/*
	 * From time to time we have to post signaled sends,
	 * or send queue will fill up and only QP reset can help.
	 */
	flags = (atomic_inc_return(&id->con->c.wr_cnt) % s->signal_interval) ?
		0 : IB_SEND_SIGNALED;

	if (need_inval) {
		inv_wr.sg_list = NULL;
		inv_wr.num_sge = 0;
		inv_wr.opcode = IB_WR_SEND_WITH_INV;
		inv_wr.wr_cqe   = &io_comp_cqe;
		inv_wr.send_flags = 0;
		inv_wr.ex.invalidate_rkey = rkey;
	}

	imm_wr.wr.next = NULL;
	if (always_invalidate) {
		struct rtrs_msg_rkey_rsp *msg;

		srv_mr = &srv_path->mrs[id->msg_id];
		rwr.wr.opcode = IB_WR_REG_MR;
		rwr.wr.wr_cqe = &local_reg_cqe;
		rwr.wr.num_sge = 0;
		rwr.mr = srv_mr->mr;
		rwr.wr.send_flags = 0;
		rwr.key = srv_mr->mr->rkey;
		rwr.access = (IB_ACCESS_LOCAL_WRITE |
			      IB_ACCESS_REMOTE_WRITE);
		msg = srv_mr->iu->buf;
		msg->buf_id = cpu_to_le16(id->msg_id);
		msg->type = cpu_to_le16(RTRS_MSG_RKEY_RSP);
		msg->rkey = cpu_to_le32(srv_mr->mr->rkey);

		list.addr   = srv_mr->iu->dma_addr;
		list.length = sizeof(*msg);
		list.lkey   = srv_path->s.dev->ib_pd->local_dma_lkey;
		imm_wr.wr.sg_list = &list;
		imm_wr.wr.num_sge = 1;
		imm_wr.wr.opcode = IB_WR_SEND_WITH_IMM;
		ib_dma_sync_single_for_device(srv_path->s.dev->ib_dev,
					      srv_mr->iu->dma_addr,
					      srv_mr->iu->size, DMA_TO_DEVICE);
	} else {
		imm_wr.wr.sg_list = NULL;
		imm_wr.wr.num_sge = 0;
		imm_wr.wr.opcode = IB_WR_RDMA_WRITE_WITH_IMM;
	}
	imm_wr.wr.send_flags = flags;
	imm_wr.wr.ex.imm_data = cpu_to_be32(rtrs_to_io_rsp_imm(id->msg_id,
							     0, need_inval));

	imm_wr.wr.wr_cqe   = &io_comp_cqe;
	ib_dma_sync_single_for_device(srv_path->s.dev->ib_dev, dma_addr,
				      offset, DMA_BIDIRECTIONAL);

	err = ib_post_send(id->con->c.qp, &id->tx_wr.wr, NULL);
	if (err)
		rtrs_err(s,
			  "Posting RDMA-Write-Request to QP failed, err: %d\n",
			  err);

	return err;
}

/**
 * send_io_resp_imm() - respond to client with empty IMM on failed READ/WRITE
 *                      requests or on successful WRITE request.
 * @con:	the connection to send back result
 * @id:		the id associated with the IO
 * @errno:	the error number of the IO.
 *
 * Return 0 on success, errno otherwise.
 */
static int send_io_resp_imm(struct rtrs_srv_con *con, struct rtrs_srv_op *id,
			    int errno)
{
	struct rtrs_path *s = con->c.path;
	struct rtrs_srv_path *srv_path = to_srv_path(s);
	struct ib_send_wr inv_wr, *wr = NULL;
	struct ib_rdma_wr imm_wr;
	struct ib_reg_wr rwr;
	struct rtrs_srv_mr *srv_mr;
	bool need_inval = false;
	enum ib_send_flags flags;
	u32 imm;
	int err;

	if (id->dir == READ) {
		struct rtrs_msg_rdma_read *rd_msg = id->rd_msg;
		size_t sg_cnt;

		need_inval = le16_to_cpu(rd_msg->flags) &
				RTRS_MSG_NEED_INVAL_F;
		sg_cnt = le16_to_cpu(rd_msg->sg_cnt);

		if (need_inval) {
			if (sg_cnt) {
				inv_wr.wr_cqe   = &io_comp_cqe;
				inv_wr.sg_list = NULL;
				inv_wr.num_sge = 0;
				inv_wr.opcode = IB_WR_SEND_WITH_INV;
				inv_wr.send_flags = 0;
				/* Only one key is actually used */
				inv_wr.ex.invalidate_rkey =
					le32_to_cpu(rd_msg->desc[0].key);
			} else {
				WARN_ON_ONCE(1);
				need_inval = false;
			}
		}
	}

	trace_send_io_resp_imm(id, need_inval, always_invalidate, errno);

	if (need_inval && always_invalidate) {
		wr = &inv_wr;
		inv_wr.next = &rwr.wr;
		rwr.wr.next = &imm_wr.wr;
	} else if (always_invalidate) {
		wr = &rwr.wr;
		rwr.wr.next = &imm_wr.wr;
	} else if (need_inval) {
		wr = &inv_wr;
		inv_wr.next = &imm_wr.wr;
	} else {
		wr = &imm_wr.wr;
	}
	/*
	 * From time to time we have to post signalled sends,
	 * or send queue will fill up and only QP reset can help.
	 */
	flags = (atomic_inc_return(&con->c.wr_cnt) % s->signal_interval) ?
		0 : IB_SEND_SIGNALED;
	imm = rtrs_to_io_rsp_imm(id->msg_id, errno, need_inval);
	imm_wr.wr.next = NULL;
	if (always_invalidate) {
		struct ib_sge list;
		struct rtrs_msg_rkey_rsp *msg;

		srv_mr = &srv_path->mrs[id->msg_id];
		rwr.wr.next = &imm_wr.wr;
		rwr.wr.opcode = IB_WR_REG_MR;
		rwr.wr.wr_cqe = &local_reg_cqe;
		rwr.wr.num_sge = 0;
		rwr.wr.send_flags = 0;
		rwr.mr = srv_mr->mr;
		rwr.key = srv_mr->mr->rkey;
		rwr.access = (IB_ACCESS_LOCAL_WRITE |
			      IB_ACCESS_REMOTE_WRITE);
		msg = srv_mr->iu->buf;
		msg->buf_id = cpu_to_le16(id->msg_id);
		msg->type = cpu_to_le16(RTRS_MSG_RKEY_RSP);
		msg->rkey = cpu_to_le32(srv_mr->mr->rkey);

		list.addr   = srv_mr->iu->dma_addr;
		list.length = sizeof(*msg);
		list.lkey   = srv_path->s.dev->ib_pd->local_dma_lkey;
		imm_wr.wr.sg_list = &list;
		imm_wr.wr.num_sge = 1;
		imm_wr.wr.opcode = IB_WR_SEND_WITH_IMM;
		ib_dma_sync_single_for_device(srv_path->s.dev->ib_dev,
					      srv_mr->iu->dma_addr,
					      srv_mr->iu->size, DMA_TO_DEVICE);
	} else {
		imm_wr.wr.sg_list = NULL;
		imm_wr.wr.num_sge = 0;
		imm_wr.wr.opcode = IB_WR_RDMA_WRITE_WITH_IMM;
	}
	imm_wr.wr.send_flags = flags;
	imm_wr.wr.wr_cqe   = &io_comp_cqe;

	imm_wr.wr.ex.imm_data = cpu_to_be32(imm);

	err = ib_post_send(id->con->c.qp, wr, NULL);
	if (err)
		rtrs_err_rl(s, "Posting RDMA-Reply to QP failed, err: %d\n",
			     err);

	return err;
}

void close_path(struct rtrs_srv_path *srv_path)
{
	if (rtrs_srv_change_state(srv_path, RTRS_SRV_CLOSING))
		queue_work(rtrs_wq, &srv_path->close_work);
	WARN_ON(srv_path->state != RTRS_SRV_CLOSING);
}

static inline const char *rtrs_srv_state_str(enum rtrs_srv_state state)
{
	switch (state) {
	case RTRS_SRV_CONNECTING:
		return "RTRS_SRV_CONNECTING";
	case RTRS_SRV_CONNECTED:
		return "RTRS_SRV_CONNECTED";
	case RTRS_SRV_CLOSING:
		return "RTRS_SRV_CLOSING";
	case RTRS_SRV_CLOSED:
		return "RTRS_SRV_CLOSED";
	default:
		return "UNKNOWN";
	}
}

/**
 * rtrs_srv_resp_rdma() - Finish an RDMA request
 *
 * @id:		Internal RTRS operation identifier
 * @status:	Response Code sent to the other side for this operation.
 *		0 = success, <=0 error
 * Context: any
 *
 * Finish a RDMA operation. A message is sent to the client and the
 * corresponding memory areas will be released.
 */
bool rtrs_srv_resp_rdma(struct rtrs_srv_op *id, int status)
{
	struct rtrs_srv_path *srv_path;
	struct rtrs_srv_con *con;
	struct rtrs_path *s;
	int err;

	if (WARN_ON(!id))
		return true;

	con = id->con;
	s = con->c.path;
	srv_path = to_srv_path(s);

	id->status = status;

	if (srv_path->state != RTRS_SRV_CONNECTED) {
		rtrs_err_rl(s,
			    "Sending I/O response failed,  server path %s is disconnected, path state %s\n",
			    kobject_name(&srv_path->kobj),
			    rtrs_srv_state_str(srv_path->state));
		goto out;
	}
	if (always_invalidate) {
		struct rtrs_srv_mr *mr = &srv_path->mrs[id->msg_id];

		ib_update_fast_reg_key(mr->mr, ib_inc_rkey(mr->mr->rkey));
	}
	if (atomic_sub_return(1, &con->c.sq_wr_avail) < 0) {
		rtrs_err(s, "IB send queue full: srv_path=%s cid=%d\n",
			 kobject_name(&srv_path->kobj),
			 con->c.cid);
		atomic_add(1, &con->c.sq_wr_avail);
		spin_lock(&con->rsp_wr_wait_lock);
		list_add_tail(&id->wait_list, &con->rsp_wr_wait_list);
		spin_unlock(&con->rsp_wr_wait_lock);
		return false;
	}

	if (status || id->dir == WRITE || !id->rd_msg->sg_cnt)
		err = send_io_resp_imm(con, id, status);
	else
		err = rdma_write_sg(id);

	if (err) {
		rtrs_err_rl(s, "IO response failed: %d: srv_path=%s\n", err,
			    kobject_name(&srv_path->kobj));
		close_path(srv_path);
	}
out:
	rtrs_srv_put_ops_ids(srv_path);
	return true;
}
EXPORT_SYMBOL(rtrs_srv_resp_rdma);

/**
 * rtrs_srv_set_sess_priv() - Set private pointer in rtrs_srv.
 * @srv:	Session pointer
 * @priv:	The private pointer that is associated with the session.
 */
void rtrs_srv_set_sess_priv(struct rtrs_srv_sess *srv, void *priv)
{
	srv->priv = priv;
}
EXPORT_SYMBOL(rtrs_srv_set_sess_priv);

static void unmap_cont_bufs(struct rtrs_srv_path *srv_path)
{
	int i;

	for (i = 0; i < srv_path->mrs_num; i++) {
		struct rtrs_srv_mr *srv_mr;

		srv_mr = &srv_path->mrs[i];

		if (always_invalidate)
			rtrs_iu_free(srv_mr->iu, srv_path->s.dev->ib_dev, 1);

		ib_dereg_mr(srv_mr->mr);
		ib_dma_unmap_sg(srv_path->s.dev->ib_dev, srv_mr->sgt.sgl,
				srv_mr->sgt.nents, DMA_BIDIRECTIONAL);
		sg_free_table(&srv_mr->sgt);
	}
	kfree(srv_path->mrs);
}

static int map_cont_bufs(struct rtrs_srv_path *srv_path)
{
	struct rtrs_srv_sess *srv = srv_path->srv;
	struct rtrs_path *ss = &srv_path->s;
	int i, mri, err, mrs_num;
	unsigned int chunk_bits;
	int chunks_per_mr = 1;

	/*
	 * Here we map queue_depth chunks to MR.  Firstly we have to
	 * figure out how many chunks can we map per MR.
	 */
	if (always_invalidate) {
		/*
		 * in order to do invalidate for each chunks of memory, we needs
		 * more memory regions.
		 */
		mrs_num = srv->queue_depth;
	} else {
		chunks_per_mr =
			srv_path->s.dev->ib_dev->attrs.max_fast_reg_page_list_len;
		mrs_num = DIV_ROUND_UP(srv->queue_depth, chunks_per_mr);
		chunks_per_mr = DIV_ROUND_UP(srv->queue_depth, mrs_num);
	}

	srv_path->mrs = kcalloc(mrs_num, sizeof(*srv_path->mrs), GFP_KERNEL);
	if (!srv_path->mrs)
		return -ENOMEM;

	srv_path->mrs_num = mrs_num;

	for (mri = 0; mri < mrs_num; mri++) {
		struct rtrs_srv_mr *srv_mr = &srv_path->mrs[mri];
		struct sg_table *sgt = &srv_mr->sgt;
		struct scatterlist *s;
		struct ib_mr *mr;
		int nr, nr_sgt, chunks;

		chunks = chunks_per_mr * mri;
		if (!always_invalidate)
			chunks_per_mr = min_t(int, chunks_per_mr,
					      srv->queue_depth - chunks);

		err = sg_alloc_table(sgt, chunks_per_mr, GFP_KERNEL);
		if (err)
			goto err;

		for_each_sg(sgt->sgl, s, chunks_per_mr, i)
			sg_set_page(s, srv->chunks[chunks + i],
				    max_chunk_size, 0);

		nr_sgt = ib_dma_map_sg(srv_path->s.dev->ib_dev, sgt->sgl,
				   sgt->nents, DMA_BIDIRECTIONAL);
		if (!nr_sgt) {
			err = -EINVAL;
			goto free_sg;
		}
		mr = ib_alloc_mr(srv_path->s.dev->ib_pd, IB_MR_TYPE_MEM_REG,
				 nr_sgt);
		if (IS_ERR(mr)) {
			err = PTR_ERR(mr);
			goto unmap_sg;
		}
		nr = ib_map_mr_sg(mr, sgt->sgl, nr_sgt,
				  NULL, max_chunk_size);
		if (nr < 0 || nr < sgt->nents) {
			err = nr < 0 ? nr : -EINVAL;
			goto dereg_mr;
		}

		if (always_invalidate) {
			srv_mr->iu = rtrs_iu_alloc(1,
					sizeof(struct rtrs_msg_rkey_rsp),
					GFP_KERNEL, srv_path->s.dev->ib_dev,
					DMA_TO_DEVICE, rtrs_srv_rdma_done);
			if (!srv_mr->iu) {
				err = -ENOMEM;
				rtrs_err(ss, "rtrs_iu_alloc(), err: %d\n", err);
				goto dereg_mr;
			}
		}
		/* Eventually dma addr for each chunk can be cached */
		for_each_sg(sgt->sgl, s, nr_sgt, i)
			srv_path->dma_addr[chunks + i] = sg_dma_address(s);

		ib_update_fast_reg_key(mr, ib_inc_rkey(mr->rkey));
		srv_mr->mr = mr;

		continue;
err:
		while (mri--) {
			srv_mr = &srv_path->mrs[mri];
			sgt = &srv_mr->sgt;
			mr = srv_mr->mr;
			rtrs_iu_free(srv_mr->iu, srv_path->s.dev->ib_dev, 1);
dereg_mr:
			ib_dereg_mr(mr);
unmap_sg:
			ib_dma_unmap_sg(srv_path->s.dev->ib_dev, sgt->sgl,
					sgt->nents, DMA_BIDIRECTIONAL);
free_sg:
			sg_free_table(sgt);
		}
		kfree(srv_path->mrs);

		return err;
	}

	chunk_bits = ilog2(srv->queue_depth - 1) + 1;
	srv_path->mem_bits = (MAX_IMM_PAYL_BITS - chunk_bits);

	return 0;
}

static void rtrs_srv_hb_err_handler(struct rtrs_con *c)
{
	close_path(to_srv_path(c->path));
}

static void rtrs_srv_init_hb(struct rtrs_srv_path *srv_path)
{
	rtrs_init_hb(&srv_path->s, &io_comp_cqe,
		      RTRS_HB_INTERVAL_MS,
		      RTRS_HB_MISSED_MAX,
		      rtrs_srv_hb_err_handler,
		      rtrs_wq);
}

static void rtrs_srv_start_hb(struct rtrs_srv_path *srv_path)
{
	rtrs_start_hb(&srv_path->s);
}

static void rtrs_srv_stop_hb(struct rtrs_srv_path *srv_path)
{
	rtrs_stop_hb(&srv_path->s);
}

static void rtrs_srv_info_rsp_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct rtrs_srv_con *con = to_srv_con(wc->qp->qp_context);
	struct rtrs_path *s = con->c.path;
	struct rtrs_srv_path *srv_path = to_srv_path(s);
	struct rtrs_iu *iu;

	iu = container_of(wc->wr_cqe, struct rtrs_iu, cqe);
	rtrs_iu_free(iu, srv_path->s.dev->ib_dev, 1);

	if (wc->status != IB_WC_SUCCESS) {
		rtrs_err(s, "Sess info response send failed: %s\n",
			  ib_wc_status_msg(wc->status));
		close_path(srv_path);
		return;
	}
	WARN_ON(wc->opcode != IB_WC_SEND);
}

static int rtrs_srv_path_up(struct rtrs_srv_path *srv_path)
{
	struct rtrs_srv_sess *srv = srv_path->srv;
	struct rtrs_srv_ctx *ctx = srv->ctx;
	int up, ret = 0;

	mutex_lock(&srv->paths_ev_mutex);
	up = ++srv->paths_up;
	if (up == 1)
		ret = ctx->ops.link_ev(srv, RTRS_SRV_LINK_EV_CONNECTED, NULL);
	mutex_unlock(&srv->paths_ev_mutex);

	/* Mark session as established */
	if (!ret)
		srv_path->established = true;

	return ret;
}

static void rtrs_srv_path_down(struct rtrs_srv_path *srv_path)
{
	struct rtrs_srv_sess *srv = srv_path->srv;
	struct rtrs_srv_ctx *ctx = srv->ctx;

	if (!srv_path->established)
		return;

	srv_path->established = false;
	mutex_lock(&srv->paths_ev_mutex);
	WARN_ON(!srv->paths_up);
	if (--srv->paths_up == 0)
		ctx->ops.link_ev(srv, RTRS_SRV_LINK_EV_DISCONNECTED, srv->priv);
	mutex_unlock(&srv->paths_ev_mutex);
}

static bool exist_pathname(struct rtrs_srv_ctx *ctx,
			   const char *pathname, const uuid_t *path_uuid)
{
	struct rtrs_srv_sess *srv;
	struct rtrs_srv_path *srv_path;
	bool found = false;

	mutex_lock(&ctx->srv_mutex);
	list_for_each_entry(srv, &ctx->srv_list, ctx_list) {
		mutex_lock(&srv->paths_mutex);

		/* when a client with same uuid and same sessname tried to add a path */
		if (uuid_equal(&srv->paths_uuid, path_uuid)) {
			mutex_unlock(&srv->paths_mutex);
			continue;
		}

		list_for_each_entry(srv_path, &srv->paths_list, s.entry) {
			if (strlen(srv_path->s.sessname) == strlen(pathname) &&
			    !strcmp(srv_path->s.sessname, pathname)) {
				found = true;
				break;
			}
		}
		mutex_unlock(&srv->paths_mutex);
		if (found)
			break;
	}
	mutex_unlock(&ctx->srv_mutex);
	return found;
}

static int post_recv_path(struct rtrs_srv_path *srv_path);
static int rtrs_rdma_do_reject(struct rdma_cm_id *cm_id, int errno);

static int process_info_req(struct rtrs_srv_con *con,
			    struct rtrs_msg_info_req *msg)
{
	struct rtrs_path *s = con->c.path;
	struct rtrs_srv_path *srv_path = to_srv_path(s);
	struct ib_send_wr *reg_wr = NULL;
	struct rtrs_msg_info_rsp *rsp;
	struct rtrs_iu *tx_iu;
	struct ib_reg_wr *rwr;
	int mri, err;
	size_t tx_sz;

	err = post_recv_path(srv_path);
	if (err) {
		rtrs_err(s, "post_recv_path(), err: %d\n", err);
		return err;
	}

	if (strchr(msg->pathname, '/') || strchr(msg->pathname, '.')) {
		rtrs_err(s, "pathname cannot contain / and .\n");
		return -EINVAL;
	}

	if (exist_pathname(srv_path->srv->ctx,
			   msg->pathname, &srv_path->srv->paths_uuid)) {
		rtrs_err(s, "pathname is duplicated: %s\n", msg->pathname);
		return -EPERM;
	}
	strscpy(srv_path->s.sessname, msg->pathname,
		sizeof(srv_path->s.sessname));

	rwr = kcalloc(srv_path->mrs_num, sizeof(*rwr), GFP_KERNEL);
	if (!rwr)
		return -ENOMEM;

	tx_sz  = sizeof(*rsp);
	tx_sz += sizeof(rsp->desc[0]) * srv_path->mrs_num;
	tx_iu = rtrs_iu_alloc(1, tx_sz, GFP_KERNEL, srv_path->s.dev->ib_dev,
			       DMA_TO_DEVICE, rtrs_srv_info_rsp_done);
	if (!tx_iu) {
		err = -ENOMEM;
		goto rwr_free;
	}

	rsp = tx_iu->buf;
	rsp->type = cpu_to_le16(RTRS_MSG_INFO_RSP);
	rsp->sg_cnt = cpu_to_le16(srv_path->mrs_num);

	for (mri = 0; mri < srv_path->mrs_num; mri++) {
		struct ib_mr *mr = srv_path->mrs[mri].mr;

		rsp->desc[mri].addr = cpu_to_le64(mr->iova);
		rsp->desc[mri].key  = cpu_to_le32(mr->rkey);
		rsp->desc[mri].len  = cpu_to_le32(mr->length);

		/*
		 * Fill in reg MR request and chain them *backwards*
		 */
		rwr[mri].wr.next = mri ? &rwr[mri - 1].wr : NULL;
		rwr[mri].wr.opcode = IB_WR_REG_MR;
		rwr[mri].wr.wr_cqe = &local_reg_cqe;
		rwr[mri].wr.num_sge = 0;
		rwr[mri].wr.send_flags = 0;
		rwr[mri].mr = mr;
		rwr[mri].key = mr->rkey;
		rwr[mri].access = (IB_ACCESS_LOCAL_WRITE |
				   IB_ACCESS_REMOTE_WRITE);
		reg_wr = &rwr[mri].wr;
	}

	err = rtrs_srv_create_path_files(srv_path);
	if (err)
		goto iu_free;
	kobject_get(&srv_path->kobj);
	get_device(&srv_path->srv->dev);
	err = rtrs_srv_change_state(srv_path, RTRS_SRV_CONNECTED);
	if (!err) {
		rtrs_err(s, "rtrs_srv_change_state(), err: %d\n", err);
		goto iu_free;
	}

	rtrs_srv_start_hb(srv_path);

	/*
	 * We do not account number of established connections at the current
	 * moment, we rely on the client, which should send info request when
	 * all connections are successfully established.  Thus, simply notify
	 * listener with a proper event if we are the first path.
	 */
	err = rtrs_srv_path_up(srv_path);
	if (err) {
		rtrs_err(s, "rtrs_srv_path_up(), err: %d\n", err);
		goto iu_free;
	}

	ib_dma_sync_single_for_device(srv_path->s.dev->ib_dev,
				      tx_iu->dma_addr,
				      tx_iu->size, DMA_TO_DEVICE);

	/* Send info response */
	err = rtrs_iu_post_send(&con->c, tx_iu, tx_sz, reg_wr);
	if (err) {
		rtrs_err(s, "rtrs_iu_post_send(), err: %d\n", err);
iu_free:
		rtrs_iu_free(tx_iu, srv_path->s.dev->ib_dev, 1);
	}
rwr_free:
	kfree(rwr);

	return err;
}

static void rtrs_srv_info_req_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct rtrs_srv_con *con = to_srv_con(wc->qp->qp_context);
	struct rtrs_path *s = con->c.path;
	struct rtrs_srv_path *srv_path = to_srv_path(s);
	struct rtrs_msg_info_req *msg;
	struct rtrs_iu *iu;
	int err;

	WARN_ON(con->c.cid);

	iu = container_of(wc->wr_cqe, struct rtrs_iu, cqe);
	if (wc->status != IB_WC_SUCCESS) {
		rtrs_err(s, "Sess info request receive failed: %s\n",
			  ib_wc_status_msg(wc->status));
		goto close;
	}
	WARN_ON(wc->opcode != IB_WC_RECV);

	if (wc->byte_len < sizeof(*msg)) {
		rtrs_err(s, "Sess info request is malformed: size %d\n",
			  wc->byte_len);
		goto close;
	}
	ib_dma_sync_single_for_cpu(srv_path->s.dev->ib_dev, iu->dma_addr,
				   iu->size, DMA_FROM_DEVICE);
	msg = iu->buf;
	if (le16_to_cpu(msg->type) != RTRS_MSG_INFO_REQ) {
		rtrs_err(s, "Sess info request is malformed: type %d\n",
			  le16_to_cpu(msg->type));
		goto close;
	}
	err = process_info_req(con, msg);
	if (err)
		goto close;

	rtrs_iu_free(iu, srv_path->s.dev->ib_dev, 1);
	return;
close:
	rtrs_iu_free(iu, srv_path->s.dev->ib_dev, 1);
	close_path(srv_path);
}

static int post_recv_info_req(struct rtrs_srv_con *con)
{
	struct rtrs_path *s = con->c.path;
	struct rtrs_srv_path *srv_path = to_srv_path(s);
	struct rtrs_iu *rx_iu;
	int err;

	rx_iu = rtrs_iu_alloc(1, sizeof(struct rtrs_msg_info_req),
			       GFP_KERNEL, srv_path->s.dev->ib_dev,
			       DMA_FROM_DEVICE, rtrs_srv_info_req_done);
	if (!rx_iu)
		return -ENOMEM;
	/* Prepare for getting info response */
	err = rtrs_iu_post_recv(&con->c, rx_iu);
	if (err) {
		rtrs_err(s, "rtrs_iu_post_recv(), err: %d\n", err);
		rtrs_iu_free(rx_iu, srv_path->s.dev->ib_dev, 1);
		return err;
	}

	return 0;
}

static int post_recv_io(struct rtrs_srv_con *con, size_t q_size)
{
	int i, err;

	for (i = 0; i < q_size; i++) {
		err = rtrs_post_recv_empty(&con->c, &io_comp_cqe);
		if (err)
			return err;
	}

	return 0;
}

static int post_recv_path(struct rtrs_srv_path *srv_path)
{
	struct rtrs_srv_sess *srv = srv_path->srv;
	struct rtrs_path *s = &srv_path->s;
	size_t q_size;
	int err, cid;

	for (cid = 0; cid < srv_path->s.con_num; cid++) {
		if (cid == 0)
			q_size = SERVICE_CON_QUEUE_DEPTH;
		else
			q_size = srv->queue_depth;
		if (srv_path->state != RTRS_SRV_CONNECTING) {
			rtrs_err(s, "Path state invalid. state %s\n",
				 rtrs_srv_state_str(srv_path->state));
			return -EIO;
		}

		if (!srv_path->s.con[cid]) {
			rtrs_err(s, "Conn not set for %d\n", cid);
			return -EIO;
		}

		err = post_recv_io(to_srv_con(srv_path->s.con[cid]), q_size);
		if (err) {
			rtrs_err(s, "post_recv_io(), err: %d\n", err);
			return err;
		}
	}

	return 0;
}

static void process_read(struct rtrs_srv_con *con,
			 struct rtrs_msg_rdma_read *msg,
			 u32 buf_id, u32 off)
{
	struct rtrs_path *s = con->c.path;
	struct rtrs_srv_path *srv_path = to_srv_path(s);
	struct rtrs_srv_sess *srv = srv_path->srv;
	struct rtrs_srv_ctx *ctx = srv->ctx;
	struct rtrs_srv_op *id;

	size_t usr_len, data_len;
	void *data;
	int ret;

	if (srv_path->state != RTRS_SRV_CONNECTED) {
		rtrs_err_rl(s,
			     "Processing read request failed,  session is disconnected, sess state %s\n",
			     rtrs_srv_state_str(srv_path->state));
		return;
	}
	if (msg->sg_cnt != 1 && msg->sg_cnt != 0) {
		rtrs_err_rl(s,
			    "Processing read request failed, invalid message\n");
		return;
	}
	rtrs_srv_get_ops_ids(srv_path);
	rtrs_srv_update_rdma_stats(srv_path->stats, off, READ);
	id = srv_path->ops_ids[buf_id];
	id->con		= con;
	id->dir		= READ;
	id->msg_id	= buf_id;
	id->rd_msg	= msg;
	usr_len = le16_to_cpu(msg->usr_len);
	data_len = off - usr_len;
	data = page_address(srv->chunks[buf_id]);
	ret = ctx->ops.rdma_ev(srv->priv, id, data, data_len,
			   data + data_len, usr_len);

	if (ret) {
		rtrs_err_rl(s,
			     "Processing read request failed, user module cb reported for msg_id %d, err: %d\n",
			     buf_id, ret);
		goto send_err_msg;
	}

	return;

send_err_msg:
	ret = send_io_resp_imm(con, id, ret);
	if (ret < 0) {
		rtrs_err_rl(s,
			     "Sending err msg for failed RDMA-Write-Req failed, msg_id %d, err: %d\n",
			     buf_id, ret);
		close_path(srv_path);
	}
	rtrs_srv_put_ops_ids(srv_path);
}

static void process_write(struct rtrs_srv_con *con,
			  struct rtrs_msg_rdma_write *req,
			  u32 buf_id, u32 off)
{
	struct rtrs_path *s = con->c.path;
	struct rtrs_srv_path *srv_path = to_srv_path(s);
	struct rtrs_srv_sess *srv = srv_path->srv;
	struct rtrs_srv_ctx *ctx = srv->ctx;
	struct rtrs_srv_op *id;

	size_t data_len, usr_len;
	void *data;
	int ret;

	if (srv_path->state != RTRS_SRV_CONNECTED) {
		rtrs_err_rl(s,
			     "Processing write request failed,  session is disconnected, sess state %s\n",
			     rtrs_srv_state_str(srv_path->state));
		return;
	}
	rtrs_srv_get_ops_ids(srv_path);
	rtrs_srv_update_rdma_stats(srv_path->stats, off, WRITE);
	id = srv_path->ops_ids[buf_id];
	id->con    = con;
	id->dir    = WRITE;
	id->msg_id = buf_id;

	usr_len = le16_to_cpu(req->usr_len);
	data_len = off - usr_len;
	data = page_address(srv->chunks[buf_id]);
	ret = ctx->ops.rdma_ev(srv->priv, id, data, data_len,
			       data + data_len, usr_len);
	if (ret) {
		rtrs_err_rl(s,
			     "Processing write request failed, user module callback reports err: %d\n",
			     ret);
		goto send_err_msg;
	}

	return;

send_err_msg:
	ret = send_io_resp_imm(con, id, ret);
	if (ret < 0) {
		rtrs_err_rl(s,
			     "Processing write request failed, sending I/O response failed, msg_id %d, err: %d\n",
			     buf_id, ret);
		close_path(srv_path);
	}
	rtrs_srv_put_ops_ids(srv_path);
}

static void process_io_req(struct rtrs_srv_con *con, void *msg,
			   u32 id, u32 off)
{
	struct rtrs_path *s = con->c.path;
	struct rtrs_srv_path *srv_path = to_srv_path(s);
	struct rtrs_msg_rdma_hdr *hdr;
	unsigned int type;

	ib_dma_sync_single_for_cpu(srv_path->s.dev->ib_dev,
				   srv_path->dma_addr[id],
				   max_chunk_size, DMA_BIDIRECTIONAL);
	hdr = msg;
	type = le16_to_cpu(hdr->type);

	switch (type) {
	case RTRS_MSG_WRITE:
		process_write(con, msg, id, off);
		break;
	case RTRS_MSG_READ:
		process_read(con, msg, id, off);
		break;
	default:
		rtrs_err(s,
			  "Processing I/O request failed, unknown message type received: 0x%02x\n",
			  type);
		goto err;
	}

	return;

err:
	close_path(srv_path);
}

static void rtrs_srv_inv_rkey_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct rtrs_srv_mr *mr =
		container_of(wc->wr_cqe, typeof(*mr), inv_cqe);
	struct rtrs_srv_con *con = to_srv_con(wc->qp->qp_context);
	struct rtrs_path *s = con->c.path;
	struct rtrs_srv_path *srv_path = to_srv_path(s);
	struct rtrs_srv_sess *srv = srv_path->srv;
	u32 msg_id, off;
	void *data;

	if (wc->status != IB_WC_SUCCESS) {
		rtrs_err(s, "Failed IB_WR_LOCAL_INV: %s\n",
			  ib_wc_status_msg(wc->status));
		close_path(srv_path);
	}
	msg_id = mr->msg_id;
	off = mr->msg_off;
	data = page_address(srv->chunks[msg_id]) + off;
	process_io_req(con, data, msg_id, off);
}

static int rtrs_srv_inv_rkey(struct rtrs_srv_con *con,
			      struct rtrs_srv_mr *mr)
{
	struct ib_send_wr wr = {
		.opcode		    = IB_WR_LOCAL_INV,
		.wr_cqe		    = &mr->inv_cqe,
		.send_flags	    = IB_SEND_SIGNALED,
		.ex.invalidate_rkey = mr->mr->rkey,
	};
	mr->inv_cqe.done = rtrs_srv_inv_rkey_done;

	return ib_post_send(con->c.qp, &wr, NULL);
}

static void rtrs_rdma_process_wr_wait_list(struct rtrs_srv_con *con)
{
	spin_lock(&con->rsp_wr_wait_lock);
	while (!list_empty(&con->rsp_wr_wait_list)) {
		struct rtrs_srv_op *id;
		int ret;

		id = list_entry(con->rsp_wr_wait_list.next,
				struct rtrs_srv_op, wait_list);
		list_del(&id->wait_list);

		spin_unlock(&con->rsp_wr_wait_lock);
		ret = rtrs_srv_resp_rdma(id, id->status);
		spin_lock(&con->rsp_wr_wait_lock);

		if (!ret) {
			list_add(&id->wait_list, &con->rsp_wr_wait_list);
			break;
		}
	}
	spin_unlock(&con->rsp_wr_wait_lock);
}

static void rtrs_srv_rdma_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct rtrs_srv_con *con = to_srv_con(wc->qp->qp_context);
	struct rtrs_path *s = con->c.path;
	struct rtrs_srv_path *srv_path = to_srv_path(s);
	struct rtrs_srv_sess *srv = srv_path->srv;
	u32 imm_type, imm_payload;
	int err;

	if (wc->status != IB_WC_SUCCESS) {
		if (wc->status != IB_WC_WR_FLUSH_ERR) {
			rtrs_err(s,
				  "%s (wr_cqe: %p, type: %d, vendor_err: 0x%x, len: %u)\n",
				  ib_wc_status_msg(wc->status), wc->wr_cqe,
				  wc->opcode, wc->vendor_err, wc->byte_len);
			close_path(srv_path);
		}
		return;
	}

	switch (wc->opcode) {
	case IB_WC_RECV_RDMA_WITH_IMM:
		/*
		 * post_recv() RDMA write completions of IO reqs (read/write)
		 * and hb
		 */
		if (WARN_ON(wc->wr_cqe != &io_comp_cqe))
			return;
		srv_path->s.hb_missed_cnt = 0;
		err = rtrs_post_recv_empty(&con->c, &io_comp_cqe);
		if (err) {
			rtrs_err(s, "rtrs_post_recv(), err: %d\n", err);
			close_path(srv_path);
			break;
		}
		rtrs_from_imm(be32_to_cpu(wc->ex.imm_data),
			       &imm_type, &imm_payload);
		if (imm_type == RTRS_IO_REQ_IMM) {
			u32 msg_id, off;
			void *data;

			msg_id = imm_payload >> srv_path->mem_bits;
			off = imm_payload & ((1 << srv_path->mem_bits) - 1);
			if (msg_id >= srv->queue_depth || off >= max_chunk_size) {
				rtrs_err(s, "Wrong msg_id %u, off %u\n",
					  msg_id, off);
				close_path(srv_path);
				return;
			}
			if (always_invalidate) {
				struct rtrs_srv_mr *mr = &srv_path->mrs[msg_id];

				mr->msg_off = off;
				mr->msg_id = msg_id;
				err = rtrs_srv_inv_rkey(con, mr);
				if (err) {
					rtrs_err(s, "rtrs_post_recv(), err: %d\n",
						  err);
					close_path(srv_path);
					break;
				}
			} else {
				data = page_address(srv->chunks[msg_id]) + off;
				process_io_req(con, data, msg_id, off);
			}
		} else if (imm_type == RTRS_HB_MSG_IMM) {
			WARN_ON(con->c.cid);
			rtrs_send_hb_ack(&srv_path->s);
		} else if (imm_type == RTRS_HB_ACK_IMM) {
			WARN_ON(con->c.cid);
			srv_path->s.hb_missed_cnt = 0;
		} else {
			rtrs_wrn(s, "Unknown IMM type %u\n", imm_type);
		}
		break;
	case IB_WC_RDMA_WRITE:
	case IB_WC_SEND:
		/*
		 * post_send() RDMA write completions of IO reqs (read/write)
		 * and hb.
		 */
		atomic_add(s->signal_interval, &con->c.sq_wr_avail);

		if (!list_empty_careful(&con->rsp_wr_wait_list))
			rtrs_rdma_process_wr_wait_list(con);

		break;
	default:
		rtrs_wrn(s, "Unexpected WC type: %d\n", wc->opcode);
		return;
	}
}

/**
 * rtrs_srv_get_path_name() - Get rtrs_srv peer hostname.
 * @srv:	Session
 * @pathname:	Pathname buffer
 * @len:	Length of sessname buffer
 */
int rtrs_srv_get_path_name(struct rtrs_srv_sess *srv, char *pathname,
			   size_t len)
{
	struct rtrs_srv_path *srv_path;
	int err = -ENOTCONN;

	mutex_lock(&srv->paths_mutex);
	list_for_each_entry(srv_path, &srv->paths_list, s.entry) {
		if (srv_path->state != RTRS_SRV_CONNECTED)
			continue;
		strscpy(pathname, srv_path->s.sessname,
			min_t(size_t, sizeof(srv_path->s.sessname), len));
		err = 0;
		break;
	}
	mutex_unlock(&srv->paths_mutex);

	return err;
}
EXPORT_SYMBOL(rtrs_srv_get_path_name);

/**
 * rtrs_srv_get_queue_depth() - Get rtrs_srv qdepth.
 * @srv:	Session
 */
int rtrs_srv_get_queue_depth(struct rtrs_srv_sess *srv)
{
	return srv->queue_depth;
}
EXPORT_SYMBOL(rtrs_srv_get_queue_depth);

static int find_next_bit_ring(struct rtrs_srv_path *srv_path)
{
	struct ib_device *ib_dev = srv_path->s.dev->ib_dev;
	int v;

	v = cpumask_next(srv_path->cur_cq_vector, &cq_affinity_mask);
	if (v >= nr_cpu_ids || v >= ib_dev->num_comp_vectors)
		v = cpumask_first(&cq_affinity_mask);
	return v;
}

static int rtrs_srv_get_next_cq_vector(struct rtrs_srv_path *srv_path)
{
	srv_path->cur_cq_vector = find_next_bit_ring(srv_path);

	return srv_path->cur_cq_vector;
}

static void rtrs_srv_dev_release(struct device *dev)
{
	struct rtrs_srv_sess *srv = container_of(dev, struct rtrs_srv_sess,
						 dev);

	kfree(srv);
}

static void free_srv(struct rtrs_srv_sess *srv)
{
	int i;

	WARN_ON(refcount_read(&srv->refcount));
	for (i = 0; i < srv->queue_depth; i++)
		__free_pages(srv->chunks[i], get_order(max_chunk_size));
	kfree(srv->chunks);
	mutex_destroy(&srv->paths_mutex);
	mutex_destroy(&srv->paths_ev_mutex);
	/* last put to release the srv structure */
	put_device(&srv->dev);
}

static struct rtrs_srv_sess *get_or_create_srv(struct rtrs_srv_ctx *ctx,
					  const uuid_t *paths_uuid,
					  bool first_conn)
{
	struct rtrs_srv_sess *srv;
	int i;

	mutex_lock(&ctx->srv_mutex);
	list_for_each_entry(srv, &ctx->srv_list, ctx_list) {
		if (uuid_equal(&srv->paths_uuid, paths_uuid) &&
		    refcount_inc_not_zero(&srv->refcount)) {
			mutex_unlock(&ctx->srv_mutex);
			return srv;
		}
	}
	mutex_unlock(&ctx->srv_mutex);
	/*
	 * If this request is not the first connection request from the
	 * client for this session then fail and return error.
	 */
	if (!first_conn) {
		pr_err_ratelimited("Error: Not the first connection request for this session\n");
		return ERR_PTR(-ENXIO);
	}

	/* need to allocate a new srv */
	srv = kzalloc(sizeof(*srv), GFP_KERNEL);
	if  (!srv)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&srv->paths_list);
	mutex_init(&srv->paths_mutex);
	mutex_init(&srv->paths_ev_mutex);
	uuid_copy(&srv->paths_uuid, paths_uuid);
	srv->queue_depth = sess_queue_depth;
	srv->ctx = ctx;
	device_initialize(&srv->dev);
	srv->dev.release = rtrs_srv_dev_release;

	srv->chunks = kcalloc(srv->queue_depth, sizeof(*srv->chunks),
			      GFP_KERNEL);
	if (!srv->chunks)
		goto err_free_srv;

	for (i = 0; i < srv->queue_depth; i++) {
		srv->chunks[i] = alloc_pages(GFP_KERNEL,
					     get_order(max_chunk_size));
		if (!srv->chunks[i])
			goto err_free_chunks;
	}
	refcount_set(&srv->refcount, 1);
	mutex_lock(&ctx->srv_mutex);
	list_add(&srv->ctx_list, &ctx->srv_list);
	mutex_unlock(&ctx->srv_mutex);

	return srv;

err_free_chunks:
	while (i--)
		__free_pages(srv->chunks[i], get_order(max_chunk_size));
	kfree(srv->chunks);

err_free_srv:
	kfree(srv);
	return ERR_PTR(-ENOMEM);
}

static void put_srv(struct rtrs_srv_sess *srv)
{
	if (refcount_dec_and_test(&srv->refcount)) {
		struct rtrs_srv_ctx *ctx = srv->ctx;

		WARN_ON(srv->dev.kobj.state_in_sysfs);

		mutex_lock(&ctx->srv_mutex);
		list_del(&srv->ctx_list);
		mutex_unlock(&ctx->srv_mutex);
		free_srv(srv);
	}
}

static void __add_path_to_srv(struct rtrs_srv_sess *srv,
			      struct rtrs_srv_path *srv_path)
{
	list_add_tail(&srv_path->s.entry, &srv->paths_list);
	srv->paths_num++;
	WARN_ON(srv->paths_num >= MAX_PATHS_NUM);
}

static void del_path_from_srv(struct rtrs_srv_path *srv_path)
{
	struct rtrs_srv_sess *srv = srv_path->srv;

	if (WARN_ON(!srv))
		return;

	mutex_lock(&srv->paths_mutex);
	list_del(&srv_path->s.entry);
	WARN_ON(!srv->paths_num);
	srv->paths_num--;
	mutex_unlock(&srv->paths_mutex);
}

/* return true if addresses are the same, error other wise */
static int sockaddr_cmp(const struct sockaddr *a, const struct sockaddr *b)
{
	switch (a->sa_family) {
	case AF_IB:
		return memcmp(&((struct sockaddr_ib *)a)->sib_addr,
			      &((struct sockaddr_ib *)b)->sib_addr,
			      sizeof(struct ib_addr)) &&
			(b->sa_family == AF_IB);
	case AF_INET:
		return memcmp(&((struct sockaddr_in *)a)->sin_addr,
			      &((struct sockaddr_in *)b)->sin_addr,
			      sizeof(struct in_addr)) &&
			(b->sa_family == AF_INET);
	case AF_INET6:
		return memcmp(&((struct sockaddr_in6 *)a)->sin6_addr,
			      &((struct sockaddr_in6 *)b)->sin6_addr,
			      sizeof(struct in6_addr)) &&
			(b->sa_family == AF_INET6);
	default:
		return -ENOENT;
	}
}

static bool __is_path_w_addr_exists(struct rtrs_srv_sess *srv,
				    struct rdma_addr *addr)
{
	struct rtrs_srv_path *srv_path;

	list_for_each_entry(srv_path, &srv->paths_list, s.entry)
		if (!sockaddr_cmp((struct sockaddr *)&srv_path->s.dst_addr,
				  (struct sockaddr *)&addr->dst_addr) &&
		    !sockaddr_cmp((struct sockaddr *)&srv_path->s.src_addr,
				  (struct sockaddr *)&addr->src_addr))
			return true;

	return false;
}

static void free_path(struct rtrs_srv_path *srv_path)
{
	if (srv_path->kobj.state_in_sysfs) {
		kobject_del(&srv_path->kobj);
		kobject_put(&srv_path->kobj);
	} else {
		free_percpu(srv_path->stats->rdma_stats);
		kfree(srv_path->stats);
		kfree(srv_path);
	}
}

static void rtrs_srv_close_work(struct work_struct *work)
{
	struct rtrs_srv_path *srv_path;
	struct rtrs_srv_con *con;
	int i;

	srv_path = container_of(work, typeof(*srv_path), close_work);

	rtrs_srv_stop_hb(srv_path);

	for (i = 0; i < srv_path->s.con_num; i++) {
		if (!srv_path->s.con[i])
			continue;
		con = to_srv_con(srv_path->s.con[i]);
		rdma_disconnect(con->c.cm_id);
		ib_drain_qp(con->c.qp);
	}

	/*
	 * Degrade ref count to the usual model with a single shared
	 * atomic_t counter
	 */
	percpu_ref_kill(&srv_path->ids_inflight_ref);

	/* Wait for all completion */
	wait_for_completion(&srv_path->complete_done);

	rtrs_srv_destroy_path_files(srv_path);

	/* Notify upper layer if we are the last path */
	rtrs_srv_path_down(srv_path);

	unmap_cont_bufs(srv_path);
	rtrs_srv_free_ops_ids(srv_path);

	for (i = 0; i < srv_path->s.con_num; i++) {
		if (!srv_path->s.con[i])
			continue;
		con = to_srv_con(srv_path->s.con[i]);
		rtrs_cq_qp_destroy(&con->c);
		rdma_destroy_id(con->c.cm_id);
		kfree(con);
	}
	rtrs_ib_dev_put(srv_path->s.dev);

	del_path_from_srv(srv_path);
	put_srv(srv_path->srv);
	srv_path->srv = NULL;
	rtrs_srv_change_state(srv_path, RTRS_SRV_CLOSED);

	kfree(srv_path->dma_addr);
	kfree(srv_path->s.con);
	free_path(srv_path);
}

static int rtrs_rdma_do_accept(struct rtrs_srv_path *srv_path,
			       struct rdma_cm_id *cm_id)
{
	struct rtrs_srv_sess *srv = srv_path->srv;
	struct rtrs_msg_conn_rsp msg;
	struct rdma_conn_param param;
	int err;

	param = (struct rdma_conn_param) {
		.rnr_retry_count = 7,
		.private_data = &msg,
		.private_data_len = sizeof(msg),
	};

	msg = (struct rtrs_msg_conn_rsp) {
		.magic = cpu_to_le16(RTRS_MAGIC),
		.version = cpu_to_le16(RTRS_PROTO_VER),
		.queue_depth = cpu_to_le16(srv->queue_depth),
		.max_io_size = cpu_to_le32(max_chunk_size - MAX_HDR_SIZE),
		.max_hdr_size = cpu_to_le32(MAX_HDR_SIZE),
	};

	if (always_invalidate)
		msg.flags = cpu_to_le32(RTRS_MSG_NEW_RKEY_F);

	err = rdma_accept(cm_id, &param);
	if (err)
		pr_err("rdma_accept(), err: %d\n", err);

	return err;
}

static int rtrs_rdma_do_reject(struct rdma_cm_id *cm_id, int errno)
{
	struct rtrs_msg_conn_rsp msg;
	int err;

	msg = (struct rtrs_msg_conn_rsp) {
		.magic = cpu_to_le16(RTRS_MAGIC),
		.version = cpu_to_le16(RTRS_PROTO_VER),
		.errno = cpu_to_le16(errno),
	};

	err = rdma_reject(cm_id, &msg, sizeof(msg), IB_CM_REJ_CONSUMER_DEFINED);
	if (err)
		pr_err("rdma_reject(), err: %d\n", err);

	/* Bounce errno back */
	return errno;
}

static struct rtrs_srv_path *
__find_path(struct rtrs_srv_sess *srv, const uuid_t *sess_uuid)
{
	struct rtrs_srv_path *srv_path;

	list_for_each_entry(srv_path, &srv->paths_list, s.entry) {
		if (uuid_equal(&srv_path->s.uuid, sess_uuid))
			return srv_path;
	}

	return NULL;
}

static int create_con(struct rtrs_srv_path *srv_path,
		      struct rdma_cm_id *cm_id,
		      unsigned int cid)
{
	struct rtrs_srv_sess *srv = srv_path->srv;
	struct rtrs_path *s = &srv_path->s;
	struct rtrs_srv_con *con;

	u32 cq_num, max_send_wr, max_recv_wr, wr_limit;
	int err, cq_vector;

	con = kzalloc(sizeof(*con), GFP_KERNEL);
	if (!con) {
		err = -ENOMEM;
		goto err;
	}

	spin_lock_init(&con->rsp_wr_wait_lock);
	INIT_LIST_HEAD(&con->rsp_wr_wait_list);
	con->c.cm_id = cm_id;
	con->c.path = &srv_path->s;
	con->c.cid = cid;
	atomic_set(&con->c.wr_cnt, 1);
	wr_limit = srv_path->s.dev->ib_dev->attrs.max_qp_wr;

	if (con->c.cid == 0) {
		/*
		 * All receive and all send (each requiring invalidate)
		 * + 2 for drain and heartbeat
		 */
		max_send_wr = min_t(int, wr_limit,
				    SERVICE_CON_QUEUE_DEPTH * 2 + 2);
		max_recv_wr = max_send_wr;
		s->signal_interval = min_not_zero(srv->queue_depth,
						  (size_t)SERVICE_CON_QUEUE_DEPTH);
	} else {
		/* when always_invlaidate enalbed, we need linv+rinv+mr+imm */
		if (always_invalidate)
			max_send_wr =
				min_t(int, wr_limit,
				      srv->queue_depth * (1 + 4) + 1);
		else
			max_send_wr =
				min_t(int, wr_limit,
				      srv->queue_depth * (1 + 2) + 1);

		max_recv_wr = srv->queue_depth + 1;
		/*
		 * If we have all receive requests posted and
		 * all write requests posted and each read request
		 * requires an invalidate request + drain
		 * and qp gets into error state.
		 */
	}
	cq_num = max_send_wr + max_recv_wr;
	atomic_set(&con->c.sq_wr_avail, max_send_wr);
	cq_vector = rtrs_srv_get_next_cq_vector(srv_path);

	/* TODO: SOFTIRQ can be faster, but be careful with softirq context */
	err = rtrs_cq_qp_create(&srv_path->s, &con->c, 1, cq_vector, cq_num,
				 max_send_wr, max_recv_wr,
				 IB_POLL_WORKQUEUE);
	if (err) {
		rtrs_err(s, "rtrs_cq_qp_create(), err: %d\n", err);
		goto free_con;
	}
	if (con->c.cid == 0) {
		err = post_recv_info_req(con);
		if (err)
			goto free_cqqp;
	}
	WARN_ON(srv_path->s.con[cid]);
	srv_path->s.con[cid] = &con->c;

	/*
	 * Change context from server to current connection.  The other
	 * way is to use cm_id->qp->qp_context, which does not work on OFED.
	 */
	cm_id->context = &con->c;

	return 0;

free_cqqp:
	rtrs_cq_qp_destroy(&con->c);
free_con:
	kfree(con);

err:
	return err;
}

static struct rtrs_srv_path *__alloc_path(struct rtrs_srv_sess *srv,
					   struct rdma_cm_id *cm_id,
					   unsigned int con_num,
					   unsigned int recon_cnt,
					   const uuid_t *uuid)
{
	struct rtrs_srv_path *srv_path;
	int err = -ENOMEM;
	char str[NAME_MAX];
	struct rtrs_addr path;

	if (srv->paths_num >= MAX_PATHS_NUM) {
		err = -ECONNRESET;
		goto err;
	}
	if (__is_path_w_addr_exists(srv, &cm_id->route.addr)) {
		err = -EEXIST;
		pr_err("Path with same addr exists\n");
		goto err;
	}
	srv_path = kzalloc(sizeof(*srv_path), GFP_KERNEL);
	if (!srv_path)
		goto err;

	srv_path->stats = kzalloc(sizeof(*srv_path->stats), GFP_KERNEL);
	if (!srv_path->stats)
		goto err_free_sess;

	srv_path->stats->rdma_stats = alloc_percpu(struct rtrs_srv_stats_rdma_stats);
	if (!srv_path->stats->rdma_stats)
		goto err_free_stats;

	srv_path->stats->srv_path = srv_path;

	srv_path->dma_addr = kcalloc(srv->queue_depth,
				     sizeof(*srv_path->dma_addr),
				     GFP_KERNEL);
	if (!srv_path->dma_addr)
		goto err_free_percpu;

	srv_path->s.con = kcalloc(con_num, sizeof(*srv_path->s.con),
				  GFP_KERNEL);
	if (!srv_path->s.con)
		goto err_free_dma_addr;

	srv_path->state = RTRS_SRV_CONNECTING;
	srv_path->srv = srv;
	srv_path->cur_cq_vector = -1;
	srv_path->s.dst_addr = cm_id->route.addr.dst_addr;
	srv_path->s.src_addr = cm_id->route.addr.src_addr;

	/* temporary until receiving session-name from client */
	path.src = &srv_path->s.src_addr;
	path.dst = &srv_path->s.dst_addr;
	rtrs_addr_to_str(&path, str, sizeof(str));
	strscpy(srv_path->s.sessname, str, sizeof(srv_path->s.sessname));

	srv_path->s.con_num = con_num;
	srv_path->s.irq_con_num = con_num;
	srv_path->s.recon_cnt = recon_cnt;
	uuid_copy(&srv_path->s.uuid, uuid);
	spin_lock_init(&srv_path->state_lock);
	INIT_WORK(&srv_path->close_work, rtrs_srv_close_work);
	rtrs_srv_init_hb(srv_path);

	srv_path->s.dev = rtrs_ib_dev_find_or_add(cm_id->device, &dev_pd);
	if (!srv_path->s.dev) {
		err = -ENOMEM;
		goto err_free_con;
	}
	err = map_cont_bufs(srv_path);
	if (err)
		goto err_put_dev;

	err = rtrs_srv_alloc_ops_ids(srv_path);
	if (err)
		goto err_unmap_bufs;

	__add_path_to_srv(srv, srv_path);

	return srv_path;

err_unmap_bufs:
	unmap_cont_bufs(srv_path);
err_put_dev:
	rtrs_ib_dev_put(srv_path->s.dev);
err_free_con:
	kfree(srv_path->s.con);
err_free_dma_addr:
	kfree(srv_path->dma_addr);
err_free_percpu:
	free_percpu(srv_path->stats->rdma_stats);
err_free_stats:
	kfree(srv_path->stats);
err_free_sess:
	kfree(srv_path);
err:
	return ERR_PTR(err);
}

static int rtrs_rdma_connect(struct rdma_cm_id *cm_id,
			      const struct rtrs_msg_conn_req *msg,
			      size_t len)
{
	struct rtrs_srv_ctx *ctx = cm_id->context;
	struct rtrs_srv_path *srv_path;
	struct rtrs_srv_sess *srv;

	u16 version, con_num, cid;
	u16 recon_cnt;
	int err = -ECONNRESET;

	if (len < sizeof(*msg)) {
		pr_err("Invalid RTRS connection request\n");
		goto reject_w_err;
	}
	if (le16_to_cpu(msg->magic) != RTRS_MAGIC) {
		pr_err("Invalid RTRS magic\n");
		goto reject_w_err;
	}
	version = le16_to_cpu(msg->version);
	if (version >> 8 != RTRS_PROTO_VER_MAJOR) {
		pr_err("Unsupported major RTRS version: %d, expected %d\n",
		       version >> 8, RTRS_PROTO_VER_MAJOR);
		goto reject_w_err;
	}
	con_num = le16_to_cpu(msg->cid_num);
	if (con_num > 4096) {
		/* Sanity check */
		pr_err("Too many connections requested: %d\n", con_num);
		goto reject_w_err;
	}
	cid = le16_to_cpu(msg->cid);
	if (cid >= con_num) {
		/* Sanity check */
		pr_err("Incorrect cid: %d >= %d\n", cid, con_num);
		goto reject_w_err;
	}
	recon_cnt = le16_to_cpu(msg->recon_cnt);
	srv = get_or_create_srv(ctx, &msg->paths_uuid, msg->first_conn);
	if (IS_ERR(srv)) {
		err = PTR_ERR(srv);
		pr_err("get_or_create_srv(), error %d\n", err);
		goto reject_w_err;
	}
	mutex_lock(&srv->paths_mutex);
	srv_path = __find_path(srv, &msg->sess_uuid);
	if (srv_path) {
		struct rtrs_path *s = &srv_path->s;

		/* Session already holds a reference */
		put_srv(srv);

		if (srv_path->state != RTRS_SRV_CONNECTING) {
			rtrs_err(s, "Session in wrong state: %s\n",
				  rtrs_srv_state_str(srv_path->state));
			mutex_unlock(&srv->paths_mutex);
			goto reject_w_err;
		}
		/*
		 * Sanity checks
		 */
		if (con_num != s->con_num || cid >= s->con_num) {
			rtrs_err(s, "Incorrect request: %d, %d\n",
				  cid, con_num);
			mutex_unlock(&srv->paths_mutex);
			goto reject_w_err;
		}
		if (s->con[cid]) {
			rtrs_err(s, "Connection already exists: %d\n",
				  cid);
			mutex_unlock(&srv->paths_mutex);
			goto reject_w_err;
		}
	} else {
		srv_path = __alloc_path(srv, cm_id, con_num, recon_cnt,
				    &msg->sess_uuid);
		if (IS_ERR(srv_path)) {
			mutex_unlock(&srv->paths_mutex);
			put_srv(srv);
			err = PTR_ERR(srv_path);
			pr_err("RTRS server session allocation failed: %d\n", err);
			goto reject_w_err;
		}
	}
	err = create_con(srv_path, cm_id, cid);
	if (err) {
		rtrs_err((&srv_path->s), "create_con(), error %d\n", err);
		rtrs_rdma_do_reject(cm_id, err);
		/*
		 * Since session has other connections we follow normal way
		 * through workqueue, but still return an error to tell cma.c
		 * to call rdma_destroy_id() for current connection.
		 */
		goto close_and_return_err;
	}
	err = rtrs_rdma_do_accept(srv_path, cm_id);
	if (err) {
		rtrs_err((&srv_path->s), "rtrs_rdma_do_accept(), error %d\n", err);
		rtrs_rdma_do_reject(cm_id, err);
		/*
		 * Since current connection was successfully added to the
		 * session we follow normal way through workqueue to close the
		 * session, thus return 0 to tell cma.c we call
		 * rdma_destroy_id() ourselves.
		 */
		err = 0;
		goto close_and_return_err;
	}
	mutex_unlock(&srv->paths_mutex);

	return 0;

reject_w_err:
	return rtrs_rdma_do_reject(cm_id, err);

close_and_return_err:
	mutex_unlock(&srv->paths_mutex);
	close_path(srv_path);

	return err;
}

static int rtrs_srv_rdma_cm_handler(struct rdma_cm_id *cm_id,
				     struct rdma_cm_event *ev)
{
	struct rtrs_srv_path *srv_path = NULL;
	struct rtrs_path *s = NULL;

	if (ev->event != RDMA_CM_EVENT_CONNECT_REQUEST) {
		struct rtrs_con *c = cm_id->context;

		s = c->path;
		srv_path = to_srv_path(s);
	}

	switch (ev->event) {
	case RDMA_CM_EVENT_CONNECT_REQUEST:
		/*
		 * In case of error cma.c will destroy cm_id,
		 * see cma_process_remove()
		 */
		return rtrs_rdma_connect(cm_id, ev->param.conn.private_data,
					  ev->param.conn.private_data_len);
	case RDMA_CM_EVENT_ESTABLISHED:
		/* Nothing here */
		break;
	case RDMA_CM_EVENT_REJECTED:
	case RDMA_CM_EVENT_CONNECT_ERROR:
	case RDMA_CM_EVENT_UNREACHABLE:
		rtrs_err(s, "CM error (CM event: %s, err: %d)\n",
			  rdma_event_msg(ev->event), ev->status);
		fallthrough;
	case RDMA_CM_EVENT_DISCONNECTED:
	case RDMA_CM_EVENT_ADDR_CHANGE:
	case RDMA_CM_EVENT_TIMEWAIT_EXIT:
	case RDMA_CM_EVENT_DEVICE_REMOVAL:
		close_path(srv_path);
		break;
	default:
		pr_err("Ignoring unexpected CM event %s, err %d\n",
		       rdma_event_msg(ev->event), ev->status);
		break;
	}

	return 0;
}

static struct rdma_cm_id *rtrs_srv_cm_init(struct rtrs_srv_ctx *ctx,
					    struct sockaddr *addr,
					    enum rdma_ucm_port_space ps)
{
	struct rdma_cm_id *cm_id;
	int ret;

	cm_id = rdma_create_id(&init_net, rtrs_srv_rdma_cm_handler,
			       ctx, ps, IB_QPT_RC);
	if (IS_ERR(cm_id)) {
		ret = PTR_ERR(cm_id);
		pr_err("Creating id for RDMA connection failed, err: %d\n",
		       ret);
		goto err_out;
	}
	ret = rdma_bind_addr(cm_id, addr);
	if (ret) {
		pr_err("Binding RDMA address failed, err: %d\n", ret);
		goto err_cm;
	}
	ret = rdma_listen(cm_id, 64);
	if (ret) {
		pr_err("Listening on RDMA connection failed, err: %d\n",
		       ret);
		goto err_cm;
	}

	return cm_id;

err_cm:
	rdma_destroy_id(cm_id);
err_out:

	return ERR_PTR(ret);
}

static int rtrs_srv_rdma_init(struct rtrs_srv_ctx *ctx, u16 port)
{
	struct sockaddr_in6 sin = {
		.sin6_family	= AF_INET6,
		.sin6_addr	= IN6ADDR_ANY_INIT,
		.sin6_port	= htons(port),
	};
	struct sockaddr_ib sib = {
		.sib_family			= AF_IB,
		.sib_sid	= cpu_to_be64(RDMA_IB_IP_PS_IB | port),
		.sib_sid_mask	= cpu_to_be64(0xffffffffffffffffULL),
		.sib_pkey	= cpu_to_be16(0xffff),
	};
	struct rdma_cm_id *cm_ip, *cm_ib;
	int ret;

	/*
	 * We accept both IPoIB and IB connections, so we need to keep
	 * two cm id's, one for each socket type and port space.
	 * If the cm initialization of one of the id's fails, we abort
	 * everything.
	 */
	cm_ip = rtrs_srv_cm_init(ctx, (struct sockaddr *)&sin, RDMA_PS_TCP);
	if (IS_ERR(cm_ip))
		return PTR_ERR(cm_ip);

	cm_ib = rtrs_srv_cm_init(ctx, (struct sockaddr *)&sib, RDMA_PS_IB);
	if (IS_ERR(cm_ib)) {
		ret = PTR_ERR(cm_ib);
		goto free_cm_ip;
	}

	ctx->cm_id_ip = cm_ip;
	ctx->cm_id_ib = cm_ib;

	return 0;

free_cm_ip:
	rdma_destroy_id(cm_ip);

	return ret;
}

static struct rtrs_srv_ctx *alloc_srv_ctx(struct rtrs_srv_ops *ops)
{
	struct rtrs_srv_ctx *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	ctx->ops = *ops;
	mutex_init(&ctx->srv_mutex);
	INIT_LIST_HEAD(&ctx->srv_list);

	return ctx;
}

static void free_srv_ctx(struct rtrs_srv_ctx *ctx)
{
	WARN_ON(!list_empty(&ctx->srv_list));
	mutex_destroy(&ctx->srv_mutex);
	kfree(ctx);
}

static int rtrs_srv_add_one(struct ib_device *device)
{
	struct rtrs_srv_ctx *ctx;
	int ret = 0;

	mutex_lock(&ib_ctx.ib_dev_mutex);
	if (ib_ctx.ib_dev_count)
		goto out;

	/*
	 * Since our CM IDs are NOT bound to any ib device we will create them
	 * only once
	 */
	ctx = ib_ctx.srv_ctx;
	ret = rtrs_srv_rdma_init(ctx, ib_ctx.port);
	if (ret) {
		/*
		 * We errored out here.
		 * According to the ib code, if we encounter an error here then the
		 * error code is ignored, and no more calls to our ops are made.
		 */
		pr_err("Failed to initialize RDMA connection");
		goto err_out;
	}

out:
	/*
	 * Keep a track on the number of ib devices added
	 */
	ib_ctx.ib_dev_count++;

err_out:
	mutex_unlock(&ib_ctx.ib_dev_mutex);
	return ret;
}

static void rtrs_srv_remove_one(struct ib_device *device, void *client_data)
{
	struct rtrs_srv_ctx *ctx;

	mutex_lock(&ib_ctx.ib_dev_mutex);
	ib_ctx.ib_dev_count--;

	if (ib_ctx.ib_dev_count)
		goto out;

	/*
	 * Since our CM IDs are NOT bound to any ib device we will remove them
	 * only once, when the last device is removed
	 */
	ctx = ib_ctx.srv_ctx;
	rdma_destroy_id(ctx->cm_id_ip);
	rdma_destroy_id(ctx->cm_id_ib);

out:
	mutex_unlock(&ib_ctx.ib_dev_mutex);
}

static struct ib_client rtrs_srv_client = {
	.name	= "rtrs_server",
	.add	= rtrs_srv_add_one,
	.remove	= rtrs_srv_remove_one
};

/**
 * rtrs_srv_open() - open RTRS server context
 * @ops:		callback functions
 * @port:               port to listen on
 *
 * Creates server context with specified callbacks.
 *
 * Return a valid pointer on success otherwise PTR_ERR.
 */
struct rtrs_srv_ctx *rtrs_srv_open(struct rtrs_srv_ops *ops, u16 port)
{
	struct rtrs_srv_ctx *ctx;
	int err;

	ctx = alloc_srv_ctx(ops);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	mutex_init(&ib_ctx.ib_dev_mutex);
	ib_ctx.srv_ctx = ctx;
	ib_ctx.port = port;

	err = ib_register_client(&rtrs_srv_client);
	if (err) {
		free_srv_ctx(ctx);
		return ERR_PTR(err);
	}

	return ctx;
}
EXPORT_SYMBOL(rtrs_srv_open);

static void close_paths(struct rtrs_srv_sess *srv)
{
	struct rtrs_srv_path *srv_path;

	mutex_lock(&srv->paths_mutex);
	list_for_each_entry(srv_path, &srv->paths_list, s.entry)
		close_path(srv_path);
	mutex_unlock(&srv->paths_mutex);
}

static void close_ctx(struct rtrs_srv_ctx *ctx)
{
	struct rtrs_srv_sess *srv;

	mutex_lock(&ctx->srv_mutex);
	list_for_each_entry(srv, &ctx->srv_list, ctx_list)
		close_paths(srv);
	mutex_unlock(&ctx->srv_mutex);
	flush_workqueue(rtrs_wq);
}

/**
 * rtrs_srv_close() - close RTRS server context
 * @ctx: pointer to server context
 *
 * Closes RTRS server context with all client sessions.
 */
void rtrs_srv_close(struct rtrs_srv_ctx *ctx)
{
	ib_unregister_client(&rtrs_srv_client);
	mutex_destroy(&ib_ctx.ib_dev_mutex);
	close_ctx(ctx);
	free_srv_ctx(ctx);
}
EXPORT_SYMBOL(rtrs_srv_close);

static int check_module_params(void)
{
	if (sess_queue_depth < 1 || sess_queue_depth > MAX_SESS_QUEUE_DEPTH) {
		pr_err("Invalid sess_queue_depth value %d, has to be >= %d, <= %d.\n",
		       sess_queue_depth, 1, MAX_SESS_QUEUE_DEPTH);
		return -EINVAL;
	}
	if (max_chunk_size < MIN_CHUNK_SIZE || !is_power_of_2(max_chunk_size)) {
		pr_err("Invalid max_chunk_size value %d, has to be >= %d and should be power of two.\n",
		       max_chunk_size, MIN_CHUNK_SIZE);
		return -EINVAL;
	}

	/*
	 * Check if IB immediate data size is enough to hold the mem_id and the
	 * offset inside the memory chunk
	 */
	if ((ilog2(sess_queue_depth - 1) + 1) +
	    (ilog2(max_chunk_size - 1) + 1) > MAX_IMM_PAYL_BITS) {
		pr_err("RDMA immediate size (%db) not enough to encode %d buffers of size %dB. Reduce 'sess_queue_depth' or 'max_chunk_size' parameters.\n",
		       MAX_IMM_PAYL_BITS, sess_queue_depth, max_chunk_size);
		return -EINVAL;
	}

	return 0;
}

static int __init rtrs_server_init(void)
{
	int err;

	pr_info("Loading module %s, proto %s: (max_chunk_size: %d (pure IO %ld, headers %ld) , sess_queue_depth: %d, always_invalidate: %d)\n",
		KBUILD_MODNAME, RTRS_PROTO_VER_STRING,
		max_chunk_size, max_chunk_size - MAX_HDR_SIZE, MAX_HDR_SIZE,
		sess_queue_depth, always_invalidate);

	rtrs_rdma_dev_pd_init(0, &dev_pd);

	err = check_module_params();
	if (err) {
		pr_err("Failed to load module, invalid module parameters, err: %d\n",
		       err);
		return err;
	}
	rtrs_dev_class = class_create(THIS_MODULE, "rtrs-server");
	if (IS_ERR(rtrs_dev_class)) {
		err = PTR_ERR(rtrs_dev_class);
		goto out_err;
	}
	rtrs_wq = alloc_workqueue("rtrs_server_wq", 0, 0);
	if (!rtrs_wq) {
		err = -ENOMEM;
		goto out_dev_class;
	}

	return 0;

out_dev_class:
	class_destroy(rtrs_dev_class);
out_err:
	return err;
}

static void __exit rtrs_server_exit(void)
{
	destroy_workqueue(rtrs_wq);
	class_destroy(rtrs_dev_class);
	rtrs_rdma_dev_pd_deinit(&dev_pd);
}

module_init(rtrs_server_init);
module_exit(rtrs_server_exit);
