// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2025 NVIDIA Corporation & Affiliates */

#include "internal.h"

static const char *
hws_pool_opt_to_str(enum mlx5hws_pool_optimize opt)
{
	switch (opt) {
	case MLX5HWS_POOL_OPTIMIZE_NONE:
		return "rx-and-tx";
	case MLX5HWS_POOL_OPTIMIZE_ORIG:
		return "rx-only";
	case MLX5HWS_POOL_OPTIMIZE_MIRROR:
		return "tx-only";
	default:
		return "unknown";
	}
}

static int
hws_action_ste_table_create_pool(struct mlx5hws_context *ctx,
				 struct mlx5hws_action_ste_table *action_tbl,
				 enum mlx5hws_pool_optimize opt, size_t log_sz)
{
	struct mlx5hws_pool_attr pool_attr = { 0 };

	pool_attr.pool_type = MLX5HWS_POOL_TYPE_STE;
	pool_attr.table_type = MLX5HWS_TABLE_TYPE_FDB;
	pool_attr.flags = MLX5HWS_POOL_FLAG_BUDDY;
	pool_attr.opt_type = opt;
	pool_attr.alloc_log_sz = log_sz;

	action_tbl->pool = mlx5hws_pool_create(ctx, &pool_attr);
	if (!action_tbl->pool) {
		mlx5hws_err(ctx, "Failed to allocate STE pool\n");
		return -EINVAL;
	}

	return 0;
}

static int hws_action_ste_table_create_single_rtc(
	struct mlx5hws_context *ctx,
	struct mlx5hws_action_ste_table *action_tbl,
	enum mlx5hws_pool_optimize opt, size_t log_sz, bool tx)
{
	struct mlx5hws_cmd_rtc_create_attr rtc_attr = { 0 };
	u32 *rtc_id;

	rtc_attr.log_depth = 0;
	rtc_attr.update_index_mode = MLX5_IFC_RTC_STE_UPDATE_MODE_BY_OFFSET;
	/* Action STEs use the default always hit definer. */
	rtc_attr.match_definer_0 = ctx->caps->trivial_match_definer;
	rtc_attr.is_frst_jumbo = false;
	rtc_attr.miss_ft_id = 0;
	rtc_attr.pd = ctx->pd_num;
	rtc_attr.reparse_mode = mlx5hws_context_get_reparse_mode(ctx);

	if (tx) {
		rtc_attr.table_type = FS_FT_FDB_TX;
		rtc_attr.ste_base =
			mlx5hws_pool_get_base_mirror_id(action_tbl->pool);
		rtc_attr.stc_base =
			mlx5hws_pool_get_base_mirror_id(ctx->stc_pool);
		rtc_attr.log_size =
			opt == MLX5HWS_POOL_OPTIMIZE_ORIG ? 0 : log_sz;
		rtc_id = &action_tbl->rtc_1_id;
	} else {
		rtc_attr.table_type = FS_FT_FDB_RX;
		rtc_attr.ste_base = mlx5hws_pool_get_base_id(action_tbl->pool);
		rtc_attr.stc_base = mlx5hws_pool_get_base_id(ctx->stc_pool);
		rtc_attr.log_size =
			opt == MLX5HWS_POOL_OPTIMIZE_MIRROR ? 0 : log_sz;
		rtc_id = &action_tbl->rtc_0_id;
	}

	return mlx5hws_cmd_rtc_create(ctx->mdev, &rtc_attr, rtc_id);
}

static int
hws_action_ste_table_create_rtcs(struct mlx5hws_context *ctx,
				 struct mlx5hws_action_ste_table *action_tbl,
				 enum mlx5hws_pool_optimize opt, size_t log_sz)
{
	int err;

	err = hws_action_ste_table_create_single_rtc(ctx, action_tbl, opt,
						     log_sz, false);
	if (err)
		return err;

	err = hws_action_ste_table_create_single_rtc(ctx, action_tbl, opt,
						     log_sz, true);
	if (err) {
		mlx5hws_cmd_rtc_destroy(ctx->mdev, action_tbl->rtc_0_id);
		return err;
	}

	return 0;
}

static void
hws_action_ste_table_destroy_rtcs(struct mlx5hws_action_ste_table *action_tbl)
{
	mlx5hws_cmd_rtc_destroy(action_tbl->pool->ctx->mdev,
				action_tbl->rtc_1_id);
	mlx5hws_cmd_rtc_destroy(action_tbl->pool->ctx->mdev,
				action_tbl->rtc_0_id);
}

