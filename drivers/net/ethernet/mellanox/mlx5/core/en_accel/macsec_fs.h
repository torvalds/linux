/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __MLX5_MACSEC_STEERING_H__
#define __MLX5_MACSEC_STEERING_H__

#ifdef CONFIG_MLX5_EN_MACSEC

#include "en_accel/macsec.h"

#define MLX5_MACSEC_NUM_OF_SUPPORTED_INTERFACES 16

struct mlx5e_macsec_fs;
struct mlx5e_macsec_tx_rule;

struct mlx5_macsec_rule_attrs {
	u32 macsec_obj_id;
	int action;
};

enum mlx5_macsec_action {
	MLX5_ACCEL_MACSEC_ACTION_ENCRYPT,
};

void mlx5e_macsec_fs_cleanup(struct mlx5e_macsec_fs *macsec_fs);

struct mlx5e_macsec_fs *
mlx5e_macsec_fs_init(struct mlx5_core_dev *mdev, struct net_device *netdev);

struct mlx5e_macsec_tx_rule *
mlx5e_macsec_fs_add_rule(struct mlx5e_macsec_fs *macsec_fs,
			 const struct macsec_context *ctx,
			 struct mlx5_macsec_rule_attrs *attrs,
			 u32 *sa_fs_id);

void mlx5e_macsec_fs_del_rule(struct mlx5e_macsec_fs *macsec_fs,
			      struct mlx5e_macsec_tx_rule *macsec_rule,
			      int action);

#endif

#endif /* __MLX5_MACSEC_STEERING_H__ */
