// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#include "internal.h"

static u16 hws_bwc_gen_queue_idx(struct mlx5hws_context *ctx)
{
	/* assign random queue */
	return get_random_u8() % mlx5hws_bwc_queues(ctx);
}

static u16
hws_bwc_get_burst_th(struct mlx5hws_context *ctx, u16 queue_id)
{
	return min(ctx->send_queue[queue_id].num_entries / 2,
		   MLX5HWS_BWC_MATCHER_REHASH_BURST_TH);
}

static struct mutex *
hws_bwc_get_queue_lock(struct mlx5hws_context *ctx, u16 idx)
{
	return &ctx->bwc_send_queue_locks[idx];
}

static void hws_bwc_lock_all_queues(struct mlx5hws_context *ctx)
{
	u16 bwc_queues = mlx5hws_bwc_queues(ctx);
	struct mutex *queue_lock; /* Protect the queue */
	int i;

	for (i = 0; i < bwc_queues; i++) {
		queue_lock = hws_bwc_get_queue_lock(ctx, i);
		mutex_lock(queue_lock);
	}
}

static void hws_bwc_unlock_all_queues(struct mlx5hws_context *ctx)
{
	u16 bwc_queues = mlx5hws_bwc_queues(ctx);
	struct mutex *queue_lock; /* Protect the queue */
	int i = bwc_queues;

	while (i--) {
		queue_lock = hws_bwc_get_queue_lock(ctx, i);
		mutex_unlock(queue_lock);
	}
}

static void hws_bwc_matcher_init_attr(struct mlx5hws_bwc_matcher *bwc_matcher,
				      u32 priority,
				      u8 size_log_rx, u8 size_log_tx,
				      struct mlx5hws_matcher_attr *attr)
{
	memset(attr, 0, sizeof(*attr));

	attr->priority = priority;
	attr->optimize_using_rule_idx = 0;
	attr->mode = MLX5HWS_MATCHER_RESOURCE_MODE_RULE;
	attr->optimize_flow_src = MLX5HWS_MATCHER_FLOW_SRC_ANY;
	attr->insert_mode = MLX5HWS_MATCHER_INSERT_BY_HASH;
	attr->distribute_mode = MLX5HWS_MATCHER_DISTRIBUTE_BY_HASH;
	attr->size[MLX5HWS_MATCHER_SIZE_TYPE_RX].rule.num_log = size_log_rx;
	attr->size[MLX5HWS_MATCHER_SIZE_TYPE_TX].rule.num_log = size_log_tx;
	attr->resizable = true;
	attr->max_num_of_at_attach = MLX5HWS_BWC_MATCHER_ATTACH_AT_NUM;
}

static int
hws_bwc_matcher_move_all_simple(struct mlx5hws_bwc_matcher *bwc_matcher)
{
	struct mlx5hws_context *ctx = bwc_matcher->matcher->tbl->ctx;
	struct mlx5hws_matcher *matcher = bwc_matcher->matcher;
	int drain_error = 0, move_error = 0, poll_error = 0;
	u16 bwc_queues = mlx5hws_bwc_queues(ctx);
	struct mlx5hws_rule_attr rule_attr;
	struct mlx5hws_bwc_rule *bwc_rule;
	struct mlx5hws_send_engine *queue;
	struct list_head *rules_list;
	u32 pending_rules;
	int i, ret = 0;
	bool drain;

	mlx5hws_bwc_rule_fill_attr(bwc_matcher, 0, 0, &rule_attr);

	for (i = 0; i < bwc_queues; i++) {
		if (list_empty(&bwc_matcher->rules[i]))
			continue;

		pending_rules = 0;
		rule_attr.queue_id = mlx5hws_bwc_get_queue_id(ctx, i);
		rules_list = &bwc_matcher->rules[i];

		list_for_each_entry(bwc_rule, rules_list, list_node) {
			ret = mlx5hws_matcher_resize_rule_move(matcher,
							       bwc_rule->rule,
							       &rule_attr);
			if (unlikely(ret)) {
				if (!move_error) {
					mlx5hws_err(ctx,
						    "Moving BWC rule: move failed (%d), attempting to move rest of the rules\n",
						    ret);
					move_error = ret;
				}
				/* Rule wasn't queued, no need to poll */
				continue;
			}

			pending_rules++;
			drain = pending_rules >=
				hws_bwc_get_burst_th(ctx, rule_attr.queue_id);
			ret = mlx5hws_bwc_queue_poll(ctx,
						     rule_attr.queue_id,
						     &pending_rules,
						     drain);
			if (unlikely(ret)) {
				if (ret == -ETIMEDOUT) {
					mlx5hws_err(ctx,
						    "Moving BWC rule: timeout polling for completions (%d), aborting rehash\n",
						    ret);
					return ret;
				}
				if (!poll_error) {
					mlx5hws_err(ctx,
						    "Moving BWC rule: polling for completions failed (%d), attempting to move rest of the rules\n",
						    ret);
					poll_error = ret;
				}
			}
		}

		if (pending_rules) {
			queue = &ctx->send_queue[rule_attr.queue_id];
			mlx5hws_send_engine_flush_queue(queue);
			ret = mlx5hws_bwc_queue_poll(ctx,
						     rule_attr.queue_id,
						     &pending_rules,
						     true);
			if (unlikely(ret)) {
				if (ret == -ETIMEDOUT) {
					mlx5hws_err(ctx,
						    "Moving bwc rule: timeout draining completions (%d), aborting rehash\n",
						    ret);
					return ret;
				}
				if (!drain_error) {
					mlx5hws_err(ctx,
						    "Moving bwc rule: drain failed (%d), attempting to move rest of the rules\n",
						    ret);
					drain_error = ret;
				}
			}
		}
	}

	/* Return the first error that happened */
	if (unlikely(move_error))
		return move_error;
	if (unlikely(poll_error))
		return poll_error;
	if (unlikely(drain_error))
		return drain_error;

	return ret;
}