static int
hws_action_ste_table_create_stc(struct mlx5hws_context *ctx,
				struct mlx5hws_action_ste_table *action_tbl)
{
	struct mlx5hws_cmd_stc_modify_attr stc_attr = { 0 };

	stc_attr.action_offset = MLX5HWS_ACTION_OFFSET_HIT;
	stc_attr.action_type = MLX5_IFC_STC_ACTION_TYPE_JUMP_TO_STE_TABLE;
	stc_attr.reparse_mode = MLX5_IFC_STC_REPARSE_IGNORE;
	stc_attr.ste_table.ste_pool = action_tbl->pool;
	stc_attr.ste_table.match_definer_id = ctx->caps->trivial_match_definer;

	return mlx5hws_action_alloc_single_stc(ctx, &stc_attr,
					       MLX5HWS_TABLE_TYPE_FDB,
					       &action_tbl->stc);
}

static struct mlx5hws_action_ste_table *
hws_action_ste_table_alloc(struct mlx5hws_action_ste_pool_element *parent_elem)
{
	enum mlx5hws_pool_optimize opt = parent_elem->opt;
	struct mlx5hws_context *ctx = parent_elem->ctx;
	struct mlx5hws_action_ste_table *action_tbl;
	size_t log_sz;
	int err;

	log_sz = min(parent_elem->log_sz ?
			     parent_elem->log_sz +
				     MLX5HWS_ACTION_STE_TABLE_STEP_LOG_SZ :
				   MLX5HWS_ACTION_STE_TABLE_INIT_LOG_SZ,
		     MLX5HWS_ACTION_STE_TABLE_MAX_LOG_SZ);

	action_tbl = kzalloc(sizeof(*action_tbl), GFP_KERNEL);
	if (!action_tbl)
		return ERR_PTR(-ENOMEM);

	err = hws_action_ste_table_create_pool(ctx, action_tbl, opt, log_sz);
	if (err)
		goto free_tbl;

	err = hws_action_ste_table_create_rtcs(ctx, action_tbl, opt, log_sz);
	if (err)
		goto destroy_pool;

	err = hws_action_ste_table_create_stc(ctx, action_tbl);
	if (err)
		goto destroy_rtcs;

	action_tbl->parent_elem = parent_elem;
	INIT_LIST_HEAD(&action_tbl->list_node);
	action_tbl->last_used = jiffies;
	list_add(&action_tbl->list_node, &parent_elem->available);
	parent_elem->log_sz = log_sz;

	mlx5hws_dbg(ctx,
		    "Allocated %s action STE table log_sz %zu; STEs (%d, %d); RTCs (%d, %d); STC %d\n",
		    hws_pool_opt_to_str(opt), log_sz,
		    mlx5hws_pool_get_base_id(action_tbl->pool),
		    mlx5hws_pool_get_base_mirror_id(action_tbl->pool),
		    action_tbl->rtc_0_id, action_tbl->rtc_1_id,
		    action_tbl->stc.offset);

	return action_tbl;

destroy_rtcs:
	hws_action_ste_table_destroy_rtcs(action_tbl);
destroy_pool:
	mlx5hws_pool_destroy(action_tbl->pool);
free_tbl:
	kfree(action_tbl);

	return ERR_PTR(err);
}

static void
hws_action_ste_table_destroy(struct mlx5hws_action_ste_table *action_tbl)
{
	struct mlx5hws_context *ctx = action_tbl->parent_elem->ctx;

	mlx5hws_dbg(ctx,
		    "Destroying %s action STE table: STEs (%d, %d); RTCs (%d, %d); STC %d\n",
		    hws_pool_opt_to_str(action_tbl->parent_elem->opt),
		    mlx5hws_pool_get_base_id(action_tbl->pool),
		    mlx5hws_pool_get_base_mirror_id(action_tbl->pool),
		    action_tbl->rtc_0_id, action_tbl->rtc_1_id,
		    action_tbl->stc.offset);

	mlx5hws_action_free_single_stc(ctx, MLX5HWS_TABLE_TYPE_FDB,
				       &action_tbl->stc);
	hws_action_ste_table_destroy_rtcs(action_tbl);
	mlx5hws_pool_destroy(action_tbl->pool);

	list_del(&action_tbl->list_node);
	kfree(action_tbl);
}

