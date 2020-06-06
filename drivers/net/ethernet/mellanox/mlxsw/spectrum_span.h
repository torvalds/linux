/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2018 Mellanox Technologies. All rights reserved */

#ifndef _MLXSW_SPECTRUM_SPAN_H
#define _MLXSW_SPECTRUM_SPAN_H

#include <linux/types.h>
#include <linux/if_ether.h>

#include "spectrum_router.h"

struct mlxsw_sp;
struct mlxsw_sp_port;

enum mlxsw_sp_span_type {
	MLXSW_SP_SPAN_EGRESS,
	MLXSW_SP_SPAN_INGRESS
};

struct mlxsw_sp_span_inspected_port {
	struct list_head list;
	enum mlxsw_sp_span_type type;
	u8 local_port;

	/* Whether this is a directly bound mirror (port-to-port) or an ACL. */
	bool bound;
};

struct mlxsw_sp_span_parms {
	struct mlxsw_sp_port *dest_port; /* NULL for unoffloaded SPAN. */
	unsigned int ttl;
	unsigned char dmac[ETH_ALEN];
	unsigned char smac[ETH_ALEN];
	union mlxsw_sp_l3addr daddr;
	union mlxsw_sp_l3addr saddr;
	u16 vid;
};

struct mlxsw_sp_span_entry_ops;

struct mlxsw_sp_span_entry {
	const struct net_device *to_dev;
	const struct mlxsw_sp_span_entry_ops *ops;
	struct mlxsw_sp_span_parms parms;
	struct list_head bound_ports_list;
	int ref_count;
	int id;
};

struct mlxsw_sp_span_entry_ops {
	bool (*can_handle)(const struct net_device *to_dev);
	int (*parms)(const struct net_device *to_dev,
		     struct mlxsw_sp_span_parms *sparmsp);
	int (*configure)(struct mlxsw_sp_span_entry *span_entry,
			 struct mlxsw_sp_span_parms sparms);
	void (*deconfigure)(struct mlxsw_sp_span_entry *span_entry);
};

int mlxsw_sp_span_init(struct mlxsw_sp *mlxsw_sp);
void mlxsw_sp_span_fini(struct mlxsw_sp *mlxsw_sp);
void mlxsw_sp_span_respin(struct mlxsw_sp *mlxsw_sp);

int mlxsw_sp_span_mirror_add(struct mlxsw_sp_port *from,
			     const struct net_device *to_dev,
			     enum mlxsw_sp_span_type type,
			     bool bind, int *p_span_id);
void mlxsw_sp_span_mirror_del(struct mlxsw_sp_port *from, int span_id,
			      enum mlxsw_sp_span_type type, bool bind);
struct mlxsw_sp_span_entry *
mlxsw_sp_span_entry_find_by_port(struct mlxsw_sp *mlxsw_sp,
				 const struct net_device *to_dev);

void mlxsw_sp_span_entry_invalidate(struct mlxsw_sp *mlxsw_sp,
				    struct mlxsw_sp_span_entry *span_entry);

int mlxsw_sp_span_port_mtu_update(struct mlxsw_sp_port *port, u16 mtu);
void mlxsw_sp_span_speed_update_work(struct work_struct *work);

#endif
