// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2025 NVIDIA Corporation & Affiliates */

#include <linux/mlx5/vport.h>
#include <mlx5_core.h>
#include <fs_core.h>
#include <fs_cmd.h>
#include "fs_hws_pools.h"
#include "mlx5hws.h"

#define MLX5HWS_CTX_MAX_NUM_OF_QUEUES 16
#define MLX5HWS_CTX_QUEUE_SIZE 256

static struct mlx5hws_action *
mlx5_fs_create_action_remove_header_vlan(struct mlx5hws_context *ctx);
static void
mlx5_fs_destroy_pr_pool(struct mlx5_fs_pool *pool, struct xarray *pr_pools,
			unsigned long index);
static void
mlx5_fs_destroy_mh_pool(struct mlx5_fs_pool *pool, struct xarray *mh_pools,
			unsigned long index);

static int mlx5_fs_init_hws_actions_pool(struct mlx5_core_dev *dev,
					 struct mlx5_fs_hws_context *fs_ctx)
{
	u32 flags = MLX5HWS_ACTION_FLAG_HWS_FDB | MLX5HWS_ACTION_FLAG_SHARED;
	struct mlx5_fs_hws_actions_pool *hws_pool = &fs_ctx->hws_pool;
	struct mlx5hws_action_reformat_header reformat_hdr = {};
	struct mlx5hws_context *ctx = fs_ctx->hws_ctx;
	enum mlx5hws_action_type action_type;
	int err = -ENOSPC;

	hws_pool->tag_action = mlx5hws_action_create_tag(ctx, flags);
	if (!hws_pool->tag_action)
		return err;
	hws_pool->pop_vlan_action = mlx5hws_action_create_pop_vlan(ctx, flags);
	if (!hws_pool->pop_vlan_action)
		goto destroy_tag;
	hws_pool->push_vlan_action = mlx5hws_action_create_push_vlan(ctx, flags);
	if (!hws_pool->push_vlan_action)
		goto destroy_pop_vlan;
	hws_pool->drop_action = mlx5hws_action_create_dest_drop(ctx, flags);
	if (!hws_pool->drop_action)
		goto destroy_push_vlan;
	action_type = MLX5HWS_ACTION_TYP_REFORMAT_TNL_L2_TO_L2;
	hws_pool->decapl2_action =
		mlx5hws_action_create_reformat(ctx, action_type, 1,
					       &reformat_hdr, 0, flags);
	if (!hws_pool->decapl2_action)
		goto destroy_drop;
	hws_pool->remove_hdr_vlan_action =
		mlx5_fs_create_action_remove_header_vlan(ctx);
	if (!hws_pool->remove_hdr_vlan_action)
		goto destroy_decapl2;
	err = mlx5_fs_hws_pr_pool_init(&hws_pool->insert_hdr_pool, dev, 0,
				       MLX5HWS_ACTION_TYP_INSERT_HEADER);
	if (err)
		goto destroy_remove_hdr;
	err = mlx5_fs_hws_pr_pool_init(&hws_pool->dl3tnltol2_pool, dev, 0,
				       MLX5HWS_ACTION_TYP_REFORMAT_TNL_L3_TO_L2);
	if (err)
		goto cleanup_insert_hdr;
	xa_init(&hws_pool->el2tol3tnl_pools);
	xa_init(&hws_pool->el2tol2tnl_pools);
	xa_init(&hws_pool->mh_pools);
	xa_init(&hws_pool->table_dests);
	xa_init(&hws_pool->vport_dests);
	xa_init(&hws_pool->vport_vhca_dests);
	xa_init(&hws_pool->aso_meters);
	xa_init(&hws_pool->sample_dests);
	return 0;

cleanup_insert_hdr:
	mlx5_fs_hws_pr_pool_cleanup(&hws_pool->insert_hdr_pool);
destroy_remove_hdr:
	mlx5hws_action_destroy(hws_pool->remove_hdr_vlan_action);
destroy_decapl2:
	mlx5hws_action_destroy(hws_pool->decapl2_action);
destroy_drop:
	mlx5hws_action_destroy(hws_pool->drop_action);
destroy_push_vlan:
	mlx5hws_action_destroy(hws_pool->push_vlan_action);
destroy_pop_vlan:
	mlx5hws_action_destroy(hws_pool->pop_vlan_action);
destroy_tag:
	mlx5hws_action_destroy(hws_pool->tag_action);
	return err;
}

static void mlx5_fs_cleanup_hws_actions_pool(struct mlx5_fs_hws_context *fs_ctx)
{
	struct mlx5_fs_hws_actions_pool *hws_pool = &fs_ctx->hws_pool;
	struct mlx5_fs_hws_data *fs_hws_data;
	struct mlx5hws_action *action;
	struct mlx5_fs_pool *pool;
	unsigned long i;

	xa_for_each(&hws_pool->sample_dests, i, fs_hws_data)
		kfree(fs_hws_data);
	xa_destroy(&hws_pool->sample_dests);
	xa_for_each(&hws_pool->aso_meters, i, fs_hws_data)
		kfree(fs_hws_data);
	xa_destroy(&hws_pool->aso_meters);
	xa_for_each(&hws_pool->vport_vhca_dests, i, action)
		mlx5hws_action_destroy(action);
	xa_destroy(&hws_pool->vport_vhca_dests);
	xa_for_each(&hws_pool->vport_dests, i, action)
		mlx5hws_action_destroy(action);
	xa_destroy(&hws_pool->vport_dests);
	xa_destroy(&hws_pool->table_dests);
	xa_for_each(&hws_pool->mh_pools, i, pool)
		mlx5_fs_destroy_mh_pool(pool, &hws_pool->mh_pools, i);
	xa_destroy(&hws_pool->mh_pools);
	xa_for_each(&hws_pool->el2tol2tnl_pools, i, pool)
		mlx5_fs_destroy_pr_pool(pool, &hws_pool->el2tol2tnl_pools, i);
	xa_destroy(&hws_pool->el2tol2tnl_pools);
	xa_for_each(&hws_pool->el2tol3tnl_pools, i, pool)
		mlx5_fs_destroy_pr_pool(pool, &hws_pool->el2tol3tnl_pools, i);
	xa_destroy(&hws_pool->el2tol3tnl_pools);
	mlx5_fs_hws_pr_pool_cleanup(&hws_pool->dl3tnltol2_pool);
	mlx5_fs_hws_pr_pool_cleanup(&hws_pool->insert_hdr_pool);
	mlx5hws_action_destroy(hws_pool->remove_hdr_vlan_action);
	mlx5hws_action_destroy(hws_pool->decapl2_action);
	mlx5hws_action_destroy(hws_pool->drop_action);
	mlx5hws_action_destroy(hws_pool->push_vlan_action);
	mlx5hws_action_destroy(hws_pool->pop_vlan_action);
	mlx5hws_action_destroy(hws_pool->tag_action);
}

static int mlx5_cmd_hws_create_ns(struct mlx5_flow_root_namespace *ns)
{
	struct mlx5hws_context_attr hws_ctx_attr = {};
	int err;

	hws_ctx_attr.queues = min_t(int, num_online_cpus(),
				    MLX5HWS_CTX_MAX_NUM_OF_QUEUES);
	hws_ctx_attr.queue_size = MLX5HWS_CTX_QUEUE_SIZE;

	ns->fs_hws_context.hws_ctx =
		mlx5hws_context_open(ns->dev, &hws_ctx_attr);
	if (!ns->fs_hws_context.hws_ctx) {
		mlx5_core_err(ns->dev, "Failed to create hws flow namespace\n");
		return -EINVAL;
	}
	err = mlx5_fs_init_hws_actions_pool(ns->dev, &ns->fs_hws_context);
	if (err) {
		mlx5_core_err(ns->dev, "Failed to init hws actions pool\n");
		mlx5hws_context_close(ns->fs_hws_context.hws_ctx);
		return err;
	}
	return 0;
}

static int mlx5_cmd_hws_destroy_ns(struct mlx5_flow_root_namespace *ns)
{
	mlx5_fs_cleanup_hws_actions_pool(&ns->fs_hws_context);
	return mlx5hws_context_close(ns->fs_hws_context.hws_ctx);
}

static int mlx5_cmd_hws_set_peer(struct mlx5_flow_root_namespace *ns,
				 struct mlx5_flow_root_namespace *peer_ns,
				 u16 peer_vhca_id)
{
	struct mlx5hws_context *peer_ctx = NULL;

	if (peer_ns)
		peer_ctx = peer_ns->fs_hws_context.hws_ctx;
	mlx5hws_context_set_peer(ns->fs_hws_context.hws_ctx, peer_ctx,
				 peer_vhca_id);
	return 0;
}

