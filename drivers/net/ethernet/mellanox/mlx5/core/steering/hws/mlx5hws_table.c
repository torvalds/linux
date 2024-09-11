// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#include "mlx5hws_internal.h"

u32 mlx5hws_table_get_id(struct mlx5hws_table *tbl)
{
	return tbl->ft_id;
}

static void hws_table_init_next_ft_attr(struct mlx5hws_table *tbl,
					struct mlx5hws_cmd_ft_create_attr *ft_attr)
{
	ft_attr->type = tbl->fw_ft_type;
	if (tbl->type == MLX5HWS_TABLE_TYPE_FDB)
		ft_attr->level = tbl->ctx->caps->fdb_ft.max_level - 1;
	else
		ft_attr->level = tbl->ctx->caps->nic_ft.max_level - 1;
	ft_attr->rtc_valid = true;
}

static void hws_table_set_cap_attr(struct mlx5hws_table *tbl,
				   struct mlx5hws_cmd_ft_create_attr *ft_attr)
{
	/* Enabling reformat_en or decap_en for the first flow table
	 * must be done when all VFs are down.
	 * However, HWS doesn't know when it is required to create the first FT.
	 * On the other hand, HWS doesn't use all these FT capabilities at all
	 * (the API doesn't even provide a way to specify these flags), so we'll
	 * just set these caps on all the flow tables.
	 * If HCA_CAP.fdb_dynamic_tunnel is set, this constraint is N/A.
	 */
	if (!MLX5_CAP_ESW_FLOWTABLE(tbl->ctx->mdev, fdb_dynamic_tunnel)) {
		ft_attr->reformat_en = true;
		ft_attr->decap_en = true;
	}
}

static int hws_table_up_default_fdb_miss_tbl(struct mlx5hws_table *tbl)
{
	struct mlx5hws_cmd_ft_create_attr ft_attr = {0};
	struct mlx5hws_cmd_set_fte_attr fte_attr = {0};
	struct mlx5hws_cmd_forward_tbl *default_miss;
	struct mlx5hws_cmd_set_fte_dest dest = {0};
	struct mlx5hws_context *ctx = tbl->ctx;
	u8 tbl_type = tbl->type;

	if (tbl->type != MLX5HWS_TABLE_TYPE_FDB)
		return 0;

	if (ctx->common_res[tbl_type].default_miss) {
		ctx->common_res[tbl_type].default_miss->refcount++;
		return 0;
	}

	ft_attr.type = tbl->fw_ft_type;
	ft_attr.level = tbl->ctx->caps->fdb_ft.max_level; /* The last level */
	ft_attr.rtc_valid = false;

	dest.destination_type = MLX5_FLOW_DESTINATION_TYPE_VPORT;
	dest.destination_id = ctx->caps->eswitch_manager_vport_number;

	fte_attr.action_flags = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	fte_attr.dests_num = 1;
	fte_attr.dests = &dest;

	default_miss = mlx5hws_cmd_forward_tbl_create(ctx->mdev, &ft_attr, &fte_attr);
	if (!default_miss) {
		mlx5hws_err(ctx, "Failed to default miss table type: 0x%x\n", tbl_type);
		return -EINVAL;
	}

	/* ctx->ctrl_lock must be held here */
	ctx->common_res[tbl_type].default_miss = default_miss;
	ctx->common_res[tbl_type].default_miss->refcount++;

	return 0;
}

/* Called under ctx->ctrl_lock */
static void hws_table_down_default_fdb_miss_tbl(struct mlx5hws_table *tbl)
{
	struct mlx5hws_cmd_forward_tbl *default_miss;
	struct mlx5hws_context *ctx = tbl->ctx;
	u8 tbl_type = tbl->type;

	if (tbl->type != MLX5HWS_TABLE_TYPE_FDB)
		return;

	default_miss = ctx->common_res[tbl_type].default_miss;
	if (--default_miss->refcount)
		return;

	mlx5hws_cmd_forward_tbl_destroy(ctx->mdev, default_miss);
	ctx->common_res[tbl_type].default_miss = NULL;
}

static int hws_table_connect_to_default_miss_tbl(struct mlx5hws_table *tbl, u32 ft_id)
{
	struct mlx5hws_cmd_ft_modify_attr ft_attr = {0};
	int ret;

	if (unlikely(tbl->type != MLX5HWS_TABLE_TYPE_FDB))
		pr_warn("HWS: invalid table type %d\n", tbl->type);

	mlx5hws_cmd_set_attr_connect_miss_tbl(tbl->ctx,
					      tbl->fw_ft_type,
					      tbl->type,
					      &ft_attr);

	ret = mlx5hws_cmd_flow_table_modify(tbl->ctx->mdev, &ft_attr, ft_id);
	if (ret) {
		mlx5hws_err(tbl->ctx, "Failed to connect FT to default FDB FT\n");
		return ret;
	}

	return 0;
}

