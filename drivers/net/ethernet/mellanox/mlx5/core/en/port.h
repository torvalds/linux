/*
 * Copyright (c) 2018, Mellanox Technologies. All rights reserved.
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
 */

#ifndef __MLX5E_EN_PORT_H
#define __MLX5E_EN_PORT_H

#include <linux/mlx5/driver.h>
#include "en.h"

struct mlx5e_port_eth_proto {
	u32 cap;
	u32 admin;
	u32 oper;
};

int mlx5_port_query_eth_proto(struct mlx5_core_dev *dev, u8 port, bool ext,
			      struct mlx5e_port_eth_proto *eproto);
void mlx5_port_query_eth_autoneg(struct mlx5_core_dev *dev, u8 *an_status,
				 u8 *an_disable_cap, u8 *an_disable_admin);
int mlx5_port_set_eth_ptys(struct mlx5_core_dev *dev, bool an_disable,
			   u32 proto_admin, bool ext);
u32 mlx5e_port_ptys2speed(struct mlx5_core_dev *mdev, u32 eth_proto_oper,
			  bool force_legacy);
int mlx5e_port_linkspeed(struct mlx5_core_dev *mdev, u32 *speed);
int mlx5e_port_max_linkspeed(struct mlx5_core_dev *mdev, u32 *speed);
u32 mlx5e_port_speed2linkmodes(struct mlx5_core_dev *mdev, u32 speed,
			       bool force_legacy);
bool mlx5e_ptys_ext_supported(struct mlx5_core_dev *mdev);
int mlx5e_port_query_pbmc(struct mlx5_core_dev *mdev, void *out);
int mlx5e_port_set_pbmc(struct mlx5_core_dev *mdev, void *in);
int mlx5e_port_query_sbpr(struct mlx5_core_dev *mdev, u32 desc, u8 dir,
			  u8 pool_idx, void *out, int size_out);
int mlx5e_port_set_sbpr(struct mlx5_core_dev *mdev, u32 desc, u8 dir,
			u8 pool_idx, u32 infi_size, u32 size);
int mlx5e_port_set_sbcm(struct mlx5_core_dev *mdev, u32 desc, u8 pg_buff_idx,
			u8 dir, u8 infi_size, u32 max_buff, u8 pool_idx);
int mlx5e_port_query_priority2buffer(struct mlx5_core_dev *mdev, u8 *buffer);
int mlx5e_port_set_priority2buffer(struct mlx5_core_dev *mdev, u8 *buffer);

bool mlx5e_fec_in_caps(struct mlx5_core_dev *dev, int fec_policy);
int mlx5e_get_fec_mode(struct mlx5_core_dev *dev, u32 *fec_mode_active,
		       u16 *fec_configured_mode);
int mlx5e_set_fec_mode(struct mlx5_core_dev *dev, u16 fec_policy);

enum {
	MLX5E_FEC_NOFEC,
	MLX5E_FEC_FIRECODE,
	MLX5E_FEC_RS_528_514,
	MLX5E_FEC_RS_544_514 = 7,
	MLX5E_FEC_LLRS_272_257_1 = 9,
};

#endif
