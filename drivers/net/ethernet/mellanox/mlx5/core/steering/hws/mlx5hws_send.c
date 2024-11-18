// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#include "mlx5hws_internal.h"
#include "lib/clock.h"

enum { CQ_OK = 0, CQ_EMPTY = -1, CQ_POLL_ERR = -2 };

struct mlx5hws_send_ring_dep_wqe *
mlx5hws_send_add_new_dep_wqe(struct mlx5hws_send_engine *queue)
{
	struct mlx5hws_send_ring_sq *send_sq = &queue->send_ring.send_sq;
	unsigned int idx = send_sq->head_dep_idx++ & (queue->num_entries - 1);

	memset(&send_sq->dep_wqe[idx].wqe_data.tag, 0, MLX5HWS_MATCH_TAG_SZ);

	return &send_sq->dep_wqe[idx];
}

void mlx5hws_send_abort_new_dep_wqe(struct mlx5hws_send_engine *queue)
{
	queue->send_ring.send_sq.head_dep_idx--;
}

void mlx5hws_send_all_dep_wqe(struct mlx5hws_send_engine *queue)
{
	struct mlx5hws_send_ring_sq *send_sq = &queue->send_ring.send_sq;
	struct mlx5hws_send_ste_attr ste_attr = {0};
	struct mlx5hws_send_ring_dep_wqe *dep_wqe;

	ste_attr.send_attr.opmod = MLX5HWS_WQE_GTA_OPMOD_STE;
	ste_attr.send_attr.opcode = MLX5HWS_WQE_OPCODE_TBL_ACCESS;
	ste_attr.send_attr.len = MLX5HWS_WQE_SZ_GTA_CTRL + MLX5HWS_WQE_SZ_GTA_DATA;
	ste_attr.gta_opcode = MLX5HWS_WQE_GTA_OP_ACTIVATE;

	/* Fence first from previous depend WQEs  */
	ste_attr.send_attr.fence = 1;

	while (send_sq->head_dep_idx != send_sq->tail_dep_idx) {
		dep_wqe = &send_sq->dep_wqe[send_sq->tail_dep_idx++ & (queue->num_entries - 1)];

		/* Notify HW on the last WQE */
		ste_attr.send_attr.notify_hw = (send_sq->tail_dep_idx == send_sq->head_dep_idx);
		ste_attr.send_attr.user_data = dep_wqe->user_data;
		ste_attr.send_attr.rule = dep_wqe->rule;

		ste_attr.rtc_0 = dep_wqe->rtc_0;
		ste_attr.rtc_1 = dep_wqe->rtc_1;
		ste_attr.retry_rtc_0 = dep_wqe->retry_rtc_0;
		ste_attr.retry_rtc_1 = dep_wqe->retry_rtc_1;
		ste_attr.used_id_rtc_0 = &dep_wqe->rule->rtc_0;
		ste_attr.used_id_rtc_1 = &dep_wqe->rule->rtc_1;
		ste_attr.wqe_ctrl = &dep_wqe->wqe_ctrl;
		ste_attr.wqe_data = &dep_wqe->wqe_data;
		ste_attr.direct_index = dep_wqe->direct_index;

		mlx5hws_send_ste(queue, &ste_attr);

		/* Fencing is done only on the first WQE */
		ste_attr.send_attr.fence = 0;
	}
}

struct mlx5hws_send_engine_post_ctrl
mlx5hws_send_engine_post_start(struct mlx5hws_send_engine *queue)
{
	struct mlx5hws_send_engine_post_ctrl ctrl;

	ctrl.queue = queue;
	/* Currently only one send ring is supported */
	ctrl.send_ring = &queue->send_ring;
	ctrl.num_wqebbs = 0;

	return ctrl;
}

void mlx5hws_send_engine_post_req_wqe(struct mlx5hws_send_engine_post_ctrl *ctrl,
				      char **buf, size_t *len)
{
	struct mlx5hws_send_ring_sq *send_sq = &ctrl->send_ring->send_sq;
	unsigned int idx;

	idx = (send_sq->cur_post + ctrl->num_wqebbs) & send_sq->buf_mask;

	/* Note that *buf is a single MLX5_SEND_WQE_BB. It cannot be used
	 * as buffer of more than one WQE_BB, since the two MLX5_SEND_WQE_BB
	 * can be on 2 different kernel memory pages.
	 */
	*buf = mlx5_wq_cyc_get_wqe(&send_sq->wq, idx);
	*len = MLX5_SEND_WQE_BB;

	if (!ctrl->num_wqebbs) {
		*buf += sizeof(struct mlx5hws_wqe_ctrl_seg);
		*len -= sizeof(struct mlx5hws_wqe_ctrl_seg);
	}

	ctrl->num_wqebbs++;
}

static void hws_send_engine_post_ring(struct mlx5hws_send_ring_sq *sq,
				      struct mlx5hws_wqe_ctrl_seg *doorbell_cseg)
{
	/* ensure wqe is visible to device before updating doorbell record */
	dma_wmb();

	*sq->wq.db = cpu_to_be32(sq->cur_post);

	/* ensure doorbell record is visible to device before ringing the
	 * doorbell
	 */
	wmb();

	mlx5_write64((__be32 *)doorbell_cseg, sq->uar_map);

	/* Ensure doorbell is written on uar_page before poll_cq */
	WRITE_ONCE(doorbell_cseg, NULL);
}

static void
hws_send_wqe_set_tag(struct mlx5hws_wqe_gta_data_seg_ste *wqe_data,
		     struct mlx5hws_rule_match_tag *tag,
		     bool is_jumbo)
{
	if (is_jumbo) {
		/* Clear previous possibly dirty control */
		memset(wqe_data, 0, MLX5HWS_STE_CTRL_SZ);
		memcpy(wqe_data->jumbo, tag->jumbo, MLX5HWS_JUMBO_TAG_SZ);
	} else {
		/* Clear previous possibly dirty control and actions */
		memset(wqe_data, 0, MLX5HWS_STE_CTRL_SZ + MLX5HWS_ACTIONS_SZ);
		memcpy(wqe_data->tag, tag->match, MLX5HWS_MATCH_TAG_SZ);
	}
}