static int mlx5_fs_set_ft_default_miss(struct mlx5_flow_root_namespace *ns,
				       struct mlx5_flow_table *ft,
				       struct mlx5_flow_table *next_ft)
{
	struct mlx5hws_table *next_tbl;
	int err;

	if (!ns->fs_hws_context.hws_ctx)
		return -EINVAL;

	/* if no change required, return */
	if (!next_ft && !ft->fs_hws_table.miss_ft_set)
		return 0;

	next_tbl = next_ft ? next_ft->fs_hws_table.hws_table : NULL;
	err = mlx5hws_table_set_default_miss(ft->fs_hws_table.hws_table, next_tbl);
	if (err) {
		mlx5_core_err(ns->dev, "Failed setting FT default miss (%d)\n", err);
		return err;
	}
	ft->fs_hws_table.miss_ft_set = !!next_tbl;
	return 0;
}

static int mlx5_fs_add_flow_table_dest_action(struct mlx5_flow_root_namespace *ns,
					      struct mlx5_flow_table *ft)
{
	u32 flags = MLX5HWS_ACTION_FLAG_HWS_FDB | MLX5HWS_ACTION_FLAG_SHARED;
	struct mlx5_fs_hws_context *fs_ctx = &ns->fs_hws_context;
	struct mlx5hws_action *dest_ft_action;
	struct xarray *dests_xa;
	int err;

	dest_ft_action = mlx5hws_action_create_dest_table_num(fs_ctx->hws_ctx,
							      ft->id, flags);
	if (!dest_ft_action) {
		mlx5_core_err(ns->dev, "Failed creating dest table action\n");
		return -ENOMEM;
	}

	dests_xa = &fs_ctx->hws_pool.table_dests;
	err = xa_insert(dests_xa, ft->id, dest_ft_action, GFP_KERNEL);
	if (err)
		mlx5hws_action_destroy(dest_ft_action);
	return err;
}

static int mlx5_fs_del_flow_table_dest_action(struct mlx5_flow_root_namespace *ns,
					      struct mlx5_flow_table *ft)
{
	struct mlx5_fs_hws_context *fs_ctx = &ns->fs_hws_context;
	struct mlx5hws_action *dest_ft_action;
	struct xarray *dests_xa;
	int err;

	dests_xa = &fs_ctx->hws_pool.table_dests;
	dest_ft_action = xa_erase(dests_xa, ft->id);
	if (!dest_ft_action) {
		mlx5_core_err(ns->dev, "Failed to erase dest ft action\n");
		return -ENOENT;
	}

	err = mlx5hws_action_destroy(dest_ft_action);
	if (err)
		mlx5_core_err(ns->dev, "Failed to destroy dest ft action\n");
	return err;
}

static int mlx5_cmd_hws_create_flow_table(struct mlx5_flow_root_namespace *ns,
					  struct mlx5_flow_table *ft,
					  struct mlx5_flow_table_attr *ft_attr,
					  struct mlx5_flow_table *next_ft)
{
	struct mlx5hws_context *ctx = ns->fs_hws_context.hws_ctx;
	struct mlx5hws_table_attr tbl_attr = {};
	struct mlx5hws_table *tbl;
	int err;

	if (mlx5_fs_cmd_is_fw_term_table(ft)) {
		err = mlx5_fs_cmd_get_fw_cmds()->create_flow_table(ns, ft, ft_attr,
								   next_ft);
		if (err)
			return err;
		err = mlx5_fs_add_flow_table_dest_action(ns, ft);
		if (err)
			mlx5_fs_cmd_get_fw_cmds()->destroy_flow_table(ns, ft);
		return err;
	}

	if (ns->table_type != FS_FT_FDB) {
		mlx5_core_err(ns->dev, "Table type %d not supported for HWS\n",
			      ns->table_type);
		return -EOPNOTSUPP;
	}

	tbl_attr.type = MLX5HWS_TABLE_TYPE_FDB;
	tbl_attr.level = ft_attr->level;
	tbl_attr.uid = ft_attr->uid;
	tbl = mlx5hws_table_create(ctx, &tbl_attr);
	if (!tbl) {
		mlx5_core_err(ns->dev, "Failed creating hws flow_table\n");
		return -EINVAL;
	}

	ft->fs_hws_table.hws_table = tbl;
	ft->id = mlx5hws_table_get_id(tbl);

	if (next_ft) {
		err = mlx5_fs_set_ft_default_miss(ns, ft, next_ft);
		if (err)
			goto destroy_table;
	}

	ft->max_fte = INT_MAX;

	err = mlx5_fs_add_flow_table_dest_action(ns, ft);
	if (err)
		goto clear_ft_miss;
	return 0;

clear_ft_miss:
	mlx5_fs_set_ft_default_miss(ns, ft, NULL);
destroy_table:
	mlx5hws_table_destroy(tbl);
	ft->fs_hws_table.hws_table = NULL;
	return err;
}

static int mlx5_cmd_hws_destroy_flow_table(struct mlx5_flow_root_namespace *ns,
					   struct mlx5_flow_table *ft)
{
	int err;

	err = mlx5_fs_del_flow_table_dest_action(ns, ft);
	if (err)
		mlx5_core_err(ns->dev, "Failed to remove dest action (%d)\n", err);

	if (mlx5_fs_cmd_is_fw_term_table(ft))
		return mlx5_fs_cmd_get_fw_cmds()->destroy_flow_table(ns, ft);

	err = mlx5_fs_set_ft_default_miss(ns, ft, NULL);
	if (err)
		mlx5_core_err(ns->dev, "Failed to disconnect next table (%d)\n", err);

	err = mlx5hws_table_destroy(ft->fs_hws_table.hws_table);
	if (err)
		mlx5_core_err(ns->dev, "Failed to destroy flow_table (%d)\n", err);

	return err;
}

static int mlx5_cmd_hws_modify_flow_table(struct mlx5_flow_root_namespace *ns,
					  struct mlx5_flow_table *ft,
					  struct mlx5_flow_table *next_ft)
{
	if (mlx5_fs_cmd_is_fw_term_table(ft))
		return mlx5_fs_cmd_get_fw_cmds()->modify_flow_table(ns, ft, next_ft);

	return mlx5_fs_set_ft_default_miss(ns, ft, next_ft);
}

static int mlx5_cmd_hws_update_root_ft(struct mlx5_flow_root_namespace *ns,
				       struct mlx5_flow_table *ft,
				       u32 underlay_qpn,
				       bool disconnect)
{
	return mlx5_fs_cmd_get_fw_cmds()->update_root_ft(ns, ft, underlay_qpn,
							 disconnect);
}

static int mlx5_cmd_hws_create_flow_group(struct mlx5_flow_root_namespace *ns,
					  struct mlx5_flow_table *ft, u32 *in,
					  struct mlx5_flow_group *fg)
{
	struct mlx5hws_match_parameters mask;
	struct mlx5hws_bwc_matcher *matcher;
	u8 match_criteria_enable;
	u32 priority;

	if (mlx5_fs_cmd_is_fw_term_table(ft))
		return mlx5_fs_cmd_get_fw_cmds()->create_flow_group(ns, ft, in, fg);

	mask.match_buf = MLX5_ADDR_OF(create_flow_group_in, in, match_criteria);
	mask.match_sz = sizeof(fg->mask.match_criteria);

	match_criteria_enable = MLX5_GET(create_flow_group_in, in,
					 match_criteria_enable);
	priority = MLX5_GET(create_flow_group_in, in, start_flow_index);
	matcher = mlx5hws_bwc_matcher_create(ft->fs_hws_table.hws_table,
					     priority, match_criteria_enable,
					     &mask);
	if (!matcher) {
		mlx5_core_err(ns->dev, "Failed creating matcher\n");
		return -EINVAL;
	}

	fg->fs_hws_matcher.matcher = matcher;
	return 0;
}

static int mlx5_cmd_hws_destroy_flow_group(struct mlx5_flow_root_namespace *ns,
					   struct mlx5_flow_table *ft,
					   struct mlx5_flow_group *fg)
{
	if (mlx5_fs_cmd_is_fw_term_table(ft))
		return mlx5_fs_cmd_get_fw_cmds()->destroy_flow_group(ns, ft, fg);

	return mlx5hws_bwc_matcher_destroy(fg->fs_hws_matcher.matcher);
}

static struct mlx5hws_action *
mlx5_fs_get_dest_action_ft(struct mlx5_fs_hws_context *fs_ctx,
			   struct mlx5_flow_rule *dst)
{
	return xa_load(&fs_ctx->hws_pool.table_dests, dst->dest_attr.ft->id);
}