static int hws_bwc_matcher_move_all(struct mlx5hws_bwc_matcher *bwc_matcher)
{
	switch (bwc_matcher->matcher_type) {
	case MLX5HWS_BWC_MATCHER_SIMPLE:
		return hws_bwc_matcher_move_all_simple(bwc_matcher);
	case MLX5HWS_BWC_MATCHER_COMPLEX_FIRST:
		return mlx5hws_bwc_matcher_complex_move_first(bwc_matcher);
	case MLX5HWS_BWC_MATCHER_COMPLEX_SUBMATCHER:
		return mlx5hws_bwc_matcher_complex_move(bwc_matcher);
	default:
		return -EINVAL;
	}
}

static int hws_bwc_matcher_move(struct mlx5hws_bwc_matcher *bwc_matcher)
{
	struct mlx5hws_context *ctx = bwc_matcher->matcher->tbl->ctx;
	struct mlx5hws_matcher_attr matcher_attr = {0};
	struct mlx5hws_matcher *old_matcher;
	struct mlx5hws_matcher *new_matcher;
	int ret;

	hws_bwc_matcher_init_attr(bwc_matcher,
				  bwc_matcher->priority,
				  bwc_matcher->rx_size.size_log,
				  bwc_matcher->tx_size.size_log,
				  &matcher_attr);

	old_matcher = bwc_matcher->matcher;
	new_matcher = mlx5hws_matcher_create(old_matcher->tbl,
					     &bwc_matcher->mt, 1,
					     bwc_matcher->at,
					     bwc_matcher->num_of_at,
					     &matcher_attr);
	if (!new_matcher) {
		mlx5hws_err(ctx, "Rehash error: matcher creation failed\n");
		return -ENOMEM;
	}

	ret = mlx5hws_matcher_resize_set_target(old_matcher, new_matcher);
	if (ret) {
		mlx5hws_err(ctx, "Rehash error: failed setting resize target\n");
		return ret;
	}

	ret = hws_bwc_matcher_move_all(bwc_matcher);
	if (ret)
		mlx5hws_err(ctx, "Rehash error: moving rules failed, attempting to remove the old matcher\n");

	/* Error during rehash can't be rolled back.
	 * The best option here is to allow the rehash to complete and remove
	 * the old matcher - can't leave the matcher in the 'in_resize' state.
	 */

	bwc_matcher->matcher = new_matcher;
	mlx5hws_matcher_destroy(old_matcher);

	return ret;
}

int mlx5hws_bwc_matcher_create_simple(struct mlx5hws_bwc_matcher *bwc_matcher,
				      struct mlx5hws_table *table,
				      u32 priority,
				      u8 match_criteria_enable,
				      struct mlx5hws_match_parameters *mask,
				      enum mlx5hws_action_type action_types[])
{
	enum mlx5hws_action_type init_action_types[1] = { MLX5HWS_ACTION_TYP_LAST };
	struct mlx5hws_context *ctx = table->ctx;
	u16 bwc_queues = mlx5hws_bwc_queues(ctx);
	struct mlx5hws_matcher_attr attr = {0};
	int i;

	bwc_matcher->rules = kcalloc(bwc_queues, sizeof(*bwc_matcher->rules), GFP_KERNEL);
	if (!bwc_matcher->rules)
		goto err;

	for (i = 0; i < bwc_queues; i++)
		INIT_LIST_HEAD(&bwc_matcher->rules[i]);

	hws_bwc_matcher_init_attr(bwc_matcher,
				  priority,
				  bwc_matcher->rx_size.size_log,
				  bwc_matcher->tx_size.size_log,
				  &attr);

	bwc_matcher->matcher_type = MLX5HWS_BWC_MATCHER_SIMPLE;
	bwc_matcher->priority = priority;

	bwc_matcher->size_of_at_array = MLX5HWS_BWC_MATCHER_ATTACH_AT_NUM;
	bwc_matcher->at = kcalloc(bwc_matcher->size_of_at_array,
				  sizeof(*bwc_matcher->at), GFP_KERNEL);
	if (!bwc_matcher->at)
		goto free_bwc_matcher_rules;

	/* create dummy action template */
	bwc_matcher->at[0] =
		mlx5hws_action_template_create(action_types ?
					       action_types : init_action_types);
	if (!bwc_matcher->at[0]) {
		mlx5hws_err(table->ctx, "BWC matcher: failed creating action template\n");
		goto free_bwc_matcher_at_array;
	}

	bwc_matcher->num_of_at = 1;

	bwc_matcher->mt = mlx5hws_match_template_create(ctx,
							mask->match_buf,
							mask->match_sz,
							match_criteria_enable);
	if (!bwc_matcher->mt) {
		mlx5hws_err(table->ctx, "BWC matcher: failed creating match template\n");
		goto free_at;
	}

	bwc_matcher->matcher = mlx5hws_matcher_create(table,
						      &bwc_matcher->mt, 1,
						      &bwc_matcher->at[0],
						      bwc_matcher->num_of_at,
						      &attr);
	if (!bwc_matcher->matcher) {
		mlx5hws_err(table->ctx, "BWC matcher: failed creating HWS matcher\n");
		goto free_mt;
	}

	return 0;

free_mt:
	mlx5hws_match_template_destroy(bwc_matcher->mt);
free_at:
	mlx5hws_action_template_destroy(bwc_matcher->at[0]);
free_bwc_matcher_at_array:
	kfree(bwc_matcher->at);
free_bwc_matcher_rules:
	kfree(bwc_matcher->rules);
err:
	return -EINVAL;
}

static void
hws_bwc_matcher_init_size_rxtx(struct mlx5hws_bwc_matcher_size *size)
{
	size->size_log = MLX5HWS_BWC_MATCHER_INIT_SIZE_LOG;
	atomic_set(&size->num_of_rules, 0);
	atomic_set(&size->rehash_required, false);
}

