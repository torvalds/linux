/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021 Mellanox Technologies. */

#ifndef __MLX5_EN_TC_TUN_ENCAP_H__
#define __MLX5_EN_TC_TUN_ENCAP_H__

#include "tc_priv.h"

void mlx5e_detach_encap(struct mlx5e_priv *priv,
			struct mlx5e_tc_flow *flow,
			struct mlx5_flow_attr *attr,
			int out_index);

int mlx5e_attach_encap(struct mlx5e_priv *priv,
		       struct mlx5e_tc_flow *flow,
		       struct mlx5_flow_attr *attr,
		       struct net_device *mirred_dev,
		       int out_index,
		       struct netlink_ext_ack *extack,
		       struct net_device **encap_dev);

int mlx5e_attach_decap(struct mlx5e_priv *priv,
		       struct mlx5e_tc_flow *flow,
		       struct netlink_ext_ack *extack);
void mlx5e_detach_decap(struct mlx5e_priv *priv,
			struct mlx5e_tc_flow *flow);

int mlx5e_attach_decap_route(struct mlx5e_priv *priv,
			     struct mlx5e_tc_flow *flow);
void mlx5e_detach_decap_route(struct mlx5e_priv *priv,
			      struct mlx5e_tc_flow *flow);

int mlx5e_tc_tun_encap_dests_set(struct mlx5e_priv *priv,
				 struct mlx5e_tc_flow *flow,
				 struct mlx5_flow_attr *attr,
				 struct netlink_ext_ack *extack,
				 bool *vf_tun);
void mlx5e_tc_tun_encap_dests_unset(struct mlx5e_priv *priv,
				    struct mlx5e_tc_flow *flow,
				    struct mlx5_flow_attr *attr);

struct ip_tunnel_info *mlx5e_dup_tun_info(const struct ip_tunnel_info *tun_info);

int mlx5e_tc_set_attr_rx_tun(struct mlx5e_tc_flow *flow,
			     struct mlx5_flow_spec *spec);

struct mlx5e_tc_tun_encap *mlx5e_tc_tun_init(struct mlx5e_priv *priv);
void mlx5e_tc_tun_cleanup(struct mlx5e_tc_tun_encap *encap);

#endif /* __MLX5_EN_TC_TUN_ENCAP_H__ */
