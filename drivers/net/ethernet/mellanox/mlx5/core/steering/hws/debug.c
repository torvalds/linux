// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/seq_file.h>
#include <linux/version.h>
#include "internal.h"

static int
hws_debug_dump_matcher_template_definer(struct seq_file *f,
					void *parent_obj,
					struct mlx5hws_definer *definer,
					enum mlx5hws_debug_res_type type)
{
	int i;

	if (!definer)
		return 0;

	seq_printf(f, "%d,0x%llx,0x%llx,%d,%d,",
		   type,
		   HWS_PTR_TO_ID(definer),
		   HWS_PTR_TO_ID(parent_obj),
		   definer->obj_id,
		   definer->type);

	for (i = 0; i < DW_SELECTORS; i++)
		seq_printf(f, "0x%x%s", definer->dw_selector[i],
			   (i == DW_SELECTORS - 1) ? "," : "-");

	for (i = 0; i < BYTE_SELECTORS; i++)
		seq_printf(f, "0x%x%s", definer->byte_selector[i],
			   (i == BYTE_SELECTORS - 1) ? "," : "-");

	for (i = 0; i < MLX5HWS_JUMBO_TAG_SZ; i++)
		seq_printf(f, "%02x", definer->mask.jumbo[i]);

	seq_puts(f, "\n");

	return 0;
}

static int
hws_debug_dump_matcher_match_template(struct seq_file *f, struct mlx5hws_matcher *matcher)
{
	enum mlx5hws_debug_res_type type;
	int i, ret;

	for (i = 0; i < matcher->num_of_mt; i++) {
		struct mlx5hws_match_template *mt = &matcher->mt[i];

		seq_printf(f, "%d,0x%llx,0x%llx,%d,%d,%d\n",
			   MLX5HWS_DEBUG_RES_TYPE_MATCHER_MATCH_TEMPLATE,
			   HWS_PTR_TO_ID(mt),
			   HWS_PTR_TO_ID(matcher),
			   mt->fc_sz,
			   0, 0);

		type = MLX5HWS_DEBUG_RES_TYPE_MATCHER_TEMPLATE_MATCH_DEFINER;
		ret = hws_debug_dump_matcher_template_definer(f, mt, mt->definer, type);
		if (ret)
			return ret;
	}

	return 0;
}

static int
hws_debug_dump_matcher_action_template(struct seq_file *f, struct mlx5hws_matcher *matcher)
{
	enum mlx5hws_action_type action_type;
	int i, j;

	for (i = 0; i < matcher->num_of_at; i++) {
		struct mlx5hws_action_template *at = &matcher->at[i];

		seq_printf(f, "%d,0x%llx,0x%llx,%d,%d,%d",
			   MLX5HWS_DEBUG_RES_TYPE_MATCHER_ACTION_TEMPLATE,
			   HWS_PTR_TO_ID(at),
			   HWS_PTR_TO_ID(matcher),
			   at->only_term,
			   at->num_of_action_stes,
			   at->num_actions);

		for (j = 0; j < at->num_actions; j++) {
			action_type = at->action_type_arr[j];
			seq_printf(f, ",%s", mlx5hws_action_type_to_str(action_type));
		}

		seq_puts(f, "\n");
	}

	return 0;
}

static int
hws_debug_dump_matcher_attr(struct seq_file *f, struct mlx5hws_matcher *matcher)
{
	struct mlx5hws_matcher_attr *attr = &matcher->attr;

	seq_printf(f, "%d,0x%llx,%d,%d,%d,%d,%d,%d,%d,%d\n",
		   MLX5HWS_DEBUG_RES_TYPE_MATCHER_ATTR,
		   HWS_PTR_TO_ID(matcher),
		   attr->priority,
		   attr->mode,
		   attr->table.sz_row_log,
		   attr->table.sz_col_log,
		   attr->optimize_using_rule_idx,
		   attr->optimize_flow_src,
		   attr->insert_mode,
		   attr->distribute_mode);

	return 0;
}