static void hws_bwc_matcher_init_size(struct mlx5hws_bwc_matcher *bwc_matcher)
{
	hws_bwc_matcher_init_size_rxtx(&bwc_matcher->rx_size);
	hws_bwc_matcher_init_size_rxtx(&bwc_matcher->tx_size);
}

struct mlx5hws_bwc_matcher *
mlx5hws_bwc_matcher_create(struct mlx5hws_table *table,
			   u32 priority,
			   u8 match_criteria_enable,
			   struct mlx5hws_match_parameters *mask)
{
	struct mlx5hws_bwc_matcher *bwc_matcher;
	bool is_complex;
	int ret;

	if (!mlx5hws_context_bwc_supported(table->ctx)) {
		mlx5hws_err(table->ctx,
			    "BWC matcher: context created w/o BWC API compatibility\n");
		return NULL;
	}

	bwc_matcher = kzalloc(sizeof(*bwc_matcher), GFP_KERNEL);
	if (!bwc_matcher)
		return NULL;

	hws_bwc_matcher_init_size(bwc_matcher);

	/* Check if the required match params can be all matched
	 * in single STE, otherwise complex matcher is needed.
	 */

	is_complex = mlx5hws_bwc_match_params_is_complex(table->ctx, match_criteria_enable, mask);
	if (is_complex)
		ret = mlx5hws_bwc_matcher_create_complex(bwc_matcher,
							 table,
							 priority,
							 match_criteria_enable,
							 mask);
	else
		ret = mlx5hws_bwc_matcher_create_simple(bwc_matcher,
							table,
							priority,
							match_criteria_enable,
							mask,
							NULL);
	if (ret)
		goto free_bwc_matcher;

	return bwc_matcher;

free_bwc_matcher:
	kfree(bwc_matcher);

	return NULL;
}

int mlx5hws_bwc_matcher_destroy_simple(struct mlx5hws_bwc_matcher *bwc_matcher)
{
	int i;

	mlx5hws_matcher_destroy(bwc_matcher->matcher);
	bwc_matcher->matcher = NULL;

	for (i = 0; i < bwc_matcher->num_of_at; i++)
		mlx5hws_action_template_destroy(bwc_matcher->at[i]);
	kfree(bwc_matcher->at);

	mlx5hws_match_template_destroy(bwc_matcher->mt);
	kfree(bwc_matcher->rules);

	return 0;
}

int mlx5hws_bwc_matcher_destroy(struct mlx5hws_bwc_matcher *bwc_matcher)
{
	u32 rx_rules = atomic_read(&bwc_matcher->rx_size.num_of_rules);
	u32 tx_rules = atomic_read(&bwc_matcher->tx_size.num_of_rules);

	if (rx_rules || tx_rules)
		mlx5hws_err(bwc_matcher->matcher->tbl->ctx,
			    "BWC matcher destroy: matcher still has %u RX and %u TX rules\n",
			    rx_rules, tx_rules);

	if (bwc_matcher->matcher_type == MLX5HWS_BWC_MATCHER_COMPLEX_FIRST)
		mlx5hws_bwc_matcher_destroy_complex(bwc_matcher);
	else
		mlx5hws_bwc_matcher_destroy_simple(bwc_matcher);

	kfree(bwc_matcher);
	return 0;
}

int mlx5hws_bwc_queue_poll(struct mlx5hws_context *ctx,
			   u16 queue_id,
			   u32 *pending_rules,
			   bool drain)
{
	unsigned long timeout = jiffies +
				secs_to_jiffies(MLX5HWS_BWC_POLLING_TIMEOUT);
	struct mlx5hws_flow_op_result comp[MLX5HWS_BWC_MATCHER_REHASH_BURST_TH];
	u16 burst_th = hws_bwc_get_burst_th(ctx, queue_id);
	bool got_comp = *pending_rules >= burst_th;
	bool queue_full;
	int err = 0;
	int ret;
	int i;

	/* Check if there are any completions at all */
	if (!got_comp && !drain)
		return 0;

	queue_full = mlx5hws_send_engine_full(&ctx->send_queue[queue_id]);
	while (queue_full || ((got_comp || drain) && *pending_rules)) {
		ret = mlx5hws_send_queue_poll(ctx, queue_id, comp, burst_th);
		if (unlikely(ret < 0)) {
			mlx5hws_err(ctx, "BWC poll error: polling queue %d returned %d\n",
				    queue_id, ret);
			return -EINVAL;
		}

		if (ret) {
			(*pending_rules) -= ret;
			for (i = 0; i < ret; i++) {
				if (unlikely(comp[i].status != MLX5HWS_FLOW_OP_SUCCESS)) {
					mlx5hws_err(ctx,
						    "BWC poll error: polling queue %d returned completion with error\n",
						    queue_id);
					err = -EINVAL;
				}
			}
			queue_full = false;
		}

		got_comp = !!ret;

		if (unlikely(!got_comp && time_after(jiffies, timeout))) {
			mlx5hws_err(ctx, "BWC poll error: polling queue %d - TIMEOUT\n", queue_id);
			return -ETIMEDOUT;
		}
	}

	return err;
}

void
mlx5hws_bwc_rule_fill_attr(struct mlx5hws_bwc_matcher *bwc_matcher,
			   u16 bwc_queue_idx,
			   u32 flow_source,
			   struct mlx5hws_rule_attr *rule_attr)
{
	struct mlx5hws_context *ctx = bwc_matcher->matcher->tbl->ctx;

	/* no use of INSERT_BY_INDEX in bwc rule */
	rule_attr->rule_idx = 0;

	/* notify HW at each rule insertion/deletion */
	rule_attr->burst = 0;

	/* We don't need user data, but the API requires it to exist */
	rule_attr->user_data = (void *)0xFACADE;

