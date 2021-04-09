/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021, Mellanox Technologies inc. All rights reserved. */

#ifndef __MLX5_EN_RX_RES_H__
#define __MLX5_EN_RX_RES_H__

#include <linux/kernel.h>
#include "rqt.h"
#include "tir.h"
#include "fs.h"

#define MLX5E_MAX_NUM_CHANNELS (MLX5E_INDIR_RQT_SIZE / 2)

struct mlx5e_rx_res;

struct mlx5e_channels;
struct mlx5e_rss_params_hash;

enum mlx5e_rx_res_features {
	MLX5E_RX_RES_FEATURE_INNER_FT = BIT(0),
	MLX5E_RX_RES_FEATURE_XSK = BIT(1),
	MLX5E_RX_RES_FEATURE_PTP = BIT(2),
};

struct mlx5e_rss_params_traffic_type
mlx5e_rss_get_default_tt_config(enum mlx5e_traffic_types tt);

/* Setup */
struct mlx5e_rx_res *mlx5e_rx_res_alloc(void);
int mlx5e_rx_res_init(struct mlx5e_rx_res *res, struct mlx5_core_dev *mdev,
		      enum mlx5e_rx_res_features features, unsigned int max_nch,
		      u32 drop_rqn, const struct mlx5e_lro_param *init_lro_param,
		      unsigned int init_nch);
void mlx5e_rx_res_destroy(struct mlx5e_rx_res *res);
void mlx5e_rx_res_free(struct mlx5e_rx_res *res);

/* TIRN getters for flow steering */
u32 mlx5e_rx_res_get_tirn_direct(struct mlx5e_rx_res *res, unsigned int ix);
u32 mlx5e_rx_res_get_tirn_xsk(struct mlx5e_rx_res *res, unsigned int ix);
u32 mlx5e_rx_res_get_tirn_rss(struct mlx5e_rx_res *res, enum mlx5e_traffic_types tt);
u32 mlx5e_rx_res_get_tirn_rss_inner(struct mlx5e_rx_res *res, enum mlx5e_traffic_types tt);
u32 mlx5e_rx_res_get_tirn_ptp(struct mlx5e_rx_res *res);

/* RQTN getters for modules that create their own TIRs */
u32 mlx5e_rx_res_get_rqtn_direct(struct mlx5e_rx_res *res, unsigned int ix);

/* Activate/deactivate API */
void mlx5e_rx_res_channels_activate(struct mlx5e_rx_res *res, struct mlx5e_channels *chs);
void mlx5e_rx_res_channels_deactivate(struct mlx5e_rx_res *res);
int mlx5e_rx_res_xsk_activate(struct mlx5e_rx_res *res, struct mlx5e_channels *chs,
			      unsigned int ix);
int mlx5e_rx_res_xsk_deactivate(struct mlx5e_rx_res *res, unsigned int ix);

/* Configuration API */
struct mlx5e_rss_params_traffic_type
mlx5e_rx_res_rss_get_current_tt_config(struct mlx5e_rx_res *res, enum mlx5e_traffic_types tt);
void mlx5e_rx_res_rss_set_indir_uniform(struct mlx5e_rx_res *res, unsigned int nch);
void mlx5e_rx_res_rss_get_rxfh(struct mlx5e_rx_res *res, u32 *indir, u8 *key, u8 *hfunc);
int mlx5e_rx_res_rss_set_rxfh(struct mlx5e_rx_res *res, const u32 *indir,
			      const u8 *key, const u8 *hfunc);
u8 mlx5e_rx_res_rss_get_hash_fields(struct mlx5e_rx_res *res, enum mlx5e_traffic_types tt);
int mlx5e_rx_res_rss_set_hash_fields(struct mlx5e_rx_res *res, enum mlx5e_traffic_types tt,
				     u8 rx_hash_fields);
int mlx5e_rx_res_lro_set_param(struct mlx5e_rx_res *res, struct mlx5e_lro_param *lro_param);

/* Workaround for hairpin */
struct mlx5e_rss_params_hash mlx5e_rx_res_get_current_hash(struct mlx5e_rx_res *res);

#endif /* __MLX5_EN_RX_RES_H__ */
