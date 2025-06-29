/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#ifndef MLX5HWS_SEND_H_
#define MLX5HWS_SEND_H_

/* As a single operation requires at least two WQEBBS.
 * This means a maximum of 16 such operations per rule.
 */
#define MAX_WQES_PER_RULE 32

enum mlx5hws_wqe_opcode {
	MLX5HWS_WQE_OPCODE_TBL_ACCESS = 0x2c,
};

enum mlx5hws_wqe_opmod {
	MLX5HWS_WQE_OPMOD_GTA_STE = 0,
	MLX5HWS_WQE_OPMOD_GTA_MOD_ARG = 1,
};

enum mlx5hws_wqe_gta_opcode {
	MLX5HWS_WQE_GTA_OP_ACTIVATE = 0,
	MLX5HWS_WQE_GTA_OP_DEACTIVATE = 1,
};

enum mlx5hws_wqe_gta_opmod {
	MLX5HWS_WQE_GTA_OPMOD_STE = 0,
	MLX5HWS_WQE_GTA_OPMOD_MOD_ARG = 1,
};

enum mlx5hws_wqe_gta_sz {
	MLX5HWS_WQE_SZ_GTA_CTRL = 48,
	MLX5HWS_WQE_SZ_GTA_DATA = 64,
};

/* WQE Control segment. */
struct mlx5hws_wqe_ctrl_seg {
	__be32 opmod_idx_opcode;
	__be32 qpn_ds;
	__be32 flags;
	__be32 imm;
};

struct mlx5hws_wqe_gta_ctrl_seg {
	__be32 op_dirix;
	__be32 stc_ix[5];
	__be32 rsvd0[6];
};

struct mlx5hws_wqe_gta_data_seg_ste {
	__be32 rsvd0_ctr_id;
	__be32 rsvd1_definer;
	__be32 rsvd2[3];
	union {
		struct {
		__be32 action[3];
		__be32 tag[8];
		};
		__be32 jumbo[11];
	};
};

struct mlx5hws_wqe_gta_data_seg_arg {
	__be32 action_args[8];
};

struct mlx5hws_wqe_gta {
	struct mlx5hws_wqe_gta_ctrl_seg gta_ctrl;
	union {
		struct mlx5hws_wqe_gta_data_seg_ste seg_ste;
		struct mlx5hws_wqe_gta_data_seg_arg seg_arg;
	};
};

struct mlx5hws_send_ring_cq {
	struct mlx5_core_dev *mdev;
	struct mlx5_cqwq wq;
	struct mlx5_wq_ctrl wq_ctrl;
	struct mlx5_core_cq mcq;
	u16 poll_wqe;
};

struct mlx5hws_send_ring_priv {
	struct mlx5hws_rule *rule;
	void *user_data;
	u32 num_wqebbs;
	u32 id;
	u32 retry_id;
	u32 *used_id;
};

struct mlx5hws_send_ring_dep_wqe {
	struct mlx5hws_wqe_gta_ctrl_seg wqe_ctrl;
	struct mlx5hws_wqe_gta_data_seg_ste wqe_data;
	struct mlx5hws_rule *rule;
	u32 rtc_0;
	u32 rtc_1;
	u32 retry_rtc_0;
	u32 retry_rtc_1;
	u32 direct_index;
	void *user_data;
};

struct mlx5hws_send_ring_sq {
	struct mlx5_core_dev *mdev;
	u16 cur_post;
	u16 buf_mask;
	struct mlx5hws_send_ring_priv *wr_priv;
	unsigned int last_idx;
	struct mlx5hws_send_ring_dep_wqe *dep_wqe;
	unsigned int head_dep_idx;
	unsigned int tail_dep_idx;
	u32 sqn;
	struct mlx5_wq_cyc wq;
	struct mlx5_wq_ctrl wq_ctrl;
	void __iomem *uar_map;
};

struct mlx5hws_send_ring {
	struct mlx5hws_send_ring_cq send_cq;
	struct mlx5hws_send_ring_sq send_sq;
};

struct mlx5hws_completed_poll_entry {
	void *user_data;
	enum mlx5hws_flow_op_status status;
};

struct mlx5hws_completed_poll {
	struct mlx5hws_completed_poll_entry *entries;
	u16 ci;
	u16 pi;
	u16 mask;
};

struct mlx5hws_send_engine {
	struct mlx5hws_send_ring send_ring;
	struct mlx5_uars_page *uar; /* Uar is shared between rings of a queue */
	struct mlx5hws_completed_poll completed;
	u16 used_entries;
	u16 num_entries;
	bool err;
	bool error_cqe_printed;
	struct mutex lock; /* Protects the send engine */
};

