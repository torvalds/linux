/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __MLX5_ECPF_H__
#define __MLX5_ECPF_H__

#include <linux/mlx5/driver.h>
#include "mlx5_core.h"

#ifdef CONFIG_MLX5_ESWITCH

enum {
	MLX5_ECPU_BIT_NUM = 23,
};

bool mlx5_read_embedded_cpu(struct mlx5_core_dev *dev);
int mlx5_ec_init(struct mlx5_core_dev *dev);
void mlx5_ec_cleanup(struct mlx5_core_dev *dev);

#else  /* CONFIG_MLX5_ESWITCH */

static inline bool
mlx5_read_embedded_cpu(struct mlx5_core_dev *dev) { return false; }
static inline int mlx5_ec_init(struct mlx5_core_dev *dev) { return 0; }
static inline void mlx5_ec_cleanup(struct mlx5_core_dev *dev) {}

#endif /* CONFIG_MLX5_ESWITCH */

#endif /* __MLX5_ECPF_H__ */
