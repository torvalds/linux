/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2018 Mellanox Technologies. All rights reserved */

#include <linux/netdevice.h>

struct mlxsw_sp_bridge;
struct mlxsw_sp_bridge_port;

struct mlxsw_sp_bridge_port *
mlxsw_sp_bridge_port_find(struct mlxsw_sp_bridge *bridge,
			  struct net_device *brport_dev);

u8 mlxsw_sp_bridge_port_stp_state(struct mlxsw_sp_bridge_port *bridge_port);
