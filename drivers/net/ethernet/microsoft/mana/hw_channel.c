// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright (c) 2021, Microsoft Corporation. */

#include "gdma.h"
#include "hw_channel.h"

static int mana_hwc_get_msg_index(struct hw_channel_context *hwc, u16 *msg_id)
{
	struct gdma_resource *r = &hwc->inflight_msg_res;
	unsigned long flags;
	u32 index;

	down(&hwc->sema);

	spin_lock_irqsave(&r->lock, flags);

	index = find_first_zero_bit(hwc->inflight_msg_res.map,
				    hwc->inflight_msg_res.size);

	bitmap_set(hwc->inflight_msg_res.map, index, 1);

	spin_unlock_irqrestore(&r->lock, flags);

	*msg_id = index;

	return 0;
}

static void mana_hwc_put_msg_index(struct hw_channel_context *hwc, u16 msg_id)
{
	struct gdma_resource *r = &hwc->inflight_msg_res;
	unsigned long flags;

	spin_lock_irqsave(&r->lock, flags);
	bitmap_clear(hwc->inflight_msg_res.map, msg_id, 1);
	spin_unlock_irqrestore(&r->lock, flags);

	up(&hwc->sema);
}

static int mana_hwc_verify_resp_msg(const struct hwc_caller_ctx *caller_ctx,
				    const struct gdma_resp_hdr *resp_msg,
				    u32 resp_len)
{
	if (resp_len < sizeof(*resp_msg))
		return -EPROTO;

	if (resp_len > caller_ctx->output_buflen)
		return -EPROTO;

	return 0;
}

static void mana_hwc_handle_resp(struct hw_channel_context *hwc, u32 resp_len,
				 const struct gdma_resp_hdr *resp_msg)
{
	struct hwc_caller_ctx *ctx;
	int err = -EPROTO;

	if (!test_bit(resp_msg->response.hwc_msg_id,
		      hwc->inflight_msg_res.map)) {
		dev_err(hwc->dev, "hwc_rx: invalid msg_id = %u\n",
			resp_msg->response.hwc_msg_id);
		return;
	}

	ctx = hwc->caller_ctx + resp_msg->response.hwc_msg_id;
	err = mana_hwc_verify_resp_msg(ctx, resp_msg, resp_len);
	if (err)
		goto out;

	ctx->status_code = resp_msg->status;

	memcpy(ctx->output_buf, resp_msg, resp_len);
out:
	ctx->error = err;
	complete(&ctx->comp_event);
}

static int mana_hwc_post_rx_wqe(const struct hwc_wq *hwc_rxq,
				struct hwc_work_request *req)
{
	struct device *dev = hwc_rxq->hwc->dev;
	struct gdma_sge *sge;
	int err;

	sge = &req->sge;
	sge->address = (u64)req->buf_sge_addr;
	sge->mem_key = hwc_rxq->msg_buf->gpa_mkey;
	sge->size = req->buf_len;

	memset(&req->wqe_req, 0, sizeof(struct gdma_wqe_request));
	req->wqe_req.sgl = sge;
	req->wqe_req.num_sge = 1;
	req->wqe_req.client_data_unit = 0;

	err = mana_gd_post_and_ring(hwc_rxq->gdma_wq, &req->wqe_req, NULL);
	if (err)
		dev_err(dev, "Failed to post WQE on HWC RQ: %d\n", err);
	return err;
}

static void mana_hwc_init_event_handler(void *ctx, struct gdma_queue *q_self,
					struct gdma_event *event)
{
	struct hw_channel_context *hwc = ctx;
	struct gdma_dev *gd = hwc->gdma_dev;
	union hwc_init_type_data type_data;
	union hwc_init_eq_id_db eq_db;
	u32 type, val;

