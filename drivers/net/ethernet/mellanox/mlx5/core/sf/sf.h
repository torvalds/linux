/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2020 Mellanox Technologies Ltd */

#ifndef __MLX5_SF_H__
#define __MLX5_SF_H__

#include <linux/mlx5/driver.h>
#include "lib/sf.h"

#ifdef CONFIG_MLX5_SF_MANAGER

int mlx5_sf_hw_table_init(struct mlx5_core_dev *dev);
void mlx5_sf_hw_table_cleanup(struct mlx5_core_dev *dev);

int mlx5_sf_hw_table_create(struct mlx5_core_dev *dev);
void mlx5_sf_hw_table_destroy(struct mlx5_core_dev *dev);

int mlx5_sf_table_init(struct mlx5_core_dev *dev);
void mlx5_sf_table_cleanup(struct mlx5_core_dev *dev);

int mlx5_devlink_sf_port_new(struct devlink *devlink,
			     const struct devlink_port_new_attrs *add_attr,
			     struct netlink_ext_ack *extack,
			     unsigned int *new_port_index);
int mlx5_devlink_sf_port_del(struct devlink *devlink, unsigned int port_index,
			     struct netlink_ext_ack *extack);
int mlx5_devlink_sf_port_fn_state_get(struct devlink_port *dl_port,
				      enum devlink_port_fn_state *state,
				      enum devlink_port_fn_opstate *opstate,
				      struct netlink_ext_ack *extack);
int mlx5_devlink_sf_port_fn_state_set(struct devlink_port *dl_port,
				      enum devlink_port_fn_state state,
				      struct netlink_ext_ack *extack);
#else

static inline int mlx5_sf_hw_table_init(struct mlx5_core_dev *dev)
{
	return 0;
}

static inline void mlx5_sf_hw_table_cleanup(struct mlx5_core_dev *dev)
{
}

static inline int mlx5_sf_hw_table_create(struct mlx5_core_dev *dev)
{
	return 0;
}

static inline void mlx5_sf_hw_table_destroy(struct mlx5_core_dev *dev)
{
}

static inline int mlx5_sf_table_init(struct mlx5_core_dev *dev)
{
	return 0;
}

static inline void mlx5_sf_table_cleanup(struct mlx5_core_dev *dev)
{
}

#endif

#endif
