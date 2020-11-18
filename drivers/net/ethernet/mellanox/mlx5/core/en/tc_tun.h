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
	MLX5E_TC_TUNNEL_TYPE_GENEVE,
	MLX5E_TC_TUNNEL_TYPE_GRETAP,
	MLX5E_TC_TUNNEL_TYPE_MPLSOUDP,
};

struct mlx5e_tc_tunnel {
	int tunnel_type;
	enum mlx5_flow_match_level match_level;

	bool (*can_offload)(struct mlx5e_priv *priv);
	int (*calc_hlen)(struct mlx5e_encap_entry *e);
	int (*init_encap_attr)(struct net_device *tunnel_dev,
			       struct mlx5e_priv *priv,
			       struct mlx5e_encap_entry *e,
			       struct netlink_ext_ack *extack);
	int (*generate_ip_tun_hdr)(char buf[],
				   __u8 *ip_proto,
				   struct mlx5e_encap_entry *e);
	int (*parse_udp_ports)(struct mlx5e_priv *priv,
			       struct mlx5_flow_spec *spec,
			       struct flow_cls_offload *f,
			       void *headers_c,
			       void *headers_v);
	int (*parse_tunnel)(struct mlx5e_priv *priv,
			    struct mlx5_flow_spec *spec,
			    struct flow_cls_offload *f,
			    void *headers_c,
			    void *headers_v);
};

extern struct mlx5e_tc_tunnel vxlan_tunnel;
extern struct mlx5e_tc_tunnel geneve_tunnel;
extern struct mlx5e_tc_tunnel gre_tunnel;
extern struct mlx5e_tc_tunnel mplsoudp_tunnel;

struct mlx5e_tc_tunnel *mlx5e_get_tc_tun(struct net_device *tunnel_dev);

int mlx5e_tc_tun_init_encap_attr(struct net_device *tunnel_dev,
				 struct mlx5e_priv *priv,
				 struct mlx5e_encap_entry *e,
				 struct netlink_ext_ack *extack);

int mlx5e_tc_tun_create_header_ipv4(struct mlx5e_priv *priv,
				    struct net_device *mirred_dev,
				    struct mlx5e_encap_entry *e);

#if IS_ENABLED(CONFIG_INET) && IS_ENABLED(CONFIG_IPV6)
int mlx5e_tc_tun_create_header_ipv6(struct mlx5e_priv *priv,
				    struct net_device *mirred_dev,
				    struct mlx5e_encap_entry *e);
#else
static inline int
mlx5e_tc_tun_create_header_ipv6(struct mlx5e_priv *priv,
				struct net_device *mirred_dev,
				struct mlx5e_encap_entry *e) { return -EOPNOTSUPP; }
#endif

bool mlx5e_tc_tun_device_to_offload(struct mlx5e_priv *priv,
				    struct net_device *netdev);

int mlx5e_tc_tun_parse(struct net_device *filter_dev,
		       struct mlx5e_priv *priv,
		       struct mlx5_flow_spec *spec,
		       struct flow_cls_offload *f,
		       u8 *match_level);

int mlx5e_tc_tun_parse_udp_ports(struct mlx5e_priv *priv,
				 struct mlx5_flow_spec *spec,
				 struct flow_cls_offload *f,
				 void *headers_c,
				 void *headers_v);

#endif //__MLX5_EN_TC_TUNNEL_H__
