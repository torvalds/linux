/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2018 Mellanox Technologies. All rights reserved */

#ifndef _MLXSW_SPECTRUM_NVE_H
#define _MLXSW_SPECTRUM_NVE_H

#include <linux/netlink.h>
#include <linux/rhashtable.h>

#include "spectrum.h"

struct mlxsw_sp_nve_config {
	enum mlxsw_sp_nve_type type;
	u8 ttl;
	u8 learning_en:1;
	__be16 udp_dport;
	__be32 flowlabel;
	u32 ul_tb_id;
	enum mlxsw_sp_l3proto ul_proto;
	union mlxsw_sp_l3addr ul_sip;
};

struct mlxsw_sp_nve {
	struct mlxsw_sp_nve_config config;
	struct rhashtable mc_list_ht;
	struct mlxsw_sp *mlxsw_sp;
	const struct mlxsw_sp_nve_ops **nve_ops_arr;
	unsigned int num_nve_tunnels;	/* Protected by RTNL */
	unsigned int num_max_mc_entries[MLXSW_SP_L3_PROTO_MAX];
	u32 tunnel_index;
	u16 ul_rif_index;	/* Reserved for Spectrum */
	unsigned int inc_parsing_depth_refs;
};

struct mlxsw_sp_nve_ops {
	enum mlxsw_sp_nve_type type;
	bool (*can_offload)(const struct mlxsw_sp_nve *nve,
			    const struct mlxsw_sp_nve_params *params,
			    struct netlink_ext_ack *extack);
	void (*nve_config)(const struct mlxsw_sp_nve *nve,
			   const struct mlxsw_sp_nve_params *params,
			   struct mlxsw_sp_nve_config *config);
	int (*init)(struct mlxsw_sp_nve *nve,
		    const struct mlxsw_sp_nve_config *config);
	void (*fini)(struct mlxsw_sp_nve *nve);
	int (*fdb_replay)(const struct net_device *nve_dev, __be32 vni,
			  struct netlink_ext_ack *extack);
	void (*fdb_clear_offload)(const struct net_device *nve_dev, __be32 vni);
};

extern const struct mlxsw_sp_nve_ops mlxsw_sp1_nve_vxlan_ops;
extern const struct mlxsw_sp_nve_ops mlxsw_sp2_nve_vxlan_ops;

#endif
