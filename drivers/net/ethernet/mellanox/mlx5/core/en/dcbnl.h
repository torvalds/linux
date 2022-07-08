/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2020 Mellanox Technologies. */

#ifndef __MLX5E_DCBNL_H__
#define __MLX5E_DCBNL_H__

#ifdef CONFIG_MLX5_CORE_EN_DCB

#define MLX5E_MAX_PRIORITY (8)

struct mlx5e_cee_config {
	/* bw pct for priority group */
	u8                         pg_bw_pct[CEE_DCBX_MAX_PGS];
	u8                         prio_to_pg_map[CEE_DCBX_MAX_PRIO];
	bool                       pfc_setting[CEE_DCBX_MAX_PRIO];
	bool                       pfc_enable;
};

struct mlx5e_dcbx {
	enum mlx5_dcbx_oper_mode   mode;
	struct mlx5e_cee_config    cee_cfg; /* pending configuration */
	u8                         dscp_app_cnt;

	/* The only setting that cannot be read from FW */
	u8                         tc_tsa[IEEE_8021QAZ_MAX_TCS];
	u8                         cap;

	/* Buffer configuration */
	bool                       manual_buffer;
	u32                        cable_len;
	u32                        xoff;
	u16                        port_buff_cell_sz;
};

#define MLX5E_MAX_DSCP (64)

struct mlx5e_dcbx_dp {
	u8                         dscp2prio[MLX5E_MAX_DSCP];
	u8                         trust_state;
};

void mlx5e_dcbnl_build_netdev(struct net_device *netdev);
void mlx5e_dcbnl_initialize(struct mlx5e_priv *priv);
void mlx5e_dcbnl_init_app(struct mlx5e_priv *priv);
void mlx5e_dcbnl_delete_app(struct mlx5e_priv *priv);
#else
static inline void mlx5e_dcbnl_build_netdev(struct net_device *netdev) {}
static inline void mlx5e_dcbnl_initialize(struct mlx5e_priv *priv) {}
static inline void mlx5e_dcbnl_init_app(struct mlx5e_priv *priv) {}
static inline void mlx5e_dcbnl_delete_app(struct mlx5e_priv *priv) {}
#endif

#endif /* __MLX5E_DCBNL_H__ */