struct mlx5hws_send_engine_post_ctrl {
	struct mlx5hws_send_engine *queue;
	struct mlx5hws_send_ring *send_ring;
	size_t num_wqebbs;
};

struct mlx5hws_send_engine_post_attr {
	u8 opcode;
	u8 opmod;
	u8 notify_hw;
	u8 fence;
	u8 match_definer_id;
	u8 range_definer_id;
	size_t len;
	struct mlx5hws_rule *rule;
	u32 id;
	u32 retry_id;
	u32 *used_id;
	void *user_data;
};

struct mlx5hws_send_ste_attr {
	u32 rtc_0;
	u32 rtc_1;
	u32 retry_rtc_0;
	u32 retry_rtc_1;
	u32 *used_id_rtc_0;
	u32 *used_id_rtc_1;
	bool wqe_tag_is_jumbo;
	u8 gta_opcode;
	u32 direct_index;
	struct mlx5hws_send_engine_post_attr send_attr;
	struct mlx5hws_rule_match_tag *wqe_tag;
	struct mlx5hws_rule_match_tag *range_wqe_tag;
	struct mlx5hws_wqe_gta_ctrl_seg *wqe_ctrl;
	struct mlx5hws_wqe_gta_data_seg_ste *wqe_data;
	struct mlx5hws_wqe_gta_data_seg_ste *range_wqe_data;
};

struct mlx5hws_send_ring_dep_wqe *
mlx5hws_send_add_new_dep_wqe(struct mlx5hws_send_engine *queue);

void mlx5hws_send_abort_new_dep_wqe(struct mlx5hws_send_engine *queue);

void mlx5hws_send_all_dep_wqe(struct mlx5hws_send_engine *queue);

void mlx5hws_send_queues_close(struct mlx5hws_context *ctx);

int mlx5hws_send_queues_open(struct mlx5hws_context *ctx,
			     u16 queues,
			     u16 queue_size);

int mlx5hws_send_queue_action(struct mlx5hws_context *ctx,
			      u16 queue_id,
			      u32 actions);

int mlx5hws_send_test(struct mlx5hws_context *ctx,
		      u16 queues,
		      u16 queue_size);

struct mlx5hws_send_engine_post_ctrl
mlx5hws_send_engine_post_start(struct mlx5hws_send_engine *queue);

void mlx5hws_send_engine_post_req_wqe(struct mlx5hws_send_engine_post_ctrl *ctrl,
				      char **buf, size_t *len);

void mlx5hws_send_engine_post_end(struct mlx5hws_send_engine_post_ctrl *ctrl,
				  struct mlx5hws_send_engine_post_attr *attr);

void mlx5hws_send_ste(struct mlx5hws_send_engine *queue,
		      struct mlx5hws_send_ste_attr *ste_attr);

void mlx5hws_send_stes_fw(struct mlx5hws_context *ctx,
			  struct mlx5hws_send_engine *queue,
			  struct mlx5hws_send_ste_attr *ste_attr);

void mlx5hws_send_engine_flush_queue(struct mlx5hws_send_engine *queue);

static inline bool mlx5hws_send_engine_empty(struct mlx5hws_send_engine *queue)
{
	struct mlx5hws_send_ring_sq *send_sq = &queue->send_ring.send_sq;
	struct mlx5hws_send_ring_cq *send_cq = &queue->send_ring.send_cq;

	return ((send_sq->cur_post & send_sq->buf_mask) == send_cq->poll_wqe);
}

static inline bool mlx5hws_send_engine_full(struct mlx5hws_send_engine *queue)
{
	return queue->used_entries >= queue->num_entries;
}

static inline void mlx5hws_send_engine_inc_rule(struct mlx5hws_send_engine *queue)
{
	queue->used_entries++;
}

static inline void mlx5hws_send_engine_dec_rule(struct mlx5hws_send_engine *queue)
{
	queue->used_entries--;
}

static inline void mlx5hws_send_engine_gen_comp(struct mlx5hws_send_engine *queue,
						void *user_data,
						int comp_status)
{
	struct mlx5hws_completed_poll *comp = &queue->completed;

	comp->entries[comp->pi].status = comp_status;
	comp->entries[comp->pi].user_data = user_data;

	comp->pi = (comp->pi + 1) & comp->mask;
}

static inline bool mlx5hws_send_engine_err(struct mlx5hws_send_engine *queue)
{
	return queue->err;
}

#endif /* MLX5HWS_SEND_H_ */