	rule_attr->queue_id = mlx5hws_bwc_get_queue_id(ctx, bwc_queue_idx);
	rule_attr->flow_source = flow_source;
}

struct mlx5hws_bwc_rule *
mlx5hws_bwc_rule_alloc(struct mlx5hws_bwc_matcher *bwc_matcher)
{
	struct mlx5hws_bwc_rule *bwc_rule;

	bwc_rule = kzalloc(sizeof(*bwc_rule), GFP_KERNEL);
	if (unlikely(!bwc_rule))
		goto out_err;

	bwc_rule->rule = kzalloc(sizeof(*bwc_rule->rule), GFP_KERNEL);
	if (unlikely(!bwc_rule->rule))
		goto free_rule;

	bwc_rule->bwc_matcher = bwc_matcher;
	return bwc_rule;

free_rule:
	kfree(bwc_rule);
out_err:
	return NULL;
}

void mlx5hws_bwc_rule_free(struct mlx5hws_bwc_rule *bwc_rule)
{
	if (likely(bwc_rule->rule))
		kfree(bwc_rule->rule);
	kfree(bwc_rule);
}

static void hws_bwc_rule_list_add(struct mlx5hws_bwc_rule *bwc_rule, u16 idx)
{
	struct mlx5hws_bwc_matcher *bwc_matcher = bwc_rule->bwc_matcher;

	bwc_rule->bwc_queue_idx = idx;
	list_add(&bwc_rule->list_node, &bwc_matcher->rules[idx]);
}

static void hws_bwc_rule_list_remove(struct mlx5hws_bwc_rule *bwc_rule)
{
	list_del_init(&bwc_rule->list_node);
}

static int
hws_bwc_rule_destroy_hws_async(struct mlx5hws_bwc_rule *bwc_rule,
			       struct mlx5hws_rule_attr *attr)
{
	return mlx5hws_rule_destroy(bwc_rule->rule, attr);
}

static int
hws_bwc_rule_destroy_hws_sync(struct mlx5hws_bwc_rule *bwc_rule,
			      struct mlx5hws_rule_attr *rule_attr)
{
	struct mlx5hws_context *ctx = bwc_rule->bwc_matcher->matcher->tbl->ctx;
	u32 expected_completions = 1;
	int ret;

	ret = hws_bwc_rule_destroy_hws_async(bwc_rule, rule_attr);
	if (unlikely(ret))
		return ret;

	ret = mlx5hws_bwc_queue_poll(ctx, rule_attr->queue_id,
				     &expected_completions, true);
	if (unlikely(ret))
		return ret;

	if (unlikely(bwc_rule->rule->status != MLX5HWS_RULE_STATUS_DELETED &&
		     bwc_rule->rule->status != MLX5HWS_RULE_STATUS_DELETING)) {
		mlx5hws_err(ctx, "Failed destroying BWC rule: rule status %d\n",
			    bwc_rule->rule->status);
		return -EINVAL;
	}

	return 0;
}

static void hws_bwc_rule_cnt_dec(struct mlx5hws_bwc_rule *bwc_rule)
{
	struct mlx5hws_bwc_matcher *bwc_matcher = bwc_rule->bwc_matcher;

	if (!bwc_rule->skip_rx)
		atomic_dec(&bwc_matcher->rx_size.num_of_rules);
	if (!bwc_rule->skip_tx)
		atomic_dec(&bwc_matcher->tx_size.num_of_rules);
}

static int
hws_bwc_matcher_rehash_shrink(struct mlx5hws_bwc_matcher *bwc_matcher)
{
	struct mlx5hws_bwc_matcher_size *rx_size = &bwc_matcher->rx_size;
	struct mlx5hws_bwc_matcher_size *tx_size = &bwc_matcher->tx_size;

	/* It is possible that another thread has added a rule.
	 * Need to check again if we really need rehash/shrink.
	 */
	if (atomic_read(&rx_size->num_of_rules) ||
	    atomic_read(&tx_size->num_of_rules))
		return 0;

	/* If the current matcher RX/TX size is already at its initial size. */
	if (rx_size->size_log == MLX5HWS_BWC_MATCHER_INIT_SIZE_LOG &&
	    tx_size->size_log == MLX5HWS_BWC_MATCHER_INIT_SIZE_LOG)
		return 0;

	/* Now we've done all the checking - do the shrinking:
	 *  - reset match RTC size to the initial size
	 *  - create new matcher
	 *  - move the rules, which will not do anything as the matcher is empty
	 *  - destroy the old matcher
	 */

	rx_size->size_log = MLX5HWS_BWC_MATCHER_INIT_SIZE_LOG;
	tx_size->size_log = MLX5HWS_BWC_MATCHER_INIT_SIZE_LOG;

	return hws_bwc_matcher_move(bwc_matcher);
}

static int hws_bwc_rule_cnt_dec_with_shrink(struct mlx5hws_bwc_rule *bwc_rule,
					    u16 bwc_queue_idx)
{
	struct mlx5hws_bwc_matcher *bwc_matcher = bwc_rule->bwc_matcher;
	struct mlx5hws_context *ctx = bwc_matcher->matcher->tbl->ctx;
	struct mutex *queue_lock; /* Protect the queue */
	int ret;

	hws_bwc_rule_cnt_dec(bwc_rule);

	if (atomic_read(&bwc_matcher->rx_size.num_of_rules) ||
	    atomic_read(&bwc_matcher->tx_size.num_of_rules))
		return 0;

	/* Matcher has no more rules - shrink it to save ICM. */

	queue_lock = hws_bwc_get_queue_lock(ctx, bwc_queue_idx);
	mutex_unlock(queue_lock);

	hws_bwc_lock_all_queues(ctx);
	ret = hws_bwc_matcher_rehash_shrink(bwc_matcher);
	hws_bwc_unlock_all_queues(ctx);

	mutex_lock(queue_lock);

	if (unlikely(ret))
		mlx5hws_err(ctx,
			    "BWC rule deletion: shrinking empty matcher failed (%d)\n",
			    ret);

	return ret;
}