void mlx5hws_send_engine_post_end(struct mlx5hws_send_engine_post_ctrl *ctrl,
				  struct mlx5hws_send_engine_post_attr *attr)
{
	struct mlx5hws_wqe_ctrl_seg *wqe_ctrl;
	struct mlx5hws_send_ring_sq *sq;
	unsigned int idx;
	u32 flags = 0;

	sq = &ctrl->send_ring->send_sq;
	idx = sq->cur_post & sq->buf_mask;
	sq->last_idx = idx;

	wqe_ctrl = mlx5_wq_cyc_get_wqe(&sq->wq, idx);

	wqe_ctrl->opmod_idx_opcode =
		cpu_to_be32((attr->opmod << 24) |
			    ((sq->cur_post & 0xffff) << 8) |
			    attr->opcode);
	wqe_ctrl->qpn_ds =
		cpu_to_be32((attr->len + sizeof(struct mlx5hws_wqe_ctrl_seg)) / 16 |
				 sq->sqn << 8);
	wqe_ctrl->imm = cpu_to_be32(attr->id);

	flags |= attr->notify_hw ? MLX5_WQE_CTRL_CQ_UPDATE : 0;
	flags |= attr->fence ? MLX5_WQE_CTRL_INITIATOR_SMALL_FENCE : 0;
	wqe_ctrl->flags = cpu_to_be32(flags);

	sq->wr_priv[idx].id = attr->id;
	sq->wr_priv[idx].retry_id = attr->retry_id;

	sq->wr_priv[idx].rule = attr->rule;
	sq->wr_priv[idx].user_data = attr->user_data;
	sq->wr_priv[idx].num_wqebbs = ctrl->num_wqebbs;

	if (attr->rule) {
		sq->wr_priv[idx].rule->pending_wqes++;
		sq->wr_priv[idx].used_id = attr->used_id;
	}

	sq->cur_post += ctrl->num_wqebbs;

	if (attr->notify_hw)
		hws_send_engine_post_ring(sq, wqe_ctrl);
}

static void hws_send_wqe(struct mlx5hws_send_engine *queue,
			 struct mlx5hws_send_engine_post_attr *send_attr,
			 struct mlx5hws_wqe_gta_ctrl_seg *send_wqe_ctrl,
			 void *send_wqe_data,
			 void *send_wqe_tag,
			 bool is_jumbo,
			 u8 gta_opcode,
			 u32 direct_index)
{
	struct mlx5hws_wqe_gta_data_seg_ste *wqe_data;
	struct mlx5hws_wqe_gta_ctrl_seg *wqe_ctrl;
	struct mlx5hws_send_engine_post_ctrl ctrl;
	size_t wqe_len;

	ctrl = mlx5hws_send_engine_post_start(queue);
	mlx5hws_send_engine_post_req_wqe(&ctrl, (void *)&wqe_ctrl, &wqe_len);
	mlx5hws_send_engine_post_req_wqe(&ctrl, (void *)&wqe_data, &wqe_len);

	wqe_ctrl->op_dirix = cpu_to_be32(gta_opcode << 28 | direct_index);
	memcpy(wqe_ctrl->stc_ix, send_wqe_ctrl->stc_ix,
	       sizeof(send_wqe_ctrl->stc_ix));

	if (send_wqe_data)
		memcpy(wqe_data, send_wqe_data, sizeof(*wqe_data));
	else
		hws_send_wqe_set_tag(wqe_data, send_wqe_tag, is_jumbo);

	mlx5hws_send_engine_post_end(&ctrl, send_attr);
}

void mlx5hws_send_ste(struct mlx5hws_send_engine *queue,
		      struct mlx5hws_send_ste_attr *ste_attr)
{
	struct mlx5hws_send_engine_post_attr *send_attr = &ste_attr->send_attr;
	u8 notify_hw = send_attr->notify_hw;
	u8 fence = send_attr->fence;

	if (ste_attr->rtc_1) {
		send_attr->id = ste_attr->rtc_1;
		send_attr->used_id = ste_attr->used_id_rtc_1;
		send_attr->retry_id = ste_attr->retry_rtc_1;
		send_attr->fence = fence;
		send_attr->notify_hw = notify_hw && !ste_attr->rtc_0;
		hws_send_wqe(queue, send_attr,
			     ste_attr->wqe_ctrl,
			     ste_attr->wqe_data,
			     ste_attr->wqe_tag,
			     ste_attr->wqe_tag_is_jumbo,
			     ste_attr->gta_opcode,
			     ste_attr->direct_index);
	}

	if (ste_attr->rtc_0) {
		send_attr->id = ste_attr->rtc_0;
		send_attr->used_id = ste_attr->used_id_rtc_0;
		send_attr->retry_id = ste_attr->retry_rtc_0;
		send_attr->fence = fence && !ste_attr->rtc_1;
		send_attr->notify_hw = notify_hw;
		hws_send_wqe(queue, send_attr,
			     ste_attr->wqe_ctrl,
			     ste_attr->wqe_data,
			     ste_attr->wqe_tag,
			     ste_attr->wqe_tag_is_jumbo,
			     ste_attr->gta_opcode,
			     ste_attr->direct_index);
	}

	/* Restore to original requested values */
	send_attr->notify_hw = notify_hw;
	send_attr->fence = fence;
}

