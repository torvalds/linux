/*
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <linux/ethtool.h>
#include <net/sock.h>

#include "en.h"
#include "ipsec.h"

static const struct counter_desc mlx5e_ipsec_hw_stats_desc[] = {
	{ MLX5E_DECLARE_STAT(struct mlx5e_ipsec_hw_stats, ipsec_rx_pkts) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_ipsec_hw_stats, ipsec_rx_bytes) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_ipsec_hw_stats, ipsec_rx_drop_pkts) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_ipsec_hw_stats, ipsec_rx_drop_bytes) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_ipsec_hw_stats, ipsec_tx_pkts) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_ipsec_hw_stats, ipsec_tx_bytes) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_ipsec_hw_stats, ipsec_tx_drop_pkts) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_ipsec_hw_stats, ipsec_tx_drop_bytes) },
};

static const struct counter_desc mlx5e_ipsec_sw_stats_desc[] = {
	{ MLX5E_DECLARE_STAT(struct mlx5e_ipsec_sw_stats, ipsec_rx_drop_sp_alloc) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_ipsec_sw_stats, ipsec_rx_drop_sadb_miss) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_ipsec_sw_stats, ipsec_tx_drop_bundle) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_ipsec_sw_stats, ipsec_tx_drop_no_state) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_ipsec_sw_stats, ipsec_tx_drop_not_ip) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_ipsec_sw_stats, ipsec_tx_drop_trailer) },
};

#define MLX5E_READ_CTR_ATOMIC64(ptr, dsc, i) \
	atomic64_read((atomic64_t *)((char *)(ptr) + (dsc)[i].offset))

#define NUM_IPSEC_HW_COUNTERS ARRAY_SIZE(mlx5e_ipsec_hw_stats_desc)
#define NUM_IPSEC_SW_COUNTERS ARRAY_SIZE(mlx5e_ipsec_sw_stats_desc)

static MLX5E_DECLARE_STATS_GRP_OP_NUM_STATS(ipsec_hw)
{
	if (!priv->ipsec)
		return 0;

	return NUM_IPSEC_HW_COUNTERS;
}

static inline MLX5E_DECLARE_STATS_GRP_OP_UPDATE_STATS(ipsec_hw) {}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STRS(ipsec_hw)
{
	unsigned int i;

	if (!priv->ipsec)
		return idx;

	for (i = 0; i < NUM_IPSEC_HW_COUNTERS; i++)
		strcpy(data + (idx++) * ETH_GSTRING_LEN,
		       mlx5e_ipsec_hw_stats_desc[i].format);

	return idx;
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STATS(ipsec_hw)
{
	int i;

	if (!priv->ipsec)
		return idx;

	mlx5e_accel_ipsec_fs_read_stats(priv, &priv->ipsec->hw_stats);
	for (i = 0; i < NUM_IPSEC_HW_COUNTERS; i++)
		data[idx++] = MLX5E_READ_CTR_ATOMIC64(&priv->ipsec->hw_stats,
						      mlx5e_ipsec_hw_stats_desc, i);

	return idx;
}

static MLX5E_DECLARE_STATS_GRP_OP_NUM_STATS(ipsec_sw)
{
	return priv->ipsec ? NUM_IPSEC_SW_COUNTERS : 0;
}

static inline MLX5E_DECLARE_STATS_GRP_OP_UPDATE_STATS(ipsec_sw) {}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STRS(ipsec_sw)
{
	unsigned int i;

	if (priv->ipsec)
		for (i = 0; i < NUM_IPSEC_SW_COUNTERS; i++)
			strcpy(data + (idx++) * ETH_GSTRING_LEN,
			       mlx5e_ipsec_sw_stats_desc[i].format);
	return idx;
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STATS(ipsec_sw)
{
	int i;

	if (priv->ipsec)
		for (i = 0; i < NUM_IPSEC_SW_COUNTERS; i++)
			data[idx++] = MLX5E_READ_CTR_ATOMIC64(&priv->ipsec->sw_stats,
							      mlx5e_ipsec_sw_stats_desc, i);
	return idx;
}

MLX5E_DEFINE_STATS_GRP(ipsec_hw, 0);
MLX5E_DEFINE_STATS_GRP(ipsec_sw, 0);
