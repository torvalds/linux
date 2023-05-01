/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __MLX5_LIB_CRYPTO_H__
#define __MLX5_LIB_CRYPTO_H__

enum {
	MLX5_ACCEL_OBJ_TLS_KEY = MLX5_GENERAL_OBJECT_TYPE_ENCRYPTION_KEY_PURPOSE_TLS,
	MLX5_ACCEL_OBJ_IPSEC_KEY = MLX5_GENERAL_OBJECT_TYPE_ENCRYPTION_KEY_PURPOSE_IPSEC,
	MLX5_ACCEL_OBJ_MACSEC_KEY = MLX5_GENERAL_OBJECT_TYPE_ENCRYPTION_KEY_PURPOSE_MACSEC,
	MLX5_ACCEL_OBJ_TYPE_KEY_NUM,
};

int mlx5_create_encryption_key(struct mlx5_core_dev *mdev,
			       const void *key, u32 sz_bytes,
			       u32 key_type, u32 *p_key_id);

void mlx5_destroy_encryption_key(struct mlx5_core_dev *mdev, u32 key_id);

struct mlx5_crypto_dek_pool;
struct mlx5_crypto_dek;

struct mlx5_crypto_dek_pool *mlx5_crypto_dek_pool_create(struct mlx5_core_dev *mdev,
							 int key_purpose);
void mlx5_crypto_dek_pool_destroy(struct mlx5_crypto_dek_pool *pool);
struct mlx5_crypto_dek *mlx5_crypto_dek_create(struct mlx5_crypto_dek_pool *dek_pool,
					       const void *key, u32 sz_bytes);
void mlx5_crypto_dek_destroy(struct mlx5_crypto_dek_pool *dek_pool,
			     struct mlx5_crypto_dek *dek);
u32 mlx5_crypto_dek_get_id(struct mlx5_crypto_dek *dek);

struct mlx5_crypto_dek_priv *mlx5_crypto_dek_init(struct mlx5_core_dev *mdev);
void mlx5_crypto_dek_cleanup(struct mlx5_crypto_dek_priv *dek_priv);
#endif /* __MLX5_LIB_CRYPTO_H__ */
