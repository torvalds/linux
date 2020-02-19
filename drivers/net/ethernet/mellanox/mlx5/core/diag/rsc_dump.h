/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __MLX5_RSC_DUMP_H
#define __MLX5_RSC_DUMP__H

#include <linux/mlx5/driver.h>
#include "mlx5_core.h"

enum mlx5_sgmt_type {
	MLX5_SGMT_TYPE_HW_CQPC,
	MLX5_SGMT_TYPE_HW_SQPC,
	MLX5_SGMT_TYPE_HW_RQPC,
	MLX5_SGMT_TYPE_FULL_SRQC,
	MLX5_SGMT_TYPE_FULL_CQC,
	MLX5_SGMT_TYPE_FULL_EQC,
	MLX5_SGMT_TYPE_FULL_QPC,
	MLX5_SGMT_TYPE_SND_BUFF,
	MLX5_SGMT_TYPE_RCV_BUFF,
	MLX5_SGMT_TYPE_SRQ_BUFF,
	MLX5_SGMT_TYPE_CQ_BUFF,
	MLX5_SGMT_TYPE_EQ_BUFF,
	MLX5_SGMT_TYPE_SX_SLICE,
	MLX5_SGMT_TYPE_SX_SLICE_ALL,
	MLX5_SGMT_TYPE_RDB,
	MLX5_SGMT_TYPE_RX_SLICE_ALL,
	MLX5_SGMT_TYPE_MENU,
	MLX5_SGMT_TYPE_TERMINATE,

	MLX5_SGMT_TYPE_NUM, /* Keep last */
};

struct mlx5_rsc_key {
	enum mlx5_sgmt_type rsc;
	int index1;
	int index2;
	int num_of_obj1;
	int num_of_obj2;
	int size;
};

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