static void hws_send_engine_retry_post_send(struct mlx5hws_send_engine *queue,
					    struct mlx5hws_send_ring_priv *priv,
					    u16 wqe_cnt)
{
	struct mlx5hws_send_engine_post_attr send_attr = {0};
	struct mlx5hws_wqe_gta_data_seg_ste *wqe_data;
	struct mlx5hws_wqe_gta_ctrl_seg *wqe_ctrl;
	struct mlx5hws_send_engine_post_ctrl ctrl;
	struct mlx5hws_send_ring_sq *send_sq;
	unsigned int idx;
	size_t wqe_len;
	char *p;

	send_attr.rule = priv->rule;
	send_attr.opcode = MLX5HWS_WQE_OPCODE_TBL_ACCESS;
	send_attr.opmod = MLX5HWS_WQE_GTA_OPMOD_STE;
	send_attr.len = MLX5_SEND_WQE_BB * 2 - sizeof(struct mlx5hws_wqe_ctrl_seg);
	send_attr.notify_hw = 1;
	send_attr.fence = 0;
	send_attr.user_data = priv->user_data;
	send_attr.id = priv->retry_id;
	send_attr.used_id = priv->used_id;

	ctrl = mlx5hws_send_engine_post_start(queue);
	mlx5hws_send_engine_post_req_wqe(&ctrl, (void *)&wqe_ctrl, &wqe_len);
	mlx5hws_send_engine_post_req_wqe(&ctrl, (void *)&wqe_data, &wqe_len);

	send_sq = &ctrl.send_ring->send_sq;
	idx = wqe_cnt & send_sq->buf_mask;
	p = mlx5_wq_cyc_get_wqe(&send_sq->wq, idx);

	/* Copy old gta ctrl */
	memcpy(wqe_ctrl, p + sizeof(struct mlx5hws_wqe_ctrl_seg),
	       MLX5_SEND_WQE_BB - sizeof(struct mlx5hws_wqe_ctrl_seg));

	idx = (wqe_cnt + 1) & send_sq->buf_mask;
	p = mlx5_wq_cyc_get_wqe(&send_sq->wq, idx);

	/* Copy old gta data */
	memcpy(wqe_data, p, MLX5_SEND_WQE_BB);

	mlx5hws_send_engine_post_end(&ctrl, &send_attr);
}

void mlx5hws_send_engine_flush_queue(struct mlx5hws_send_engine *queue)
{
	struct mlx5hws_send_ring_sq *sq = &queue->send_ring.send_sq;
	struct mlx5hws_wqe_ctrl_seg *wqe_ctrl;

	wqe_ctrl = mlx5_wq_cyc_get_wqe(&sq->wq, sq->last_idx);
	wqe_ctrl->flags |= cpu_to_be32(MLX5_WQE_CTRL_CQ_UPDATE);

	hws_send_engine_post_ring(sq, wqe_ctrl);
}

static void
hws_send_engine_update_rule_resize(struct mlx5hws_send_engine *queue,
				   struct mlx5hws_send_ring_priv *priv,
				   enum mlx5hws_flow_op_status *status)
{
	switch (priv->rule->resize_info->state) {
	case MLX5HWS_RULE_RESIZE_STATE_WRITING:
		if (priv->rule->status == MLX5HWS_RULE_STATUS_FAILING) {
			/* Backup original RTCs */
			u32 orig_rtc_0 = priv->rule->resize_info->rtc_0;
			u32 orig_rtc_1 = priv->rule->resize_info->rtc_1;

			/* Delete partially failed move rule using resize_info */
			priv->rule->resize_info->rtc_0 = priv->rule->rtc_0;
			priv->rule->resize_info->rtc_1 = priv->rule->rtc_1;

			/* Move rule to original RTC for future delete */
			priv->rule->rtc_0 = orig_rtc_0;
			priv->rule->rtc_1 = orig_rtc_1;
		}
		/* Clean leftovers */
		mlx5hws_rule_move_hws_remove(priv->rule, queue, priv->user_data);
		break;

	case MLX5HWS_RULE_RESIZE_STATE_DELETING:
		if (priv->rule->status == MLX5HWS_RULE_STATUS_FAILING) {
			*status = MLX5HWS_FLOW_OP_ERROR;
		} else {
			*status = MLX5HWS_FLOW_OP_SUCCESS;
			priv->rule->matcher = priv->rule->matcher->resize_dst;
		}
		priv->rule->resize_info->state = MLX5HWS_RULE_RESIZE_STATE_IDLE;
		priv->rule->status = MLX5HWS_RULE_STATUS_CREATED;
		break;

	default:
		break;
	}
}

static void hws_send_engine_update_rule(struct mlx5hws_send_engine *queue,
					struct mlx5hws_send_ring_priv *priv,
					u16 wqe_cnt,
					enum mlx5hws_flow_op_status *status)
{
	priv->rule->pending_wqes--;

	if (*status == MLX5HWS_FLOW_OP_ERROR) {
		if (priv->retry_id) {
			hws_send_engine_retry_post_send(queue, priv, wqe_cnt);
			return;
		}
		/* Some part of the rule failed */
		priv->rule->status = MLX5HWS_RULE_STATUS_FAILING;
		*priv->used_id = 0;
	} else {
		*priv->used_id = priv->id;
	}

	/* Update rule status for the last completion */
	if (!priv->rule->pending_wqes) {
		if (unlikely(mlx5hws_rule_move_in_progress(priv->rule))) {
			hws_send_engine_update_rule_resize(queue, priv, status);
			return;
		}

		if (unlikely(priv->rule->status == MLX5HWS_RULE_STATUS_FAILING)) {
			/* Rule completely failed and doesn't require cleanup */
			if (!priv->rule->rtc_0 && !priv->rule->rtc_1)
				priv->rule->status = MLX5HWS_RULE_STATUS_FAILED;

			*status = MLX5HWS_FLOW_OP_ERROR;
		} else {
			/* Increase the status, this only works on good flow as the enum
			 * is arrange it away creating -> created -> deleting -> deleted
			 */
			priv->rule->status++;
			*status = MLX5HWS_FLOW_OP_SUCCESS;
			/* Rule was deleted now we can safely release action STEs
			 * and clear resize info
			 */
			if (priv->rule->status == MLX5HWS_RULE_STATUS_DELETED) {
				mlx5hws_rule_free_action_ste(priv->rule);
				mlx5hws_rule_clear_resize_info(priv->rule);
			}
		}
	}
}