static int
hws_action_ste_pool_element_init(struct mlx5hws_context *ctx,
				 struct mlx5hws_action_ste_pool_element *elem,
				 enum mlx5hws_pool_optimize opt)
{
	elem->ctx = ctx;
	elem->opt = opt;
	INIT_LIST_HEAD(&elem->available);
	INIT_LIST_HEAD(&elem->full);

	return 0;
}

static void hws_action_ste_pool_element_destroy(
	struct mlx5hws_action_ste_pool_element *elem)
{
	struct mlx5hws_action_ste_table *action_tbl, *p;

	/* This should be empty, but attempt to free its elements anyway. */
	list_for_each_entry_safe(action_tbl, p, &elem->full, list_node)
		hws_action_ste_table_destroy(action_tbl);

	list_for_each_entry_safe(action_tbl, p, &elem->available, list_node)
		hws_action_ste_table_destroy(action_tbl);
}

static int hws_action_ste_pool_init(struct mlx5hws_context *ctx,
				    struct mlx5hws_action_ste_pool *pool)
{
	enum mlx5hws_pool_optimize opt;
	int err;

	mutex_init(&pool->lock);

	/* Rules which are added for both RX and TX must use the same action STE
	 * indices for both. If we were to use a single table, then RX-only and
	 * TX-only rules would waste the unused entries. Thus, we use separate
	 * table sets for the three cases.
	 */
	for (opt = MLX5HWS_POOL_OPTIMIZE_NONE; opt < MLX5HWS_POOL_OPTIMIZE_MAX;
	     opt++) {
		err = hws_action_ste_pool_element_init(ctx, &pool->elems[opt],
						       opt);
		if (err)
			goto destroy_elems;
		pool->elems[opt].parent_pool = pool;
	}

	return 0;

destroy_elems:
	while (opt-- > MLX5HWS_POOL_OPTIMIZE_NONE)
		hws_action_ste_pool_element_destroy(&pool->elems[opt]);

	return err;
}

static void hws_action_ste_pool_destroy(struct mlx5hws_action_ste_pool *pool)
{
	int opt;

	for (opt = MLX5HWS_POOL_OPTIMIZE_MAX - 1;
	     opt >= MLX5HWS_POOL_OPTIMIZE_NONE; opt--)
		hws_action_ste_pool_element_destroy(&pool->elems[opt]);
}

static void hws_action_ste_pool_element_collect_stale(
	struct mlx5hws_action_ste_pool_element *elem, struct list_head *cleanup)
{
	struct mlx5hws_action_ste_table *action_tbl, *p;
	unsigned long expire_time, now;

	expire_time = secs_to_jiffies(MLX5HWS_ACTION_STE_POOL_EXPIRE_SECONDS);
	now = jiffies;

	list_for_each_entry_safe(action_tbl, p, &elem->available, list_node) {
		if (mlx5hws_pool_full(action_tbl->pool) &&
		    time_before(action_tbl->last_used + expire_time, now))
			list_move(&action_tbl->list_node, cleanup);
	}
}

static void hws_action_ste_table_cleanup_list(struct list_head *cleanup)
{
	struct mlx5hws_action_ste_table *action_tbl, *p;

	list_for_each_entry_safe(action_tbl, p, cleanup, list_node)
		hws_action_ste_table_destroy(action_tbl);
}

static void hws_action_ste_pool_cleanup(struct work_struct *work)
{
	enum mlx5hws_pool_optimize opt;
	struct mlx5hws_context *ctx;
	LIST_HEAD(cleanup);
	int i;

	ctx = container_of(work, struct mlx5hws_context,
			   action_ste_cleanup.work);

	for (i = 0; i < ctx->queues; i++) {
		struct mlx5hws_action_ste_pool *p = &ctx->action_ste_pool[i];

		mutex_lock(&p->lock);
		for (opt = MLX5HWS_POOL_OPTIMIZE_NONE;
		     opt < MLX5HWS_POOL_OPTIMIZE_MAX; opt++)
			hws_action_ste_pool_element_collect_stale(
				&p->elems[opt], &cleanup);
		mutex_unlock(&p->lock);
	}

	hws_action_ste_table_cleanup_list(&cleanup);

	schedule_delayed_work(&ctx->action_ste_cleanup,
			      secs_to_jiffies(
				  MLX5HWS_ACTION_STE_POOL_CLEANUP_SECONDS));
}

