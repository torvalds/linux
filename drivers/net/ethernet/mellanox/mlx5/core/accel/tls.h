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

static inline bool mlx5_accel_is_ktls_device(struct mlx5_core_dev *mdev)
{
	if (!MLX5_CAP_GEN(mdev, tls))
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

enum {
	MLX5_ACCEL_TLS_TX = BIT(0),
	MLX5_ACCEL_TLS_RX = BIT(1),
	MLX5_ACCEL_TLS_V12 = BIT(2),
	MLX5_ACCEL_TLS_V13 = BIT(3),
	MLX5_ACCEL_TLS_LRO = BIT(4),
	MLX5_ACCEL_TLS_IPV6 = BIT(5),
	MLX5_ACCEL_TLS_AES_GCM128 = BIT(30),
	MLX5_ACCEL_TLS_AES_GCM256 = BIT(31),
};

struct mlx5_ifc_tls_flow_bits {
	u8         src_port[0x10];
	u8         dst_port[0x10];
	union mlx5_ifc_ipv6_layout_ipv4_layout_auto_bits src_ipv4_src_ipv6;
	union mlx5_ifc_ipv6_layout_ipv4_layout_auto_bits dst_ipv4_dst_ipv6;
	u8         ipv6[0x1];
	u8         direction_sx[0x1];
	u8         reserved_at_2[0x1e];
};

#ifdef CONFIG_MLX5_FPGA_TLS
int mlx5_accel_tls_add_flow(struct mlx5_core_dev *mdev, void *flow,
			    struct tls_crypto_info *crypto_info,
			    u32 start_offload_tcp_sn, u32 *p_swid,
			    bool direction_sx);
void mlx5_accel_tls_del_flow(struct mlx5_core_dev *mdev, u32 swid,
			     bool direction_sx);
int mlx5_accel_tls_resync_rx(struct mlx5_core_dev *mdev, u32 handle, u32 seq,
			     u64 rcd_sn);
bool mlx5_accel_is_tls_device(struct mlx5_core_dev *mdev);
u32 mlx5_accel_tls_device_caps(struct mlx5_core_dev *mdev);
int mlx5_accel_tls_init(struct mlx5_core_dev *mdev);
void mlx5_accel_tls_cleanup(struct mlx5_core_dev *mdev);

#else

static inline int
mlx5_accel_tls_add_flow(struct mlx5_core_dev *mdev, void *flow,
			struct tls_crypto_info *crypto_info,
			u32 start_offload_tcp_sn, u32 *p_swid,
			bool direction_sx) { return -ENOTSUPP; }
static inline void mlx5_accel_tls_del_flow(struct mlx5_core_dev *mdev, u32 swid,
					   bool direction_sx) { }
static inline int mlx5_accel_tls_resync_rx(struct mlx5_core_dev *mdev, u32 handle,
					   u32 seq, u64 rcd_sn) { return 0; }
static inline bool mlx5_accel_is_tls_device(struct mlx5_core_dev *mdev)
{
	return mlx5_accel_is_ktls_device(mdev);
}
static inline u32 mlx5_accel_tls_device_caps(struct mlx5_core_dev *mdev) { return 0; }
static inline int mlx5_accel_tls_init(struct mlx5_core_dev *mdev) { return 0; }
static inline void mlx5_accel_tls_cleanup(struct mlx5_core_dev *mdev) { }
#endif

#endif	/* __MLX5_ACCEL_TLS_H__ */
