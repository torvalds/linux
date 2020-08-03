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

#ifndef __MLX5_FPGA_IPSEC_H__
#define __MLX5_FPGA_IPSEC_H__

#include "accel/ipsec.h"
#include "fs_cmd.h"

#ifdef CONFIG_MLX5_FPGA_IPSEC
u32 mlx5_fpga_ipsec_device_caps(struct mlx5_core_dev *mdev);
unsigned int mlx5_fpga_ipsec_counters_count(struct mlx5_core_dev *mdev);
int mlx5_fpga_ipsec_counters_read(struct mlx5_core_dev *mdev, u64 *counters,
				  unsigned int counters_count);

void *mlx5_fpga_ipsec_create_sa_ctx(struct mlx5_core_dev *mdev,
				    struct mlx5_accel_esp_xfrm *accel_xfrm,
				    const __be32 saddr[4],
				    const __be32 daddr[4],
				    const __be32 spi, bool is_ipv6,
				    u32 *sa_handle);
void mlx5_fpga_ipsec_delete_sa_ctx(void *context);

int mlx5_fpga_ipsec_init(struct mlx5_core_dev *mdev);
void mlx5_fpga_ipsec_cleanup(struct mlx5_core_dev *mdev);
void mlx5_fpga_ipsec_build_fs_cmds(void);

struct mlx5_accel_esp_xfrm *
mlx5_fpga_esp_create_xfrm(struct mlx5_core_dev *mdev,
			  const struct mlx5_accel_esp_xfrm_attrs *attrs,
			  u32 flags);
void mlx5_fpga_esp_destroy_xfrm(struct mlx5_accel_esp_xfrm *xfrm);
int mlx5_fpga_esp_modify_xfrm(struct mlx5_accel_esp_xfrm *xfrm,
			      const struct mlx5_accel_esp_xfrm_attrs *attrs);

const struct mlx5_flow_cmds *
mlx5_fs_cmd_get_default_ipsec_fpga_cmds(enum fs_flow_table_type type);
#else
static inline u32 mlx5_fpga_ipsec_device_caps(struct mlx5_core_dev *mdev)
{
	return 0;
}

static inline const struct mlx5_flow_cmds *
mlx5_fs_cmd_get_default_ipsec_fpga_cmds(enum fs_flow_table_type type)
{
	return mlx5_fs_cmd_get_default(type);
}

#endif /* CONFIG_MLX5_FPGA_IPSEC */
#endif	/* __MLX5_FPGA_IPSEC_H__ */
