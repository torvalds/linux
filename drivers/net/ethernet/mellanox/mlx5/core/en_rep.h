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

#ifndef __MLX5E_REP_H__
#define __MLX5E_REP_H__

#include <net/ip_tunnels.h>
#include "eswitch.h"
#include "en.h"

struct mlx5e_rep_priv {
	struct mlx5_eswitch_rep *rep;
};

struct mlx5e_encap_entry {
	struct hlist_node encap_hlist;
	struct list_head flows;
	u32 encap_id;
	struct neighbour *n;
	struct ip_tunnel_info tun_info;
	unsigned char h_dest[ETH_ALEN];	/* destination eth addr	*/

	struct net_device *out_dev;
	int tunnel_type;
};

void mlx5e_register_vport_reps(struct mlx5e_priv *priv);
void mlx5e_unregister_vport_reps(struct mlx5e_priv *priv);
bool mlx5e_is_uplink_rep(struct mlx5e_priv *priv);
int mlx5e_add_sqs_fwd_rules(struct mlx5e_priv *priv);
void mlx5e_remove_sqs_fwd_rules(struct mlx5e_priv *priv);

int mlx5e_get_offload_stats(int attr_id, const struct net_device *dev, void *sp);
bool mlx5e_has_offload_stats(const struct net_device *dev, int attr_id);

int mlx5e_attr_get(struct net_device *dev, struct switchdev_attr *attr);
void mlx5e_handle_rx_cqe_rep(struct mlx5e_rq *rq, struct mlx5_cqe64 *cqe);

#endif /* __MLX5E_REP_H__ */
