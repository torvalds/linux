/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2018 Mellanox Technologies. */

#ifndef __MLX5_EN_TC_TUNNEL_H__
#define __MLX5_EN_TC_TUNNEL_H__

#include <linux/netdevice.h>
#include <linux/mlx5/fs.h>
#include <net/pkt_cls.h>
#include <linux/netlink.h>
#include "en.h"
#include "en_rep.h"

enum {
	MLX5E_TC_TUNNEL_TYPE_UNKNOWN,
	MLX5E_TC_TUNNEL_TYPE_VXLAN,
	MLX5E_TC_TUNNEL_TYPE_GRETAP
};

int mlx5e_tc_tun_init_encap_attr(struct net_device *tunnel_dev,
				 struct mlx5e_priv *priv,
				 struct mlx5e_encap_entry *e,
				 struct netlink_ext_ack *extack);

int mlx5e_tc_tun_create_header_ipv4(struct mlx5e_priv *priv,
				    struct net_device *mirred_dev,
				    struct mlx5e_encap_entry *e);

int mlx5e_tc_tun_create_header_ipv6(struct mlx5e_priv *priv,
				    struct net_device *mirred_dev,
				    struct mlx5e_encap_entry *e);

int mlx5e_tc_tun_get_type(struct net_device *tunnel_dev);
bool mlx5e_tc_tun_device_to_offload(struct mlx5e_priv *priv,
				    struct net_device *netdev);

int mlx5e_tc_tun_parse(struct net_device *filter_dev,
		       struct mlx5e_priv *priv,
		       struct mlx5_flow_spec *spec,
		       struct tc_cls_flower_offload *f,
		       void *headers_c,
		       void *headers_v);

#endif //__MLX5_EN_TC_TUNNEL_H__
