/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2018 Mellanox Technologies. All rights reserved.
 */

#ifndef __MLX5_IB_REP_H__
#define __MLX5_IB_REP_H__

#include <linux/mlx5/eswitch.h>
#include "mlx5_ib.h"

extern const struct mlx5_ib_profile raw_eth_profile;

#ifdef CONFIG_MLX5_ESWITCH
int mlx5r_rep_init(void);
void mlx5r_rep_cleanup(void);
struct mlx5_flow_handle *create_flow_rule_vport_sq(struct mlx5_ib_dev *dev,
						   struct mlx5_ib_sq *sq,
						   u16 port);
struct net_device *mlx5_ib_get_rep_netdev(struct mlx5_eswitch *esw,
					  u16 vport_num);
#else /* CONFIG_MLX5_ESWITCH */
static inline int mlx5r_rep_init(void) { return 0; }
static inline void mlx5r_rep_cleanup(void) {}
static inline
struct mlx5_flow_handle *create_flow_rule_vport_sq(struct mlx5_ib_dev *dev,
						   struct mlx5_ib_sq *sq,
						   u16 port)
{
	return NULL;
}

static inline
struct net_device *mlx5_ib_get_rep_netdev(struct mlx5_eswitch *esw,
					  u16 vport_num)
{
	return NULL;
}
#endif
#endif /* __MLX5_IB_REP_H__ */