	switch (event->type) {
	case GDMA_EQE_HWC_INIT_EQ_ID_DB:
		eq_db.as_uint32 = event->details[0];
		hwc->cq->gdma_eq->id = eq_db.eq_id;
		gd->doorbell = eq_db.doorbell;
		break;

	case GDMA_EQE_HWC_INIT_DATA:
		type_data.as_uint32 = event->details[0];
		type = type_data.type;
		val = type_data.value;

		switch (type) {
		case HWC_INIT_DATA_CQID:
			hwc->cq->gdma_cq->id = val;
			break;

		case HWC_INIT_DATA_RQID:
			hwc->rxq->gdma_wq->id = val;
			break;

		case HWC_INIT_DATA_SQID:
			hwc->txq->gdma_wq->id = val;
			break;

		case HWC_INIT_DATA_QUEUE_DEPTH:
			hwc->hwc_init_q_depth_max = (u16)val;
			break;

		case HWC_INIT_DATA_MAX_REQUEST:
			hwc->hwc_init_max_req_msg_size = val;
			break;

		case HWC_INIT_DATA_MAX_RESPONSE:
			hwc->hwc_init_max_resp_msg_size = val;
			break;

		case HWC_INIT_DATA_MAX_NUM_CQS:
			gd->gdma_context->max_num_cqs = val;
			break;

		case HWC_INIT_DATA_PDID:
			hwc->gdma_dev->pdid = val;
			break;

		case HWC_INIT_DATA_GPA_MKEY:
			hwc->rxq->msg_buf->gpa_mkey = val;
			hwc->txq->msg_buf->gpa_mkey = val;
			break;
		}

		break;

	case GDMA_EQE_HWC_INIT_DONE:
		complete(&hwc->hwc_init_eqe_comp);
		break;

	default:
		/* Ignore unknown events, which should never happen. */
		break;
	}
}

static void mana_hwc_rx_event_handler(void *ctx, u32 gdma_rxq_id,
				      const struct hwc_rx_oob *rx_oob)
{
	struct hw_channel_context *hwc = ctx;
	struct hwc_wq *hwc_rxq = hwc->rxq;
	struct hwc_work_request *rx_req;
	struct gdma_resp_hdr *resp;
	struct gdma_wqe *dma_oob;
	struct gdma_queue *rq;
	struct gdma_sge *sge;
	u64 rq_base_addr;
	u64 rx_req_idx;
	u8 *wqe;

	if (WARN_ON_ONCE(hwc_rxq->gdma_wq->id != gdma_rxq_id))
		return;

	rq = hwc_rxq->gdma_wq;
	wqe = mana_gd_get_wqe_ptr(rq, rx_oob->wqe_offset / GDMA_WQE_BU_SIZE);
	dma_oob = (struct gdma_wqe *)wqe;

	sge = (struct gdma_sge *)(wqe + 8 + dma_oob->inline_oob_size_div4 * 4);

	/* Select the RX work request for virtual address and for reposting. */
	rq_base_addr = hwc_rxq->msg_buf->mem_info.dma_handle;
	rx_req_idx = (sge->address - rq_base_addr) / hwc->max_req_msg_size;

	rx_req = &hwc_rxq->msg_buf->reqs[rx_req_idx];
	resp = (struct gdma_resp_hdr *)rx_req->buf_va;

	if (resp->response.hwc_msg_id >= hwc->num_inflight_msg) {
		dev_err(hwc->dev, "HWC RX: wrong msg_id=%u\n",
			resp->response.hwc_msg_id);
		return;
	}

	mana_hwc_handle_resp(hwc, rx_oob->tx_oob_data_size, resp);

	/* Do no longer use 'resp', because the buffer is posted to the HW
	 * in the below mana_hwc_post_rx_wqe().
	 */
	resp = NULL;

	mana_hwc_post_rx_wqe(hwc_rxq, rx_req);
}

static void mana_hwc_tx_event_handler(void *ctx, u32 gdma_txq_id,
				      const struct hwc_rx_oob *rx_oob)
{
	struct hw_channel_context *hwc = ctx;
	struct hwc_wq *hwc_txq = hwc->txq;

	WARN_ON_ONCE(!hwc_txq || hwc_txq->gdma_wq->id != gdma_txq_id);
}

static int mana_hwc_create_gdma_wq(struct hw_channel_context *hwc,
				   enum gdma_queue_type type, u64 queue_size,
				   struct gdma_queue **queue)
{
	struct gdma_queue_spec spec = {};

	if (type != GDMA_SQ && type != GDMA_RQ)
		return -EINVAL;

	spec.type = type;
	spec.monitor_avl_buf = false;
	spec.queue_size = queue_size;

	return mana_gd_create_hwc_queue(hwc->gdma_dev, &spec, queue);
}

static int mana_hwc_create_gdma_cq(struct hw_channel_context *hwc,
				   u64 queue_size,
				   void *ctx, gdma_cq_callback *cb,
				   struct gdma_queue *parent_eq,
				   struct gdma_queue **queue)
{
	struct gdma_queue_spec spec = {};

