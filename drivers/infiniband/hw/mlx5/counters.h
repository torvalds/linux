/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2013-2020, Mellanox Technologies inc. All rights reserved.
 */

#ifndef _MLX5_IB_COUNTERS_H
#define _MLX5_IB_COUNTERS_H

#include "mlx5_ib.h"

int mlx5_ib_counters_init(struct mlx5_ib_dev *dev);
void mlx5_ib_counters_cleanup(struct mlx5_ib_dev *dev);
void mlx5_ib_counters_clear_description(struct ib_counters *counters);
int mlx5_ib_flow_counters_set_data(struct ib_counters *ibcounters,
				   struct mlx5_ib_create_flow *ucmd);
u16 mlx5_ib_get_counters_id(struct mlx5_ib_dev *dev, u32 port_num);
#endif /* _MLX5_IB_COUNTERS_H */