static struct mlx5hws_action *
mlx5_fs_get_dest_action_table_num(struct mlx5_fs_hws_context *fs_ctx,
				  struct mlx5_flow_rule *dst)
{
	u32 table_num = dst->dest_attr.ft_num;

	return xa_load(&fs_ctx->hws_pool.table_dests, table_num);
}

static struct mlx5hws_action *
mlx5_fs_create_dest_action_table_num(struct mlx5_fs_hws_context *fs_ctx,
				     struct mlx5_flow_rule *dst)
{
	u32 flags = MLX5HWS_ACTION_FLAG_HWS_FDB | MLX5HWS_ACTION_FLAG_SHARED;
	struct mlx5hws_context *ctx = fs_ctx->hws_ctx;
	u32 table_num = dst->dest_attr.ft_num;

	return mlx5hws_action_create_dest_table_num(ctx, table_num, flags);
}

static struct mlx5hws_action *
mlx5_fs_get_dest_action_vport(struct mlx5_fs_hws_context *fs_ctx,
			      struct mlx5_flow_rule *dst,
			      bool is_dest_type_uplink)
{
	u32 flags = MLX5HWS_ACTION_FLAG_HWS_FDB | MLX5HWS_ACTION_FLAG_SHARED;
	struct mlx5_flow_destination *dest_attr = &dst->dest_attr;
	struct mlx5hws_context *ctx = fs_ctx->hws_ctx;
	struct mlx5hws_action *dest;
	struct xarray *dests_xa;
	bool vhca_id_valid;
	unsigned long idx;
	u16 vport_num;
	int err;

	vhca_id_valid = is_dest_type_uplink ||
			(dest_attr->vport.flags & MLX5_FLOW_DEST_VPORT_VHCA_ID);
	vport_num = is_dest_type_uplink ? MLX5_VPORT_UPLINK : dest_attr->vport.num;
	if (vhca_id_valid) {
		dests_xa = &fs_ctx->hws_pool.vport_vhca_dests;
		idx = (unsigned long)dest_attr->vport.vhca_id << 16 | vport_num;
	} else {
		dests_xa = &fs_ctx->hws_pool.vport_dests;
		idx = vport_num;
	}
dest_load:
	dest = xa_load(dests_xa, idx);
	if (dest)
		return dest;

	dest = mlx5hws_action_create_dest_vport(ctx, vport_num,	vhca_id_valid,
						dest_attr->vport.vhca_id, flags);

	err = xa_insert(dests_xa, idx, dest, GFP_KERNEL);
	if (err) {
		mlx5hws_action_destroy(dest);
		dest = NULL;

		if (err == -EBUSY)
			/* xarray entry was already stored by another thread */
			goto dest_load;
	}

	return dest;
}

static struct mlx5hws_action *
mlx5_fs_create_dest_action_range(struct mlx5hws_context *ctx,
				 struct mlx5_flow_rule *dst)
{
	u32 flags = MLX5HWS_ACTION_FLAG_HWS_FDB | MLX5HWS_ACTION_FLAG_SHARED;
	struct mlx5_flow_destination *dest_attr = &dst->dest_attr;

	return mlx5hws_action_create_dest_match_range(ctx,
						      dest_attr->range.field,
						      dest_attr->range.hit_ft,
						      dest_attr->range.miss_ft,
						      dest_attr->range.min,
						      dest_attr->range.max,
						      flags);
}

static struct mlx5_fs_hws_data *
mlx5_fs_get_cached_hws_data(struct xarray *cache_xa, unsigned long index)
{
	struct mlx5_fs_hws_data *fs_hws_data;
	int err;

	xa_lock(cache_xa);
	fs_hws_data = xa_load(cache_xa, index);
	if (!fs_hws_data) {
		fs_hws_data = kzalloc(sizeof(*fs_hws_data), GFP_ATOMIC);
		if (!fs_hws_data) {
			xa_unlock(cache_xa);
			return NULL;
		}
		refcount_set(&fs_hws_data->hws_action_refcount, 0);
		mutex_init(&fs_hws_data->lock);
		err = __xa_insert(cache_xa, index, fs_hws_data, GFP_ATOMIC);
		if (err) {
			kfree(fs_hws_data);
			xa_unlock(cache_xa);
			return NULL;
		}
	}
	xa_unlock(cache_xa);

	return fs_hws_data;
}

static struct mlx5hws_action *
mlx5_fs_get_action_aso_meter(struct mlx5_fs_hws_context *fs_ctx,
			     struct mlx5_exe_aso *exe_aso)
{
	struct mlx5_fs_hws_create_action_ctx create_ctx;
	struct mlx5hws_context *ctx = fs_ctx->hws_ctx;
	struct mlx5_fs_hws_data *meter_hws_data;
	u32 id = exe_aso->base_id;
	struct xarray *meters_xa;

	meters_xa = &fs_ctx->hws_pool.aso_meters;
	meter_hws_data = mlx5_fs_get_cached_hws_data(meters_xa, id);
	if (!meter_hws_data)
		return NULL;

	create_ctx.hws_ctx = ctx;
	create_ctx.actions_type = MLX5HWS_ACTION_TYP_ASO_METER;
	create_ctx.id = id;
	create_ctx.return_reg_id = exe_aso->return_reg_id;

	return mlx5_fs_get_hws_action(meter_hws_data, &create_ctx);
}

static void mlx5_fs_put_action_aso_meter(struct mlx5_fs_hws_context *fs_ctx,
					 struct mlx5_exe_aso *exe_aso)
{
	struct mlx5_fs_hws_data *meter_hws_data;
	struct xarray *meters_xa;

	meters_xa = &fs_ctx->hws_pool.aso_meters;
	meter_hws_data = xa_load(meters_xa, exe_aso->base_id);
	if (!meter_hws_data)
		return;
	return mlx5_fs_put_hws_action(meter_hws_data);
}

static struct mlx5hws_action *
mlx5_fs_get_dest_action_sampler(struct mlx5_fs_hws_context *fs_ctx,
				struct mlx5_flow_rule *dst)
{
	struct mlx5_fs_hws_create_action_ctx create_ctx;
	struct mlx5hws_context *ctx = fs_ctx->hws_ctx;
	struct mlx5_fs_hws_data *sampler_hws_data;
	u32 id = dst->dest_attr.sampler_id;
	struct xarray *sampler_xa;

	sampler_xa = &fs_ctx->hws_pool.sample_dests;
	sampler_hws_data = mlx5_fs_get_cached_hws_data(sampler_xa, id);
	if (!sampler_hws_data)
		return NULL;

	create_ctx.hws_ctx = ctx;
	create_ctx.actions_type = MLX5HWS_ACTION_TYP_SAMPLER;
	create_ctx.id = id;

	return mlx5_fs_get_hws_action(sampler_hws_data, &create_ctx);
}

static void mlx5_fs_put_dest_action_sampler(struct mlx5_fs_hws_context *fs_ctx,
					    u32 sampler_id)
{
	struct mlx5_fs_hws_data *sampler_hws_data;
	struct xarray *sampler_xa;

	sampler_xa = &fs_ctx->hws_pool.sample_dests;
	sampler_hws_data = xa_load(sampler_xa, sampler_id);
	if (!sampler_hws_data)
		return;

	mlx5_fs_put_hws_action(sampler_hws_data);
}

static struct mlx5hws_action *
mlx5_fs_create_action_dest_array(struct mlx5hws_context *ctx,
				 struct mlx5hws_action_dest_attr *dests,
				 u32 num_of_dests)
{
	u32 flags = MLX5HWS_ACTION_FLAG_HWS_FDB | MLX5HWS_ACTION_FLAG_SHARED;

	return mlx5hws_action_create_dest_array(ctx, num_of_dests, dests,
						flags);
}

static struct mlx5hws_action *
mlx5_fs_get_action_push_vlan(struct mlx5_fs_hws_context *fs_ctx)
{
	return fs_ctx->hws_pool.push_vlan_action;
}

static u32 mlx5_fs_calc_vlan_hdr(struct mlx5_fs_vlan *vlan)
{
	u16 n_ethtype = vlan->ethtype;
	u8 prio = vlan->prio;
	u16 vid = vlan->vid;

	return (u32)n_ethtype << 16 | (u32)(prio) << 12 | (u32)vid;
}

static struct mlx5hws_action *
mlx5_fs_get_action_pop_vlan(struct mlx5_fs_hws_context *fs_ctx)
{
	return fs_ctx->hws_pool.pop_vlan_action;
}