int mlx5hws_action_ste_pool_init(struct mlx5hws_context *ctx)
{
	struct mlx5hws_action_ste_pool *pool;
	size_t queues = ctx->queues;
	int i, err;

	pool = kcalloc(queues, sizeof(*pool), GFP_KERNEL);
	if (!pool)
		return -ENOMEM;

	for (i = 0; i < queues; i++) {
		err = hws_action_ste_pool_init(ctx, &pool[i]);
		if (err)
			goto free_pool;
	}

	ctx->action_ste_pool = pool;

	INIT_DELAYED_WORK(&ctx->action_ste_cleanup,
			  hws_action_ste_pool_cleanup);
	schedule_delayed_work(
		&ctx->action_ste_cleanup,
		secs_to_jiffies(MLX5HWS_ACTION_STE_POOL_CLEANUP_SECONDS));

	return 0;

free_pool:
	while (i--)
		hws_action_ste_pool_destroy(&pool[i]);
	kfree(pool);

	return err;
}

void mlx5hws_action_ste_pool_uninit(struct mlx5hws_context *ctx)
{
	size_t queues = ctx->queues;
	int i;

	cancel_delayed_work_sync(&ctx->action_ste_cleanup);

	for (i = 0; i < queues; i++)
		hws_action_ste_pool_destroy(&ctx->action_ste_pool[i]);

	kfree(ctx->action_ste_pool);
}

static struct mlx5hws_action_ste_pool_element *
hws_action_ste_choose_elem(struct mlx5hws_action_ste_pool *pool,
			   bool skip_rx, bool skip_tx)
{
	if (skip_rx)
		return &pool->elems[MLX5HWS_POOL_OPTIMIZE_MIRROR];

	if (skip_tx)
		return &pool->elems[MLX5HWS_POOL_OPTIMIZE_ORIG];

	return &pool->elems[MLX5HWS_POOL_OPTIMIZE_NONE];
}

static int
hws_action_ste_table_chunk_alloc(struct mlx5hws_action_ste_table *action_tbl,
				 struct mlx5hws_action_ste_chunk *chunk)
{
	int err;

	err = mlx5hws_pool_chunk_alloc(action_tbl->pool, &chunk->ste);
	if (err)
		return err;

	chunk->action_tbl = action_tbl;
	action_tbl->last_used = jiffies;

	return 0;
}

int mlx5hws_action_ste_chunk_alloc(struct mlx5hws_action_ste_pool *pool,
				   bool skip_rx, bool skip_tx,
				   struct mlx5hws_action_ste_chunk *chunk)
{
	struct mlx5hws_action_ste_pool_element *elem;
	struct mlx5hws_action_ste_table *action_tbl;
	bool found;
	int err;

	if (skip_rx && skip_tx)
		return -EINVAL;

	mutex_lock(&pool->lock);

	elem = hws_action_ste_choose_elem(pool, skip_rx, skip_tx);

	mlx5hws_dbg(elem->ctx,
		    "Allocating action STEs skip_rx %d skip_tx %d order %d\n",
		    skip_rx, skip_tx, chunk->ste.order);

	found = false;
	list_for_each_entry(action_tbl, &elem->available, list_node) {
		if (!hws_action_ste_table_chunk_alloc(action_tbl, chunk)) {
			found = true;
			break;
		}
	}

	if (!found) {
		action_tbl = hws_action_ste_table_alloc(elem);
		if (IS_ERR(action_tbl)) {
			err = PTR_ERR(action_tbl);
			goto out;
		}

		err = hws_action_ste_table_chunk_alloc(action_tbl, chunk);
		if (err)
			goto out;
	}

	if (mlx5hws_pool_empty(action_tbl->pool))
		list_move(&action_tbl->list_node, &elem->full);

	err = 0;

out:
	mutex_unlock(&pool->lock);

	return err;
}

void mlx5hws_action_ste_chunk_free(struct mlx5hws_action_ste_chunk *chunk)
{
	struct mutex *lock = &chunk->action_tbl->parent_elem->parent_pool->lock;

	mlx5hws_dbg(chunk->action_tbl->pool->ctx,
		    "Freeing action STEs offset %d order %d\n",
		    chunk->ste.offset, chunk->ste.order);

	mutex_lock(lock);
	mlx5hws_pool_chunk_free(chunk->action_tbl->pool, &chunk->ste);
	chunk->action_tbl->last_used = jiffies;
	list_move(&chunk->action_tbl->list_node,
		  &chunk->action_tbl->parent_elem->available);
	mutex_unlock(lock);
}