int mlx5hws_bwc_rule_destroy_simple(struct mlx5hws_bwc_rule *bwc_rule)
{
	struct mlx5hws_bwc_matcher *bwc_matcher = bwc_rule->bwc_matcher;
	struct mlx5hws_context *ctx = bwc_matcher->matcher->tbl->ctx;
	u16 idx = bwc_rule->bwc_queue_idx;
	struct mlx5hws_rule_attr attr;
	struct mutex *queue_lock; /* Protect the queue */
	int ret;

	mlx5hws_bwc_rule_fill_attr(bwc_matcher, idx, 0, &attr);

	queue_lock = hws_bwc_get_queue_lock(ctx, idx);

	mutex_lock(queue_lock);

	ret = hws_bwc_rule_destroy_hws_sync(bwc_rule, &attr);
	hws_bwc_rule_list_remove(bwc_rule);
	hws_bwc_rule_cnt_dec_with_shrink(bwc_rule, idx);

	mutex_unlock(queue_lock);

	return ret;
}

int mlx5hws_bwc_rule_destroy(struct mlx5hws_bwc_rule *bwc_rule)
{
	bool is_complex = bwc_rule->bwc_matcher->matcher_type ==
			  MLX5HWS_BWC_MATCHER_COMPLEX_FIRST;
	int ret = 0;

	if (is_complex)
		ret = mlx5hws_bwc_rule_destroy_complex(bwc_rule);
	else
		ret = mlx5hws_bwc_rule_destroy_simple(bwc_rule);

	mlx5hws_bwc_rule_free(bwc_rule);
	return ret;
}

static int
hws_bwc_rule_create_async(struct mlx5hws_bwc_rule *bwc_rule,
			  u32 *match_param,
			  u8 at_idx,
			  struct mlx5hws_rule_action rule_actions[],
			  struct mlx5hws_rule_attr *rule_attr)
{
	return mlx5hws_rule_create(bwc_rule->bwc_matcher->matcher,
				   0, /* only one match template supported */
				   match_param,
				   at_idx,
				   rule_actions,
				   rule_attr,
				   bwc_rule->rule);
}

static int
hws_bwc_rule_create_sync(struct mlx5hws_bwc_rule *bwc_rule,
			 u32 *match_param,
			 u8 at_idx,
			 struct mlx5hws_rule_action rule_actions[],
			 struct mlx5hws_rule_attr *rule_attr)

{
	struct mlx5hws_context *ctx = bwc_rule->bwc_matcher->matcher->tbl->ctx;
	u32 expected_completions = 1;
	int ret;

	ret = hws_bwc_rule_create_async(bwc_rule, match_param,
					at_idx, rule_actions,
					rule_attr);
	if (unlikely(ret))
		return ret;

	return mlx5hws_bwc_queue_poll(ctx, rule_attr->queue_id,
				      &expected_completions, true);
}

static int
hws_bwc_rule_update_sync(struct mlx5hws_bwc_rule *bwc_rule,
			 u8 at_idx,
			 struct mlx5hws_rule_action rule_actions[],
			 struct mlx5hws_rule_attr *rule_attr)
{
	struct mlx5hws_bwc_matcher *bwc_matcher = bwc_rule->bwc_matcher;
	struct mlx5hws_context *ctx = bwc_matcher->matcher->tbl->ctx;
	u32 expected_completions = 1;
	int ret;

	ret = mlx5hws_rule_action_update(bwc_rule->rule,
					 at_idx,
					 rule_actions,
					 rule_attr);
	if (unlikely(ret))
		return ret;

	ret = mlx5hws_bwc_queue_poll(ctx, rule_attr->queue_id,
				     &expected_completions, true);
	if (unlikely(ret))
		mlx5hws_err(ctx, "Failed updating BWC rule (%d)\n", ret);

	return ret;
}

static bool
hws_bwc_matcher_size_maxed_out(struct mlx5hws_bwc_matcher *bwc_matcher,
			       struct mlx5hws_bwc_matcher_size *size)
{
	struct mlx5hws_cmd_query_caps *caps = bwc_matcher->matcher->tbl->ctx->caps;

	/* check the match RTC size */
	return (size->size_log + MLX5HWS_MATCHER_ASSURED_MAIN_TBL_DEPTH +
		MLX5HWS_BWC_MATCHER_SIZE_LOG_STEP) >
	       (caps->ste_alloc_log_max - 1);
}

static bool
hws_bwc_matcher_rehash_size_needed(struct mlx5hws_bwc_matcher *bwc_matcher,
				   struct mlx5hws_bwc_matcher_size *size,
				   u32 num_of_rules)
{
	if (unlikely(hws_bwc_matcher_size_maxed_out(bwc_matcher, size)))
		return false;

	if (unlikely((num_of_rules * 100 / MLX5HWS_BWC_MATCHER_REHASH_PERCENT_TH) >=
		     (1UL << size->size_log)))
		return true;

	return false;
}

static void
hws_bwc_rule_actions_to_action_types(struct mlx5hws_rule_action rule_actions[],
				     enum mlx5hws_action_type action_types[])
{
	int i = 0;

	for (i = 0;
	     rule_actions[i].action && (rule_actions[i].action->type != MLX5HWS_ACTION_TYP_LAST);
	     i++) {
		action_types[i] = (enum mlx5hws_action_type)rule_actions[i].action->type;
	}

	action_types[i] = MLX5HWS_ACTION_TYP_LAST;
}

static int
hws_bwc_matcher_extend_at(struct mlx5hws_bwc_matcher *bwc_matcher,
			  struct mlx5hws_rule_action rule_actions[])
{
	enum mlx5hws_action_type action_types[MLX5HWS_BWC_MAX_ACTS];
	void *p;