static void hws_send_engine_update(struct mlx5hws_send_engine *queue,
				   struct mlx5_cqe64 *cqe,
				   struct mlx5hws_send_ring_priv *priv,
				   struct mlx5hws_flow_op_result res[],
				   s64 *i,
				   u32 res_nb,
				   u16 wqe_cnt)
{
	enum mlx5hws_flow_op_status status;

	if (!cqe || (likely(be32_to_cpu(cqe->byte_cnt) >> 31 == 0) &&
		     likely(get_cqe_opcode(cqe) == MLX5_CQE_REQ))) {
		status = MLX5HWS_FLOW_OP_SUCCESS;
	} else {
		status = MLX5HWS_FLOW_OP_ERROR;
	}

	if (priv->user_data) {
		if (priv->rule) {
			hws_send_engine_update_rule(queue, priv, wqe_cnt, &status);
			/* Completion is provided on the last rule WQE */
			if (priv->rule->pending_wqes)
				return;
		}

		if (*i < res_nb) {
			res[*i].user_data = priv->user_data;
			res[*i].status = status;
			(*i)++;
			mlx5hws_send_engine_dec_rule(queue);
		} else {
			mlx5hws_send_engine_gen_comp(queue, priv->user_data, status);
		}
	}
}

static int mlx5hws_parse_cqe(struct mlx5hws_send_ring_cq *cq,
			     struct mlx5_cqe64 *cqe64)
{
	if (unlikely(get_cqe_opcode(cqe64) != MLX5_CQE_REQ)) {
		struct mlx5_err_cqe *err_cqe = (struct mlx5_err_cqe *)cqe64;

		mlx5_core_err(cq->mdev, "Bad OP in HWS SQ CQE: 0x%x\n", get_cqe_opcode(cqe64));
		mlx5_core_err(cq->mdev, "vendor_err_synd=%x\n", err_cqe->vendor_err_synd);
		mlx5_core_err(cq->mdev, "syndrome=%x\n", err_cqe->syndrome);
		print_hex_dump(KERN_WARNING, "", DUMP_PREFIX_OFFSET,
			       16, 1, err_cqe,
			       sizeof(*err_cqe), false);
		return CQ_POLL_ERR;
	}

	return CQ_OK;
}

static int mlx5hws_cq_poll_one(struct mlx5hws_send_ring_cq *cq)
{
	struct mlx5_cqe64 *cqe64;
	int err;

	cqe64 = mlx5_cqwq_get_cqe(&cq->wq);
	if (!cqe64) {
		if (unlikely(cq->mdev->state ==
			     MLX5_DEVICE_STATE_INTERNAL_ERROR)) {
			mlx5_core_dbg_once(cq->mdev,
					   "Polling CQ while device is shutting down\n");
			return CQ_POLL_ERR;
		}
		return CQ_EMPTY;
	}

	mlx5_cqwq_pop(&cq->wq);
	err = mlx5hws_parse_cqe(cq, cqe64);
	mlx5_cqwq_update_db_record(&cq->wq);

	return err;
}

static void hws_send_engine_poll_cq(struct mlx5hws_send_engine *queue,
				    struct mlx5hws_flow_op_result res[],
				    s64 *polled,
				    u32 res_nb)
{
	struct mlx5hws_send_ring *send_ring = &queue->send_ring;
	struct mlx5hws_send_ring_cq *cq = &send_ring->send_cq;
	struct mlx5hws_send_ring_sq *sq = &send_ring->send_sq;
	struct mlx5hws_send_ring_priv *priv;
	struct mlx5_cqe64 *cqe;
	u8 cqe_opcode;
	u16 wqe_cnt;

	cqe = mlx5_cqwq_get_cqe(&cq->wq);
	if (!cqe)
		return;

	cqe_opcode = get_cqe_opcode(cqe);
	if (cqe_opcode == MLX5_CQE_INVALID)
		return;

	if (unlikely(cqe_opcode != MLX5_CQE_REQ))
		queue->err = true;

	wqe_cnt = be16_to_cpu(cqe->wqe_counter) & sq->buf_mask;

	while (cq->poll_wqe != wqe_cnt) {
		priv = &sq->wr_priv[cq->poll_wqe];
		hws_send_engine_update(queue, NULL, priv, res, polled, res_nb, 0);
		cq->poll_wqe = (cq->poll_wqe + priv->num_wqebbs) & sq->buf_mask;
	}

	priv = &sq->wr_priv[wqe_cnt];
	cq->poll_wqe = (wqe_cnt + priv->num_wqebbs) & sq->buf_mask;
	hws_send_engine_update(queue, cqe, priv, res, polled, res_nb, wqe_cnt);
	mlx5hws_cq_poll_one(cq);
}

static void hws_send_engine_poll_list(struct mlx5hws_send_engine *queue,
				      struct mlx5hws_flow_op_result res[],
				      s64 *polled,
				      u32 res_nb)
{
	struct mlx5hws_completed_poll *comp = &queue->completed;

	while (comp->ci != comp->pi) {
		if (*polled < res_nb) {
			res[*polled].status =
				comp->entries[comp->ci].status;
			res[*polled].user_data =
				comp->entries[comp->ci].user_data;
			(*polled)++;
			comp->ci = (comp->ci + 1) & comp->mask;
			mlx5hws_send_engine_dec_rule(queue);
		} else {
			return;
		}
	}
}

static int hws_send_engine_poll(struct mlx5hws_send_engine *queue,
				struct mlx5hws_flow_op_result res[],
				u32 res_nb)
{
	s64 polled = 0;

	hws_send_engine_poll_list(queue, res, &polled, res_nb);

	if (polled >= res_nb)
		return polled;

	hws_send_engine_poll_cq(queue, res, &polled, res_nb);

	return polled;
}

int mlx5hws_send_queue_poll(struct mlx5hws_context *ctx,
			    u16 queue_id,
			    struct mlx5hws_flow_op_result res[],
			    u32 res_nb)
{
	return hws_send_engine_poll(&ctx->send_queue[queue_id], res, res_nb);
}

