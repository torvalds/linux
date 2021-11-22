/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2017-2018 Mellanox Technologies. All rights reserved */

#ifndef _MLXSW_IPIP_H_
#define _MLXSW_IPIP_H_

#include "spectrum_router.h"
#include <net/ip_fib.h>
#include <linux/if_tunnel.h>
#include <net/ip6_tunnel.h>

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
	MLXSW_SP_IPIP_TYPE_GRE6,
	MLXSW_SP_IPIP_TYPE_MAX,
};

struct mlxsw_sp_ipip_parms {
	enum mlxsw_sp_l3proto proto;
	union mlxsw_sp_l3addr saddr;
	union mlxsw_sp_l3addr daddr;
	int link;
	u32 ikey;
	u32 okey;
};

struct mlxsw_sp_ipip_entry {
	enum mlxsw_sp_ipip_type ipipt;
	struct net_device *ol_dev; /* Overlay. */
	struct mlxsw_sp_rif_ipip_lb *ol_lb;
	struct mlxsw_sp_fib_entry *decap_fib_entry;
	struct list_head ipip_list_node;
	struct mlxsw_sp_ipip_parms parms;
	u32 dip_kvdl_index;
};

struct mlxsw_sp_ipip_ops {
	int dev_type;
	enum mlxsw_sp_l3proto ul_proto; /* Underlay. */
	bool inc_parsing_depth;

	struct mlxsw_sp_ipip_parms
	(*parms_init)(const struct net_device *ol_dev);

	int (*nexthop_update)(struct mlxsw_sp *mlxsw_sp, u32 adj_index,
			      struct mlxsw_sp_ipip_entry *ipip_entry,
			      bool force, char *ratr_pl);

	bool (*can_offload)(const struct mlxsw_sp *mlxsw_sp,
			    const struct net_device *ol_dev);

	/* Return a configuration for creating an overlay loopback RIF. */
	struct mlxsw_sp_rif_ipip_lb_config
	(*ol_loopback_config)(struct mlxsw_sp *mlxsw_sp,
			      const struct net_device *ol_dev);

	int (*decap_config)(struct mlxsw_sp *mlxsw_sp,
			    struct mlxsw_sp_ipip_entry *ipip_entry,
			    u32 tunnel_index);

	int (*ol_netdev_change)(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_ipip_entry *ipip_entry,
				struct netlink_ext_ack *extack);
	int (*rem_ip_addr_set)(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_ipip_entry *ipip_entry);
	void (*rem_ip_addr_unset)(struct mlxsw_sp *mlxsw_sp,
				  const struct mlxsw_sp_ipip_entry *ipip_entry);
};

extern const struct mlxsw_sp_ipip_ops *mlxsw_sp1_ipip_ops_arr[];
extern const struct mlxsw_sp_ipip_ops *mlxsw_sp2_ipip_ops_arr[];

#endif /* _MLXSW_IPIP_H_*/