	if (unlikely(bwc_matcher->num_of_at >= bwc_matcher->size_of_at_array)) {
		if (bwc_matcher->size_of_at_array >= MLX5HWS_MATCHER_MAX_AT)
			return -ENOMEM;
		bwc_matcher->size_of_at_array *= 2;
		p = krealloc(bwc_matcher->at,
			     bwc_matcher->size_of_at_array *
				     sizeof(*bwc_matcher->at),
			     __GFP_ZERO | GFP_KERNEL);
		if (!p) {
			bwc_matcher->size_of_at_array /= 2;
			return -ENOMEM;
		}

		bwc_matcher->at = p;
	}

	hws_bwc_rule_actions_to_action_types(rule_actions, action_types);

	bwc_matcher->at[bwc_matcher->num_of_at] =
		mlx5hws_action_template_create(action_types);

	if (unlikely(!bwc_matcher->at[bwc_matcher->num_of_at]))
		return -ENOMEM;

	bwc_matcher->num_of_at++;
	return 0;
}

static int
hws_bwc_matcher_extend_size(struct mlx5hws_bwc_matcher *bwc_matcher,
			    struct mlx5hws_bwc_matcher_size *size)
{
	struct mlx5hws_context *ctx = bwc_matcher->matcher->tbl->ctx;
	struct mlx5hws_cmd_query_caps *caps = ctx->caps;

	if (unlikely(hws_bwc_matcher_size_maxed_out(bwc_matcher, size))) {
		mlx5hws_err(ctx, "Can't resize matcher: depth exceeds limit %d\n",
			    caps->rtc_log_depth_max);
		return -ENOMEM;
	}

	size->size_log = min(size->size_log + MLX5HWS_BWC_MATCHER_SIZE_LOG_STEP,
			     caps->ste_alloc_log_max -
				     MLX5HWS_MATCHER_ASSURED_MAIN_TBL_DEPTH);

	return 0;
}

static int
hws_bwc_matcher_find_at(struct mlx5hws_bwc_matcher *bwc_matcher,
			struct mlx5hws_rule_action rule_actions[])
{
	enum mlx5hws_action_type *action_type_arr;
	int i, j;

	/* start from index 1 - first action template is a dummy */
	for (i = 1; i < bwc_matcher->num_of_at; i++) {
		j = 0;
		action_type_arr = bwc_matcher->at[i]->action_type_arr;

		while (rule_actions[j].action &&
		       rule_actions[j].action->type != MLX5HWS_ACTION_TYP_LAST) {
			if (action_type_arr[j] != rule_actions[j].action->type)
				break;
			j++;
		}

		if (action_type_arr[j] == MLX5HWS_ACTION_TYP_LAST &&
		    (!rule_actions[j].action ||
		     rule_actions[j].action->type == MLX5HWS_ACTION_TYP_LAST))
			return i;
	}

	return -1;
}

static int
hws_bwc_matcher_rehash_size(struct mlx5hws_bwc_matcher *bwc_matcher)
{
	bool need_rx_rehash, need_tx_rehash;
	int ret;

	need_rx_rehash = atomic_read(&bwc_matcher->rx_size.rehash_required);
	need_tx_rehash = atomic_read(&bwc_matcher->tx_size.rehash_required);

	/* It is possible that another rule has already performed rehash.
	 * Need to check again if we really need rehash.
	 */
	if (!need_rx_rehash && !need_tx_rehash)
		return 0;

	/* If the current matcher RX/TX size is already at its max size,
	 * it can't be rehashed.
	 */
	if (need_rx_rehash &&
	    hws_bwc_matcher_size_maxed_out(bwc_matcher,
					   &bwc_matcher->rx_size)) {
		atomic_set(&bwc_matcher->rx_size.rehash_required, false);
		need_rx_rehash = false;
	}
	if (need_tx_rehash &&
	    hws_bwc_matcher_size_maxed_out(bwc_matcher,
					   &bwc_matcher->tx_size)) {
		atomic_set(&bwc_matcher->tx_size.rehash_required, false);
		need_tx_rehash = false;
	}

	/* If both RX and TX rehash flags are now off, it means that whatever
	 * we wanted to rehash is now at its max size - no rehash can be done.
	 * Return and try adding the rule again - perhaps there was some change.
	 */
	if (!need_rx_rehash && !need_tx_rehash)
		return 0;

	/* Now we're done all the checking - do the rehash:
	 *  - extend match RTC size
	 *  - create new matcher
	 *  - move all the rules to the new matcher
	 *  - destroy the old matcher
	 */
	atomic_set(&bwc_matcher->rx_size.rehash_required, false);
	atomic_set(&bwc_matcher->tx_size.rehash_required, false);

	if (need_rx_rehash) {
		ret = hws_bwc_matcher_extend_size(bwc_matcher,
						  &bwc_matcher->rx_size);
		if (ret)
			return ret;
	}

	if (need_tx_rehash) {
		ret = hws_bwc_matcher_extend_size(bwc_matcher,
						  &bwc_matcher->tx_size);
		if (ret)
			return ret;
	}

	return hws_bwc_matcher_move(bwc_matcher);
}

