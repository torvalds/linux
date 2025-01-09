// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2025 NVIDIA Corporation & Affiliates */

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
	struct mlx5_fs_pool *pool;
	unsigned long i;

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

static int mlx5_cmd_hws_create_flow_table(struct mlx5_flow_root_namespace *ns,
					  struct mlx5_flow_table *ft,
					  struct mlx5_flow_table_attr *ft_attr,
					  struct mlx5_flow_table *next_ft)
{
	struct mlx5hws_context *ctx = ns->fs_hws_context.hws_ctx;
	struct mlx5hws_table_attr tbl_attr = {};
	struct mlx5hws_table *tbl;
	int err;

	if (mlx5_fs_cmd_is_fw_term_table(ft))
		return mlx5_fs_cmd_get_fw_cmds()->create_flow_table(ns, ft, ft_attr,
								    next_ft);

	if (ns->table_type != FS_FT_FDB) {
		mlx5_core_err(ns->dev, "Table type %d not supported for HWS\n",
			      ns->table_type);
		return -EOPNOTSUPP;
	}

	tbl_attr.type = MLX5HWS_TABLE_TYPE_FDB;
	tbl_attr.level = ft_attr->level;
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

	return 0;

destroy_table:
	mlx5hws_table_destroy(tbl);
	ft->fs_hws_table.hws_table = NULL;
	return err;
}

static int mlx5_cmd_hws_destroy_flow_table(struct mlx5_flow_root_namespace *ns,
					   struct mlx5_flow_table *ft)
{
	int err;

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

	pkt_reformat->owner = MLX5_FLOW_RESOURCE_OWNER_SW;
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

static const struct mlx5_flow_cmds mlx5_flow_cmds_hws = {
	.create_flow_table = mlx5_cmd_hws_create_flow_table,
	.destroy_flow_table = mlx5_cmd_hws_destroy_flow_table,
	.modify_flow_table = mlx5_cmd_hws_modify_flow_table,
	.update_root_ft = mlx5_cmd_hws_update_root_ft,
	.create_flow_group = mlx5_cmd_hws_create_flow_group,
	.destroy_flow_group = mlx5_cmd_hws_destroy_flow_group,
	.packet_reformat_alloc = mlx5_cmd_hws_packet_reformat_alloc,
	.packet_reformat_dealloc = mlx5_cmd_hws_packet_reformat_dealloc,
	.modify_header_alloc = mlx5_cmd_hws_modify_header_alloc,
	.modify_header_dealloc = mlx5_cmd_hws_modify_header_dealloc,
	.create_ns = mlx5_cmd_hws_create_ns,
	.destroy_ns = mlx5_cmd_hws_destroy_ns,
	.set_peer = mlx5_cmd_hws_set_peer,
};

const struct mlx5_flow_cmds *mlx5_fs_cmd_get_hws_cmds(void)
{
	return &mlx5_flow_cmds_hws;
}
