/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2013-2020, Mellanox Technologies inc. All rights reserved.
 */

#ifndef _MLX5_IB_COUNTERS_H
#define _MLX5_IB_COUNTERS_H

#include "mlx5_ib.h"

struct mlx5_rdma_counter {
	struct rdma_counter rdma_counter;

	struct mlx5_fc *fc[MLX5_IB_OPCOUNTER_MAX];
	struct xarray qpn_opfc_xa;
};

static inline struct mlx5_rdma_counter *
to_mcounter(struct rdma_counter *counter)
{
	return container_of(counter, struct mlx5_rdma_counter, rdma_counter);
}

int mlx5_ib_counters_init(struct mlx5_ib_dev *dev);
void mlx5_ib_counters_cleanup(struct mlx5_ib_dev *dev);
void mlx5_ib_counters_clear_description(struct ib_counters *counters);
int mlx5_ib_flow_counters_set_data(struct ib_counters *ibcounters,
				   struct mlx5_ib_create_flow *ucmd);
u16 mlx5_ib_get_counters_id(struct mlx5_ib_dev *dev, u32 port_num);
bool mlx5r_is_opfc_shared_and_in_use(struct mlx5_ib_op_fc *opfcs, u32 type,
				     struct mlx5_ib_op_fc **opfc);
#endif /* _MLX5_IB_COUNTERS_H */
