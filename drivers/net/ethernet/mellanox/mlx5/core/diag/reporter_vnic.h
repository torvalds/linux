/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
 * Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES.
 */
#ifndef __MLX5_REPORTER_VNIC_H
#define __MLX5_REPORTER_VNIC_H

#include "mlx5_core.h"

void mlx5_reporter_vnic_create(struct mlx5_core_dev *dev);
void mlx5_reporter_vnic_destroy(struct mlx5_core_dev *dev);

int mlx5_reporter_vnic_diagnose_counters(struct mlx5_core_dev *dev,
					 struct devlink_fmsg *fmsg,
					 u16 vport_num, bool other_vport);

#endif /* __MLX5_REPORTER_VNIC_H */
