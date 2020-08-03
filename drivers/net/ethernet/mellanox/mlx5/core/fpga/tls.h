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

#ifndef __MLX5_FPGA_TLS_H__
#define __MLX5_FPGA_TLS_H__

#include <linux/mlx5/driver.h>

#include <net/tls.h>
#include "fpga/core.h"

struct mlx5_fpga_tls {
	struct list_head pending_cmds;
	spinlock_t pending_cmds_lock; /* Protects pending_cmds */
	u32 caps;
	struct mlx5_fpga_conn *conn;

	struct idr tx_idr;
	struct idr rx_idr;
	spinlock_t tx_idr_spinlock; /* protects the IDR */
	spinlock_t rx_idr_spinlock; /* protects the IDR */
};

int mlx5_fpga_tls_add_flow(struct mlx5_core_dev *mdev, void *flow,
			   struct tls_crypto_info *crypto_info,
			   u32 start_offload_tcp_sn, u32 *p_swid,
			   bool direction_sx);

void mlx5_fpga_tls_del_flow(struct mlx5_core_dev *mdev, u32 swid,
			    gfp_t flags, bool direction_sx);

bool mlx5_fpga_is_tls_device(struct mlx5_core_dev *mdev);
int mlx5_fpga_tls_init(struct mlx5_core_dev *mdev);
void mlx5_fpga_tls_cleanup(struct mlx5_core_dev *mdev);

static inline u32 mlx5_fpga_tls_device_caps(struct mlx5_core_dev *mdev)
{
	return mdev->fpga->tls->caps;
}

int mlx5_fpga_tls_resync_rx(struct mlx5_core_dev *mdev, __be32 handle,
			    u32 seq, __be64 rcd_sn);

#endif /* __MLX5_FPGA_TLS_H__ */
