/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __MLX5_EN_PARAMS_H__
#define __MLX5_EN_PARAMS_H__

#include "en.h"

struct mlx5e_xsk_param {
	u16 headroom;
	u16 chunk_size;
	bool unaligned;
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
	bool                       is_tls;
	bool                       is_xdp_mb;
	u16                        stop_room;
};

struct mlx5e_channel_param {
	struct mlx5e_rq_param      rq;
	struct mlx5e_sq_param      txq_sq;
	struct mlx5e_sq_param      xdp_sq;
	struct mlx5e_sq_param      icosq;
	struct mlx5e_sq_param      async_icosq;
};

struct mlx5e_create_sq_param {
	struct mlx5_wq_ctrl        *wq_ctrl;
	u32                         cqn;
	u32                         ts_cqe_to_dest_cqn;
	u32                         tisn;
	u8                          tis_lst_sz;
	u8                          min_inline_mode;
};

/* Striding RQ dynamic parameters */

u8 mlx5e_mpwrq_page_shift(struct mlx5_core_dev *mdev, struct mlx5e_xsk_param *xsk);
enum mlx5e_mpwrq_umr_mode
mlx5e_mpwrq_umr_mode(struct mlx5_core_dev *mdev, struct mlx5e_xsk_param *xsk);
u8 mlx5e_mpwrq_umr_entry_size(enum mlx5e_mpwrq_umr_mode mode);
u8 mlx5e_mpwrq_log_wqe_sz(struct mlx5_core_dev *mdev, u8 page_shift,
			  enum mlx5e_mpwrq_umr_mode umr_mode);
u8 mlx5e_mpwrq_pages_per_wqe(struct mlx5_core_dev *mdev, u8 page_shift,
			     enum mlx5e_mpwrq_umr_mode umr_mode);
u16 mlx5e_mpwrq_umr_wqe_sz(struct mlx5_core_dev *mdev, u8 page_shift,
			   enum mlx5e_mpwrq_umr_mode umr_mode);
u8 mlx5e_mpwrq_umr_wqebbs(struct mlx5_core_dev *mdev, u8 page_shift,
			  enum mlx5e_mpwrq_umr_mode umr_mode);
u8 mlx5e_mpwrq_mtts_per_wqe(struct mlx5_core_dev *mdev, u8 page_shift,
			    enum mlx5e_mpwrq_umr_mode umr_mode);
u32 mlx5e_mpwrq_max_num_entries(struct mlx5_core_dev *mdev,
				enum mlx5e_mpwrq_umr_mode umr_mode);
u8 mlx5e_mpwrq_max_log_rq_pkts(struct mlx5_core_dev *mdev, u8 page_shift,
			       enum mlx5e_mpwrq_umr_mode umr_mode);

/* Parameter calculations */

void mlx5e_reset_tx_moderation(struct mlx5e_params *params, u8 cq_period_mode);
void mlx5e_reset_rx_moderation(struct mlx5e_params *params, u8 cq_period_mode);
void mlx5e_set_tx_cq_mode_params(struct mlx5e_params *params, u8 cq_period_mode);
void mlx5e_set_rx_cq_mode_params(struct mlx5e_params *params, u8 cq_period_mode);

bool slow_pci_heuristic(struct mlx5_core_dev *mdev);
int mlx5e_mpwrq_validate_regular(struct mlx5_core_dev *mdev, struct mlx5e_params *params);
int mlx5e_mpwrq_validate_xsk(struct mlx5_core_dev *mdev, struct mlx5e_params *params,
			     struct mlx5e_xsk_param *xsk);
void mlx5e_build_rq_params(struct mlx5_core_dev *mdev, struct mlx5e_params *params);
void mlx5e_set_rq_type(struct mlx5_core_dev *mdev, struct mlx5e_params *params);
void mlx5e_init_rq_type_params(struct mlx5_core_dev *mdev, struct mlx5e_params *params);

u16 mlx5e_get_linear_rq_headroom(struct mlx5e_params *params,
				 struct mlx5e_xsk_param *xsk);
bool mlx5e_rx_is_linear_skb(struct mlx5_core_dev *mdev,
			    struct mlx5e_params *params,
			    struct mlx5e_xsk_param *xsk);
bool mlx5e_rx_mpwqe_is_linear_skb(struct mlx5_core_dev *mdev,
				  struct mlx5e_params *params,
				  struct mlx5e_xsk_param *xsk);
