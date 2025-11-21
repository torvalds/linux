/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __MLX5E_ACCEL_PSP_H__
#define __MLX5E_ACCEL_PSP_H__
#if IS_ENABLED(CONFIG_MLX5_EN_PSP)
#include <net/psp/types.h>
#include "en.h"

struct mlx5e_psp {
	struct psp_dev *psp;
	struct psp_dev_caps caps;
	struct mlx5e_psp_fs *fs;
	atomic_t tx_key_cnt;
};

static inline bool mlx5_is_psp_device(struct mlx5_core_dev *mdev)
{
	if (!MLX5_CAP_GEN(mdev, psp))
		return false;

	if (!MLX5_CAP_PSP(mdev, psp_crypto_offload) ||
	    !MLX5_CAP_PSP(mdev, psp_crypto_esp_aes_gcm_128_encrypt) ||
	    !MLX5_CAP_PSP(mdev, psp_crypto_esp_aes_gcm_128_decrypt))
		return false;

	return true;
}

int mlx5_accel_psp_fs_init_rx_tables(struct mlx5e_priv *priv);
void mlx5_accel_psp_fs_cleanup_rx_tables(struct mlx5e_priv *priv);
int mlx5_accel_psp_fs_init_tx_tables(struct mlx5e_priv *priv);
void mlx5_accel_psp_fs_cleanup_tx_tables(struct mlx5e_priv *priv);
void mlx5e_psp_register(struct mlx5e_priv *priv);
void mlx5e_psp_unregister(struct mlx5e_priv *priv);
int mlx5e_psp_init(struct mlx5e_priv *priv);
void mlx5e_psp_cleanup(struct mlx5e_priv *priv);
#else
static inline int mlx5_accel_psp_fs_init_rx_tables(struct mlx5e_priv *priv)
{
	return 0;
}

static inline void mlx5_accel_psp_fs_cleanup_rx_tables(struct mlx5e_priv *priv) { }
static inline int mlx5_accel_psp_fs_init_tx_tables(struct mlx5e_priv *priv)
{
	return 0;
}

static inline void mlx5_accel_psp_fs_cleanup_tx_tables(struct mlx5e_priv *priv) { }
static inline bool mlx5_is_psp_device(struct mlx5_core_dev *mdev)
{
	return false;
}

static inline void mlx5e_psp_register(struct mlx5e_priv *priv) { }
static inline void mlx5e_psp_unregister(struct mlx5e_priv *priv) { }
static inline int mlx5e_psp_init(struct mlx5e_priv *priv) { return 0; }
static inline void mlx5e_psp_cleanup(struct mlx5e_priv *priv) { }
#endif /* CONFIG_MLX5_EN_PSP */
#endif /* __MLX5E_ACCEL_PSP_H__ */
