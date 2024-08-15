/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. */

#ifndef __MLX5E_FS_ETHTOOL_H__
#define __MLX5E_FS_ETHTOOL_H__

struct mlx5e_priv;
struct mlx5e_ethtool_steering;
#ifdef CONFIG_MLX5_EN_RXNFC
int mlx5e_ethtool_alloc(struct mlx5e_ethtool_steering **ethtool);
void mlx5e_ethtool_free(struct mlx5e_ethtool_steering *ethtool);
void mlx5e_ethtool_init_steering(struct mlx5e_flow_steering *fs);
void mlx5e_ethtool_cleanup_steering(struct mlx5e_flow_steering *fs);
int mlx5e_ethtool_set_rxnfc(struct mlx5e_priv *priv, struct ethtool_rxnfc *cmd);
int mlx5e_ethtool_get_rxnfc(struct mlx5e_priv *priv,
			    struct ethtool_rxnfc *info, u32 *rule_locs);
#else
static inline int mlx5e_ethtool_alloc(struct mlx5e_ethtool_steering **ethtool)
{ return 0; }
static inline void mlx5e_ethtool_free(struct mlx5e_ethtool_steering *ethtool) { }
static inline void mlx5e_ethtool_init_steering(struct mlx5e_flow_steering *fs) { }
static inline void mlx5e_ethtool_cleanup_steering(struct mlx5e_flow_steering *fs) { }
static inline int mlx5e_ethtool_set_rxnfc(struct mlx5e_priv *priv, struct ethtool_rxnfc *cmd)
{ return -EOPNOTSUPP; }
static inline int mlx5e_ethtool_get_rxnfc(struct mlx5e_priv *priv,
					  struct ethtool_rxnfc *info, u32 *rule_locs)
{ return -EOPNOTSUPP; }
#endif
#endif