static int hws_send_ring_alloc_sq(struct mlx5_core_dev *mdev,
				  int numa_node,
				  struct mlx5hws_send_engine *queue,
				  struct mlx5hws_send_ring_sq *sq,
				  void *sqc_data)
{
	void *sqc_wq = MLX5_ADDR_OF(sqc, sqc_data, wq);
	struct mlx5_wq_cyc *wq = &sq->wq;
	struct mlx5_wq_param param;
	size_t buf_sz;
	int err;

	sq->uar_map = mdev->mlx5e_res.hw_objs.bfreg.map;
	sq->mdev = mdev;

	param.db_numa_node = numa_node;
	param.buf_numa_node = numa_node;
	err = mlx5_wq_cyc_create(mdev, &param, sqc_wq, wq, &sq->wq_ctrl);
	if (err)
		return err;
	wq->db = &wq->db[MLX5_SND_DBR];

	buf_sz = queue->num_entries * MAX_WQES_PER_RULE;
	sq->dep_wqe = kcalloc(queue->num_entries, sizeof(*sq->dep_wqe), GFP_KERNEL);
	if (!sq->dep_wqe) {
		err = -ENOMEM;
		goto destroy_wq_cyc;
	}

	sq->wr_priv = kzalloc(sizeof(*sq->wr_priv) * buf_sz, GFP_KERNEL);
	if (!sq->wr_priv) {
		err = -ENOMEM;
		goto free_dep_wqe;
	}

	sq->buf_mask = (queue->num_entries * MAX_WQES_PER_RULE) - 1;

	return 0;

free_dep_wqe:
	kfree(sq->dep_wqe);
destroy_wq_cyc:
	mlx5_wq_destroy(&sq->wq_ctrl);
	return err;
}

static void hws_send_ring_free_sq(struct mlx5hws_send_ring_sq *sq)
{
	if (!sq)
		return;
	kfree(sq->wr_priv);
	kfree(sq->dep_wqe);
	mlx5_wq_destroy(&sq->wq_ctrl);
}

static int hws_send_ring_create_sq(struct mlx5_core_dev *mdev, u32 pdn,
				   void *sqc_data,
				   struct mlx5hws_send_engine *queue,
				   struct mlx5hws_send_ring_sq *sq,
				   struct mlx5hws_send_ring_cq *cq)
{
	void *in, *sqc, *wq;
	int inlen, err;
	u8 ts_format;

	inlen = MLX5_ST_SZ_BYTES(create_sq_in) +
		sizeof(u64) * sq->wq_ctrl.buf.npages;
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	sqc = MLX5_ADDR_OF(create_sq_in, in, ctx);
	wq = MLX5_ADDR_OF(sqc, sqc, wq);

	memcpy(sqc, sqc_data, MLX5_ST_SZ_BYTES(sqc));
	MLX5_SET(sqc, sqc, cqn, cq->mcq.cqn);

	MLX5_SET(sqc, sqc, state, MLX5_SQC_STATE_RST);
	MLX5_SET(sqc, sqc, flush_in_error_en, 1);

	ts_format = mlx5_is_real_time_sq(mdev) ? MLX5_TIMESTAMP_FORMAT_REAL_TIME :
						 MLX5_TIMESTAMP_FORMAT_FREE_RUNNING;
	MLX5_SET(sqc, sqc, ts_format, ts_format);

	MLX5_SET(wq, wq, wq_type, MLX5_WQ_TYPE_CYCLIC);
	MLX5_SET(wq, wq, uar_page, mdev->mlx5e_res.hw_objs.bfreg.index);
	MLX5_SET(wq, wq, log_wq_pg_sz, sq->wq_ctrl.buf.page_shift - MLX5_ADAPTER_PAGE_SHIFT);
	MLX5_SET64(wq, wq, dbr_addr, sq->wq_ctrl.db.dma);

	mlx5_fill_page_frag_array(&sq->wq_ctrl.buf,
				  (__be64 *)MLX5_ADDR_OF(wq, wq, pas));

	err = mlx5_core_create_sq(mdev, in, inlen, &sq->sqn);

	kvfree(in);

	return err;
}

static void hws_send_ring_destroy_sq(struct mlx5_core_dev *mdev,
				     struct mlx5hws_send_ring_sq *sq)
{
	mlx5_core_destroy_sq(mdev, sq->sqn);
}

static int hws_send_ring_set_sq_rdy(struct mlx5_core_dev *mdev, u32 sqn)
{
	void *in, *sqc;
	int inlen, err;

	inlen = MLX5_ST_SZ_BYTES(modify_sq_in);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(modify_sq_in, in, sq_state, MLX5_SQC_STATE_RST);
	sqc = MLX5_ADDR_OF(modify_sq_in, in, ctx);
	MLX5_SET(sqc, sqc, state, MLX5_SQC_STATE_RDY);

	err = mlx5_core_modify_sq(mdev, sqn, in);

	kvfree(in);

	return err;
}

static void hws_send_ring_close_sq(struct mlx5hws_send_ring_sq *sq)
{
	mlx5_core_destroy_sq(sq->mdev, sq->sqn);
	mlx5_wq_destroy(&sq->wq_ctrl);
	kfree(sq->wr_priv);
	kfree(sq->dep_wqe);
}

static int hws_send_ring_create_sq_rdy(struct mlx5_core_dev *mdev, u32 pdn,
				       void *sqc_data,
				       struct mlx5hws_send_engine *queue,
				       struct mlx5hws_send_ring_sq *sq,
				       struct mlx5hws_send_ring_cq *cq)
{
	int err;

	err = hws_send_ring_create_sq(mdev, pdn, sqc_data, queue, sq, cq);
	if (err)
		return err;

	err = hws_send_ring_set_sq_rdy(mdev, sq->sqn);
	if (err)
		hws_send_ring_destroy_sq(mdev, sq);

	return err;
}

static int hws_send_ring_open_sq(struct mlx5hws_context *ctx,
				 int numa_node,
				 struct mlx5hws_send_engine *queue,
				 struct mlx5hws_send_ring_sq *sq,
				 struct mlx5hws_send_ring_cq *cq)
{
	size_t buf_sz, sq_log_buf_sz;
	void *sqc_data, *wq;
	int err;

