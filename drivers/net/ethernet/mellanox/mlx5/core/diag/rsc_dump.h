/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __MLX5_RSC_DUMP_H
#define __MLX5_RSC_DUMP_H

#include <linux/mlx5/rsc_dump.h>
#include <linux/mlx5/driver.h>
#include "mlx5_core.h"

#define MLX5_RSC_DUMP_ALL 0xFFFF
struct mlx5_rsc_dump_cmd;
struct mlx5_rsc_dump;

struct mlx5_rsc_dump *mlx5_rsc_dump_create(struct mlx5_core_dev *dev);
void mlx5_rsc_dump_destroy(struct mlx5_core_dev *dev);

int mlx5_rsc_dump_init(struct mlx5_core_dev *dev);
void mlx5_rsc_dump_cleanup(struct mlx5_core_dev *dev);

struct mlx5_rsc_dump_cmd *mlx5_rsc_dump_cmd_create(struct mlx5_core_dev *dev,
						   struct mlx5_rsc_key *key);
void mlx5_rsc_dump_cmd_destroy(struct mlx5_rsc_dump_cmd *cmd);

int mlx5_rsc_dump_next(struct mlx5_core_dev *dev, struct mlx5_rsc_dump_cmd *cmd,
		       struct page *page, int *size);
#endif