static int hws_debug_dump_matcher(struct seq_file *f, struct mlx5hws_matcher *matcher)
{
	enum mlx5hws_table_type tbl_type = matcher->tbl->type;
	struct mlx5hws_cmd_ft_query_attr ft_attr = {0};
	struct mlx5hws_pool_chunk *ste;
	struct mlx5hws_pool *ste_pool;
	u64 icm_addr_0 = 0;
	u64 icm_addr_1 = 0;
	u32 ste_0_id = -1;
	u32 ste_1_id = -1;
	int ret;

	seq_printf(f, "%d,0x%llx,0x%llx,%d,%d,0x%llx",
		   MLX5HWS_DEBUG_RES_TYPE_MATCHER,
		   HWS_PTR_TO_ID(matcher),
		   HWS_PTR_TO_ID(matcher->tbl),
		   matcher->num_of_mt,
		   matcher->end_ft_id,
		   matcher->col_matcher ? HWS_PTR_TO_ID(matcher->col_matcher) : 0);

	ste = &matcher->match_ste.ste;
	ste_pool = matcher->match_ste.pool;
	if (ste_pool) {
		ste_0_id = mlx5hws_pool_chunk_get_base_id(ste_pool, ste);
		if (tbl_type == MLX5HWS_TABLE_TYPE_FDB)
			ste_1_id = mlx5hws_pool_chunk_get_base_mirror_id(ste_pool, ste);
	}

	seq_printf(f, ",%d,%d,%d,%d",
		   matcher->match_ste.rtc_0_id,
		   (int)ste_0_id,
		   matcher->match_ste.rtc_1_id,
		   (int)ste_1_id);

	ste = &matcher->action_ste[0].ste;
	ste_pool = matcher->action_ste[0].pool;
	if (ste_pool) {
		ste_0_id = mlx5hws_pool_chunk_get_base_id(ste_pool, ste);
		if (tbl_type == MLX5HWS_TABLE_TYPE_FDB)
			ste_1_id = mlx5hws_pool_chunk_get_base_mirror_id(ste_pool, ste);
		else
			ste_1_id = -1;
	} else {
		ste_0_id = -1;
		ste_1_id = -1;
	}

	ft_attr.type = matcher->tbl->fw_ft_type;
	ret = mlx5hws_cmd_flow_table_query(matcher->tbl->ctx->mdev,
					   matcher->end_ft_id,
					   &ft_attr,
					   &icm_addr_0,
					   &icm_addr_1);
	if (ret)
		return ret;

	seq_printf(f, ",%d,%d,%d,%d,%d,0x%llx,0x%llx\n",
		   matcher->action_ste[0].rtc_0_id,
		   (int)ste_0_id,
		   matcher->action_ste[0].rtc_1_id,
		   (int)ste_1_id,
		   0,
		   mlx5hws_debug_icm_to_idx(icm_addr_0),
		   mlx5hws_debug_icm_to_idx(icm_addr_1));

	ret = hws_debug_dump_matcher_attr(f, matcher);
	if (ret)
		return ret;

	ret = hws_debug_dump_matcher_match_template(f, matcher);
	if (ret)
		return ret;

	ret = hws_debug_dump_matcher_action_template(f, matcher);
	if (ret)
		return ret;

	return 0;
}

static int hws_debug_dump_table(struct seq_file *f, struct mlx5hws_table *tbl)
{
	struct mlx5hws_cmd_ft_query_attr ft_attr = {0};
	struct mlx5hws_matcher *matcher;
	u64 local_icm_addr_0 = 0;
	u64 local_icm_addr_1 = 0;
	u64 icm_addr_0 = 0;
	u64 icm_addr_1 = 0;
	int ret;

	seq_printf(f, "%d,0x%llx,0x%llx,%d,%d,%d,%d,%d",
		   MLX5HWS_DEBUG_RES_TYPE_TABLE,
		   HWS_PTR_TO_ID(tbl),
		   HWS_PTR_TO_ID(tbl->ctx),
		   tbl->ft_id,
		   MLX5HWS_TABLE_TYPE_BASE + tbl->type,
		   tbl->fw_ft_type,
		   tbl->level,
		   0);

	ft_attr.type = tbl->fw_ft_type;
	ret = mlx5hws_cmd_flow_table_query(tbl->ctx->mdev,
					   tbl->ft_id,
					   &ft_attr,
					   &icm_addr_0,
					   &icm_addr_1);
	if (ret)
		return ret;

	seq_printf(f, ",0x%llx,0x%llx,0x%llx,0x%llx,0x%llx\n",
		   mlx5hws_debug_icm_to_idx(icm_addr_0),
		   mlx5hws_debug_icm_to_idx(icm_addr_1),
		   mlx5hws_debug_icm_to_idx(local_icm_addr_0),
		   mlx5hws_debug_icm_to_idx(local_icm_addr_1),
		   HWS_PTR_TO_ID(tbl->default_miss.miss_tbl));

	list_for_each_entry(matcher, &tbl->matchers_list, list_node) {
		ret = hws_debug_dump_matcher(f, matcher);
		if (ret)
			return ret;
	}

	return 0;
}