int mlx5hws_table_create_default_ft(struct mlx5_core_dev *mdev,
				    struct mlx5hws_table *tbl,
				    u32 *ft_id)
{
	struct mlx5hws_cmd_ft_create_attr ft_attr = {0};
	int ret;

	hws_table_init_next_ft_attr(tbl, &ft_attr);
	hws_table_set_cap_attr(tbl, &ft_attr);

	ret = mlx5hws_cmd_flow_table_create(mdev, &ft_attr, ft_id);
	if (ret) {
		mlx5hws_err(tbl->ctx, "Failed creating default ft\n");
		return ret;
	}

	if (tbl->type == MLX5HWS_TABLE_TYPE_FDB) {
		/* Take/create ref over the default miss */
		ret = hws_table_up_default_fdb_miss_tbl(tbl);
		if (ret) {
			mlx5hws_err(tbl->ctx, "Failed to get default fdb miss\n");
			goto free_ft_obj;
		}
		ret = hws_table_connect_to_default_miss_tbl(tbl, *ft_id);
		if (ret) {
			mlx5hws_err(tbl->ctx, "Failed connecting to default miss tbl\n");
			goto down_miss_tbl;
		}
	}

	return 0;

down_miss_tbl:
	hws_table_down_default_fdb_miss_tbl(tbl);
free_ft_obj:
	mlx5hws_cmd_flow_table_destroy(mdev, ft_attr.type, *ft_id);
	return ret;
}

void mlx5hws_table_destroy_default_ft(struct mlx5hws_table *tbl,
				      u32 ft_id)
{
	mlx5hws_cmd_flow_table_destroy(tbl->ctx->mdev, tbl->fw_ft_type, ft_id);
	hws_table_down_default_fdb_miss_tbl(tbl);
}

static int hws_table_init_check_hws_support(struct mlx5hws_context *ctx,
					    struct mlx5hws_table *tbl)
{
	if (!(ctx->flags & MLX5HWS_CONTEXT_FLAG_HWS_SUPPORT)) {
		mlx5hws_err(ctx, "HWS not supported, cannot create mlx5hws_table\n");
		return -EOPNOTSUPP;
	}

	return 0;
}

static int hws_table_init(struct mlx5hws_table *tbl)
{
	struct mlx5hws_context *ctx = tbl->ctx;
	int ret;

	ret = hws_table_init_check_hws_support(ctx, tbl);
	if (ret)
		return ret;

	if (mlx5hws_table_get_fw_ft_type(tbl->type, (u8 *)&tbl->fw_ft_type)) {
		pr_warn("HWS: invalid table type %d\n", tbl->type);
		return -EOPNOTSUPP;
	}

	mutex_lock(&ctx->ctrl_lock);
	ret = mlx5hws_table_create_default_ft(tbl->ctx->mdev, tbl, &tbl->ft_id);
	if (ret) {
		mlx5hws_err(tbl->ctx, "Failed to create flow table object\n");
		mutex_unlock(&ctx->ctrl_lock);
		return ret;
	}

	ret = mlx5hws_action_get_default_stc(ctx, tbl->type);
	if (ret)
		goto tbl_destroy;

	INIT_LIST_HEAD(&tbl->matchers_list);
	INIT_LIST_HEAD(&tbl->default_miss.head);

	mutex_unlock(&ctx->ctrl_lock);

	return 0;

tbl_destroy:
	mlx5hws_table_destroy_default_ft(tbl, tbl->ft_id);
	mutex_unlock(&ctx->ctrl_lock);
	return ret;
}

static void hws_table_uninit(struct mlx5hws_table *tbl)
{
	mutex_lock(&tbl->ctx->ctrl_lock);
	mlx5hws_action_put_default_stc(tbl->ctx, tbl->type);
	mlx5hws_table_destroy_default_ft(tbl, tbl->ft_id);
	mutex_unlock(&tbl->ctx->ctrl_lock);
}

struct mlx5hws_table *mlx5hws_table_create(struct mlx5hws_context *ctx,
					   struct mlx5hws_table_attr *attr)
{
	struct mlx5hws_table *tbl;
	int ret;

	if (attr->type > MLX5HWS_TABLE_TYPE_FDB) {
		mlx5hws_err(ctx, "Invalid table type %d\n", attr->type);
		return NULL;
	}