	spec.type = GDMA_CQ;
	spec.monitor_avl_buf = false;
	spec.queue_size = queue_size;
	spec.cq.context = ctx;
	spec.cq.callback = cb;
	spec.cq.parent_eq = parent_eq;

	return mana_gd_create_hwc_queue(hwc->gdma_dev, &spec, queue);
}

static int mana_hwc_create_gdma_eq(struct hw_channel_context *hwc,
				   u64 queue_size,
				   void *ctx, gdma_eq_callback *cb,
				   struct gdma_queue **queue)
{
	struct gdma_queue_spec spec = {};

	spec.type = GDMA_EQ;
	spec.monitor_avl_buf = false;
	spec.queue_size = queue_size;
	spec.eq.context = ctx;
	spec.eq.callback = cb;
	spec.eq.log2_throttle_limit = DEFAULT_LOG2_THROTTLING_FOR_ERROR_EQ;

	return mana_gd_create_hwc_queue(hwc->gdma_dev, &spec, queue);
}

static void mana_hwc_comp_event(void *ctx, struct gdma_queue *q_self)
{
	struct hwc_rx_oob comp_data = {};
	struct gdma_comp *completions;
	struct hwc_cq *hwc_cq = ctx;
	u32 comp_read, i;

	WARN_ON_ONCE(hwc_cq->gdma_cq != q_self);

	completions = hwc_cq->comp_buf;
	comp_read = mana_gd_poll_cq(q_self, completions, hwc_cq->queue_depth);
	WARN_ON_ONCE(comp_read <= 0 || comp_read > hwc_cq->queue_depth);

	for (i = 0; i < comp_read; ++i) {
		comp_data = *(struct hwc_rx_oob *)completions[i].cqe_data;

		if (completions[i].is_sq)
			hwc_cq->tx_event_handler(hwc_cq->tx_event_ctx,
						completions[i].wq_num,
						&comp_data);
		else
			hwc_cq->rx_event_handler(hwc_cq->rx_event_ctx,
						completions[i].wq_num,
						&comp_data);
	}

	mana_gd_arm_cq(q_self);
}

static void mana_hwc_destroy_cq(struct gdma_context *gc, struct hwc_cq *hwc_cq)
{
	if (!hwc_cq)
		return;

	kfree(hwc_cq->comp_buf);

	if (hwc_cq->gdma_cq)
		mana_gd_destroy_queue(gc, hwc_cq->gdma_cq);

	if (hwc_cq->gdma_eq)
		mana_gd_destroy_queue(gc, hwc_cq->gdma_eq);

	kfree(hwc_cq);
}

static int mana_hwc_create_cq(struct hw_channel_context *hwc, u16 q_depth,
			      gdma_eq_callback *callback, void *ctx,
			      hwc_rx_event_handler_t *rx_ev_hdlr,
			      void *rx_ev_ctx,
			      hwc_tx_event_handler_t *tx_ev_hdlr,
			      void *tx_ev_ctx, struct hwc_cq **hwc_cq_ptr)
{
	struct gdma_queue *eq, *cq;
	struct gdma_comp *comp_buf;
	struct hwc_cq *hwc_cq;
	u32 eq_size, cq_size;
	int err;

	eq_size = roundup_pow_of_two(GDMA_EQE_SIZE * q_depth);
	if (eq_size < MINIMUM_SUPPORTED_PAGE_SIZE)
		eq_size = MINIMUM_SUPPORTED_PAGE_SIZE;

	cq_size = roundup_pow_of_two(GDMA_CQE_SIZE * q_depth);
	if (cq_size < MINIMUM_SUPPORTED_PAGE_SIZE)
		cq_size = MINIMUM_SUPPORTED_PAGE_SIZE;

	hwc_cq = kzalloc(sizeof(*hwc_cq), GFP_KERNEL);
	if (!hwc_cq)
		return -ENOMEM;

	err = mana_hwc_create_gdma_eq(hwc, eq_size, ctx, callback, &eq);
	if (err) {
		dev_err(hwc->dev, "Failed to create HWC EQ for RQ: %d\n", err);
		goto out;
	}
	hwc_cq->gdma_eq = eq;

	err = mana_hwc_create_gdma_cq(hwc, cq_size, hwc_cq, mana_hwc_comp_event,
				      eq, &cq);
	if (err) {
		dev_err(hwc->dev, "Failed to create HWC CQ for RQ: %d\n", err);
		goto out;
	}
	hwc_cq->gdma_cq = cq;

	comp_buf = kcalloc(q_depth, sizeof(struct gdma_comp), GFP_KERNEL);
	if (!comp_buf) {
		err = -ENOMEM;
		goto out;
	}

	hwc_cq->hwc = hwc;
	hwc_cq->comp_buf = comp_buf;
	hwc_cq->queue_depth = q_depth;
	hwc_cq->rx_event_handler = rx_ev_hdlr;
	hwc_cq->rx_event_ctx = rx_ev_ctx;
	hwc_cq->tx_event_handler = tx_ev_hdlr;
	hwc_cq->tx_event_ctx = tx_ev_ctx;

	*hwc_cq_ptr = hwc_cq;
	return 0;
out:
	mana_hwc_destroy_cq(hwc->gdma_dev->gdma_context, hwc_cq);
	return err;
}

