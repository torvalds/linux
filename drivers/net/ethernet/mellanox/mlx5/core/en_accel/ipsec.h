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

#ifndef __MLX5E_IPSEC_H__
#define __MLX5E_IPSEC_H__

#ifdef CONFIG_MLX5_EN_IPSEC

#include <linux/mlx5/device.h>
#include <net/xfrm.h>
#include <linux/idr.h>

#define MLX5E_IPSEC_SADB_RX_BITS 10

struct mlx5e_priv;

struct mlx5e_ipsec {
	struct mlx5e_priv *en_priv;
	DECLARE_HASHTABLE(sadb_rx, MLX5E_IPSEC_SADB_RX_BITS);
	spinlock_t sadb_rx_lock; /* Protects sadb_rx and halloc */
	struct ida halloc;
};

int mlx5e_ipsec_init(struct mlx5e_priv *priv);
void mlx5e_ipsec_cleanup(struct mlx5e_priv *priv);
void mlx5e_ipsec_build_netdev(struct mlx5e_priv *priv);

struct xfrm_state *mlx5e_ipsec_sadb_rx_lookup(struct mlx5e_ipsec *dev,
					      unsigned int handle);

#else

static inline int mlx5e_ipsec_init(struct mlx5e_priv *priv)
{
	return 0;
}

static inline void mlx5e_ipsec_cleanup(struct mlx5e_priv *priv)
{
}

static inline void mlx5e_ipsec_build_netdev(struct mlx5e_priv *priv)
{
}

#endif

#endif	/* __MLX5E_IPSEC_H__ */
