// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include <linux/ethtool.h>
#include <net/sock.h>

#include "en.h"
#include "en_accel/macsec.h"

static const struct counter_desc mlx5e_macsec_hw_stats_desc[] = {
	{ MLX5E_DECLARE_STAT(struct mlx5e_macsec_stats, macsec_rx_pkts) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_macsec_stats, macsec_rx_bytes) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_macsec_stats, macsec_rx_pkts_drop) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_macsec_stats, macsec_rx_bytes_drop) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_macsec_stats, macsec_tx_pkts) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_macsec_stats, macsec_tx_bytes) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_macsec_stats, macsec_tx_pkts_drop) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_macsec_stats, macsec_tx_bytes_drop) },
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
		return idx;

	if (!mlx5e_is_macsec_device(priv->mdev))
		return idx;

	for (i = 0; i < NUM_MACSEC_HW_COUNTERS; i++)
		strcpy(data + (idx++) * ETH_GSTRING_LEN,
		       mlx5e_macsec_hw_stats_desc[i].format);

	return idx;
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STATS(macsec_hw)
{
	int i;

	if (!priv->macsec)
		return idx;

	if (!mlx5e_is_macsec_device(priv->mdev))
		return idx;

	mlx5e_macsec_get_stats_fill(priv->macsec, mlx5e_macsec_get_stats(priv->macsec));
	for (i = 0; i < NUM_MACSEC_HW_COUNTERS; i++)
		data[idx++] = MLX5E_READ_CTR64_CPU(mlx5e_macsec_get_stats(priv->macsec),
						   mlx5e_macsec_hw_stats_desc,
						   i);

	return idx;
}

MLX5E_DEFINE_STATS_GRP(macsec_hw, 0);