static int mana_hwc_alloc_dma_buf(struct hw_channel_context *hwc, u16 q_depth,
				  u32 max_msg_size,
				  struct hwc_dma_buf **dma_buf_ptr)
{
	struct gdma_context *gc = hwc->gdma_dev->gdma_context;
	struct hwc_work_request *hwc_wr;
	struct hwc_dma_buf *dma_buf;
	struct gdma_mem_info *gmi;
	void *virt_addr;
	u32 buf_size;
	u8 *base_pa;
	int err;
	u16 i;

	dma_buf = kzalloc(sizeof(*dma_buf) +
			  q_depth * sizeof(struct hwc_work_request),
			  GFP_KERNEL);
	if (!dma_buf)
		return -ENOMEM;

	dma_buf->num_reqs = q_depth;

	buf_size = PAGE_ALIGN(q_depth * max_msg_size);

	gmi = &dma_buf->mem_info;
	err = mana_gd_alloc_memory(gc, buf_size, gmi);
	if (err) {
		dev_err(hwc->dev, "Failed to allocate DMA buffer: %d\n", err);
		goto out;
	}

	virt_addr = dma_buf->mem_info.virt_addr;
	base_pa = (u8 *)dma_buf->mem_info.dma_handle;

	for (i = 0; i < q_depth; i++) {
		hwc_wr = &dma_buf->reqs[i];

		hwc_wr->buf_va = virt_addr + i * max_msg_size;
		hwc_wr->buf_sge_addr = base_pa + i * max_msg_size;

		hwc_wr->buf_len = max_msg_size;
	}

	*dma_buf_ptr = dma_buf;
	return 0;
out:
	kfree(dma_buf);
	return err;
}

static void mana_hwc_dealloc_dma_buf(struct hw_channel_context *hwc,
				     struct hwc_dma_buf *dma_buf)
{
	if (!dma_buf)
		return;

	mana_gd_free_memory(&dma_buf->mem_info);

	kfree(dma_buf);
}

static void mana_hwc_destroy_wq(struct hw_channel_context *hwc,
				struct hwc_wq *hwc_wq)
{
	if (!hwc_wq)
		return;

	mana_hwc_dealloc_dma_buf(hwc, hwc_wq->msg_buf);

	if (hwc_wq->gdma_wq)
		mana_gd_destroy_queue(hwc->gdma_dev->gdma_context,
				      hwc_wq->gdma_wq);

	kfree(hwc_wq);
}

static int mana_hwc_create_wq(struct hw_channel_context *hwc,
			      enum gdma_queue_type q_type, u16 q_depth,
			      u32 max_msg_size, struct hwc_cq *hwc_cq,
			      struct hwc_wq **hwc_wq_ptr)
{
	struct gdma_queue *queue;
	struct hwc_wq *hwc_wq;
	u32 queue_size;
	int err;

	WARN_ON(q_type != GDMA_SQ && q_type != GDMA_RQ);

	if (q_type == GDMA_RQ)
		queue_size = roundup_pow_of_two(GDMA_MAX_RQE_SIZE * q_depth);
	else
		queue_size = roundup_pow_of_two(GDMA_MAX_SQE_SIZE * q_depth);

