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

#ifndef __MLX5_ACCEL_IPSEC_H__
#define __MLX5_ACCEL_IPSEC_H__

#include <linux/mlx5/driver.h>
#include <linux/mlx5/accel.h>

#ifdef CONFIG_MLX5_ACCEL

#define MLX5_IPSEC_SADB_IP_AH       BIT(7)
#define MLX5_IPSEC_SADB_IP_ESP      BIT(6)
#define MLX5_IPSEC_SADB_SA_VALID    BIT(5)
#define MLX5_IPSEC_SADB_SPI_EN      BIT(4)
#define MLX5_IPSEC_SADB_DIR_SX      BIT(3)
#define MLX5_IPSEC_SADB_IPV6        BIT(2)

enum {
	MLX5_IPSEC_CMD_ADD_SA = 0,
	MLX5_IPSEC_CMD_DEL_SA = 1,
	MLX5_IPSEC_CMD_ADD_SA_V2 = 2,
	MLX5_IPSEC_CMD_DEL_SA_V2 = 3,
	MLX5_IPSEC_CMD_MOD_SA_V2 = 4,
	MLX5_IPSEC_CMD_SET_CAP = 5,
};

enum mlx5_accel_ipsec_enc_mode {
	MLX5_IPSEC_SADB_MODE_NONE = 0,
	MLX5_IPSEC_SADB_MODE_AES_GCM_128_AUTH_128 = 1,
	MLX5_IPSEC_SADB_MODE_AES_GCM_256_AUTH_128 = 3,
};

#define MLX5_IPSEC_DEV(mdev) (mlx5_accel_ipsec_device_caps(mdev) & \
			      MLX5_ACCEL_IPSEC_CAP_DEVICE)

struct mlx5_accel_ipsec_sa_v1 {
	__be32 cmd;
	u8 key_enc[32];
	u8 key_auth[32];
	__be32 sip[4];
	__be32 dip[4];
	union {
		struct {
			__be32 reserved;
			u8 salt_iv[8];
			__be32 salt;
		} __packed gcm;
		struct {
			u8 salt[16];
		} __packed cbc;
	};
	__be32 spi;
	__be32 sw_sa_handle;
	__be16 tfclen;
	u8 enc_mode;
	u8 reserved1[2];
	u8 flags;
	u8 reserved2[2];
};

struct mlx5_accel_ipsec_sa {
	struct mlx5_accel_ipsec_sa_v1 ipsec_sa_v1;
	__be16 udp_sp;
	__be16 udp_dp;
	u8 reserved1[4];
	__be32 esn;
	__be16 vid;     /* only 12 bits, rest is reserved */
	__be16 reserved2;
} __packed;

/**
 * mlx5_accel_ipsec_sa_cmd_exec - Execute an IPSec SADB command
 * @mdev: mlx5 device
 * @cmd: command to execute
 * May be called from atomic context. Returns context pointer, or error
 * Caller must eventually call mlx5_accel_ipsec_sa_cmd_wait from non-atomic
 * context, to cleanup the context pointer
 */
void *mlx5_accel_ipsec_sa_cmd_exec(struct mlx5_core_dev *mdev,
				   struct mlx5_accel_ipsec_sa *cmd);

/**
 * mlx5_accel_ipsec_sa_cmd_wait - Wait for command execution completion
 * @context: Context pointer returned from call to mlx5_accel_ipsec_sa_cmd_exec
 * Sleeps (killable) until command execution is complete.
 * Returns the command result, or -EINTR if killed
 */
int mlx5_accel_ipsec_sa_cmd_wait(void *context);

unsigned int mlx5_accel_ipsec_counters_count(struct mlx5_core_dev *mdev);
int mlx5_accel_ipsec_counters_read(struct mlx5_core_dev *mdev, u64 *counters,
				   unsigned int count);

int mlx5_accel_ipsec_init(struct mlx5_core_dev *mdev);
void mlx5_accel_ipsec_cleanup(struct mlx5_core_dev *mdev);

#else

#define MLX5_IPSEC_DEV(mdev) false

static inline int mlx5_accel_ipsec_init(struct mlx5_core_dev *mdev)
{
	return 0;
}

static inline void mlx5_accel_ipsec_cleanup(struct mlx5_core_dev *mdev)
{
}

#endif

#endif	/* __MLX5_ACCEL_IPSEC_H__ */
