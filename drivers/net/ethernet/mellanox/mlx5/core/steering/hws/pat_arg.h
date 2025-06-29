/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#ifndef MLX5HWS_PAT_ARG_H_
#define MLX5HWS_PAT_ARG_H_

/* Modify-header arg pool */
enum mlx5hws_arg_chunk_size {
	MLX5HWS_ARG_CHUNK_SIZE_1,
	/* Keep MIN updated when changing */
	MLX5HWS_ARG_CHUNK_SIZE_MIN = MLX5HWS_ARG_CHUNK_SIZE_1,
	MLX5HWS_ARG_CHUNK_SIZE_2,
	MLX5HWS_ARG_CHUNK_SIZE_3,
	MLX5HWS_ARG_CHUNK_SIZE_4,
	MLX5HWS_ARG_CHUNK_SIZE_MAX,
};

enum {
	MLX5HWS_MODIFY_ACTION_SIZE = 8,
	MLX5HWS_ARG_DATA_SIZE = 64,
};

struct mlx5hws_pattern_cache {
	struct mutex lock; /* Protect pattern list */
	struct list_head ptrn_list;
};

struct mlx5hws_pattern_cache_item {
	struct {
		u32 pattern_id;
		u8 *data;
		u16 num_of_actions;
	} mh_data;
	u32 refcount; /* protected by pattern_cache lock */
	struct list_head ptrn_list_node;
};

enum mlx5hws_arg_chunk_size
mlx5hws_arg_get_arg_log_size(u16 num_of_actions);

u32 mlx5hws_arg_get_arg_size(u16 num_of_actions);

enum mlx5hws_arg_chunk_size
mlx5hws_arg_data_size_to_arg_log_size(u16 data_size);

u32 mlx5hws_arg_data_size_to_arg_size(u16 data_size);

int mlx5hws_pat_init_pattern_cache(struct mlx5hws_pattern_cache **cache);

void mlx5hws_pat_uninit_pattern_cache(struct mlx5hws_pattern_cache *cache);

bool mlx5hws_pat_verify_actions(struct mlx5hws_context *ctx, __be64 pattern[], size_t sz);

int mlx5hws_arg_create(struct mlx5hws_context *ctx,
		       u8 *data,
		       size_t data_sz,
		       u32 log_bulk_sz,
		       bool write_data,
		       u32 *arg_id);

void mlx5hws_arg_destroy(struct mlx5hws_context *ctx, u32 arg_id);

int mlx5hws_arg_create_modify_header_arg(struct mlx5hws_context *ctx,
					 __be64 *data,
					 u8 num_of_actions,
					 u32 log_bulk_sz,
					 bool write_data,
					 u32 *modify_hdr_arg_id);

int mlx5hws_pat_get_pattern(struct mlx5hws_context *ctx,
			    __be64 *pattern,
			    size_t pattern_sz,
			    u32 *ptrn_id);

void mlx5hws_pat_put_pattern(struct mlx5hws_context *ctx,
			     u32 ptrn_id);

bool mlx5hws_arg_is_valid_arg_request_size(struct mlx5hws_context *ctx,
					   u32 arg_size);

bool mlx5hws_pat_require_reparse(__be64 *actions, u16 num_of_actions);

void mlx5hws_arg_write(struct mlx5hws_send_engine *queue,
		       void *comp_data,
		       u32 arg_idx,
		       u8 *arg_data,
		       size_t data_size);

void mlx5hws_arg_decapl3_write(struct mlx5hws_send_engine *queue,
			       u32 arg_idx,
			       u8 *arg_data,
			       u16 num_of_actions);

int mlx5hws_arg_write_inline_arg_data(struct mlx5hws_context *ctx,
				      u32 arg_idx,
				      u8 *arg_data,
				      size_t data_size);

int mlx5hws_pat_calc_nop(__be64 *pattern, size_t num_actions,
			 size_t max_actions, size_t *new_size,
			 u32 *nop_locations, __be64 *new_pat);
#endif /* MLX5HWS_PAT_ARG_H_ */