	if (queue_size < MINIMUM_SUPPORTED_PAGE_SIZE)
		queue_size = MINIMUM_SUPPORTED_PAGE_SIZE;

	hwc_wq = kzalloc(sizeof(*hwc_wq), GFP_KERNEL);
	if (!hwc_wq)
		return -ENOMEM;

	err = mana_hwc_create_gdma_wq(hwc, q_type, queue_size, &queue);
	if (err)
		goto out;

	err = mana_hwc_alloc_dma_buf(hwc, q_depth, max_msg_size,
				     &hwc_wq->msg_buf);
	if (err)
		goto out;

	hwc_wq->hwc = hwc;
	hwc_wq->gdma_wq = queue;
	hwc_wq->queue_depth = q_depth;
	hwc_wq->hwc_cq = hwc_cq;

	*hwc_wq_ptr = hwc_wq;
	return 0;
out:
	if (err)
		mana_hwc_destroy_wq(hwc, hwc_wq);
	return err;
}

static int mana_hwc_post_tx_wqe(const struct hwc_wq *hwc_txq,
				struct hwc_work_request *req,
				u32 dest_virt_rq_id, u32 dest_virt_rcq_id,
				bool dest_pf)
{
	struct device *dev = hwc_txq->hwc->dev;
	struct hwc_tx_oob *tx_oob;
	struct gdma_sge *sge;
	int err;

	if (req->msg_size == 0 || req->msg_size > req->buf_len) {
		dev_err(dev, "wrong msg_size: %u, buf_len: %u\n",
			req->msg_size, req->buf_len);
		return -EINVAL;
	}

	tx_oob = &req->tx_oob;

	tx_oob->vrq_id = dest_virt_rq_id;
	tx_oob->dest_vfid = 0;
	tx_oob->vrcq_id = dest_virt_rcq_id;
	tx_oob->vscq_id = hwc_txq->hwc_cq->gdma_cq->id;
	tx_oob->loopback = false;
	tx_oob->lso_override = false;
	tx_oob->dest_pf = dest_pf;
	tx_oob->vsq_id = hwc_txq->gdma_wq->id;

	sge = &req->sge;
	sge->address = (u64)req->buf_sge_addr;
	sge->mem_key = hwc_txq->msg_buf->gpa_mkey;
	sge->size = req->msg_size;

	memset(&req->wqe_req, 0, sizeof(struct gdma_wqe_request));
	req->wqe_req.sgl = sge;
	req->wqe_req.num_sge = 1;
	req->wqe_req.inline_oob_size = sizeof(struct hwc_tx_oob);
	req->wqe_req.inline_oob_data = tx_oob;
	req->wqe_req.client_data_unit = 0;

	err = mana_gd_post_and_ring(hwc_txq->gdma_wq, &req->wqe_req, NULL);
	if (err)
		dev_err(dev, "Failed to post WQE on HWC SQ: %d\n", err);
	return err;
}

static int mana_hwc_init_inflight_msg(struct hw_channel_context *hwc,
				      u16 num_msg)
{
	int err;

	sema_init(&hwc->sema, num_msg);

	err = mana_gd_alloc_res_map(num_msg, &hwc->inflight_msg_res);
	if (err)
		dev_err(hwc->dev, "Failed to init inflight_msg_res: %d\n", err);
	return err;
}