static int
hws_debug_dump_context_send_engine(struct seq_file *f, struct mlx5hws_context *ctx)
{
	struct mlx5hws_send_engine *send_queue;
	struct mlx5hws_send_ring *send_ring;
	struct mlx5hws_send_ring_cq *cq;
	struct mlx5hws_send_ring_sq *sq;
	int i;

	for (i = 0; i < (int)ctx->queues; i++) {
		send_queue = &ctx->send_queue[i];
		seq_printf(f, "%d,0x%llx,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
			   MLX5HWS_DEBUG_RES_TYPE_CONTEXT_SEND_ENGINE,
			   HWS_PTR_TO_ID(ctx),
			   i,
			   send_queue->used_entries,
			   send_queue->num_entries,
			   1, /* one send ring per queue */
			   send_queue->num_entries,
			   send_queue->err,
			   send_queue->completed.ci,
			   send_queue->completed.pi,
			   send_queue->completed.mask);

		send_ring = &send_queue->send_ring;
		cq = &send_ring->send_cq;
		sq = &send_ring->send_sq;

		seq_printf(f, "%d,0x%llx,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
			   MLX5HWS_DEBUG_RES_TYPE_CONTEXT_SEND_RING,
			   HWS_PTR_TO_ID(ctx),
			   0, /* one send ring per send queue */
			   i,
			   cq->mcq.cqn,
			   0,
			   0,
			   0,
			   0,
			   0,
			   0,
			   cq->mcq.cqe_sz,
			   sq->sqn,
			   0,
			   0,
			   0);
	}

	return 0;
}

static int hws_debug_dump_context_caps(struct seq_file *f, struct mlx5hws_context *ctx)
{
	struct mlx5hws_cmd_query_caps *caps = ctx->caps;

	seq_printf(f, "%d,0x%llx,%s,%d,%d,%d,%d,",
		   MLX5HWS_DEBUG_RES_TYPE_CONTEXT_CAPS,
		   HWS_PTR_TO_ID(ctx),
		   caps->fw_ver,
		   caps->wqe_based_update,
		   caps->ste_format,
		   caps->ste_alloc_log_max,
		   caps->log_header_modify_argument_max_alloc);

	seq_printf(f, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%s\n",
		   caps->flex_protocols,
		   caps->rtc_reparse_mode,
		   caps->rtc_index_mode,
		   caps->ste_alloc_log_gran,
		   caps->stc_alloc_log_max,
		   caps->stc_alloc_log_gran,
		   caps->rtc_log_depth_max,
		   caps->format_select_gtpu_dw_0,
		   caps->format_select_gtpu_dw_1,
		   caps->format_select_gtpu_dw_2,
		   caps->format_select_gtpu_ext_dw_0,
		   caps->nic_ft.max_level,
		   caps->nic_ft.reparse,
		   caps->fdb_ft.max_level,
		   caps->fdb_ft.reparse,
		   caps->log_header_modify_argument_granularity,
		   caps->linear_match_definer,
		   "regc_3");

	return 0;
}

static int hws_debug_dump_context_attr(struct seq_file *f, struct mlx5hws_context *ctx)
{
	seq_printf(f, "%u,0x%llx,%d,%zu,%d,%s,%d,%d\n",
		   MLX5HWS_DEBUG_RES_TYPE_CONTEXT_ATTR,
		   HWS_PTR_TO_ID(ctx),
		   ctx->pd_num,
		   ctx->queues,
		   ctx->send_queue->num_entries,
		   "None", /* no shared gvmi */
		   ctx->caps->vhca_id,
		   0xffff); /* no shared gvmi */

	return 0;
}

