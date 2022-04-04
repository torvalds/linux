/*
 * Copyright (c) 2018 Mellanox Technologies. All rights reserved.
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

#ifndef __MLX5_ACCEL_TLS_H__
#define __MLX5_ACCEL_TLS_H__

#include <linux/mlx5/driver.h>
#include <linux/tls.h>

#ifdef CONFIG_MLX5_TLS
int mlx5_ktls_create_key(struct mlx5_core_dev *mdev,
			 struct tls_crypto_info *crypto_info,
			 u32 *p_key_id);
void mlx5_ktls_destroy_key(struct mlx5_core_dev *mdev, u32 key_id);

static inline bool mlx5_accel_is_ktls_tx(struct mlx5_core_dev *mdev)
{
	return MLX5_CAP_GEN(mdev, tls_tx);
}

static inline bool mlx5_accel_is_ktls_rx(struct mlx5_core_dev *mdev)
{
	return MLX5_CAP_GEN(mdev, tls_rx);
}

static inline bool mlx5_accel_is_ktls_device(struct mlx5_core_dev *mdev)
{
	if (!mlx5_accel_is_ktls_tx(mdev) &&
	    !mlx5_accel_is_ktls_rx(mdev))
		return false;

	if (!MLX5_CAP_GEN(mdev, log_max_dek))
		return false;

	return MLX5_CAP_TLS(mdev, tls_1_2_aes_gcm_128);
}

static inline bool mlx5e_ktls_type_check(struct mlx5_core_dev *mdev,
					 struct tls_crypto_info *crypto_info)
{
	switch (crypto_info->cipher_type) {
	case TLS_CIPHER_AES_GCM_128:
		if (crypto_info->version == TLS_1_2_VERSION)
			return MLX5_CAP_TLS(mdev,  tls_1_2_aes_gcm_128);
		break;
	}

	return false;
}
#else
static inline bool mlx5_accel_is_ktls_tx(struct mlx5_core_dev *mdev)
{ return false; }

static inline bool mlx5_accel_is_ktls_rx(struct mlx5_core_dev *mdev)
{ return false; }

static inline int
mlx5_ktls_create_key(struct mlx5_core_dev *mdev,
		     struct tls_crypto_info *crypto_info,
		     u32 *p_key_id) { return -ENOTSUPP; }
static inline void
mlx5_ktls_destroy_key(struct mlx5_core_dev *mdev, u32 key_id) {}

static inline bool
mlx5_accel_is_ktls_device(struct mlx5_core_dev *mdev) { return false; }
static inline bool
mlx5e_ktls_type_check(struct mlx5_core_dev *mdev,
		      struct tls_crypto_info *crypto_info) { return false; }
#endif
#endif	/* __MLX5_ACCEL_TLS_H__ */