	tbl = kzalloc(sizeof(*tbl), GFP_KERNEL);
	if (!tbl)
		return NULL;

	tbl->ctx = ctx;
	tbl->type = attr->type;
	tbl->level = attr->level;

	ret = hws_table_init(tbl);
	if (ret) {
		mlx5hws_err(ctx, "Failed to initialise table\n");
		goto free_tbl;
	}

	mutex_lock(&ctx->ctrl_lock);
	list_add(&tbl->tbl_list_node, &ctx->tbl_list);
	mutex_unlock(&ctx->ctrl_lock);

	return tbl;

free_tbl:
	kfree(tbl);
	return NULL;
}

int mlx5hws_table_destroy(struct mlx5hws_table *tbl)
{
	struct mlx5hws_context *ctx = tbl->ctx;
	int ret;

	mutex_lock(&ctx->ctrl_lock);
	if (!list_empty(&tbl->matchers_list)) {
		mlx5hws_err(tbl->ctx, "Cannot destroy table containing matchers\n");
		ret = -EBUSY;
		goto unlock_err;
	}

	if (!list_empty(&tbl->default_miss.head)) {
		mlx5hws_err(tbl->ctx, "Cannot destroy table pointed by default miss\n");
		ret = -EBUSY;
		goto unlock_err;
	}

	list_del_init(&tbl->tbl_list_node);
	mutex_unlock(&ctx->ctrl_lock);

	hws_table_uninit(tbl);
	kfree(tbl);

	return 0;

unlock_err:
	mutex_unlock(&ctx->ctrl_lock);
	return ret;
}

static u32 hws_table_get_last_ft(struct mlx5hws_table *tbl)
{
	struct mlx5hws_matcher *matcher;

	if (list_empty(&tbl->matchers_list))
		return tbl->ft_id;

	matcher = list_last_entry(&tbl->matchers_list, struct mlx5hws_matcher, list_node);
	return matcher->end_ft_id;
}

int mlx5hws_table_ft_set_default_next_ft(struct mlx5hws_table *tbl, u32 ft_id)
{
	struct mlx5hws_cmd_ft_modify_attr ft_attr = {0};
	int ret;

	/* Due to FW limitation, resetting the flow table to default action will
	 * disconnect RTC when ignore_flow_level_rtc_valid is not supported.
	 */
	if (!tbl->ctx->caps->nic_ft.ignore_flow_level_rtc_valid)
		return 0;

	if (tbl->type == MLX5HWS_TABLE_TYPE_FDB)
		return hws_table_connect_to_default_miss_tbl(tbl, ft_id);

	ft_attr.type = tbl->fw_ft_type;
	ft_attr.modify_fs = MLX5_IFC_MODIFY_FLOW_TABLE_MISS_ACTION;
	ft_attr.table_miss_action = MLX5_IFC_MODIFY_FLOW_TABLE_MISS_ACTION_DEFAULT;

	ret = mlx5hws_cmd_flow_table_modify(tbl->ctx->mdev, &ft_attr, ft_id);
	if (ret) {
		mlx5hws_err(tbl->ctx, "Failed to set FT default miss action\n");
		return ret;
	}

	return 0;
}

int mlx5hws_table_ft_set_next_rtc(struct mlx5hws_context *ctx,
				  u32 ft_id,
				  u32 fw_ft_type,
				  u32 rtc_0_id,
				  u32 rtc_1_id)
{
	struct mlx5hws_cmd_ft_modify_attr ft_attr = {0};

	ft_attr.modify_fs = MLX5_IFC_MODIFY_FLOW_TABLE_RTC_ID;
	ft_attr.type = fw_ft_type;
	ft_attr.rtc_id_0 = rtc_0_id;
	ft_attr.rtc_id_1 = rtc_1_id;

	return mlx5hws_cmd_flow_table_modify(ctx->mdev, &ft_attr, ft_id);
}

static int hws_table_ft_set_next_ft(struct mlx5hws_context *ctx,
				    u32 ft_id,
				    u32 fw_ft_type,
				    u32 next_ft_id)
{
	struct mlx5hws_cmd_ft_modify_attr ft_attr = {0};

	ft_attr.modify_fs = MLX5_IFC_MODIFY_FLOW_TABLE_MISS_ACTION;
	ft_attr.table_miss_action = MLX5_IFC_MODIFY_FLOW_TABLE_MISS_ACTION_GOTO_TBL;
	ft_attr.type = fw_ft_type;
	ft_attr.table_miss_id = next_ft_id;

	return mlx5hws_cmd_flow_table_modify(ctx->mdev, &ft_attr, ft_id);
}

