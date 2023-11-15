/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021, Mellanox Technologies inc. All rights reserved. */

#ifndef __MLX5_EN_RX_RES_H__
#define __MLX5_EN_RX_RES_H__

#include <linux/kernel.h>
#include "rqt.h"
#include "tir.h"
#include "fs.h"
#include "rss.h"

struct mlx5e_rx_res;

struct mlx5e_channels;
struct mlx5e_rss_params_hash;

enum mlx5e_rx_res_features {
	MLX5E_RX_RES_FEATURE_INNER_FT = BIT(0),
	MLX5E_RX_RES_FEATURE_PTP = BIT(1),
};

/* Setup */
struct mlx5e_rx_res *
mlx5e_rx_res_create(struct mlx5_core_dev *mdev, enum mlx5e_rx_res_features features,
		    unsigned int max_nch, u32 drop_rqn,
		    const struct mlx5e_packet_merge_param *init_pkt_merge_param,
		    unsigned int init_nch);
void mlx5e_rx_res_destroy(struct mlx5e_rx_res *res);

/* TIRN getters for flow steering */
u32 mlx5e_rx_res_get_tirn_direct(struct mlx5e_rx_res *res, unsigned int ix);
u32 mlx5e_rx_res_get_tirn_rss(struct mlx5e_rx_res *res, enum mlx5_traffic_types tt);
u32 mlx5e_rx_res_get_tirn_rss_inner(struct mlx5e_rx_res *res, enum mlx5_traffic_types tt);
u32 mlx5e_rx_res_get_tirn_ptp(struct mlx5e_rx_res *res);

/* Activate/deactivate API */
void mlx5e_rx_res_channels_activate(struct mlx5e_rx_res *res, struct mlx5e_channels *chs);
void mlx5e_rx_res_channels_deactivate(struct mlx5e_rx_res *res);
void mlx5e_rx_res_xsk_update(struct mlx5e_rx_res *res, struct mlx5e_channels *chs,
			     unsigned int ix, bool xsk);

/* Configuration API */
void mlx5e_rx_res_rss_set_indir_uniform(struct mlx5e_rx_res *res, unsigned int nch);
int mlx5e_rx_res_rss_get_rxfh(struct mlx5e_rx_res *res, u32 rss_idx,
			      u32 *indir, u8 *key, u8 *hfunc);
int mlx5e_rx_res_rss_set_rxfh(struct mlx5e_rx_res *res, u32 rss_idx,
			      const u32 *indir, const u8 *key, const u8 *hfunc);

int mlx5e_rx_res_rss_get_hash_fields(struct mlx5e_rx_res *res, u32 rss_idx,
				     enum mlx5_traffic_types tt);
int mlx5e_rx_res_rss_set_hash_fields(struct mlx5e_rx_res *res, u32 rss_idx,
				     enum mlx5_traffic_types tt, u8 rx_hash_fields);
int mlx5e_rx_res_packet_merge_set_param(struct mlx5e_rx_res *res,
					struct mlx5e_packet_merge_param *pkt_merge_param);

int mlx5e_rx_res_rss_init(struct mlx5e_rx_res *res, u32 *rss_idx, unsigned int init_nch);
int mlx5e_rx_res_rss_destroy(struct mlx5e_rx_res *res, u32 rss_idx);
int mlx5e_rx_res_rss_cnt(struct mlx5e_rx_res *res);
int mlx5e_rx_res_rss_index(struct mlx5e_rx_res *res, struct mlx5e_rss *rss);
struct mlx5e_rss *mlx5e_rx_res_rss_get(struct mlx5e_rx_res *res, u32 rss_idx);
void mlx5e_rx_res_rss_update_num_channels(struct mlx5e_rx_res *res, u32 nch);

/* Workaround for hairpin */
struct mlx5e_rss_params_hash mlx5e_rx_res_get_current_hash(struct mlx5e_rx_res *res);

/* Accel TIRs */
int mlx5e_rx_res_tls_tir_create(struct mlx5e_rx_res *res, unsigned int rxq,
				struct mlx5e_tir *tir);
#endif /* __MLX5_EN_RX_RES_H__ */
