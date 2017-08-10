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

#ifndef __MLX5E_IPSEC_RXTX_H__
#define __MLX5E_IPSEC_RXTX_H__

#ifdef CONFIG_MLX5_EN_IPSEC

#include <linux/skbuff.h>
#include "en.h"

struct sk_buff *mlx5e_ipsec_handle_rx_skb(struct net_device *netdev,
					  struct sk_buff *skb);
void mlx5e_ipsec_handle_rx_cqe(struct mlx5e_rq *rq, struct mlx5_cqe64 *cqe);

void mlx5e_ipsec_inverse_table_init(void);
bool mlx5e_ipsec_feature_check(struct sk_buff *skb, struct net_device *netdev,
			       netdev_features_t features);
struct sk_buff *mlx5e_ipsec_handle_tx_skb(struct net_device *netdev,
					  struct mlx5e_tx_wqe *wqe,
					  struct sk_buff *skb);

#endif /* CONFIG_MLX5_EN_IPSEC */

#endif /* __MLX5E_IPSEC_RXTX_H__ */
