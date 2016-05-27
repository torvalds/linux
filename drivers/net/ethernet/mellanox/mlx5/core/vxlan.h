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
#include "en.h"

struct mlx5e_vxlan {
	u16 udp_port;
};

struct mlx5e_vxlan_work {
	struct work_struct	work;
	struct mlx5e_priv	*priv;
	sa_family_t		sa_family;
	u16			port;
};

static inline bool mlx5e_vxlan_allowed(struct mlx5_core_dev *mdev)
{
	return IS_ENABLED(CONFIG_MLX5_CORE_EN_VXLAN) &&
		(MLX5_CAP_ETH(mdev, tunnel_stateless_vxlan) &&
		mlx5_core_is_pf(mdev));
}

#ifdef CONFIG_MLX5_CORE_EN_VXLAN
void mlx5e_vxlan_init(struct mlx5e_priv *priv);
void mlx5e_vxlan_cleanup(struct mlx5e_priv *priv);
#else
static inline void mlx5e_vxlan_init(struct mlx5e_priv *priv) {}
static inline void mlx5e_vxlan_cleanup(struct mlx5e_priv *priv) {}
#endif

void mlx5e_vxlan_queue_work(struct mlx5e_priv *priv, sa_family_t sa_family,
			    u16 port, int add);
struct mlx5e_vxlan *mlx5e_vxlan_lookup_port(struct mlx5e_priv *priv, u16 port);

#endif /* __MLX5_VXLAN_H__ */
