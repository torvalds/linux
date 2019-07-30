/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __MLX5_PORT_TUN_H__
#define __MLX5_PORT_TUN_H__

#include <linux/mlx5/driver.h>

struct mlx5_tun_entropy {
	struct mlx5_core_dev *mdev;
	u32 num_enabling_entries;
	u32 num_disabling_entries;
	u8  enabled;
	struct mutex lock;	/* lock the entropy fields */
};

void mlx5_init_port_tun_entropy(struct mlx5_tun_entropy *tun_entropy,
				struct mlx5_core_dev *mdev);
int mlx5_tun_entropy_refcount_inc(struct mlx5_tun_entropy *tun_entropy,
				  int reformat_type);
void mlx5_tun_entropy_refcount_dec(struct mlx5_tun_entropy *tun_entropy,
				   int reformat_type);

#endif /* __MLX5_PORT_TUN_H__ */
