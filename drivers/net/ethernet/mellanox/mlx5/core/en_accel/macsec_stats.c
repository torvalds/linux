// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include <linux/ethtool.h>
#include <net/sock.h>

#include "en.h"
#include "en_accel/macsec.h"

static const struct counter_desc mlx5e_macsec_hw_stats_desc[] = {
	{ MLX5E_DECLARE_STAT(struct mlx5_macsec_stats, macsec_rx_pkts) },
	{ MLX5E_DECLARE_STAT(struct mlx5_macsec_stats, macsec_rx_bytes) },
	{ MLX5E_DECLARE_STAT(struct mlx5_macsec_stats, macsec_rx_pkts_drop) },
	{ MLX5E_DECLARE_STAT(struct mlx5_macsec_stats, macsec_rx_bytes_drop) },
	{ MLX5E_DECLARE_STAT(struct mlx5_macsec_stats, macsec_tx_pkts) },
	{ MLX5E_DECLARE_STAT(struct mlx5_macsec_stats, macsec_tx_bytes) },
	{ MLX5E_DECLARE_STAT(struct mlx5_macsec_stats, macsec_tx_pkts_drop) },
	{ MLX5E_DECLARE_STAT(struct mlx5_macsec_stats, macsec_tx_bytes_drop) },
};

#define NUM_MACSEC_HW_COUNTERS ARRAY_SIZE(mlx5e_macsec_hw_stats_desc)

static MLX5E_DECLARE_STATS_GRP_OP_NUM_STATS(macsec_hw)
{
	if (!priv->macsec)
		return 0;

	if (mlx5e_is_macsec_device(priv->mdev))
		return NUM_MACSEC_HW_COUNTERS;

	return 0;
}

static MLX5E_DECLARE_STATS_GRP_OP_UPDATE_STATS(macsec_hw) {}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STRS(macsec_hw)
{
	unsigned int i;

	if (!priv->macsec)
		return;

	if (!mlx5e_is_macsec_device(priv->mdev))
		return;

	for (i = 0; i < NUM_MACSEC_HW_COUNTERS; i++)
		ethtool_puts(data, mlx5e_macsec_hw_stats_desc[i].format);
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STATS(macsec_hw)
{
	struct mlx5_macsec_fs *macsec_fs;
	int i;

	if (!priv->macsec)
		return;

	if (!mlx5e_is_macsec_device(priv->mdev))
		return;

	macsec_fs = priv->mdev->macsec_fs;
	mlx5_macsec_fs_get_stats_fill(macsec_fs, mlx5_macsec_fs_get_stats(macsec_fs));
	for (i = 0; i < NUM_MACSEC_HW_COUNTERS; i++)
		mlx5e_ethtool_put_stat(
			data, MLX5E_READ_CTR64_CPU(
				      mlx5_macsec_fs_get_stats(macsec_fs),
				      mlx5e_macsec_hw_stats_desc, i));
}

MLX5E_DEFINE_STATS_GRP(macsec_hw, 0);