	sqc_data = kvzalloc(MLX5_ST_SZ_BYTES(sqc), GFP_KERNEL);
	if (!sqc_data)
		return -ENOMEM;

	buf_sz = queue->num_entries * MAX_WQES_PER_RULE;
	sq_log_buf_sz = ilog2(roundup_pow_of_two(buf_sz));

	wq = MLX5_ADDR_OF(sqc, sqc_data, wq);
	MLX5_SET(wq, wq, log_wq_stride, ilog2(MLX5_SEND_WQE_BB));
	MLX5_SET(wq, wq, pd, ctx->pd_num);
	MLX5_SET(wq, wq, log_wq_sz, sq_log_buf_sz);

	err = hws_send_ring_alloc_sq(ctx->mdev, numa_node, queue, sq, sqc_data);
	if (err)
		goto err_free_sqc;

	err = hws_send_ring_create_sq_rdy(ctx->mdev, ctx->pd_num, sqc_data,
					  queue, sq, cq);
	if (err)
		goto err_free_sq;

	kvfree(sqc_data);

	return 0;
err_free_sq:
	hws_send_ring_free_sq(sq);
err_free_sqc:
	kvfree(sqc_data);
	return err;
}

static void hws_cq_complete(struct mlx5_core_cq *mcq,
			    struct mlx5_eqe *eqe)
{
	pr_err("CQ completion CQ: #%u\n", mcq->cqn);
}

static int hws_send_ring_alloc_cq(struct mlx5_core_dev *mdev,
				  int numa_node,
				  struct mlx5hws_send_engine *queue,
				  void *cqc_data,
				  struct mlx5hws_send_ring_cq *cq)
{
	struct mlx5_core_cq *mcq = &cq->mcq;
	struct mlx5_wq_param param;
	struct mlx5_cqe64 *cqe;
	int err;
	u32 i;

	param.buf_numa_node = numa_node;
	param.db_numa_node = numa_node;

	err = mlx5_cqwq_create(mdev, &param, cqc_data, &cq->wq, &cq->wq_ctrl);
	if (err)
		return err;

	mcq->cqe_sz = 64;
	mcq->set_ci_db = cq->wq_ctrl.db.db;
	mcq->arm_db = cq->wq_ctrl.db.db + 1;
	mcq->comp = hws_cq_complete;

	for (i = 0; i < mlx5_cqwq_get_size(&cq->wq); i++) {
		cqe = mlx5_cqwq_get_wqe(&cq->wq, i);
		cqe->op_own = 0xf1;
	}

	cq->mdev = mdev;

	return 0;
}

static int hws_send_ring_create_cq(struct mlx5_core_dev *mdev,
				   struct mlx5hws_send_engine *queue,
				   void *cqc_data,
				   struct mlx5hws_send_ring_cq *cq)
{
	u32 out[MLX5_ST_SZ_DW(create_cq_out)];
	struct mlx5_core_cq *mcq = &cq->mcq;
	void *in, *cqc;
	int inlen, eqn;
	int err;

	err = mlx5_comp_eqn_get(mdev, 0, &eqn);
	if (err)
		return err;

	inlen = MLX5_ST_SZ_BYTES(create_cq_in) +
		sizeof(u64) * cq->wq_ctrl.buf.npages;
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	cqc = MLX5_ADDR_OF(create_cq_in, in, cq_context);
	memcpy(cqc, cqc_data, MLX5_ST_SZ_BYTES(cqc));
	mlx5_fill_page_frag_array(&cq->wq_ctrl.buf,
				  (__be64 *)MLX5_ADDR_OF(create_cq_in, in, pas));

	MLX5_SET(cqc, cqc, c_eqn_or_apu_element, eqn);
	MLX5_SET(cqc, cqc, uar_page, mdev->priv.uar->index);
	MLX5_SET(cqc, cqc, log_page_size, cq->wq_ctrl.buf.page_shift - MLX5_ADAPTER_PAGE_SHIFT);
	MLX5_SET64(cqc, cqc, dbr_addr, cq->wq_ctrl.db.dma);

	err = mlx5_core_create_cq(mdev, mcq, in, inlen, out, sizeof(out));

	kvfree(in);

	return err;
}

static int hws_send_ring_open_cq(struct mlx5_core_dev *mdev,
				 struct mlx5hws_send_engine *queue,
				 int numa_node,
				 struct mlx5hws_send_ring_cq *cq)
{
	void *cqc_data;
	int err;

	cqc_data = kvzalloc(MLX5_ST_SZ_BYTES(cqc), GFP_KERNEL);
	if (!cqc_data)
		return -ENOMEM;

	MLX5_SET(cqc, cqc_data, uar_page, mdev->priv.uar->index);
	MLX5_SET(cqc, cqc_data, cqe_sz, queue->num_entries);
	MLX5_SET(cqc, cqc_data, log_cq_size, ilog2(queue->num_entries));

	err = hws_send_ring_alloc_cq(mdev, numa_node, queue, cqc_data, cq);
	if (err)
		goto err_out;

	err = hws_send_ring_create_cq(mdev, queue, cqc_data, cq);
	if (err)
		goto err_free_cq;

	kvfree(cqc_data);

	return 0;

err_free_cq:
	mlx5_wq_destroy(&cq->wq_ctrl);
err_out:
	kvfree(cqc_data);
	return err;
}

static void hws_send_ring_close_cq(struct mlx5hws_send_ring_cq *cq)
{
	mlx5_core_destroy_cq(cq->mdev, &cq->mcq);
	mlx5_wq_destroy(&cq->wq_ctrl);
}

static void hws_send_ring_close(struct mlx5hws_send_engine *queue)
{
	hws_send_ring_close_sq(&queue->send_ring.send_sq);
	hws_send_ring_close_cq(&queue->send_ring.send_cq);
}

static int mlx5hws_send_ring_open(struct mlx5hws_context *ctx,
				  struct mlx5hws_send_engine *queue)
{
	int numa_node = dev_to_node(mlx5_core_dma_dev(ctx->mdev));
	struct mlx5hws_send_ring *ring = &queue->send_ring;
	int err;