static int hws_bwc_rule_get_at_idx(struct mlx5hws_bwc_rule *bwc_rule,
				   struct mlx5hws_rule_action rule_actions[],
				   u16 bwc_queue_idx)
{
	struct mlx5hws_bwc_matcher *bwc_matcher = bwc_rule->bwc_matcher;
	struct mlx5hws_context *ctx = bwc_matcher->matcher->tbl->ctx;
	struct mutex *queue_lock; /* Protect the queue */
	int at_idx, ret;

	/* check if rehash needed due to missing action template */
	at_idx = hws_bwc_matcher_find_at(bwc_matcher, rule_actions);
	if (likely(at_idx >= 0))
		return at_idx;

	/* we need to extend BWC matcher action templates array */
	queue_lock = hws_bwc_get_queue_lock(ctx, bwc_queue_idx);
	mutex_unlock(queue_lock);
	hws_bwc_lock_all_queues(ctx);

	/* check again - perhaps other thread already did extend_at */
	at_idx = hws_bwc_matcher_find_at(bwc_matcher, rule_actions);
	if (at_idx >= 0)
		goto out;

	ret = hws_bwc_matcher_extend_at(bwc_matcher, rule_actions);
	if (unlikely(ret)) {
		mlx5hws_err(ctx, "BWC rule: failed extending AT (%d)", ret);
		at_idx = -EINVAL;
		goto out;
	}

	/* action templates array was extended, we need the last idx */
	at_idx = bwc_matcher->num_of_at - 1;
	ret = mlx5hws_matcher_attach_at(bwc_matcher->matcher,
					bwc_matcher->at[at_idx]);
	if (unlikely(ret)) {
		mlx5hws_err(ctx, "BWC rule: failed attaching new AT (%d)", ret);
		at_idx = -EINVAL;
		goto out;
	}

out:
	hws_bwc_unlock_all_queues(ctx);
	mutex_lock(queue_lock);
	return at_idx;
}

static void hws_bwc_rule_cnt_inc_rxtx(struct mlx5hws_bwc_rule *bwc_rule,
				      struct mlx5hws_bwc_matcher_size *size)
{
	u32 num_of_rules = atomic_inc_return(&size->num_of_rules);

	if (unlikely(hws_bwc_matcher_rehash_size_needed(bwc_rule->bwc_matcher,
							size, num_of_rules)))
		atomic_set(&size->rehash_required, true);
}

static void hws_bwc_rule_cnt_inc(struct mlx5hws_bwc_rule *bwc_rule)
{
	struct mlx5hws_bwc_matcher *bwc_matcher = bwc_rule->bwc_matcher;

	if (!bwc_rule->skip_rx)
		hws_bwc_rule_cnt_inc_rxtx(bwc_rule, &bwc_matcher->rx_size);
	if (!bwc_rule->skip_tx)
		hws_bwc_rule_cnt_inc_rxtx(bwc_rule, &bwc_matcher->tx_size);
}

static int hws_bwc_rule_cnt_inc_with_rehash(struct mlx5hws_bwc_rule *bwc_rule,
					    u16 bwc_queue_idx)
{
	struct mlx5hws_bwc_matcher *bwc_matcher = bwc_rule->bwc_matcher;
	struct mlx5hws_context *ctx = bwc_matcher->matcher->tbl->ctx;
	struct mutex *queue_lock; /* Protect the queue */
	int ret;

	hws_bwc_rule_cnt_inc(bwc_rule);

	if (!atomic_read(&bwc_matcher->rx_size.rehash_required) &&
	    !atomic_read(&bwc_matcher->tx_size.rehash_required))
		return 0;

	queue_lock = hws_bwc_get_queue_lock(ctx, bwc_queue_idx);
	mutex_unlock(queue_lock);

	hws_bwc_lock_all_queues(ctx);
	ret = hws_bwc_matcher_rehash_size(bwc_matcher);
	hws_bwc_unlock_all_queues(ctx);

	mutex_lock(queue_lock);

	if (likely(!ret))
		return 0;

	/* Failed to rehash. Print a diagnostic and rollback the counters. */
	mlx5hws_err(ctx,
		    "BWC rule insertion: rehash to sizes [%d, %d] failed (%d)\n",
		    bwc_matcher->rx_size.size_log,
		    bwc_matcher->tx_size.size_log, ret);
	hws_bwc_rule_cnt_dec(bwc_rule);

	return ret;
}

int mlx5hws_bwc_rule_create_simple(struct mlx5hws_bwc_rule *bwc_rule,
				   u32 *match_param,
				   struct mlx5hws_rule_action rule_actions[],
				   u32 flow_source,
				   u16 bwc_queue_idx)
{
	struct mlx5hws_bwc_matcher *bwc_matcher = bwc_rule->bwc_matcher;
	struct mlx5hws_context *ctx = bwc_matcher->matcher->tbl->ctx;
	struct mlx5hws_rule_attr rule_attr;
	struct mutex *queue_lock; /* Protect the queue */
	int ret = 0;
	int at_idx;

	mlx5hws_bwc_rule_fill_attr(bwc_matcher, bwc_queue_idx, flow_source, &rule_attr);

	queue_lock = hws_bwc_get_queue_lock(ctx, bwc_queue_idx);

	mutex_lock(queue_lock);

	at_idx = hws_bwc_rule_get_at_idx(bwc_rule, rule_actions, bwc_queue_idx);
	if (unlikely(at_idx < 0)) {
		mutex_unlock(queue_lock);
		mlx5hws_err(ctx, "BWC rule create: failed getting AT (%d)",
			    ret);
		return -EINVAL;
	}

	ret = hws_bwc_rule_cnt_inc_with_rehash(bwc_rule, bwc_queue_idx);
	if (unlikely(ret)) {
		mutex_unlock(queue_lock);
		return ret;
	}

	ret = hws_bwc_rule_create_sync(bwc_rule,
				       match_param,
				       at_idx,
				       rule_actions,
				       &rule_attr);
	if (likely(!ret)) {
		hws_bwc_rule_list_add(bwc_rule, bwc_queue_idx);
		mutex_unlock(queue_lock);
		return 0; /* rule inserted successfully */
	}

	/* Rule insertion could fail due to queue being full, timeout, or
	 * matcher in resize. In such cases, no point in trying to rehash.
	 */
	if (ret == -EBUSY || ret == -ETIMEDOUT || ret == -EAGAIN) {
		mutex_unlock(queue_lock);
		mlx5hws_err(ctx,
			    "BWC rule insertion failed - %s (%d)\n",
			    ret == -EBUSY ? "queue is full" :
			    ret == -ETIMEDOUT ? "timeout" :
			    ret == -EAGAIN ? "matcher in resize" : "N/A",
			    ret);
		hws_bwc_rule_cnt_dec(bwc_rule);
		return ret;
	}

	/* At this point the rule wasn't added.
	 * It could be because there was collision, or some other problem.
	 * Try rehash by size and insert rule again - last chance.
	 */
	if (!bwc_rule->skip_rx)
		atomic_set(&bwc_matcher->rx_size.rehash_required, true);
	if (!bwc_rule->skip_tx)
		atomic_set(&bwc_matcher->tx_size.rehash_required, true);

	mutex_unlock(queue_lock);

	hws_bwc_lock_all_queues(ctx);
	ret = hws_bwc_matcher_rehash_size(bwc_matcher);
	hws_bwc_unlock_all_queues(ctx);

	if (ret) {
		mlx5hws_err(ctx, "BWC rule insertion: rehash failed (%d)\n", ret);
		hws_bwc_rule_cnt_dec(bwc_rule);
		return ret;
	}

	/* Rehash done, but we still have that pesky rule to add */
	mutex_lock(queue_lock);

	ret = hws_bwc_rule_create_sync(bwc_rule,
				       match_param,
				       at_idx,
				       rule_actions,
				       &rule_attr);

	if (unlikely(ret)) {
		mutex_unlock(queue_lock);
		mlx5hws_err(ctx, "BWC rule insertion failed (%d)\n", ret);
		hws_bwc_rule_cnt_dec(bwc_rule);
		return ret;
	}

	hws_bwc_rule_list_add(bwc_rule, bwc_queue_idx);
	mutex_unlock(queue_lock);

	return 0;
}

