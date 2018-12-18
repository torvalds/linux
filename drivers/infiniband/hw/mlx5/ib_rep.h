/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2018 Mellanox Technologies. All rights reserved.
 */

#ifndef __MLX5_IB_REP_H__
#define __MLX5_IB_REP_H__

#include <linux/mlx5/eswitch.h>
#include "mlx5_ib.h"

#ifdef CONFIG_MLX5_ESWITCH
u8 mlx5_ib_eswitch_mode(struct mlx5_eswitch *esw);
struct mlx5_ib_dev *mlx5_ib_get_rep_ibdev(struct mlx5_eswitch *esw,
					  int vport_index);
struct mlx5_ib_dev *mlx5_ib_get_uplink_ibdev(struct mlx5_eswitch *esw);
struct mlx5_eswitch_rep *mlx5_ib_vport_rep(struct mlx5_eswitch *esw,
					   int vport_index);
void mlx5_ib_register_vport_reps(struct mlx5_ib_dev *dev);
void mlx5_ib_unregister_vport_reps(struct mlx5_ib_dev *dev);
int create_flow_rule_vport_sq(struct mlx5_ib_dev *dev,
			      struct mlx5_ib_sq *sq);
struct net_device *mlx5_ib_get_rep_netdev(struct mlx5_eswitch *esw,
					  int vport_index);
#else /* CONFIG_MLX5_ESWITCH */
static inline u8 mlx5_ib_eswitch_mode(struct mlx5_eswitch *esw)
{
	return SRIOV_NONE;
}

static inline
struct mlx5_ib_dev *mlx5_ib_get_rep_ibdev(struct mlx5_eswitch *esw,
					  int vport_index)
{
	return NULL;
}

static inline
struct mlx5_ib_dev *mlx5_ib_get_uplink_ibdev(struct mlx5_eswitch *esw)
{
	return NULL;
}

static inline
struct mlx5_eswitch_rep *mlx5_ib_vport_rep(struct mlx5_eswitch *esw,
					   int vport_index)
{
	return NULL;
}

static inline void mlx5_ib_register_vport_reps(struct mlx5_ib_dev *dev) {}
static inline void mlx5_ib_unregister_vport_reps(struct mlx5_ib_dev *dev) {}
static inline int create_flow_rule_vport_sq(struct mlx5_ib_dev *dev,
					    struct mlx5_ib_sq *sq)
{
	return 0;
}

static inline
struct net_device *mlx5_ib_get_rep_netdev(struct mlx5_eswitch *esw,
					  int vport_index)
{
	return NULL;
}
#endif

static inline
struct mlx5_ib_dev *mlx5_ib_rep_to_dev(struct mlx5_eswitch_rep *rep)
{
	return (struct mlx5_ib_dev *)rep->rep_if[REP_IB].priv;
}
#endif /* __MLX5_IB_REP_H__ */