	err = hws_send_ring_open_cq(ctx->mdev, queue, numa_node, &ring->send_cq);
	if (err)
		return err;

	err = hws_send_ring_open_sq(ctx, numa_node, queue, &ring->send_sq,
				    &ring->send_cq);
	if (err)
		goto close_cq;

	return err;

close_cq:
	hws_send_ring_close_cq(&ring->send_cq);
	return err;
}

void mlx5hws_send_queue_close(struct mlx5hws_send_engine *queue)
{
	hws_send_ring_close(queue);
	kfree(queue->completed.entries);
}

int mlx5hws_send_queue_open(struct mlx5hws_context *ctx,
			    struct mlx5hws_send_engine *queue,
			    u16 queue_size)
{
	int err;

	mutex_init(&queue->lock);

	queue->num_entries = roundup_pow_of_two(queue_size);
	queue->used_entries = 0;

	queue->completed.entries = kcalloc(queue->num_entries,
					   sizeof(queue->completed.entries[0]),
					   GFP_KERNEL);
	if (!queue->completed.entries)
		return -ENOMEM;

	queue->completed.pi = 0;
	queue->completed.ci = 0;
	queue->completed.mask = queue->num_entries - 1;
	err = mlx5hws_send_ring_open(ctx, queue);
	if (err)
		goto free_completed_entries;

	return 0;

free_completed_entries:
	kfree(queue->completed.entries);
	return err;
}

static void __hws_send_queues_close(struct mlx5hws_context *ctx, u16 queues)
{
	while (queues--)
		mlx5hws_send_queue_close(&ctx->send_queue[queues]);
}

static void hws_send_queues_bwc_locks_destroy(struct mlx5hws_context *ctx)
{
	int bwc_queues = ctx->queues - 1;
	int i;

	if (!mlx5hws_context_bwc_supported(ctx))
		return;

	for (i = 0; i < bwc_queues; i++)
		mutex_destroy(&ctx->bwc_send_queue_locks[i]);
	kfree(ctx->bwc_send_queue_locks);
}

void mlx5hws_send_queues_close(struct mlx5hws_context *ctx)
{
	hws_send_queues_bwc_locks_destroy(ctx);
	__hws_send_queues_close(ctx, ctx->queues);
	kfree(ctx->send_queue);
}

static int hws_bwc_send_queues_init(struct mlx5hws_context *ctx)
{
	/* Number of BWC queues is equal to number of the usual HWS queues */
	int bwc_queues = ctx->queues - 1;
	int i;

	if (!mlx5hws_context_bwc_supported(ctx))
		return 0;

	ctx->queues += bwc_queues;

	ctx->bwc_send_queue_locks = kcalloc(bwc_queues,
					    sizeof(*ctx->bwc_send_queue_locks),
					    GFP_KERNEL);

	if (!ctx->bwc_send_queue_locks)
		return -ENOMEM;

	for (i = 0; i < bwc_queues; i++)
		mutex_init(&ctx->bwc_send_queue_locks[i]);

	return 0;
}

int mlx5hws_send_queues_open(struct mlx5hws_context *ctx,
			     u16 queues,
			     u16 queue_size)
{
	int err = 0;
	u32 i;

	/* Open one extra queue for control path */
	ctx->queues = queues + 1;

	/* open a separate set of queues and locks for bwc API */
	err = hws_bwc_send_queues_init(ctx);
	if (err)
		return err;

	ctx->send_queue = kcalloc(ctx->queues, sizeof(*ctx->send_queue), GFP_KERNEL);
	if (!ctx->send_queue) {
		err = -ENOMEM;
		goto free_bwc_locks;
	}

	for (i = 0; i < ctx->queues; i++) {
		err = mlx5hws_send_queue_open(ctx, &ctx->send_queue[i], queue_size);
		if (err)
			goto close_send_queues;
	}

	return 0;

close_send_queues:
	 __hws_send_queues_close(ctx, i);

	kfree(ctx->send_queue);

free_bwc_locks:
	hws_send_queues_bwc_locks_destroy(ctx);

	return err;
}