static int mana_hwc_test_channel(struct hw_channel_context *hwc, u16 q_depth,
				 u32 max_req_msg_size, u32 max_resp_msg_size)
{
	struct gdma_context *gc = hwc->gdma_dev->gdma_context;
	struct hwc_wq *hwc_rxq = hwc->rxq;
	struct hwc_work_request *req;
	struct hwc_caller_ctx *ctx;
	int err;
	int i;

	/* Post all WQEs on the RQ */
	for (i = 0; i < q_depth; i++) {
		req = &hwc_rxq->msg_buf->reqs[i];
		err = mana_hwc_post_rx_wqe(hwc_rxq, req);
		if (err)
			return err;
	}

	ctx = kzalloc(q_depth * sizeof(struct hwc_caller_ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	for (i = 0; i < q_depth; ++i)
		init_completion(&ctx[i].comp_event);

	hwc->caller_ctx = ctx;

	return mana_gd_test_eq(gc, hwc->cq->gdma_eq);
}

static int mana_hwc_establish_channel(struct gdma_context *gc, u16 *q_depth,
				      u32 *max_req_msg_size,
				      u32 *max_resp_msg_size)
{
	struct hw_channel_context *hwc = gc->hwc.driver_data;
	struct gdma_queue *rq = hwc->rxq->gdma_wq;
	struct gdma_queue *sq = hwc->txq->gdma_wq;
	struct gdma_queue *eq = hwc->cq->gdma_eq;
	struct gdma_queue *cq = hwc->cq->gdma_cq;
	int err;

	init_completion(&hwc->hwc_init_eqe_comp);

	err = mana_smc_setup_hwc(&gc->shm_channel, false,
				 eq->mem_info.dma_handle,
				 cq->mem_info.dma_handle,
				 rq->mem_info.dma_handle,
				 sq->mem_info.dma_handle,
				 eq->eq.msix_index);
	if (err)
		return err;

	if (!wait_for_completion_timeout(&hwc->hwc_init_eqe_comp, 60 * HZ))
		return -ETIMEDOUT;

	*q_depth = hwc->hwc_init_q_depth_max;
	*max_req_msg_size = hwc->hwc_init_max_req_msg_size;
	*max_resp_msg_size = hwc->hwc_init_max_resp_msg_size;

	if (WARN_ON(cq->id >= gc->max_num_cqs))
		return -EPROTO;

	gc->cq_table = vzalloc(gc->max_num_cqs * sizeof(struct gdma_queue *));
	if (!gc->cq_table)
		return -ENOMEM;

	gc->cq_table[cq->id] = cq;

	return 0;
}

static int mana_hwc_init_queues(struct hw_channel_context *hwc, u16 q_depth,
				u32 max_req_msg_size, u32 max_resp_msg_size)
{
	struct hwc_wq *hwc_rxq = NULL;
	struct hwc_wq *hwc_txq = NULL;
	struct hwc_cq *hwc_cq = NULL;
	int err;

	err = mana_hwc_init_inflight_msg(hwc, q_depth);
	if (err)
		return err;

	/* CQ is shared by SQ and RQ, so CQ's queue depth is the sum of SQ
	 * queue depth and RQ queue depth.
	 */
	err = mana_hwc_create_cq(hwc, q_depth * 2,
				 mana_hwc_init_event_handler, hwc,
				 mana_hwc_rx_event_handler, hwc,
				 mana_hwc_tx_event_handler, hwc, &hwc_cq);
	if (err) {
		dev_err(hwc->dev, "Failed to create HWC CQ: %d\n", err);
		goto out;
	}
	hwc->cq = hwc_cq;

	err = mana_hwc_create_wq(hwc, GDMA_RQ, q_depth, max_req_msg_size,
				 hwc_cq, &hwc_rxq);
	if (err) {
		dev_err(hwc->dev, "Failed to create HWC RQ: %d\n", err);
		goto out;
	}
	hwc->rxq = hwc_rxq;

	err = mana_hwc_create_wq(hwc, GDMA_SQ, q_depth, max_resp_msg_size,
				 hwc_cq, &hwc_txq);
	if (err) {
		dev_err(hwc->dev, "Failed to create HWC SQ: %d\n", err);
		goto out;
	}
	hwc->txq = hwc_txq;

	hwc->num_inflight_msg = q_depth;
	hwc->max_req_msg_size = max_req_msg_size;

	return 0;
out:
	if (hwc_txq)
		mana_hwc_destroy_wq(hwc, hwc_txq);

	if (hwc_rxq)
		mana_hwc_destroy_wq(hwc, hwc_rxq);

	if (hwc_cq)
		mana_hwc_destroy_cq(hwc->gdma_dev->gdma_context, hwc_cq);

	mana_gd_free_res_map(&hwc->inflight_msg_res);
	return err;
}

int mana_hwc_create_channel(struct gdma_context *gc)
{
	u32 max_req_msg_size, max_resp_msg_size;
	struct gdma_dev *gd = &gc->hwc;
	struct hw_channel_context *hwc;
	u16 q_depth_max;
	int err;

	hwc = kzalloc(sizeof(*hwc), GFP_KERNEL);
	if (!hwc)
		return -ENOMEM;

	gd->gdma_context = gc;
	gd->driver_data = hwc;
	hwc->gdma_dev = gd;
	hwc->dev = gc->dev;

	/* HWC's instance number is always 0. */
	gd->dev_id.as_uint32 = 0;
	gd->dev_id.type = GDMA_DEVICE_HWC;

	gd->pdid = INVALID_PDID;
	gd->doorbell = INVALID_DOORBELL;

	err = mana_hwc_init_queues(hwc, HW_CHANNEL_VF_BOOTSTRAP_QUEUE_DEPTH,
				   HW_CHANNEL_MAX_REQUEST_SIZE,
				   HW_CHANNEL_MAX_RESPONSE_SIZE);
	if (err) {
		dev_err(hwc->dev, "Failed to initialize HWC: %d\n", err);
		goto out;
	}

	err = mana_hwc_establish_channel(gc, &q_depth_max, &max_req_msg_size,
					 &max_resp_msg_size);
	if (err) {
		dev_err(hwc->dev, "Failed to establish HWC: %d\n", err);
		goto out;
	}

	err = mana_hwc_test_channel(gc->hwc.driver_data,
				    HW_CHANNEL_VF_BOOTSTRAP_QUEUE_DEPTH,
				    max_req_msg_size, max_resp_msg_size);
	if (err) {
		dev_err(hwc->dev, "Failed to test HWC: %d\n", err);
		goto out;
	}

	return 0;
out:
	kfree(hwc);
	return err;
}

void mana_hwc_destroy_channel(struct gdma_context *gc)
{
	struct hw_channel_context *hwc = gc->hwc.driver_data;
	struct hwc_caller_ctx *ctx;

	mana_smc_teardown_hwc(&gc->shm_channel, false);

	ctx = hwc->caller_ctx;
	kfree(ctx);
	hwc->caller_ctx = NULL;

	mana_hwc_destroy_wq(hwc, hwc->txq);
	hwc->txq = NULL;

	mana_hwc_destroy_wq(hwc, hwc->rxq);
	hwc->rxq = NULL;

	mana_hwc_destroy_cq(hwc->gdma_dev->gdma_context, hwc->cq);
	hwc->cq = NULL;

	mana_gd_free_res_map(&hwc->inflight_msg_res);

	hwc->num_inflight_msg = 0;

	if (hwc->gdma_dev->pdid != INVALID_PDID) {
		hwc->gdma_dev->doorbell = INVALID_DOORBELL;
		hwc->gdma_dev->pdid = INVALID_PDID;
	}

	kfree(hwc);
	gc->hwc.driver_data = NULL;
	gc->hwc.gdma_context = NULL;
}

int mana_hwc_send_request(struct hw_channel_context *hwc, u32 req_len,
			  const void *req, u32 resp_len, void *resp)
{
	struct hwc_work_request *tx_wr;
	struct hwc_wq *txq = hwc->txq;
	struct gdma_req_hdr *req_msg;
	struct hwc_caller_ctx *ctx;
	u16 msg_id;
	int err;

	mana_hwc_get_msg_index(hwc, &msg_id);

	tx_wr = &txq->msg_buf->reqs[msg_id];

	if (req_len > tx_wr->buf_len) {
		dev_err(hwc->dev, "HWC: req msg size: %d > %d\n", req_len,
			tx_wr->buf_len);
		err = -EINVAL;
		goto out;
	}

	ctx = hwc->caller_ctx + msg_id;
	ctx->output_buf = resp;
	ctx->output_buflen = resp_len;

	req_msg = (struct gdma_req_hdr *)tx_wr->buf_va;
	if (req)
		memcpy(req_msg, req, req_len);

	req_msg->req.hwc_msg_id = msg_id;

	tx_wr->msg_size = req_len;

	err = mana_hwc_post_tx_wqe(txq, tx_wr, 0, 0, false);
	if (err) {
		dev_err(hwc->dev, "HWC: Failed to post send WQE: %d\n", err);
		goto out;
	}

	if (!wait_for_completion_timeout(&ctx->comp_event, 30 * HZ)) {
		dev_err(hwc->dev, "HWC: Request timed out!\n");
		err = -ETIMEDOUT;
		goto out;
	}

	if (ctx->error) {
		err = ctx->error;
		goto out;
	}

	if (ctx->status_code) {
		dev_err(hwc->dev, "HWC: Failed hw_channel req: 0x%x\n",
			ctx->status_code);
		err = -EPROTO;
		goto out;
	}
out:
	mana_hwc_put_msg_index(hwc, msg_id);
	return err;
}
