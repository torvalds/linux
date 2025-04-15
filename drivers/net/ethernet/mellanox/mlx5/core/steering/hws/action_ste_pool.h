/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2025 NVIDIA Corporation & Affiliates */

#ifndef ACTION_STE_POOL_H_
#define ACTION_STE_POOL_H_

#define MLX5HWS_ACTION_STE_TABLE_INIT_LOG_SZ 10
#define MLX5HWS_ACTION_STE_TABLE_STEP_LOG_SZ 1
#define MLX5HWS_ACTION_STE_TABLE_MAX_LOG_SZ 20

#define MLX5HWS_ACTION_STE_POOL_CLEANUP_SECONDS 300
#define MLX5HWS_ACTION_STE_POOL_EXPIRE_SECONDS 300

struct mlx5hws_action_ste_pool_element;

struct mlx5hws_action_ste_table {
	struct mlx5hws_action_ste_pool_element *parent_elem;
	/* Wraps the RTC and STE range for this given action. */
	struct mlx5hws_pool *pool;
	/* Match STEs use this STC to jump to this pool's RTC. */
	struct mlx5hws_pool_chunk stc;
	u32 rtc_0_id;
	u32 rtc_1_id;
	struct list_head list_node;
	unsigned long last_used;
};

struct mlx5hws_action_ste_pool_element {
	struct mlx5hws_context *ctx;
	struct mlx5hws_action_ste_pool *parent_pool;
	size_t log_sz;  /* Size of the largest table so far. */
	enum mlx5hws_pool_optimize opt;
	struct list_head available;
	struct list_head full;
};

/* Central repository of action STEs. The context contains one of these pools
 * per queue.
 */
struct mlx5hws_action_ste_pool {
	/* Protects the entire pool. We have one pool per queue and only one
	 * operation can be active per rule at a given time. Thus this lock
	 * protects solely against concurrent garbage collection and we expect
	 * very little contention.
	 */
	struct mutex lock;
	struct mlx5hws_action_ste_pool_element elems[MLX5HWS_POOL_OPTIMIZE_MAX];
};

/* A chunk of STEs and the table it was allocated from. Used by rules. */
struct mlx5hws_action_ste_chunk {
	struct mlx5hws_action_ste_table *action_tbl;
	struct mlx5hws_pool_chunk ste;
};

int mlx5hws_action_ste_pool_init(struct mlx5hws_context *ctx);

void mlx5hws_action_ste_pool_uninit(struct mlx5hws_context *ctx);

/* Callers are expected to fill chunk->ste.order. On success, this function
 * populates chunk->tbl and chunk->ste.offset.
 */
int mlx5hws_action_ste_chunk_alloc(struct mlx5hws_action_ste_pool *pool,
				   bool skip_rx, bool skip_tx,
				   struct mlx5hws_action_ste_chunk *chunk);

void mlx5hws_action_ste_chunk_free(struct mlx5hws_action_ste_chunk *chunk);

#endif /* ACTION_STE_POOL_H_ */
