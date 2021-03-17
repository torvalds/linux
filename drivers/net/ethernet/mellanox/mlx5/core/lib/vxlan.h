/*
 * Copyright (c) 2016, Mellanox Technologies, Ltd.  All rights reserved.
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
#ifndef __MLX5_VXLAN_H__
#define __MLX5_VXLAN_H__

#include <linux/mlx5/driver.h>

struct mlx5_vxlan;
struct mlx5_vxlan_port;

static inline u8 mlx5_vxlan_max_udp_ports(struct mlx5_core_dev *mdev)
{
	return MLX5_CAP_ETH(mdev, max_vxlan_udp_ports) ?: 4;
}

static inline bool mlx5_vxlan_allowed(struct mlx5_vxlan *vxlan)
{
	/* not allowed reason is encoded in vxlan pointer as error,
	 * on mlx5_vxlan_create
	 */
	return !IS_ERR_OR_NULL(vxlan);
}

#if IS_ENABLED(CONFIG_VXLAN)
struct mlx5_vxlan *mlx5_vxlan_create(struct mlx5_core_dev *mdev);
void mlx5_vxlan_destroy(struct mlx5_vxlan *vxlan);
int mlx5_vxlan_add_port(struct mlx5_vxlan *vxlan, u16 port);
int mlx5_vxlan_del_port(struct mlx5_vxlan *vxlan, u16 port);
bool mlx5_vxlan_lookup_port(struct mlx5_vxlan *vxlan, u16 port);
void mlx5_vxlan_reset_to_default(struct mlx5_vxlan *vxlan);
#else
static inline struct mlx5_vxlan*
mlx5_vxlan_create(struct mlx5_core_dev *mdev) { return ERR_PTR(-EOPNOTSUPP); }
static inline void mlx5_vxlan_destroy(struct mlx5_vxlan *vxlan) { return; }
static inline int mlx5_vxlan_add_port(struct mlx5_vxlan *vxlan, u16 port) { return -EOPNOTSUPP; }
static inline int mlx5_vxlan_del_port(struct mlx5_vxlan *vxlan, u16 port) { return -EOPNOTSUPP; }
static inline bool mlx5_vxlan_lookup_port(struct mlx5_vxlan *vxlan, u16 port) { return false; }
static inline void mlx5_vxlan_reset_to_default(struct mlx5_vxlan *vxlan) { return; }
#endif

#endif /* __MLX5_VXLAN_H__ */