static struct mlx5hws_action *
mlx5_fs_get_action_decap_tnl_l2_to_l2(struct mlx5_fs_hws_context *fs_ctx)
{
	return fs_ctx->hws_pool.decapl2_action;
}

static struct mlx5hws_action *
mlx5_fs_get_dest_action_drop(struct mlx5_fs_hws_context *fs_ctx)
{
	return fs_ctx->hws_pool.drop_action;
}

static struct mlx5hws_action *
mlx5_fs_get_action_tag(struct mlx5_fs_hws_context *fs_ctx)
{
	return fs_ctx->hws_pool.tag_action;
}

static struct mlx5hws_action *
mlx5_fs_create_action_last(struct mlx5hws_context *ctx)
{
	u32 flags = MLX5HWS_ACTION_FLAG_HWS_FDB | MLX5HWS_ACTION_FLAG_SHARED;

	return mlx5hws_action_create_last(ctx, flags);
}

static struct mlx5hws_action *
mlx5_fs_create_hws_action(struct mlx5_fs_hws_create_action_ctx *create_ctx)
{
	u32 flags = MLX5HWS_ACTION_FLAG_HWS_FDB | MLX5HWS_ACTION_FLAG_SHARED;

	switch (create_ctx->actions_type) {
	case MLX5HWS_ACTION_TYP_CTR:
		return mlx5hws_action_create_counter(create_ctx->hws_ctx,
						     create_ctx->id, flags);
	case MLX5HWS_ACTION_TYP_ASO_METER:
		return mlx5hws_action_create_aso_meter(create_ctx->hws_ctx,
						       create_ctx->id,
						       create_ctx->return_reg_id,
						       flags);
	case MLX5HWS_ACTION_TYP_SAMPLER:
		return mlx5hws_action_create_flow_sampler(create_ctx->hws_ctx,
							  create_ctx->id, flags);
	default:
		return NULL;
	}
}

struct mlx5hws_action *
mlx5_fs_get_hws_action(struct mlx5_fs_hws_data *fs_hws_data,
		       struct mlx5_fs_hws_create_action_ctx *create_ctx)
{
	/* try avoid locking if not necessary */
	if (refcount_inc_not_zero(&fs_hws_data->hws_action_refcount))
		return fs_hws_data->hws_action;

	mutex_lock(&fs_hws_data->lock);
	if (refcount_inc_not_zero(&fs_hws_data->hws_action_refcount)) {
		mutex_unlock(&fs_hws_data->lock);
		return fs_hws_data->hws_action;
	}
	fs_hws_data->hws_action = mlx5_fs_create_hws_action(create_ctx);
	if (!fs_hws_data->hws_action) {
		mutex_unlock(&fs_hws_data->lock);
		return NULL;
	}
	refcount_set(&fs_hws_data->hws_action_refcount, 1);
	mutex_unlock(&fs_hws_data->lock);

	return fs_hws_data->hws_action;
}

void mlx5_fs_put_hws_action(struct mlx5_fs_hws_data *fs_hws_data)
{
	if (!fs_hws_data)
		return;

	/* try avoid locking if not necessary */
	if (refcount_dec_not_one(&fs_hws_data->hws_action_refcount))
		return;

	mutex_lock(&fs_hws_data->lock);
	if (!refcount_dec_and_test(&fs_hws_data->hws_action_refcount)) {
		mutex_unlock(&fs_hws_data->lock);
		return;
	}
	mlx5hws_action_destroy(fs_hws_data->hws_action);
	fs_hws_data->hws_action = NULL;
	mutex_unlock(&fs_hws_data->lock);
}

static void mlx5_fs_destroy_fs_action(struct mlx5_flow_root_namespace *ns,
				      struct mlx5_fs_hws_rule_action *fs_action)
{
	struct mlx5_fs_hws_context *fs_ctx = &ns->fs_hws_context;

	switch (mlx5hws_action_get_type(fs_action->action)) {
	case MLX5HWS_ACTION_TYP_CTR:
		mlx5_fc_put_hws_action(fs_action->counter);
		break;
	case MLX5HWS_ACTION_TYP_ASO_METER:
		mlx5_fs_put_action_aso_meter(fs_ctx, fs_action->exe_aso);
		break;
	case MLX5HWS_ACTION_TYP_SAMPLER:
		mlx5_fs_put_dest_action_sampler(fs_ctx, fs_action->sampler_id);
		break;
	default:
		mlx5hws_action_destroy(fs_action->action);
	}
}

static void
mlx5_fs_destroy_fs_actions(struct mlx5_flow_root_namespace *ns,
			   struct mlx5_fs_hws_rule_action **fs_actions,
			   int *num_fs_actions)
{
	int i;

	/* Free in reverse order to handle action dependencies */
	for (i = *num_fs_actions - 1; i >= 0; i--)
		mlx5_fs_destroy_fs_action(ns, *fs_actions + i);
	*num_fs_actions = 0;
	kfree(*fs_actions);
	*fs_actions = NULL;
}

/* Splits FTE's actions into cached, rule and destination actions.
 * The cached and destination actions are saved on the fte hws rule.
 * The rule actions are returned as a parameter, together with their count.
 * We want to support a rule with 32 destinations, which means we need to
 * account for 32 destinations plus usually a counter plus one more action
 * for a multi-destination flow table.
 * 32 is SW limitation for array size, keep. HWS limitation is 16M STEs per matcher
 */
#define MLX5_FLOW_CONTEXT_ACTION_MAX 34
static int mlx5_fs_fte_get_hws_actions(struct mlx5_flow_root_namespace *ns,
				       struct mlx5_flow_table *ft,
				       struct mlx5_flow_group *group,
				       struct fs_fte *fte,
				       struct mlx5hws_rule_action **ractions)
{
	struct mlx5_flow_act *fte_action = &fte->act_dests.action;
	struct mlx5_fs_hws_context *fs_ctx = &ns->fs_hws_context;
	struct mlx5hws_action_dest_attr *dest_actions;
	struct mlx5hws_context *ctx = fs_ctx->hws_ctx;
	struct mlx5_fs_hws_rule_action *fs_actions;
	struct mlx5_core_dev *dev = ns->dev;
	struct mlx5hws_action *dest_action;
	struct mlx5hws_action *tmp_action;
	struct mlx5_fs_hws_pr *pr_data;
	struct mlx5_fs_hws_mh *mh_data;
	bool delay_encap_set = false;
	struct mlx5_flow_rule *dst;
	int num_dest_actions = 0;
	int num_fs_actions = 0;
	int num_actions = 0;
	int err;

	*ractions = kcalloc(MLX5_FLOW_CONTEXT_ACTION_MAX, sizeof(**ractions),
			    GFP_KERNEL);
	if (!*ractions) {
		err = -ENOMEM;
		goto out_err;
	}

	fs_actions = kcalloc(MLX5_FLOW_CONTEXT_ACTION_MAX,
			     sizeof(*fs_actions), GFP_KERNEL);
	if (!fs_actions) {
		err = -ENOMEM;
		goto free_actions_alloc;
	}

	dest_actions = kcalloc(MLX5_FLOW_CONTEXT_ACTION_MAX,
			       sizeof(*dest_actions), GFP_KERNEL);
	if (!dest_actions) {
		err = -ENOMEM;
		goto free_fs_actions_alloc;
	}

	/* The order of the actions are must to be kept, only the following
	 * order is supported by HW steering:
	 * HWS: decap -> remove_hdr -> pop_vlan -> modify header -> push_vlan
	 *      -> reformat (insert_hdr/encap) -> ctr -> tag -> aso
	 *      -> drop -> FWD:tbl/vport/sampler/tbl_num/range -> dest_array -> last
	 */
	if (fte_action->action & MLX5_FLOW_CONTEXT_ACTION_DECAP) {
		tmp_action = mlx5_fs_get_action_decap_tnl_l2_to_l2(fs_ctx);
		if (!tmp_action) {
			err = -ENOMEM;
			goto free_dest_actions_alloc;
		}
		(*ractions)[num_actions++].action = tmp_action;
	}

	if (fte_action->action & MLX5_FLOW_CONTEXT_ACTION_PACKET_REFORMAT) {
		int reformat_type = fte_action->pkt_reformat->reformat_type;

		if (fte_action->pkt_reformat->owner == MLX5_FLOW_RESOURCE_OWNER_FW) {
			mlx5_core_err(dev, "FW-owned reformat can't be used in HWS rule\n");
			err = -EINVAL;
			goto free_actions;
		}

		if (reformat_type == MLX5_REFORMAT_TYPE_L3_TUNNEL_TO_L2) {
			pr_data = fte_action->pkt_reformat->fs_hws_action.pr_data;
			(*ractions)[num_actions].reformat.offset = pr_data->offset;
			(*ractions)[num_actions].reformat.hdr_idx = pr_data->hdr_idx;
			(*ractions)[num_actions].reformat.data = pr_data->data;
			(*ractions)[num_actions++].action =
				fte_action->pkt_reformat->fs_hws_action.hws_action;
		} else if (reformat_type == MLX5_REFORMAT_TYPE_REMOVE_HDR) {
			(*ractions)[num_actions++].action =
				fte_action->pkt_reformat->fs_hws_action.hws_action;
		} else {
			delay_encap_set = true;
		}
	}