static int hws_debug_dump_context_info(struct seq_file *f, struct mlx5hws_context *ctx)
{
	struct mlx5_core_dev *dev = ctx->mdev;
	int ret;

	seq_printf(f, "%d,0x%llx,%d,%s,%s.KERNEL_%u_%u_%u\n",
		   MLX5HWS_DEBUG_RES_TYPE_CONTEXT,
		   HWS_PTR_TO_ID(ctx),
		   ctx->flags & MLX5HWS_CONTEXT_FLAG_HWS_SUPPORT,
		   pci_name(dev->pdev),
		   HWS_DEBUG_FORMAT_VERSION,
		   LINUX_VERSION_MAJOR,
		   LINUX_VERSION_PATCHLEVEL,
		   LINUX_VERSION_SUBLEVEL);

	ret = hws_debug_dump_context_attr(f, ctx);
	if (ret)
		return ret;

	ret = hws_debug_dump_context_caps(f, ctx);
	if (ret)
		return ret;

	return 0;
}

static int hws_debug_dump_context_stc_resource(struct seq_file *f,
					       struct mlx5hws_context *ctx,
					       u32 tbl_type,
					       struct mlx5hws_pool_resource *resource)
{
	seq_printf(f, "%d,0x%llx,%u,%u\n",
		   MLX5HWS_DEBUG_RES_TYPE_CONTEXT_STC,
		   HWS_PTR_TO_ID(ctx),
		   tbl_type,
		   resource->base_id);

	return 0;
}

static int hws_debug_dump_context_stc(struct seq_file *f, struct mlx5hws_context *ctx)
{
	struct mlx5hws_pool *stc_pool;
	u32 table_type;
	int ret;
	int i;

	for (i = 0; i < MLX5HWS_TABLE_TYPE_MAX; i++) {
		stc_pool = ctx->stc_pool[i];
		table_type = MLX5HWS_TABLE_TYPE_BASE + i;

		if (!stc_pool)
			continue;

		if (stc_pool->resource[0]) {
			ret = hws_debug_dump_context_stc_resource(f, ctx, table_type,
								  stc_pool->resource[0]);
			if (ret)
				return ret;
		}

		if (i == MLX5HWS_TABLE_TYPE_FDB && stc_pool->mirror_resource[0]) {
			ret = hws_debug_dump_context_stc_resource(f, ctx, table_type,
								  stc_pool->mirror_resource[0]);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int hws_debug_dump_context(struct seq_file *f, struct mlx5hws_context *ctx)
{
	struct mlx5hws_table *tbl;
	int ret;

	ret = hws_debug_dump_context_info(f, ctx);
	if (ret)
		return ret;

	ret = hws_debug_dump_context_send_engine(f, ctx);
	if (ret)
		return ret;

	ret = hws_debug_dump_context_stc(f, ctx);
	if (ret)
		return ret;

	list_for_each_entry(tbl, &ctx->tbl_list, tbl_list_node) {
		ret = hws_debug_dump_table(f, tbl);
		if (ret)
			return ret;
	}

	return 0;
}

static int
hws_debug_dump(struct seq_file *f, struct mlx5hws_context *ctx)
{
	int ret;

	if (!f || !ctx)
		return -EINVAL;

	mutex_lock(&ctx->ctrl_lock);
	ret = hws_debug_dump_context(f, ctx);
	mutex_unlock(&ctx->ctrl_lock);

	return ret;
}

static int hws_dump_show(struct seq_file *file, void *priv)
{
	return hws_debug_dump(file, file->private);
}
DEFINE_SHOW_ATTRIBUTE(hws_dump);

void mlx5hws_debug_init_dump(struct mlx5hws_context *ctx)
{
	struct mlx5_core_dev *dev = ctx->mdev;
	char file_name[128];

	ctx->debug_info.steering_debugfs =
		debugfs_create_dir("steering", mlx5_debugfs_get_dev_root(dev));
	ctx->debug_info.fdb_debugfs =
		debugfs_create_dir("fdb", ctx->debug_info.steering_debugfs);

	sprintf(file_name, "ctx_%p", ctx);
	debugfs_create_file(file_name, 0444, ctx->debug_info.fdb_debugfs,
			    ctx, &hws_dump_fops);
}

void mlx5hws_debug_uninit_dump(struct mlx5hws_context *ctx)
{
	debugfs_remove_recursive(ctx->debug_info.steering_debugfs);
}