struct mlx5hws_bwc_rule *
mlx5hws_bwc_rule_create(struct mlx5hws_bwc_matcher *bwc_matcher,
			struct mlx5hws_match_parameters *params,
			u32 flow_source,
			struct mlx5hws_rule_action rule_actions[])
{
	struct mlx5hws_context *ctx = bwc_matcher->matcher->tbl->ctx;
	struct mlx5hws_bwc_rule *bwc_rule;
	u16 bwc_queue_idx;
	int ret;

	if (unlikely(!mlx5hws_context_bwc_supported(ctx))) {
		mlx5hws_err(ctx, "BWC rule: Context created w/o BWC API compatibility\n");
		return NULL;
	}

	bwc_rule = mlx5hws_bwc_rule_alloc(bwc_matcher);
	if (unlikely(!bwc_rule))
		return NULL;

	bwc_rule->flow_source = flow_source;
	mlx5hws_rule_skip(bwc_matcher->matcher, flow_source,
			  &bwc_rule->skip_rx, &bwc_rule->skip_tx);

	bwc_queue_idx = hws_bwc_gen_queue_idx(ctx);

	if (bwc_matcher->matcher_type == MLX5HWS_BWC_MATCHER_COMPLEX_FIRST)
		ret = mlx5hws_bwc_rule_create_complex(bwc_rule,
						      params,
						      flow_source,
						      rule_actions,
						      bwc_queue_idx);
	else
		ret = mlx5hws_bwc_rule_create_simple(bwc_rule,
						     params->match_buf,
						     rule_actions,
						     flow_source,
						     bwc_queue_idx);
	if (unlikely(ret)) {
		mlx5hws_bwc_rule_free(bwc_rule);
		return NULL;
	}

	return bwc_rule;
}

static int
hws_bwc_rule_action_update(struct mlx5hws_bwc_rule *bwc_rule,
			   struct mlx5hws_rule_action rule_actions[])
{
	struct mlx5hws_bwc_matcher *bwc_matcher = bwc_rule->bwc_matcher;
	struct mlx5hws_context *ctx = bwc_matcher->matcher->tbl->ctx;
	struct mlx5hws_rule_attr rule_attr;
	struct mutex *queue_lock; /* Protect the queue */
	int at_idx, ret;
	u16 idx;

	idx = bwc_rule->bwc_queue_idx;

	mlx5hws_bwc_rule_fill_attr(bwc_matcher, idx, bwc_rule->flow_source,
				   &rule_attr);
	queue_lock = hws_bwc_get_queue_lock(ctx, idx);

	mutex_lock(queue_lock);

	at_idx = hws_bwc_rule_get_at_idx(bwc_rule, rule_actions, idx);
	if (unlikely(at_idx < 0)) {
		mutex_unlock(queue_lock);
		mlx5hws_err(ctx, "BWC rule update: failed getting AT\n");
		return -EINVAL;
	}

	ret = hws_bwc_rule_update_sync(bwc_rule,
				       at_idx,
				       rule_actions,
				       &rule_attr);
	mutex_unlock(queue_lock);

	if (unlikely(ret))
		mlx5hws_err(ctx, "BWC rule: update failed (%d)\n", ret);

	return ret;
}

int mlx5hws_bwc_rule_action_update(struct mlx5hws_bwc_rule *bwc_rule,
				   struct mlx5hws_rule_action rule_actions[])
{
	struct mlx5hws_bwc_matcher *bwc_matcher = bwc_rule->bwc_matcher;
	struct mlx5hws_context *ctx = bwc_matcher->matcher->tbl->ctx;

	if (unlikely(!mlx5hws_context_bwc_supported(ctx))) {
		mlx5hws_err(ctx, "BWC rule: Context created w/o BWC API compatibility\n");
		return -EINVAL;
	}

	/* For complex rules, the update should happen on the last subrule. */
	while (bwc_rule->next_subrule)
		bwc_rule = bwc_rule->next_subrule;

	return hws_bwc_rule_action_update(bwc_rule, rule_actions);
}