	if (fte_action->action & MLX5_FLOW_CONTEXT_ACTION_VLAN_POP) {
		tmp_action = mlx5_fs_get_action_pop_vlan(fs_ctx);
		if (!tmp_action) {
			err = -ENOMEM;
			goto free_actions;
		}
		(*ractions)[num_actions++].action = tmp_action;
	}

	if (fte_action->action & MLX5_FLOW_CONTEXT_ACTION_VLAN_POP_2) {
		tmp_action = mlx5_fs_get_action_pop_vlan(fs_ctx);
		if (!tmp_action) {
			err = -ENOMEM;
			goto free_actions;
		}
		(*ractions)[num_actions++].action = tmp_action;
	}

	if (fte_action->action & MLX5_FLOW_CONTEXT_ACTION_MOD_HDR) {
		mh_data = fte_action->modify_hdr->fs_hws_action.mh_data;
		(*ractions)[num_actions].modify_header.offset = mh_data->offset;
		(*ractions)[num_actions].modify_header.data = mh_data->data;
		(*ractions)[num_actions++].action =
			fte_action->modify_hdr->fs_hws_action.hws_action;
	}

	if (fte_action->action & MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH) {
		tmp_action = mlx5_fs_get_action_push_vlan(fs_ctx);
		if (!tmp_action) {
			err = -ENOMEM;
			goto free_actions;
		}
		(*ractions)[num_actions].push_vlan.vlan_hdr =
			htonl(mlx5_fs_calc_vlan_hdr(&fte_action->vlan[0]));
		(*ractions)[num_actions++].action = tmp_action;
	}

	if (fte_action->action & MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH_2) {
		tmp_action = mlx5_fs_get_action_push_vlan(fs_ctx);
		if (!tmp_action) {
			err = -ENOMEM;
			goto free_actions;
		}
		(*ractions)[num_actions].push_vlan.vlan_hdr =
			htonl(mlx5_fs_calc_vlan_hdr(&fte_action->vlan[1]));
		(*ractions)[num_actions++].action = tmp_action;
	}

	if (delay_encap_set) {
		pr_data = fte_action->pkt_reformat->fs_hws_action.pr_data;
		(*ractions)[num_actions].reformat.offset = pr_data->offset;
		(*ractions)[num_actions].reformat.data = pr_data->data;
		(*ractions)[num_actions++].action =
			fte_action->pkt_reformat->fs_hws_action.hws_action;
	}

	if (fte_action->action & MLX5_FLOW_CONTEXT_ACTION_COUNT) {
		list_for_each_entry(dst, &fte->node.children, node.list) {
			struct mlx5_fc *counter;

			if (dst->dest_attr.type !=
			    MLX5_FLOW_DESTINATION_TYPE_COUNTER)
				continue;

			if (num_actions == MLX5_FLOW_CONTEXT_ACTION_MAX) {
				err = -EOPNOTSUPP;
				goto free_actions;
			}

			counter = dst->dest_attr.counter;
			tmp_action = mlx5_fc_get_hws_action(ctx, counter);
			if (!tmp_action) {
				err = -EINVAL;
				goto free_actions;
			}

			(*ractions)[num_actions].counter.offset =
				mlx5_fc_id(counter) - mlx5_fc_get_base_id(counter);
			(*ractions)[num_actions++].action = tmp_action;
			fs_actions[num_fs_actions].action = tmp_action;
			fs_actions[num_fs_actions++].counter = counter;
		}
	}

	if (fte->act_dests.flow_context.flow_tag) {
		if (num_actions == MLX5_FLOW_CONTEXT_ACTION_MAX) {
			err = -EOPNOTSUPP;
			goto free_actions;
		}
		tmp_action = mlx5_fs_get_action_tag(fs_ctx);
		if (!tmp_action) {
			err = -ENOMEM;
			goto free_actions;
		}
		(*ractions)[num_actions].tag.value = fte->act_dests.flow_context.flow_tag;
		(*ractions)[num_actions++].action = tmp_action;
	}

	if (fte_action->action & MLX5_FLOW_CONTEXT_ACTION_EXECUTE_ASO) {
		if (fte_action->exe_aso.type != MLX5_EXE_ASO_FLOW_METER ||
		    num_actions == MLX5_FLOW_CONTEXT_ACTION_MAX) {
			err = -EOPNOTSUPP;
			goto free_actions;
		}

		tmp_action = mlx5_fs_get_action_aso_meter(fs_ctx,
							  &fte_action->exe_aso);
		if (!tmp_action) {
			err = -ENOMEM;
			goto free_actions;
		}
		(*ractions)[num_actions].aso_meter.offset =
			fte_action->exe_aso.flow_meter.meter_idx;
		(*ractions)[num_actions].aso_meter.init_color =
			fte_action->exe_aso.flow_meter.init_color;
		(*ractions)[num_actions++].action = tmp_action;
		fs_actions[num_fs_actions].action = tmp_action;
		fs_actions[num_fs_actions++].exe_aso = &fte_action->exe_aso;
	}

	if (fte_action->action & MLX5_FLOW_CONTEXT_ACTION_DROP) {
		dest_action = mlx5_fs_get_dest_action_drop(fs_ctx);
		if (!dest_action) {
			err = -ENOMEM;
			goto free_actions;
		}
		dest_actions[num_dest_actions++].dest = dest_action;
	}

	if (fte_action->action & MLX5_FLOW_CONTEXT_ACTION_FWD_DEST) {
		list_for_each_entry(dst, &fte->node.children, node.list) {
			struct mlx5_flow_destination *attr = &dst->dest_attr;
			bool type_uplink =
				attr->type == MLX5_FLOW_DESTINATION_TYPE_UPLINK;

			if (num_fs_actions == MLX5_FLOW_CONTEXT_ACTION_MAX ||
			    num_dest_actions == MLX5_FLOW_CONTEXT_ACTION_MAX) {
				err = -EOPNOTSUPP;
				goto free_actions;
			}
			if (attr->type == MLX5_FLOW_DESTINATION_TYPE_COUNTER)
				continue;

			switch (attr->type) {
			case MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE:
				dest_action = mlx5_fs_get_dest_action_ft(fs_ctx, dst);
				if (dst->dest_attr.ft->flags &
				    MLX5_FLOW_TABLE_UPLINK_VPORT)
					dest_actions[num_dest_actions].is_wire_ft = true;
				break;
			case MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE_NUM:
				dest_action = mlx5_fs_get_dest_action_table_num(fs_ctx,
										dst);
				if (dest_action)
					break;
				dest_action = mlx5_fs_create_dest_action_table_num(fs_ctx,
										   dst);
				fs_actions[num_fs_actions++].action = dest_action;
				break;
			case MLX5_FLOW_DESTINATION_TYPE_RANGE:
				dest_action = mlx5_fs_create_dest_action_range(ctx, dst);
				fs_actions[num_fs_actions++].action = dest_action;
				break;
			case MLX5_FLOW_DESTINATION_TYPE_UPLINK:
			case MLX5_FLOW_DESTINATION_TYPE_VPORT:
				dest_action = mlx5_fs_get_dest_action_vport(fs_ctx, dst,
									    type_uplink);
				break;
			case MLX5_FLOW_DESTINATION_TYPE_FLOW_SAMPLER:
				dest_action =
					mlx5_fs_get_dest_action_sampler(fs_ctx,
									dst);
				fs_actions[num_fs_actions].action = dest_action;
				fs_actions[num_fs_actions++].sampler_id =
							dst->dest_attr.sampler_id;
				break;
			default:
				err = -EOPNOTSUPP;
				goto free_actions;
			}
			if (!dest_action) {
				err = -ENOMEM;
				goto free_actions;
			}
			dest_actions[num_dest_actions++].dest = dest_action;
		}
	}

