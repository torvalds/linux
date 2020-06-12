/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2018 Mellanox Technologies. All rights reserved */

#ifndef _MLXSW_SPECTRUM_SPAN_H
#define _MLXSW_SPECTRUM_SPAN_H

#include <linux/types.h>
#include <linux/if_ether.h>
#include <linux/refcount.h>

#include "spectrum_router.h"

struct mlxsw_sp;
struct mlxsw_sp_port;

struct mlxsw_sp_span_parms {
	struct mlxsw_sp_port *dest_port; /* NULL for unoffloaded SPAN. */
	unsigned int ttl;
	unsigned char dmac[ETH_ALEN];
	unsigned char smac[ETH_ALEN];
	union mlxsw_sp_l3addr daddr;
	union mlxsw_sp_l3addr saddr;
	u16 vid;
};

enum mlxsw_sp_span_trigger {
	MLXSW_SP_SPAN_TRIGGER_INGRESS,
	MLXSW_SP_SPAN_TRIGGER_EGRESS,
};

struct mlxsw_sp_span_trigger_parms {
	int span_id;
};

struct mlxsw_sp_span_entry_ops;

struct mlxsw_sp_span_entry {
	const struct net_device *to_dev;
	const struct mlxsw_sp_span_entry_ops *ops;
	struct mlxsw_sp_span_parms parms;
	refcount_t ref_count;
	int id;
};

struct mlxsw_sp_span_entry_ops {
	bool (*can_handle)(const struct net_device *to_dev);
	int (*parms_set)(const struct net_device *to_dev,
			 struct mlxsw_sp_span_parms *sparmsp);
	int (*configure)(struct mlxsw_sp_span_entry *span_entry,
			 struct mlxsw_sp_span_parms sparms);
	void (*deconfigure)(struct mlxsw_sp_span_entry *span_entry);
};

int mlxsw_sp_span_init(struct mlxsw_sp *mlxsw_sp);
void mlxsw_sp_span_fini(struct mlxsw_sp *mlxsw_sp);
void mlxsw_sp_span_respin(struct mlxsw_sp *mlxsw_sp);

struct mlxsw_sp_span_entry *
mlxsw_sp_span_entry_find_by_port(struct mlxsw_sp *mlxsw_sp,
				 const struct net_device *to_dev);

void mlxsw_sp_span_entry_invalidate(struct mlxsw_sp *mlxsw_sp,
				    struct mlxsw_sp_span_entry *span_entry);

int mlxsw_sp_span_port_mtu_update(struct mlxsw_sp_port *port, u16 mtu);
void mlxsw_sp_span_speed_update_work(struct work_struct *work);

int mlxsw_sp_span_agent_get(struct mlxsw_sp *mlxsw_sp,
			    const struct net_device *to_dev, int *p_span_id);
void mlxsw_sp_span_agent_put(struct mlxsw_sp *mlxsw_sp, int span_id);
int mlxsw_sp_span_analyzed_port_get(struct mlxsw_sp_port *mlxsw_sp_port,
				    bool ingress);
void mlxsw_sp_span_analyzed_port_put(struct mlxsw_sp_port *mlxsw_sp_port,
				     bool ingress);
int mlxsw_sp_span_agent_bind(struct mlxsw_sp *mlxsw_sp,
			     enum mlxsw_sp_span_trigger trigger,
			     struct mlxsw_sp_port *mlxsw_sp_port,
			     const struct mlxsw_sp_span_trigger_parms *parms);
void
mlxsw_sp_span_agent_unbind(struct mlxsw_sp *mlxsw_sp,
			   enum mlxsw_sp_span_trigger trigger,
			   struct mlxsw_sp_port *mlxsw_sp_port,
			   const struct mlxsw_sp_span_trigger_parms *parms);

#endif
