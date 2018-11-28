/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2013-2018, Mellanox Technologies. All rights reserved.
 */

#ifndef MLX5_IB_SRQ_H
#define MLX5_IB_SRQ_H

enum {
	MLX5_SRQ_FLAG_ERR    = (1 << 0),
	MLX5_SRQ_FLAG_WQ_SIG = (1 << 1),
	MLX5_SRQ_FLAG_RNDV   = (1 << 2),
};

struct mlx5_srq_attr {
	u32 type;
	u32 flags;
	u32 log_size;
	u32 wqe_shift;
	u32 log_page_size;
	u32 wqe_cnt;
	u32 srqn;
	u32 xrcd;
	u32 page_offset;
	u32 cqn;
	u32 pd;
	u32 lwm;
	u32 user_index;
	u64 db_record;
	__be64 *pas;
	u32 tm_log_list_size;
	u32 tm_next_tag;
	u32 tm_hw_phase_cnt;
	u32 tm_sw_phase_cnt;
	u16 uid;
};

struct mlx5_ib_dev;

int mlx5_cmd_create_srq(struct mlx5_ib_dev *dev, struct mlx5_core_srq *srq,
			struct mlx5_srq_attr *in);
int mlx5_cmd_destroy_srq(struct mlx5_ib_dev *dev, struct mlx5_core_srq *srq);
int mlx5_cmd_query_srq(struct mlx5_ib_dev *dev, struct mlx5_core_srq *srq,
		       struct mlx5_srq_attr *out);
int mlx5_cmd_arm_srq(struct mlx5_ib_dev *dev, struct mlx5_core_srq *srq,
		     u16 lwm, int is_srq);
struct mlx5_core_srq *mlx5_cmd_get_srq(struct mlx5_ib_dev *dev, u32 srqn);
#endif /* MLX5_IB_SRQ_H */