int mlx5hws_send_queue_action(struct mlx5hws_context *ctx,
			      u16 queue_id,
			      u32 actions)
{
	struct mlx5hws_send_ring_sq *send_sq;
	struct mlx5hws_send_engine *queue;
	bool wait_comp = false;
	s64 polled = 0;

	queue = &ctx->send_queue[queue_id];
	send_sq = &queue->send_ring.send_sq;

	switch (actions) {
	case MLX5HWS_SEND_QUEUE_ACTION_DRAIN_SYNC:
		wait_comp = true;
		fallthrough;
	case MLX5HWS_SEND_QUEUE_ACTION_DRAIN_ASYNC:
		if (send_sq->head_dep_idx != send_sq->tail_dep_idx)
			/* Send dependent WQEs to drain the queue */
			mlx5hws_send_all_dep_wqe(queue);
		else
			/* Signal on the last posted WQE */
			mlx5hws_send_engine_flush_queue(queue);

		/* Poll queue until empty */
		while (wait_comp && !mlx5hws_send_engine_empty(queue))
			hws_send_engine_poll_cq(queue, NULL, &polled, 0);

		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int
hws_send_wqe_fw(struct mlx5_core_dev *mdev,
		u32 pd_num,
		struct mlx5hws_send_engine_post_attr *send_attr,
		struct mlx5hws_wqe_gta_ctrl_seg *send_wqe_ctrl,
		void *send_wqe_match_data,
		void *send_wqe_match_tag,
		void *send_wqe_range_data,
		void *send_wqe_range_tag,
		bool is_jumbo,
		u8 gta_opcode)
{
	bool has_range = send_wqe_range_data || send_wqe_range_tag;
	bool has_match = send_wqe_match_data || send_wqe_match_tag;
	struct mlx5hws_wqe_gta_data_seg_ste gta_wqe_data0 = {0};
	struct mlx5hws_wqe_gta_data_seg_ste gta_wqe_data1 = {0};
	struct mlx5hws_wqe_gta_ctrl_seg gta_wqe_ctrl = {0};
	struct mlx5hws_cmd_generate_wqe_attr attr = {0};
	struct mlx5hws_wqe_ctrl_seg wqe_ctrl = {0};
	struct mlx5_cqe64 cqe;
	u32 flags = 0;
	int ret;

	/* Set WQE control */
	wqe_ctrl.opmod_idx_opcode = cpu_to_be32((send_attr->opmod << 24) | send_attr->opcode);
	wqe_ctrl.qpn_ds = cpu_to_be32((send_attr->len + sizeof(struct mlx5hws_wqe_ctrl_seg)) / 16);
	flags |= send_attr->notify_hw ? MLX5_WQE_CTRL_CQ_UPDATE : 0;
	wqe_ctrl.flags = cpu_to_be32(flags);
	wqe_ctrl.imm = cpu_to_be32(send_attr->id);

	/* Set GTA WQE CTRL */
	memcpy(gta_wqe_ctrl.stc_ix, send_wqe_ctrl->stc_ix, sizeof(send_wqe_ctrl->stc_ix));
	gta_wqe_ctrl.op_dirix = cpu_to_be32(gta_opcode << 28);

	/* Set GTA match WQE DATA */
	if (has_match) {
		if (send_wqe_match_data)
			memcpy(&gta_wqe_data0, send_wqe_match_data, sizeof(gta_wqe_data0));
		else
			hws_send_wqe_set_tag(&gta_wqe_data0, send_wqe_match_tag, is_jumbo);

		gta_wqe_data0.rsvd1_definer = cpu_to_be32(send_attr->match_definer_id << 8);
		attr.gta_data_0 = (u8 *)&gta_wqe_data0;
	}

	/* Set GTA range WQE DATA */
	if (has_range) {
		if (send_wqe_range_data)
			memcpy(&gta_wqe_data1, send_wqe_range_data, sizeof(gta_wqe_data1));
		else
			hws_send_wqe_set_tag(&gta_wqe_data1, send_wqe_range_tag, false);

		gta_wqe_data1.rsvd1_definer = cpu_to_be32(send_attr->range_definer_id << 8);
		attr.gta_data_1 = (u8 *)&gta_wqe_data1;
	}

	attr.pdn = pd_num;
	attr.wqe_ctrl = (u8 *)&wqe_ctrl;
	attr.gta_ctrl = (u8 *)&gta_wqe_ctrl;

send_wqe:
	ret = mlx5hws_cmd_generate_wqe(mdev, &attr, &cqe);
	if (ret) {
		mlx5_core_err(mdev, "Failed to write WQE using command");
		return ret;
	}

	if ((get_cqe_opcode(&cqe) == MLX5_CQE_REQ) &&
	    (be32_to_cpu(cqe.byte_cnt) >> 31 == 0)) {
		*send_attr->used_id = send_attr->id;
		return 0;
	}

	/* Retry if rule failed */
	if (send_attr->retry_id) {
		wqe_ctrl.imm = cpu_to_be32(send_attr->retry_id);
		send_attr->id = send_attr->retry_id;
		send_attr->retry_id = 0;
		goto send_wqe;
	}

	return -1;
}

void mlx5hws_send_stes_fw(struct mlx5hws_context *ctx,
			  struct mlx5hws_send_engine *queue,
			  struct mlx5hws_send_ste_attr *ste_attr)
{
	struct mlx5hws_send_engine_post_attr *send_attr = &ste_attr->send_attr;
	struct mlx5hws_rule *rule = send_attr->rule;
	struct mlx5_core_dev *mdev;
	u16 queue_id;
	u32 pdn;
	int ret;

	queue_id = queue - ctx->send_queue;
	mdev = ctx->mdev;
	pdn = ctx->pd_num;

	/* Writing through FW can't HW fence, therefore we drain the queue */
	if (send_attr->fence)
		mlx5hws_send_queue_action(ctx,
					  queue_id,
					  MLX5HWS_SEND_QUEUE_ACTION_DRAIN_SYNC);

	if (ste_attr->rtc_1) {
		send_attr->id = ste_attr->rtc_1;
		send_attr->used_id = ste_attr->used_id_rtc_1;
		send_attr->retry_id = ste_attr->retry_rtc_1;
		ret = hws_send_wqe_fw(mdev, pdn, send_attr,
				      ste_attr->wqe_ctrl,
				      ste_attr->wqe_data,
				      ste_attr->wqe_tag,
				      ste_attr->range_wqe_data,
				      ste_attr->range_wqe_tag,
				      ste_attr->wqe_tag_is_jumbo,
				      ste_attr->gta_opcode);
		if (ret)
			goto fail_rule;
	}

	if (ste_attr->rtc_0) {
		send_attr->id = ste_attr->rtc_0;
		send_attr->used_id = ste_attr->used_id_rtc_0;
		send_attr->retry_id = ste_attr->retry_rtc_0;
		ret = hws_send_wqe_fw(mdev, pdn, send_attr,
				      ste_attr->wqe_ctrl,
				      ste_attr->wqe_data,
				      ste_attr->wqe_tag,
				      ste_attr->range_wqe_data,
				      ste_attr->range_wqe_tag,
				      ste_attr->wqe_tag_is_jumbo,
				      ste_attr->gta_opcode);
		if (ret)
			goto fail_rule;
	}

	/* Increase the status, this only works on good flow as the enum
	 * is arrange it away creating -> created -> deleting -> deleted
	 */
	if (likely(rule))
		rule->status++;

	mlx5hws_send_engine_gen_comp(queue, send_attr->user_data, MLX5HWS_FLOW_OP_SUCCESS);

	return;

fail_rule:
	if (likely(rule))
		rule->status = !rule->rtc_0 && !rule->rtc_1 ?
			MLX5HWS_RULE_STATUS_FAILED : MLX5HWS_RULE_STATUS_FAILING;

	mlx5hws_send_engine_gen_comp(queue, send_attr->user_data, MLX5HWS_FLOW_OP_ERROR);
}