	if (num_dest_actions == 1) {
		if (num_actions == MLX5_FLOW_CONTEXT_ACTION_MAX) {
			err = -EOPNOTSUPP;
			goto free_actions;
		}
		(*ractions)[num_actions++].action = dest_actions->dest;
	} else if (num_dest_actions > 1) {
		if (num_actions == MLX5_FLOW_CONTEXT_ACTION_MAX ||
		    num_fs_actions == MLX5_FLOW_CONTEXT_ACTION_MAX) {
			err = -EOPNOTSUPP;
			goto free_actions;
		}
		tmp_action =
			mlx5_fs_create_action_dest_array(ctx, dest_actions,
							 num_dest_actions);
		if (!tmp_action) {
			err = -EOPNOTSUPP;
			goto free_actions;
		}
		fs_actions[num_fs_actions++].action = tmp_action;
		(*ractions)[num_actions++].action = tmp_action;
	}

	if (num_actions == MLX5_FLOW_CONTEXT_ACTION_MAX ||
	    num_fs_actions == MLX5_FLOW_CONTEXT_ACTION_MAX) {
		err = -EOPNOTSUPP;
		goto free_actions;
	}

	tmp_action = mlx5_fs_create_action_last(ctx);
	if (!tmp_action) {
		err = -ENOMEM;
		goto free_actions;
	}
	fs_actions[num_fs_actions++].action = tmp_action;
	(*ractions)[num_actions++].action = tmp_action;

	kfree(dest_actions);

	/* Actions created specifically for this rule will be destroyed
	 * once rule is deleted.
	 */
	fte->fs_hws_rule.num_fs_actions = num_fs_actions;
	fte->fs_hws_rule.hws_fs_actions = fs_actions;

	return 0;

free_actions:
	mlx5_fs_destroy_fs_actions(ns, &fs_actions, &num_fs_actions);
free_dest_actions_alloc:
	kfree(dest_actions);
free_fs_actions_alloc:
	kfree(fs_actions);
free_actions_alloc:
	kfree(*ractions);
	*ractions = NULL;
out_err:
	return err;
}

static int mlx5_cmd_hws_create_fte(struct mlx5_flow_root_namespace *ns,
				   struct mlx5_flow_table *ft,
				   struct mlx5_flow_group *group,
				   struct fs_fte *fte)
{
	struct mlx5hws_match_parameters params;
	struct mlx5hws_rule_action *ractions;
	struct mlx5hws_bwc_rule *rule;
	int err = 0;

	if (mlx5_fs_cmd_is_fw_term_table(ft))
		return mlx5_fs_cmd_get_fw_cmds()->create_fte(ns, ft, group, fte);

	err = mlx5_fs_fte_get_hws_actions(ns, ft, group, fte, &ractions);
	if (err)
		goto out_err;

	params.match_sz = sizeof(fte->val);
	params.match_buf = fte->val;

	rule = mlx5hws_bwc_rule_create(group->fs_hws_matcher.matcher, &params,
				       fte->act_dests.flow_context.flow_source,
				       ractions);
	kfree(ractions);
	if (!rule) {
		err = -EINVAL;
		goto free_actions;
	}

	fte->fs_hws_rule.bwc_rule = rule;
	return 0;

free_actions:
	mlx5_fs_destroy_fs_actions(ns, &fte->fs_hws_rule.hws_fs_actions,
				   &fte->fs_hws_rule.num_fs_actions);
out_err:
	mlx5_core_err(ns->dev, "Failed to create hws rule err(%d)\n", err);
	return err;
}

static int mlx5_cmd_hws_delete_fte(struct mlx5_flow_root_namespace *ns,
				   struct mlx5_flow_table *ft,
				   struct fs_fte *fte)
{
	struct mlx5_fs_hws_rule *rule = &fte->fs_hws_rule;
	int err;

	if (mlx5_fs_cmd_is_fw_term_table(ft))
		return mlx5_fs_cmd_get_fw_cmds()->delete_fte(ns, ft, fte);

	err = mlx5hws_bwc_rule_destroy(rule->bwc_rule);
	rule->bwc_rule = NULL;

	mlx5_fs_destroy_fs_actions(ns, &rule->hws_fs_actions,
				   &rule->num_fs_actions);

	return err;
}

static int mlx5_cmd_hws_update_fte(struct mlx5_flow_root_namespace *ns,
				   struct mlx5_flow_table *ft,
				   struct mlx5_flow_group *group,
				   int modify_mask,
				   struct fs_fte *fte)
{
	int allowed_mask = BIT(MLX5_SET_FTE_MODIFY_ENABLE_MASK_ACTION) |
		BIT(MLX5_SET_FTE_MODIFY_ENABLE_MASK_DESTINATION_LIST) |
		BIT(MLX5_SET_FTE_MODIFY_ENABLE_MASK_FLOW_COUNTERS);
	struct mlx5_fs_hws_rule_action *saved_hws_fs_actions;
	struct mlx5hws_rule_action *ractions;
	int saved_num_fs_actions;
	int ret;

	if (mlx5_fs_cmd_is_fw_term_table(ft))
		return mlx5_fs_cmd_get_fw_cmds()->update_fte(ns, ft, group,
							     modify_mask, fte);

	if ((modify_mask & ~allowed_mask) != 0)
		return -EINVAL;

	saved_hws_fs_actions = fte->fs_hws_rule.hws_fs_actions;
	saved_num_fs_actions = fte->fs_hws_rule.num_fs_actions;

	ret = mlx5_fs_fte_get_hws_actions(ns, ft, group, fte, &ractions);
	if (ret)
		return ret;

	ret = mlx5hws_bwc_rule_action_update(fte->fs_hws_rule.bwc_rule, ractions);
	kfree(ractions);
	if (ret)
		goto restore_actions;

	mlx5_fs_destroy_fs_actions(ns, &saved_hws_fs_actions,
				   &saved_num_fs_actions);
	return ret;

restore_actions:
	mlx5_fs_destroy_fs_actions(ns, &fte->fs_hws_rule.hws_fs_actions,
				   &fte->fs_hws_rule.num_fs_actions);
	fte->fs_hws_rule.hws_fs_actions = saved_hws_fs_actions;
	fte->fs_hws_rule.num_fs_actions = saved_num_fs_actions;
	return ret;
}

static struct mlx5hws_action *
mlx5_fs_create_action_remove_header_vlan(struct mlx5hws_context *ctx)
{
	u32 flags = MLX5HWS_ACTION_FLAG_HWS_FDB | MLX5HWS_ACTION_FLAG_SHARED;
	struct mlx5hws_action_remove_header_attr remove_hdr_vlan = {};

	/* MAC anchor not supported in HWS reformat, use VLAN anchor */
	remove_hdr_vlan.anchor = MLX5_REFORMAT_CONTEXT_ANCHOR_VLAN_START;
	remove_hdr_vlan.offset = 0;
	remove_hdr_vlan.size = sizeof(struct vlan_hdr);
	return mlx5hws_action_create_remove_header(ctx, &remove_hdr_vlan, flags);
}

static struct mlx5hws_action *
mlx5_fs_get_action_remove_header_vlan(struct mlx5_fs_hws_context *fs_ctx,
				      struct mlx5_pkt_reformat_params *params)
{
	if (!params ||
	    params->param_0 != MLX5_REFORMAT_CONTEXT_ANCHOR_MAC_START ||
	    params->param_1 != offsetof(struct vlan_ethhdr, h_vlan_proto) ||
	    params->size != sizeof(struct vlan_hdr))
		return NULL;

	return fs_ctx->hws_pool.remove_hdr_vlan_action;
}

static int
mlx5_fs_verify_insert_header_params(struct mlx5_core_dev *mdev,
				    struct mlx5_pkt_reformat_params *params)
{
	if ((!params->data && params->size) || (params->data && !params->size) ||
	    MLX5_CAP_GEN_2(mdev, max_reformat_insert_size) < params->size ||
	    MLX5_CAP_GEN_2(mdev, max_reformat_insert_offset) < params->param_1) {
		mlx5_core_err(mdev, "Invalid reformat params for INSERT_HDR\n");
		return -EINVAL;
	}
	if (params->param_0 != MLX5_FS_INSERT_HDR_VLAN_ANCHOR ||
	    params->param_1 != MLX5_FS_INSERT_HDR_VLAN_OFFSET ||
	    params->size != MLX5_FS_INSERT_HDR_VLAN_SIZE) {
		mlx5_core_err(mdev, "Only vlan insert header supported\n");
		return -EOPNOTSUPP;
	}
	return 0;
}

static int
mlx5_fs_verify_encap_decap_params(struct mlx5_core_dev *dev,
				  struct mlx5_pkt_reformat_params *params)
{
	if (params->param_0 || params->param_1) {
		mlx5_core_err(dev, "Invalid reformat params\n");
		return -EINVAL;
	}
	return 0;
}

