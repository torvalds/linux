/*
 * Copyright (c) 2017, Mellanox Technologies. All rights reserved.
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

#ifndef MLX5_IB_CMD_H
#define MLX5_IB_CMD_H

#include "mlx5_ib.h"
#include <linux/kernel.h>
#include <linux/mlx5/driver.h>

int mlx5_cmd_dump_fill_mkey(struct mlx5_core_dev *dev, u32 *mkey);
int mlx5_cmd_null_mkey(struct mlx5_core_dev *dev, u32 *null_mkey);
int mlx5_cmd_query_cong_params(struct mlx5_core_dev *dev, int cong_point,
			       void *out);
int mlx5_cmd_dealloc_pd(struct mlx5_core_dev *dev, u32 pdn, u16 uid);
void mlx5_cmd_destroy_tir(struct mlx5_core_dev *dev, u32 tirn, u16 uid);
void mlx5_cmd_destroy_tis(struct mlx5_core_dev *dev, u32 tisn, u16 uid);
int mlx5_cmd_destroy_rqt(struct mlx5_core_dev *dev, u32 rqtn, u16 uid);
int mlx5_cmd_alloc_transport_domain(struct mlx5_core_dev *dev, u32 *tdn,
				    u16 uid);
void mlx5_cmd_dealloc_transport_domain(struct mlx5_core_dev *dev, u32 tdn,
				       u16 uid);
int mlx5_cmd_attach_mcg(struct mlx5_core_dev *dev, union ib_gid *mgid,
			u32 qpn, u16 uid);
int mlx5_cmd_detach_mcg(struct mlx5_core_dev *dev, union ib_gid *mgid,
			u32 qpn, u16 uid);
int mlx5_cmd_xrcd_alloc(struct mlx5_core_dev *dev, u32 *xrcdn, u16 uid);
int mlx5_cmd_xrcd_dealloc(struct mlx5_core_dev *dev, u32 xrcdn, u16 uid);
int mlx5_cmd_mad_ifc(struct mlx5_core_dev *dev, const void *inb, void *outb,
		     u16 opmod, u8 port);
#endif /* MLX5_IB_CMD_H */
