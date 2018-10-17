// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2018 Mellanox Technologies. All rights reserved */

#include <linux/netdevice.h>
#include <linux/netlink.h>

#include "spectrum_nve.h"

static bool mlxsw_sp1_nve_vxlan_can_offload(const struct mlxsw_sp_nve *nve,
					    const struct net_device *dev,
					    struct netlink_ext_ack *extack)
{
	return false;
}

static void mlxsw_sp_nve_vxlan_config(const struct mlxsw_sp_nve *nve,
				      const struct net_device *dev,
				      struct mlxsw_sp_nve_config *config)
{
}

static int mlxsw_sp1_nve_vxlan_init(struct mlxsw_sp_nve *nve,
				    const struct mlxsw_sp_nve_config *config)
{
	return -EOPNOTSUPP;
}

static void mlxsw_sp1_nve_vxlan_fini(struct mlxsw_sp_nve *nve)
{
}

const struct mlxsw_sp_nve_ops mlxsw_sp1_nve_vxlan_ops = {
	.type		= MLXSW_SP_NVE_TYPE_VXLAN,
	.can_offload	= mlxsw_sp1_nve_vxlan_can_offload,
	.nve_config	= mlxsw_sp_nve_vxlan_config,
	.init		= mlxsw_sp1_nve_vxlan_init,
	.fini		= mlxsw_sp1_nve_vxlan_fini,
};

static bool mlxsw_sp2_nve_vxlan_can_offload(const struct mlxsw_sp_nve *nve,
					    const struct net_device *dev,
					    struct netlink_ext_ack *extack)
{
	return false;
}

static int mlxsw_sp2_nve_vxlan_init(struct mlxsw_sp_nve *nve,
				    const struct mlxsw_sp_nve_config *config)
{
	return -EOPNOTSUPP;
}

static void mlxsw_sp2_nve_vxlan_fini(struct mlxsw_sp_nve *nve)
{
}

const struct mlxsw_sp_nve_ops mlxsw_sp2_nve_vxlan_ops = {
	.type		= MLXSW_SP_NVE_TYPE_VXLAN,
	.can_offload	= mlxsw_sp2_nve_vxlan_can_offload,
	.nve_config	= mlxsw_sp_nve_vxlan_config,
	.init		= mlxsw_sp2_nve_vxlan_init,
	.fini		= mlxsw_sp2_nve_vxlan_fini,
};