static struct mlx5_fs_pool *
mlx5_fs_get_pr_encap_pool(struct mlx5_core_dev *dev, struct xarray *pr_pools,
			  enum mlx5hws_action_type reformat_type, size_t size)
{
	struct mlx5_fs_pool *pr_pool;
	unsigned long index = size;
	int err;

	pr_pool = xa_load(pr_pools, index);
	if (pr_pool)
		return pr_pool;

	pr_pool = kzalloc(sizeof(*pr_pool), GFP_KERNEL);
	if (!pr_pool)
		return ERR_PTR(-ENOMEM);
	err = mlx5_fs_hws_pr_pool_init(pr_pool, dev, size, reformat_type);
	if (err)
		goto free_pr_pool;
	err = xa_insert(pr_pools, index, pr_pool, GFP_KERNEL);
	if (err)
		goto cleanup_pr_pool;
	return pr_pool;

cleanup_pr_pool:
	mlx5_fs_hws_pr_pool_cleanup(pr_pool);
free_pr_pool:
	kfree(pr_pool);
	return ERR_PTR(err);
}

static void
mlx5_fs_destroy_pr_pool(struct mlx5_fs_pool *pool, struct xarray *pr_pools,
			unsigned long index)
{
	xa_erase(pr_pools, index);
	mlx5_fs_hws_pr_pool_cleanup(pool);
	kfree(pool);
}

static int
mlx5_cmd_hws_packet_reformat_alloc(struct mlx5_flow_root_namespace *ns,
				   struct mlx5_pkt_reformat_params *params,
				   enum mlx5_flow_namespace_type namespace,
				   struct mlx5_pkt_reformat *pkt_reformat)
{
	struct mlx5_fs_hws_context *fs_ctx = &ns->fs_hws_context;
	struct mlx5_fs_hws_actions_pool *hws_pool;
	struct mlx5hws_action *hws_action = NULL;
	struct mlx5_fs_hws_pr *pr_data = NULL;
	struct mlx5_fs_pool *pr_pool = NULL;
	struct mlx5_core_dev *dev = ns->dev;
	u8 hdr_idx = 0;
	int err;

	if (!params)
		return -EINVAL;

	hws_pool = &fs_ctx->hws_pool;

	switch (params->type) {
	case MLX5_REFORMAT_TYPE_L2_TO_VXLAN:
	case MLX5_REFORMAT_TYPE_L2_TO_NVGRE:
	case MLX5_REFORMAT_TYPE_L2_TO_L2_TUNNEL:
		if (mlx5_fs_verify_encap_decap_params(dev, params))
			return -EINVAL;
		pr_pool = mlx5_fs_get_pr_encap_pool(dev, &hws_pool->el2tol2tnl_pools,
						    MLX5HWS_ACTION_TYP_REFORMAT_L2_TO_TNL_L2,
						    params->size);
		if (IS_ERR(pr_pool))
			return PTR_ERR(pr_pool);
		break;
	case MLX5_REFORMAT_TYPE_L2_TO_L3_TUNNEL:
		if (mlx5_fs_verify_encap_decap_params(dev, params))
			return -EINVAL;
		pr_pool = mlx5_fs_get_pr_encap_pool(dev, &hws_pool->el2tol3tnl_pools,
						    MLX5HWS_ACTION_TYP_REFORMAT_L2_TO_TNL_L3,
						    params->size);
		if (IS_ERR(pr_pool))
			return PTR_ERR(pr_pool);
		break;
	case MLX5_REFORMAT_TYPE_L3_TUNNEL_TO_L2:
		if (mlx5_fs_verify_encap_decap_params(dev, params))
			return -EINVAL;
		pr_pool = &hws_pool->dl3tnltol2_pool;
		hdr_idx = params->size == ETH_HLEN ?
			  MLX5_FS_DL3TNLTOL2_MAC_HDR_IDX :
			  MLX5_FS_DL3TNLTOL2_MAC_VLAN_HDR_IDX;
		break;
	case MLX5_REFORMAT_TYPE_INSERT_HDR:
		err = mlx5_fs_verify_insert_header_params(dev, params);
		if (err)
			return err;
		pr_pool = &hws_pool->insert_hdr_pool;
		break;
	case MLX5_REFORMAT_TYPE_REMOVE_HDR:
		hws_action = mlx5_fs_get_action_remove_header_vlan(fs_ctx, params);
		if (!hws_action)
			mlx5_core_err(dev, "Only vlan remove header supported\n");
		break;
	default:
		mlx5_core_err(ns->dev, "Packet-reformat not supported(%d)\n",
			      params->type);
		return -EOPNOTSUPP;
	}

	if (pr_pool) {
		pr_data = mlx5_fs_hws_pr_pool_acquire_pr(pr_pool);
		if (IS_ERR_OR_NULL(pr_data))
			return !pr_data ? -EINVAL : PTR_ERR(pr_data);
		hws_action = pr_data->bulk->hws_action;
		if (!hws_action) {
			mlx5_core_err(dev,
				      "Failed allocating packet-reformat action\n");
			err = -EINVAL;
			goto release_pr;
		}
		pr_data->data = kmemdup(params->data, params->size, GFP_KERNEL);
		if (!pr_data->data) {
			err = -ENOMEM;
			goto release_pr;
		}
		pr_data->hdr_idx = hdr_idx;
		pr_data->data_size = params->size;
		pkt_reformat->fs_hws_action.pr_data = pr_data;
	}

	mutex_init(&pkt_reformat->fs_hws_action.lock);
	pkt_reformat->owner = MLX5_FLOW_RESOURCE_OWNER_HWS;
	pkt_reformat->fs_hws_action.hws_action = hws_action;
	return 0;

release_pr:
	if (pr_pool && pr_data)
		mlx5_fs_hws_pr_pool_release_pr(pr_pool, pr_data);
	return err;
}

static void mlx5_cmd_hws_packet_reformat_dealloc(struct mlx5_flow_root_namespace *ns,
						 struct mlx5_pkt_reformat *pkt_reformat)
{
	struct mlx5_fs_hws_actions_pool *hws_pool = &ns->fs_hws_context.hws_pool;
	struct mlx5_core_dev *dev = ns->dev;
	struct mlx5_fs_hws_pr *pr_data;
	struct mlx5_fs_pool *pr_pool;

	if (pkt_reformat->fs_hws_action.fw_reformat_id != 0) {
		struct mlx5_pkt_reformat fw_pkt_reformat = { 0 };

		fw_pkt_reformat.id = pkt_reformat->fs_hws_action.fw_reformat_id;
		mlx5_fs_cmd_get_fw_cmds()->
			packet_reformat_dealloc(ns, &fw_pkt_reformat);
		pkt_reformat->fs_hws_action.fw_reformat_id = 0;
	}

	if (pkt_reformat->reformat_type == MLX5_REFORMAT_TYPE_REMOVE_HDR)
		return;

	if (!pkt_reformat->fs_hws_action.pr_data) {
		mlx5_core_err(ns->dev, "Failed release packet-reformat\n");
		return;
	}
	pr_data = pkt_reformat->fs_hws_action.pr_data;

	switch (pkt_reformat->reformat_type) {
	case MLX5_REFORMAT_TYPE_L2_TO_VXLAN:
	case MLX5_REFORMAT_TYPE_L2_TO_NVGRE:
	case MLX5_REFORMAT_TYPE_L2_TO_L2_TUNNEL:
		pr_pool = mlx5_fs_get_pr_encap_pool(dev, &hws_pool->el2tol2tnl_pools,
						    MLX5HWS_ACTION_TYP_REFORMAT_L2_TO_TNL_L2,
						    pr_data->data_size);
		break;
	case MLX5_REFORMAT_TYPE_L2_TO_L3_TUNNEL:
		pr_pool = mlx5_fs_get_pr_encap_pool(dev, &hws_pool->el2tol2tnl_pools,
						    MLX5HWS_ACTION_TYP_REFORMAT_L2_TO_TNL_L2,
						    pr_data->data_size);
		break;
	case MLX5_REFORMAT_TYPE_L3_TUNNEL_TO_L2:
		pr_pool = &hws_pool->dl3tnltol2_pool;
		break;
	case MLX5_REFORMAT_TYPE_INSERT_HDR:
		pr_pool = &hws_pool->insert_hdr_pool;
		break;
	default:
		mlx5_core_err(ns->dev, "Unknown packet-reformat type\n");
		return;
	}
	if (!pkt_reformat->fs_hws_action.pr_data || IS_ERR(pr_pool)) {
		mlx5_core_err(ns->dev, "Failed release packet-reformat\n");
		return;
	}
	kfree(pr_data->data);
	mlx5_fs_hws_pr_pool_release_pr(pr_pool, pr_data);
	pkt_reformat->fs_hws_action.pr_data = NULL;
}

