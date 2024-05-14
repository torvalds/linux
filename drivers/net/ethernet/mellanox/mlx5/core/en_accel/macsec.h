/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __MLX5_EN_ACCEL_MACSEC_H__
#define __MLX5_EN_ACCEL_MACSEC_H__

#ifdef CONFIG_MLX5_EN_MACSEC

#include <linux/mlx5/driver.h>
#include <net/macsec.h>
#include <net/dst_metadata.h>

/* Bit31 - 30: MACsec marker, Bit15-0: MACsec id */
#define MLX5_MACEC_RX_FS_ID_MAX USHRT_MAX /* Must be power of two */
#define MLX5_MACSEC_RX_FS_ID_MASK MLX5_MACEC_RX_FS_ID_MAX
#define MLX5_MACSEC_METADATA_MARKER(metadata)  ((((metadata) >> 30) & 0x3)  == 0x1)
#define MLX5_MACSEC_RX_METADAT_HANDLE(metadata)  ((metadata) & MLX5_MACSEC_RX_FS_ID_MASK)

struct mlx5e_priv;
struct mlx5e_macsec;

struct mlx5e_macsec_stats {
	u64 macsec_rx_pkts;
	u64 macsec_rx_bytes;
	u64 macsec_rx_pkts_drop;
	u64 macsec_rx_bytes_drop;
	u64 macsec_tx_pkts;
	u64 macsec_tx_bytes;
	u64 macsec_tx_pkts_drop;
	u64 macsec_tx_bytes_drop;
};

void mlx5e_macsec_build_netdev(struct mlx5e_priv *priv);
int mlx5e_macsec_init(struct mlx5e_priv *priv);
void mlx5e_macsec_cleanup(struct mlx5e_priv *priv);
bool mlx5e_macsec_handle_tx_skb(struct mlx5e_macsec *macsec, struct sk_buff *skb);
void mlx5e_macsec_tx_build_eseg(struct mlx5e_macsec *macsec,
				struct sk_buff *skb,
				struct mlx5_wqe_eth_seg *eseg);

static inline bool mlx5e_macsec_skb_is_offload(struct sk_buff *skb)
{
	struct metadata_dst *md_dst = skb_metadata_dst(skb);

	return md_dst && (md_dst->type == METADATA_MACSEC);
}

static inline bool mlx5e_macsec_is_rx_flow(struct mlx5_cqe64 *cqe)
{
	return MLX5_MACSEC_METADATA_MARKER(be32_to_cpu(cqe->ft_metadata));
}

void mlx5e_macsec_offload_handle_rx_skb(struct net_device *netdev, struct sk_buff *skb,
					struct mlx5_cqe64 *cqe);
bool mlx5e_is_macsec_device(const struct mlx5_core_dev *mdev);
void mlx5e_macsec_get_stats_fill(struct mlx5e_macsec *macsec, void *macsec_stats);
struct mlx5e_macsec_stats *mlx5e_macsec_get_stats(struct mlx5e_macsec *macsec);

#else

static inline void mlx5e_macsec_build_netdev(struct mlx5e_priv *priv) {}
static inline int mlx5e_macsec_init(struct mlx5e_priv *priv) { return 0; }
static inline void mlx5e_macsec_cleanup(struct mlx5e_priv *priv) {}
static inline bool mlx5e_macsec_skb_is_offload(struct sk_buff *skb) { return false; }
static inline bool mlx5e_macsec_is_rx_flow(struct mlx5_cqe64 *cqe) { return false; }
static inline void mlx5e_macsec_offload_handle_rx_skb(struct net_device *netdev,
						      struct sk_buff *skb,
						      struct mlx5_cqe64 *cqe)
{}
static inline bool mlx5e_is_macsec_device(const struct mlx5_core_dev *mdev) { return false; }
#endif  /* CONFIG_MLX5_EN_MACSEC */

#endif	/* __MLX5_ACCEL_EN_MACSEC_H__ */
