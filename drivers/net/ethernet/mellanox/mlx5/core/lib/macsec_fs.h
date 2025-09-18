/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __MLX5_MACSEC_STEERING_H__
#define __MLX5_MACSEC_STEERING_H__

#ifdef CONFIG_MLX5_MACSEC

/* Bit31 - 30: MACsec marker, Bit15-0: MACsec id */
#define MLX5_MACEC_RX_FS_ID_MAX USHRT_MAX /* Must be power of two */
#define MLX5_MACSEC_RX_FS_ID_MASK MLX5_MACEC_RX_FS_ID_MAX
#define MLX5_MACSEC_METADATA_MARKER(metadata)  ((((metadata) >> 30) & 0x3)  == 0x1)
#define MLX5_MACSEC_RX_METADAT_HANDLE(metadata)  ((metadata) & MLX5_MACSEC_RX_FS_ID_MASK)

/* MACsec TX flow steering */
#define MLX5_ETH_WQE_FT_META_MACSEC_MASK \
	(MLX5_ETH_WQE_FT_META_MACSEC | MLX5_ETH_WQE_FT_META_MACSEC_FS_ID_MASK)
#define MLX5_ETH_WQE_FT_META_MACSEC_SHIFT MLX5_ETH_WQE_FT_META_SHIFT

/* MACsec fs_id handling for steering */
#define mlx5_macsec_fs_set_tx_fs_id(fs_id) \
	(((MLX5_ETH_WQE_FT_META_MACSEC) >> MLX5_ETH_WQE_FT_META_MACSEC_SHIFT) \
	 | ((fs_id) << 2))

#define MLX5_MACSEC_TX_METADATA(fs_id) \
	(mlx5_macsec_fs_set_tx_fs_id(fs_id) << \
	 MLX5_ETH_WQE_FT_META_MACSEC_SHIFT)

/* MACsec fs_id uses 4 bits, supports up to 16 interfaces */
#define MLX5_MACSEC_NUM_OF_SUPPORTED_INTERFACES 16

struct mlx5_macsec_fs;
union mlx5_macsec_rule;

struct mlx5_macsec_rule_attrs {
	sci_t sci;
	u32 macsec_obj_id;
	u8 assoc_num;
	int action;
};

struct mlx5_macsec_stats {
	u64 macsec_rx_pkts;
	u64 macsec_rx_bytes;
	u64 macsec_rx_pkts_drop;
	u64 macsec_rx_bytes_drop;
	u64 macsec_tx_pkts;
	u64 macsec_tx_bytes;
	u64 macsec_tx_pkts_drop;
	u64 macsec_tx_bytes_drop;
};

enum mlx5_macsec_action {
	MLX5_ACCEL_MACSEC_ACTION_ENCRYPT,
	MLX5_ACCEL_MACSEC_ACTION_DECRYPT,
};

void mlx5_macsec_fs_cleanup(struct mlx5_macsec_fs *macsec_fs);

struct mlx5_macsec_fs *
mlx5_macsec_fs_init(struct mlx5_core_dev *mdev);

union mlx5_macsec_rule *
mlx5_macsec_fs_add_rule(struct mlx5_macsec_fs *macsec_fs,
			const struct macsec_context *ctx,
			struct mlx5_macsec_rule_attrs *attrs,
			u32 *sa_fs_id);

void mlx5_macsec_fs_del_rule(struct mlx5_macsec_fs *macsec_fs,
			     union mlx5_macsec_rule *macsec_rule,
			     int action, void *macdev, u32 sa_fs_id);

void mlx5_macsec_fs_get_stats_fill(struct mlx5_macsec_fs *macsec_fs, void *macsec_stats);
struct mlx5_macsec_stats *mlx5_macsec_fs_get_stats(struct mlx5_macsec_fs *macsec_fs);
u32 mlx5_macsec_fs_get_fs_id_from_hashtable(struct mlx5_macsec_fs *macsec_fs, sci_t *sci);

#endif

#endif /* __MLX5_MACSEC_STEERING_H__ */
