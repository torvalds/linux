/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __MLX5_EN_PARAMS_H__
#define __MLX5_EN_PARAMS_H__

#include "en.h"

struct mlx5e_xsk_param {
	u16 headroom;
	u16 chunk_size;
};

struct mlx5e_cq_param {
	u32                        cqc[MLX5_ST_SZ_DW(cqc)];
	struct mlx5_wq_param       wq;
	u16                        eq_ix;
	u8                         cq_period_mode;
};

struct mlx5e_rq_param {
	struct mlx5e_cq_param      cqp;
	u32                        rqc[MLX5_ST_SZ_DW(rqc)];
	struct mlx5_wq_param       wq;
	struct mlx5e_rq_frags_info frags_info;
};

struct mlx5e_sq_param {
	struct mlx5e_cq_param      cqp;
	u32                        sqc[MLX5_ST_SZ_DW(sqc)];
	struct mlx5_wq_param       wq;
	bool                       is_mpw;
};

struct mlx5e_channel_param {
	struct mlx5e_rq_param      rq;
	struct mlx5e_sq_param      txq_sq;
	struct mlx5e_sq_param      xdp_sq;
	struct mlx5e_sq_param      icosq;
	struct mlx5e_sq_param      async_icosq;
};

static inline bool mlx5e_qid_get_ch_if_in_group(struct mlx5e_params *params,
						u16 qid,
						enum mlx5e_rq_group group,
						u16 *ix)
{
	int nch = params->num_channels;
	int ch = qid - nch * group;

	if (ch < 0 || ch >= nch)
		return false;

	*ix = ch;
	return true;
}

static inline void mlx5e_qid_get_ch_and_group(struct mlx5e_params *params,
					      u16 qid,
					      u16 *ix,
					      enum mlx5e_rq_group *group)
{
	u16 nch = params->num_channels;

	*ix = qid % nch;
	*group = qid / nch;
}

static inline bool mlx5e_qid_validate(const struct mlx5e_profile *profile,
				      struct mlx5e_params *params, u64 qid)
{
	return qid < params->num_channels * profile->rq_groups;
}

/* Parameter calculations */

u16 mlx5e_get_linear_rq_headroom(struct mlx5e_params *params,
				 struct mlx5e_xsk_param *xsk);
u32 mlx5e_rx_get_min_frag_sz(struct mlx5e_params *params,
			     struct mlx5e_xsk_param *xsk);
u32 mlx5e_rx_get_linear_frag_sz(struct mlx5e_params *params,
				struct mlx5e_xsk_param *xsk);
u8 mlx5e_mpwqe_log_pkts_per_wqe(struct mlx5e_params *params,
				struct mlx5e_xsk_param *xsk);
bool mlx5e_rx_is_linear_skb(struct mlx5e_params *params,
			    struct mlx5e_xsk_param *xsk);
bool mlx5e_rx_mpwqe_is_linear_skb(struct mlx5_core_dev *mdev,
				  struct mlx5e_params *params,
				  struct mlx5e_xsk_param *xsk);
u8 mlx5e_mpwqe_get_log_rq_size(struct mlx5e_params *params,
			       struct mlx5e_xsk_param *xsk);
u8 mlx5e_mpwqe_get_log_stride_size(struct mlx5_core_dev *mdev,
				   struct mlx5e_params *params,
				   struct mlx5e_xsk_param *xsk);
u8 mlx5e_mpwqe_get_log_num_strides(struct mlx5_core_dev *mdev,
				   struct mlx5e_params *params,
				   struct mlx5e_xsk_param *xsk);
u16 mlx5e_get_rq_headroom(struct mlx5_core_dev *mdev,
			  struct mlx5e_params *params,
			  struct mlx5e_xsk_param *xsk);

/* Build queue parameters */

void mlx5e_build_rq_param(struct mlx5e_priv *priv,
			  struct mlx5e_params *params,
			  struct mlx5e_xsk_param *xsk,
			  struct mlx5e_rq_param *param);
void mlx5e_build_sq_param_common(struct mlx5e_priv *priv,
				 struct mlx5e_sq_param *param);
void mlx5e_build_rx_cq_param(struct mlx5e_priv *priv,
			     struct mlx5e_params *params,
			     struct mlx5e_xsk_param *xsk,
			     struct mlx5e_cq_param *param);
void mlx5e_build_tx_cq_param(struct mlx5e_priv *priv,
			     struct mlx5e_params *params,
			     struct mlx5e_cq_param *param);
void mlx5e_build_ico_cq_param(struct mlx5e_priv *priv,
			      u8 log_wq_size,
			      struct mlx5e_cq_param *param);
void mlx5e_build_icosq_param(struct mlx5e_priv *priv,
			     u8 log_wq_size,
			     struct mlx5e_sq_param *param);
void mlx5e_build_xdpsq_param(struct mlx5e_priv *priv,
			     struct mlx5e_params *params,
			     struct mlx5e_sq_param *param);

#endif /* __MLX5_EN_PARAMS_H__ */
