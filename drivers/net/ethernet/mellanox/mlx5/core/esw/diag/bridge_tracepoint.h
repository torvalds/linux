/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021 Mellanox Technologies. */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mlx5

#if !defined(_MLX5_ESW_BRIDGE_TRACEPOINT_) || defined(TRACE_HEADER_MULTI_READ)
#define _MLX5_ESW_BRIDGE_TRACEPOINT_

#include <linux/tracepoint.h>
#include "../bridge_priv.h"

DECLARE_EVENT_CLASS(mlx5_esw_bridge_fdb_template,
		    TP_PROTO(const struct mlx5_esw_bridge_fdb_entry *fdb),
		    TP_ARGS(fdb),
		    TP_STRUCT__entry(
			    __array(char, dev_name, IFNAMSIZ)
			    __array(unsigned char, addr, ETH_ALEN)
			    __field(u16, vid)
			    __field(u16, flags)
			    __field(unsigned int, used)
			    ),
		    TP_fast_assign(
			    strncpy(__entry->dev_name,
				    netdev_name(fdb->dev),
				    IFNAMSIZ);
			    memcpy(__entry->addr, fdb->key.addr, ETH_ALEN);
			    __entry->vid = fdb->key.vid;
			    __entry->flags = fdb->flags;
			    __entry->used = jiffies_to_msecs(jiffies - fdb->lastuse)
			    ),
		    TP_printk("net_device=%s addr=%pM vid=%hu flags=%hx used=%u",
			      __entry->dev_name,
			      __entry->addr,
			      __entry->vid,
			      __entry->flags,
			      __entry->used / 1000)
	);

DEFINE_EVENT(mlx5_esw_bridge_fdb_template,
	     mlx5_esw_bridge_fdb_entry_init,
	     TP_PROTO(const struct mlx5_esw_bridge_fdb_entry *fdb),
	     TP_ARGS(fdb)
	);
DEFINE_EVENT(mlx5_esw_bridge_fdb_template,
	     mlx5_esw_bridge_fdb_entry_refresh,
	     TP_PROTO(const struct mlx5_esw_bridge_fdb_entry *fdb),
	     TP_ARGS(fdb)
	);
DEFINE_EVENT(mlx5_esw_bridge_fdb_template,
	     mlx5_esw_bridge_fdb_entry_cleanup,
	     TP_PROTO(const struct mlx5_esw_bridge_fdb_entry *fdb),
	     TP_ARGS(fdb)
	);

DECLARE_EVENT_CLASS(mlx5_esw_bridge_vlan_template,
		    TP_PROTO(const struct mlx5_esw_bridge_vlan *vlan),
		    TP_ARGS(vlan),
		    TP_STRUCT__entry(
			    __field(u16, vid)
			    __field(u16, flags)
			    ),
		    TP_fast_assign(
			    __entry->vid = vlan->vid;
			    __entry->flags = vlan->flags;
			    ),
		    TP_printk("vid=%hu flags=%hx",
			      __entry->vid,
			      __entry->flags)
	);

DEFINE_EVENT(mlx5_esw_bridge_vlan_template,
	     mlx5_esw_bridge_vlan_create,
	     TP_PROTO(const struct mlx5_esw_bridge_vlan *vlan),
	     TP_ARGS(vlan)
	);
DEFINE_EVENT(mlx5_esw_bridge_vlan_template,
	     mlx5_esw_bridge_vlan_cleanup,
	     TP_PROTO(const struct mlx5_esw_bridge_vlan *vlan),
	     TP_ARGS(vlan)
	);

DECLARE_EVENT_CLASS(mlx5_esw_bridge_port_template,
		    TP_PROTO(const struct mlx5_esw_bridge_port *port),
		    TP_ARGS(port),
		    TP_STRUCT__entry(
			    __field(u16, vport_num)
			    ),
		    TP_fast_assign(
			    __entry->vport_num = port->vport_num;
			    ),
		    TP_printk("vport_num=%hu", __entry->vport_num)
	);

DEFINE_EVENT(mlx5_esw_bridge_port_template,
	     mlx5_esw_bridge_vport_init,
	     TP_PROTO(const struct mlx5_esw_bridge_port *port),
	     TP_ARGS(port)
	);
DEFINE_EVENT(mlx5_esw_bridge_port_template,
	     mlx5_esw_bridge_vport_cleanup,
	     TP_PROTO(const struct mlx5_esw_bridge_port *port),
	     TP_ARGS(port)
	);

#endif

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH esw/diag
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE bridge_tracepoint
#include <trace/define_trace.h>
