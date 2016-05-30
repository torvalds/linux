/*
 * Copyright (c) 2016, Mellanox Technologies. All rights reserved.
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

#ifndef __MLX5_EN_TC_H__
#define __MLX5_EN_TC_H__

#define MLX5E_TC_FLOW_ID_MASK 0x0000ffff

int mlx5e_tc_init(struct mlx5e_priv *priv);
void mlx5e_tc_cleanup(struct mlx5e_priv *priv);

int mlx5e_configure_flower(struct mlx5e_priv *priv, __be16 protocol,
			   struct tc_cls_flower_offload *f);
int mlx5e_delete_flower(struct mlx5e_priv *priv,
			struct tc_cls_flower_offload *f);

int mlx5e_stats_flower(struct mlx5e_priv *priv,
		       struct tc_cls_flower_offload *f);

static inline int mlx5e_tc_num_filters(struct mlx5e_priv *priv)
{
	return atomic_read(&priv->fs.tc.ht.nelems);
}

#endif /* __MLX5_EN_TC_H__ */