int mlx5hws_table_update_connected_miss_tables(struct mlx5hws_table *dst_tbl)
{
	struct mlx5hws_table *src_tbl;
	int ret;

	if (list_empty(&dst_tbl->default_miss.head))
		return 0;

	list_for_each_entry(src_tbl, &dst_tbl->default_miss.head, default_miss.next) {
		ret = mlx5hws_table_connect_to_miss_table(src_tbl, dst_tbl);
		if (ret) {
			mlx5hws_err(dst_tbl->ctx,
				    "Failed to update source miss table, unexpected behavior\n");
			return ret;
		}
	}

	return 0;
}

int mlx5hws_table_connect_to_miss_table(struct mlx5hws_table *src_tbl,
					struct mlx5hws_table *dst_tbl)
{
	struct mlx5hws_matcher *matcher;
	u32 last_ft_id;
	int ret;

	last_ft_id = hws_table_get_last_ft(src_tbl);

	if (dst_tbl) {
		if (list_empty(&dst_tbl->matchers_list)) {
			/* Connect src_tbl last_ft to dst_tbl start anchor */
			ret = hws_table_ft_set_next_ft(src_tbl->ctx,
						       last_ft_id,
						       src_tbl->fw_ft_type,
						       dst_tbl->ft_id);
			if (ret)
				return ret;

			/* Reset last_ft RTC to default RTC */
			ret = mlx5hws_table_ft_set_next_rtc(src_tbl->ctx,
							    last_ft_id,
							    src_tbl->fw_ft_type,
							    0, 0);
			if (ret)
				return ret;
		} else {
			/* Connect src_tbl last_ft to first matcher RTC */
			matcher = list_first_entry(&dst_tbl->matchers_list,
						   struct mlx5hws_matcher,
						   list_node);
			ret = mlx5hws_table_ft_set_next_rtc(src_tbl->ctx,
							    last_ft_id,
							    src_tbl->fw_ft_type,
							    matcher->match_ste.rtc_0_id,
							    matcher->match_ste.rtc_1_id);
			if (ret)
				return ret;

			/* Reset next miss FT to default */
			ret = mlx5hws_table_ft_set_default_next_ft(src_tbl, last_ft_id);
			if (ret)
				return ret;
		}
	} else {
		/* Reset next miss FT to default */
		ret = mlx5hws_table_ft_set_default_next_ft(src_tbl, last_ft_id);
		if (ret)
			return ret;

		/* Reset last_ft RTC to default RTC */
		ret = mlx5hws_table_ft_set_next_rtc(src_tbl->ctx,
						    last_ft_id,
						    src_tbl->fw_ft_type,
						    0, 0);
		if (ret)
			return ret;
	}

	src_tbl->default_miss.miss_tbl = dst_tbl;

	return 0;
}

static int hws_table_set_default_miss_not_valid(struct mlx5hws_table *tbl,
						struct mlx5hws_table *miss_tbl)
{
	if (!tbl->ctx->caps->nic_ft.ignore_flow_level_rtc_valid) {
		mlx5hws_err(tbl->ctx, "Default miss table is not supported\n");
		return -EOPNOTSUPP;
	}

	if ((miss_tbl && miss_tbl->type != tbl->type)) {
		mlx5hws_err(tbl->ctx, "Invalid arguments\n");
		return -EINVAL;
	}

	return 0;
}

int mlx5hws_table_set_default_miss(struct mlx5hws_table *tbl,
				   struct mlx5hws_table *miss_tbl)
{
	struct mlx5hws_context *ctx = tbl->ctx;
	struct mlx5hws_table *old_miss_tbl;
	int ret;

	ret = hws_table_set_default_miss_not_valid(tbl, miss_tbl);
	if (ret)
		return ret;

	mutex_lock(&ctx->ctrl_lock);

	old_miss_tbl = tbl->default_miss.miss_tbl;
	ret = mlx5hws_table_connect_to_miss_table(tbl, miss_tbl);
	if (ret)
		goto out;

	if (old_miss_tbl)
		list_del_init(&tbl->default_miss.next);

	old_miss_tbl = tbl->default_miss.miss_tbl;
	if (old_miss_tbl)
		list_del_init(&old_miss_tbl->default_miss.head);

	if (miss_tbl)
		list_add(&tbl->default_miss.next, &miss_tbl->default_miss.head);

	mutex_unlock(&ctx->ctrl_lock);
	return 0;
out:
	mutex_unlock(&ctx->ctrl_lock);
	return ret;
}
