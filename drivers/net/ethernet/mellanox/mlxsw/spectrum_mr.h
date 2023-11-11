/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2017-2018 Mellanox Technologies. All rights reserved */

#ifndef _MLXSW_SPECTRUM_MCROUTER_H
#define _MLXSW_SPECTRUM_MCROUTER_H

#include <linux/mroute.h>
#include <linux/mroute6.h>
#include "spectrum_router.h"
#include "spectrum.h"

enum mlxsw_sp_mr_route_action {
	MLXSW_SP_MR_ROUTE_ACTION_FORWARD,
	MLXSW_SP_MR_ROUTE_ACTION_TRAP,
	MLXSW_SP_MR_ROUTE_ACTION_TRAP_AND_FORWARD,
};

struct mlxsw_sp_mr_route_key {
	int vrid;
	enum mlxsw_sp_l3proto proto;
	union mlxsw_sp_l3addr group;
	union mlxsw_sp_l3addr group_mask;
	union mlxsw_sp_l3addr source;
	union mlxsw_sp_l3addr source_mask;
};

struct mlxsw_sp_mr_route_info {
	enum mlxsw_sp_mr_route_action route_action;
	u16 irif_index;
	u16 *erif_indices;
	size_t erif_num;
	u16 min_mtu;
};

struct mlxsw_sp_mr_route_params {
	struct mlxsw_sp_mr_route_key key;
	struct mlxsw_sp_mr_route_info value;
	enum mlxsw_sp_mr_route_prio prio;
};

struct mlxsw_sp_mr_ops {
	int priv_size;
	int route_priv_size;
	int (*init)(struct mlxsw_sp *mlxsw_sp, void *priv);
	int (*route_create)(struct mlxsw_sp *mlxsw_sp, void *priv,
			    void *route_priv,
			    struct mlxsw_sp_mr_route_params *route_params);
	int (*route_update)(struct mlxsw_sp *mlxsw_sp, void *route_priv,
			    struct mlxsw_sp_mr_route_info *route_info);
	int (*route_stats)(struct mlxsw_sp *mlxsw_sp, void *route_priv,
			   u64 *packets, u64 *bytes);
	int (*route_action_update)(struct mlxsw_sp *mlxsw_sp, void *route_priv,
				   enum mlxsw_sp_mr_route_action route_action);
	int (*route_min_mtu_update)(struct mlxsw_sp *mlxsw_sp, void *route_priv,
				    u16 min_mtu);
	int (*route_irif_update)(struct mlxsw_sp *mlxsw_sp, void *route_priv,
				 u16 irif_index);
	int (*route_erif_add)(struct mlxsw_sp *mlxsw_sp, void *route_priv,
			      u16 erif_index);
	int (*route_erif_del)(struct mlxsw_sp *mlxsw_sp, void *route_priv,
			      u16 erif_index);
	void (*route_destroy)(struct mlxsw_sp *mlxsw_sp, void *priv,
			      void *route_priv);
	void (*fini)(struct mlxsw_sp *mlxsw_sp, void *priv);
};

struct mlxsw_sp_mr;
struct mlxsw_sp_mr_table;

int mlxsw_sp_mr_init(struct mlxsw_sp *mlxsw_sp,
		     const struct mlxsw_sp_mr_ops *mr_ops);
void mlxsw_sp_mr_fini(struct mlxsw_sp *mlxsw_sp);
int mlxsw_sp_mr_route_add(struct mlxsw_sp_mr_table *mr_table,
			  struct mr_mfc *mfc, bool replace);
void mlxsw_sp_mr_route_del(struct mlxsw_sp_mr_table *mr_table,
			   struct mr_mfc *mfc);
int mlxsw_sp_mr_vif_add(struct mlxsw_sp_mr_table *mr_table,
			struct net_device *dev, vifi_t vif_index,
			unsigned long vif_flags,
			const struct mlxsw_sp_rif *rif);
void mlxsw_sp_mr_vif_del(struct mlxsw_sp_mr_table *mr_table, vifi_t vif_index);
int mlxsw_sp_mr_rif_add(struct mlxsw_sp_mr_table *mr_table,
			const struct mlxsw_sp_rif *rif);
void mlxsw_sp_mr_rif_del(struct mlxsw_sp_mr_table *mr_table,
			 const struct mlxsw_sp_rif *rif);
void mlxsw_sp_mr_rif_mtu_update(struct mlxsw_sp_mr_table *mr_table,
				const struct mlxsw_sp_rif *rif, int mtu);
struct mlxsw_sp_mr_table *mlxsw_sp_mr_table_create(struct mlxsw_sp *mlxsw_sp,
						   u32 tb_id,
						   enum mlxsw_sp_l3proto proto);
void mlxsw_sp_mr_table_destroy(struct mlxsw_sp_mr_table *mr_table);
void mlxsw_sp_mr_table_flush(struct mlxsw_sp_mr_table *mr_table);
bool mlxsw_sp_mr_table_empty(const struct mlxsw_sp_mr_table *mr_table);

#endif