u8 mlx5e_mpwqe_get_log_rq_size(struct mlx5_core_dev *mdev,
			       struct mlx5e_params *params,
			       struct mlx5e_xsk_param *xsk);
u8 mlx5e_shampo_get_log_hd_entry_size(struct mlx5_core_dev *mdev,
				      struct mlx5e_params *params);
u8 mlx5e_shampo_get_log_rsrv_size(struct mlx5_core_dev *mdev,
				  struct mlx5e_params *params);
u8 mlx5e_shampo_get_log_pkt_per_rsrv(struct mlx5_core_dev *mdev,
				     struct mlx5e_params *params);
u32 mlx5e_shampo_hd_per_wqe(struct mlx5_core_dev *mdev,
			    struct mlx5e_params *params,
			    struct mlx5e_rq_param *rq_param);
u32 mlx5e_shampo_hd_per_wq(struct mlx5_core_dev *mdev,
			   struct mlx5e_params *params,
			   struct mlx5e_rq_param *rq_param);
u8 mlx5e_mpwqe_get_log_stride_size(struct mlx5_core_dev *mdev,
				   struct mlx5e_params *params,
				   struct mlx5e_xsk_param *xsk);
u8 mlx5e_mpwqe_get_log_num_strides(struct mlx5_core_dev *mdev,
				   struct mlx5e_params *params,
				   struct mlx5e_xsk_param *xsk);
u8 mlx5e_mpwqe_get_min_wqe_bulk(unsigned int wq_sz);
u16 mlx5e_get_rq_headroom(struct mlx5_core_dev *mdev,
			  struct mlx5e_params *params,
			  struct mlx5e_xsk_param *xsk);

/* Build queue parameters */

void mlx5e_build_create_cq_param(struct mlx5e_create_cq_param *ccp, struct mlx5e_channel *c);
int mlx5e_build_rq_param(struct mlx5_core_dev *mdev,
			 struct mlx5e_params *params,
			 struct mlx5e_xsk_param *xsk,
			 u16 q_counter,
			 struct mlx5e_rq_param *param);
void mlx5e_build_drop_rq_param(struct mlx5_core_dev *mdev,
			       u16 q_counter,
			       struct mlx5e_rq_param *param);
void mlx5e_build_sq_param_common(struct mlx5_core_dev *mdev,
				 struct mlx5e_sq_param *param);
void mlx5e_build_sq_param(struct mlx5_core_dev *mdev,
			  struct mlx5e_params *params,
			  struct mlx5e_sq_param *param);
void mlx5e_build_tx_cq_param(struct mlx5_core_dev *mdev,
			     struct mlx5e_params *params,
			     struct mlx5e_cq_param *param);
void mlx5e_build_xdpsq_param(struct mlx5_core_dev *mdev,
			     struct mlx5e_params *params,
			     struct mlx5e_xsk_param *xsk,
			     struct mlx5e_sq_param *param);
int mlx5e_build_channel_param(struct mlx5_core_dev *mdev,
			      struct mlx5e_params *params,
			      u16 q_counter,
			      struct mlx5e_channel_param *cparam);

u16 mlx5e_calc_sq_stop_room(struct mlx5_core_dev *mdev, struct mlx5e_params *params);
int mlx5e_validate_params(struct mlx5_core_dev *mdev, struct mlx5e_params *params);

static inline void mlx5e_params_print_info(struct mlx5_core_dev *mdev,
					   struct mlx5e_params *params)
{
	mlx5_core_info(mdev, "MLX5E: StrdRq(%d) RqSz(%ld) StrdSz(%ld) RxCqeCmprss(%d %s)\n",
		       params->rq_wq_type == MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ,
		       params->rq_wq_type == MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ ?
		       BIT(mlx5e_mpwqe_get_log_rq_size(mdev, params, NULL)) :
		       BIT(params->log_rq_mtu_frames),
		       BIT(mlx5e_mpwqe_get_log_stride_size(mdev, params, NULL)),
		       MLX5E_GET_PFLAG(params, MLX5E_PFLAG_RX_CQE_COMPRESS),
		       MLX5_CAP_GEN(mdev, enhanced_cqe_compression) ?
				       "enhanced" : "basic");
};

#endif /* __MLX5_EN_PARAMS_H__ */