static struct mlx5_fs_pool *
mlx5_fs_create_mh_pool(struct mlx5_core_dev *dev,
		       struct mlx5hws_action_mh_pattern *pattern,
		       struct xarray *mh_pools, unsigned long index)
{
	struct mlx5_fs_pool *pool;
	int err;

	pool = kzalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool)
		return ERR_PTR(-ENOMEM);
	err = mlx5_fs_hws_mh_pool_init(pool, dev, pattern);
	if (err)
		goto free_pool;
	err = xa_insert(mh_pools, index, pool, GFP_KERNEL);
	if (err)
		goto cleanup_pool;
	return pool;

cleanup_pool:
	mlx5_fs_hws_mh_pool_cleanup(pool);
free_pool:
	kfree(pool);
	return ERR_PTR(err);
}

static void
mlx5_fs_destroy_mh_pool(struct mlx5_fs_pool *pool, struct xarray *mh_pools,
			unsigned long index)
{
	xa_erase(mh_pools, index);
	mlx5_fs_hws_mh_pool_cleanup(pool);
	kfree(pool);
}

static int mlx5_cmd_hws_modify_header_alloc(struct mlx5_flow_root_namespace *ns,
					    u8 namespace, u8 num_actions,
					    void *modify_actions,
					    struct mlx5_modify_hdr *modify_hdr)
{
	struct mlx5_fs_hws_actions_pool *hws_pool = &ns->fs_hws_context.hws_pool;
	struct mlx5hws_action_mh_pattern pattern = {};
	struct mlx5_fs_hws_mh *mh_data = NULL;
	struct mlx5hws_action *hws_action;
	struct mlx5_fs_pool *pool;
	unsigned long i, cnt = 0;
	bool known_pattern;
	int err;

	pattern.sz = MLX5_UN_SZ_BYTES(set_add_copy_action_in_auto) * num_actions;
	pattern.data = modify_actions;

	known_pattern = false;
	xa_for_each(&hws_pool->mh_pools, i, pool) {
		if (mlx5_fs_hws_mh_pool_match(pool, &pattern)) {
			known_pattern = true;
			break;
		}
		cnt++;
	}

	if (!known_pattern) {
		pool = mlx5_fs_create_mh_pool(ns->dev, &pattern,
					      &hws_pool->mh_pools, cnt);
		if (IS_ERR(pool))
			return PTR_ERR(pool);
	}
	mh_data = mlx5_fs_hws_mh_pool_acquire_mh(pool);
	if (IS_ERR(mh_data)) {
		err = PTR_ERR(mh_data);
		goto destroy_pool;
	}
	hws_action = mh_data->bulk->hws_action;
	mh_data->data = kmemdup(pattern.data, pattern.sz, GFP_KERNEL);
	if (!mh_data->data) {
		err = -ENOMEM;
		goto release_mh;
	}
	modify_hdr->fs_hws_action.mh_data = mh_data;
	modify_hdr->fs_hws_action.fs_pool = pool;
	modify_hdr->owner = MLX5_FLOW_RESOURCE_OWNER_SW;
	modify_hdr->fs_hws_action.hws_action = hws_action;

	return 0;

release_mh:
	mlx5_fs_hws_mh_pool_release_mh(pool, mh_data);
destroy_pool:
	if (!known_pattern)
		mlx5_fs_destroy_mh_pool(pool, &hws_pool->mh_pools, cnt);
	return err;
}

static void mlx5_cmd_hws_modify_header_dealloc(struct mlx5_flow_root_namespace *ns,
					       struct mlx5_modify_hdr *modify_hdr)
{
	struct mlx5_fs_hws_mh *mh_data;
	struct mlx5_fs_pool *pool;

	if (!modify_hdr->fs_hws_action.fs_pool || !modify_hdr->fs_hws_action.mh_data) {
		mlx5_core_err(ns->dev, "Failed release modify-header\n");
		return;
	}

	mh_data = modify_hdr->fs_hws_action.mh_data;
	kfree(mh_data->data);
	pool = modify_hdr->fs_hws_action.fs_pool;
	mlx5_fs_hws_mh_pool_release_mh(pool, mh_data);
	modify_hdr->fs_hws_action.mh_data = NULL;
}

int
mlx5_fs_hws_action_get_pkt_reformat_id(struct mlx5_pkt_reformat *pkt_reformat,
				       u32 *reformat_id)
{
	enum mlx5_flow_namespace_type ns_type = pkt_reformat->ns_type;
	struct mutex *lock = &pkt_reformat->fs_hws_action.lock;
	u32 *id = &pkt_reformat->fs_hws_action.fw_reformat_id;
	struct mlx5_pkt_reformat fw_pkt_reformat = { 0 };
	struct mlx5_pkt_reformat_params params = { 0 };
	struct mlx5_flow_root_namespace *ns;
	struct mlx5_core_dev *dev;
	int ret;

	mutex_lock(lock);

	if (*id != 0) {
		*reformat_id = *id;
		ret = 0;
		goto unlock;
	}

	dev = mlx5hws_action_get_dev(pkt_reformat->fs_hws_action.hws_action);
	if (!dev) {
		ret = -EINVAL;
		goto unlock;
	}

	ns = mlx5_get_root_namespace(dev, ns_type);
	if (!ns) {
		ret = -EINVAL;
		goto unlock;
	}

	params.type = pkt_reformat->reformat_type;
	params.size = pkt_reformat->fs_hws_action.pr_data->data_size;
	params.data = pkt_reformat->fs_hws_action.pr_data->data;

	ret = mlx5_fs_cmd_get_fw_cmds()->
		packet_reformat_alloc(ns, &params, ns_type, &fw_pkt_reformat);
	if (ret)
		goto unlock;

	*id = fw_pkt_reformat.id;
	*reformat_id = *id;
	ret = 0;

unlock:
	mutex_unlock(lock);

	return ret;
}

static int mlx5_cmd_hws_create_match_definer(struct mlx5_flow_root_namespace *ns,
					     u16 format_id, u32 *match_mask)
{
	return -EOPNOTSUPP;
}

static int mlx5_cmd_hws_destroy_match_definer(struct mlx5_flow_root_namespace *ns,
					      int definer_id)
{
	return -EOPNOTSUPP;
}

static u32 mlx5_cmd_hws_get_capabilities(struct mlx5_flow_root_namespace *ns,
					 enum fs_flow_table_type ft_type)
{
	if (ft_type != FS_FT_FDB)
		return 0;

	return MLX5_FLOW_STEERING_CAP_VLAN_PUSH_ON_RX |
	       MLX5_FLOW_STEERING_CAP_VLAN_POP_ON_TX |
	       MLX5_FLOW_STEERING_CAP_MATCH_RANGES;
}

bool mlx5_fs_hws_is_supported(struct mlx5_core_dev *dev)
{
	return mlx5hws_is_supported(dev);
}

static const struct mlx5_flow_cmds mlx5_flow_cmds_hws = {
	.create_flow_table = mlx5_cmd_hws_create_flow_table,
	.destroy_flow_table = mlx5_cmd_hws_destroy_flow_table,
	.modify_flow_table = mlx5_cmd_hws_modify_flow_table,
	.update_root_ft = mlx5_cmd_hws_update_root_ft,
	.create_flow_group = mlx5_cmd_hws_create_flow_group,
	.destroy_flow_group = mlx5_cmd_hws_destroy_flow_group,
	.create_fte = mlx5_cmd_hws_create_fte,
	.delete_fte = mlx5_cmd_hws_delete_fte,
	.update_fte = mlx5_cmd_hws_update_fte,
	.packet_reformat_alloc = mlx5_cmd_hws_packet_reformat_alloc,
	.packet_reformat_dealloc = mlx5_cmd_hws_packet_reformat_dealloc,
	.modify_header_alloc = mlx5_cmd_hws_modify_header_alloc,
	.modify_header_dealloc = mlx5_cmd_hws_modify_header_dealloc,
	.create_match_definer = mlx5_cmd_hws_create_match_definer,
	.destroy_match_definer = mlx5_cmd_hws_destroy_match_definer,
	.create_ns = mlx5_cmd_hws_create_ns,
	.destroy_ns = mlx5_cmd_hws_destroy_ns,
	.set_peer = mlx5_cmd_hws_set_peer,
	.get_capabilities = mlx5_cmd_hws_get_capabilities,
};

const struct mlx5_flow_cmds *mlx5_fs_cmd_get_hws_cmds(void)
{
	return &mlx5_flow_cmds_hws;
}
