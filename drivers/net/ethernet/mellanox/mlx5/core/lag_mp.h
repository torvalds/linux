/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __MLX5_LAG_MP_H__
#define __MLX5_LAG_MP_H__

#include "lag.h"
#include "mlx5_core.h"

struct lag_mp {
	struct notifier_block     fib_nb;
	struct fib_info           *mfi; /* used in tracking fib events */
};

#ifdef CONFIG_MLX5_ESWITCH

int mlx5_lag_mp_init(struct mlx5_lag *ldev);
void mlx5_lag_mp_cleanup(struct mlx5_lag *ldev);

#else /* CONFIG_MLX5_ESWITCH */

static inline int mlx5_lag_mp_init(struct mlx5_lag *ldev) { return 0; }
static inline void mlx5_lag_mp_cleanup(struct mlx5_lag *ldev) {}

#endif /* CONFIG_MLX5_ESWITCH */
#endif /* __MLX5_LAG_MP_H__ */
