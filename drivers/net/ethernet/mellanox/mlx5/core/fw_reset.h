/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2020, Mellanox Technologies inc.  All rights reserved. */

#ifndef __MLX5_FW_RESET_H
#define __MLX5_FW_RESET_H

#include "mlx5_core.h"

int mlx5_fw_reset_query(struct mlx5_core_dev *dev, u8 *reset_level, u8 *reset_type);
int mlx5_fw_reset_set_reset_sync(struct mlx5_core_dev *dev, u8 reset_type_sel);
int mlx5_fw_reset_set_live_patch(struct mlx5_core_dev *dev);

void mlx5_fw_reset_events_start(struct mlx5_core_dev *dev);
void mlx5_fw_reset_events_stop(struct mlx5_core_dev *dev);
int mlx5_fw_reset_init(struct mlx5_core_dev *dev);
void mlx5_fw_reset_cleanup(struct mlx5_core_dev *dev);

#endif
