/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2017-2018 Mellanox Technologies. All rights reserved */

#ifndef _MLXSW_IPIP_H_
#define _MLXSW_IPIP_H_

#include "spectrum_router.h"
#include <net/ip_fib.h>
#include <linux/if_tunnel.h>

struct ip_tunnel_parm
mlxsw_sp_ipip_netdev_parms4(const struct net_device *ol_dev);
struct __ip6_tnl_parm
mlxsw_sp_ipip_netdev_parms6(const struct net_device *ol_dev);

union mlxsw_sp_l3addr
mlxsw_sp_ipip_netdev_saddr(enum mlxsw_sp_l3proto proto,
			   const struct net_device *ol_dev);

bool mlxsw_sp_l3addr_is_zero(union mlxsw_sp_l3addr addr);

enum mlxsw_sp_ipip_type {
	MLXSW_SP_IPIP_TYPE_GRE4,
	MLXSW_SP_IPIP_TYPE_MAX,
};

struct mlxsw_sp_ipip_entry {
	enum mlxsw_sp_ipip_type ipipt;
	struct net_device *ol_dev; /* Overlay. */
	struct mlxsw_sp_rif_ipip_lb *ol_lb;
	struct mlxsw_sp_fib_entry *decap_fib_entry;
	struct list_head ipip_list_node;
	union {
		struct ip_tunnel_parm parms4;
	};
};

struct mlxsw_sp_ipip_ops {
	int dev_type;
	enum mlxsw_sp_l3proto ul_proto; /* Underlay. */

	int (*nexthop_update)(struct mlxsw_sp *mlxsw_sp, u32 adj_index,
			      struct mlxsw_sp_ipip_entry *ipip_entry);

	bool (*can_offload)(const struct mlxsw_sp *mlxsw_sp,
			    const struct net_device *ol_dev);

	/* Return a configuration for creating an overlay loopback RIF. */
	struct mlxsw_sp_rif_ipip_lb_config
	(*ol_loopback_config)(struct mlxsw_sp *mlxsw_sp,
			      const struct net_device *ol_dev);

	int (*fib_entry_op)(struct mlxsw_sp *mlxsw_sp,
			    const struct mlxsw_sp_router_ll_ops *ll_ops,
			    struct mlxsw_sp_fib_entry_op_ctx *op_ctx,
			    struct mlxsw_sp_ipip_entry *ipip_entry,
			    enum mlxsw_sp_fib_entry_op op,
			    u32 tunnel_index,
			    struct mlxsw_sp_fib_entry_priv *priv);

	int (*ol_netdev_change)(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_ipip_entry *ipip_entry,
				struct netlink_ext_ack *extack);
};

extern const struct mlxsw_sp_ipip_ops *mlxsw_sp_ipip_ops_arr[];

#endif /* _MLXSW_IPIP_H_*/
