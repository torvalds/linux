/*
 * Copyright (c) 2015, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _MLX5_FS_CMD_
#define _MLX5_FS_CMD_

#include "fs_core.h"

struct mlx5_flow_cmds {
	int (*create_flow_table)(struct mlx5_flow_root_namespace *ns,
				 struct mlx5_flow_table *ft,
				 unsigned int log_size,
				 struct mlx5_flow_table *next_ft);
	int (*destroy_flow_table)(struct mlx5_flow_root_namespace *ns,
				  struct mlx5_flow_table *ft);

	int (*modify_flow_table)(struct mlx5_flow_root_namespace *ns,
				 struct mlx5_flow_table *ft,
				 struct mlx5_flow_table *next_ft);

	int (*create_flow_group)(struct mlx5_flow_root_namespace *ns,
				 struct mlx5_flow_table *ft,
				 u32 *in,
				 struct mlx5_flow_group *fg);

	int (*destroy_flow_group)(struct mlx5_flow_root_namespace *ns,
				  struct mlx5_flow_table *ft,
				  struct mlx5_flow_group *fg);

	int (*create_fte)(struct mlx5_flow_root_namespace *ns,
			  struct mlx5_flow_table *ft,
			  struct mlx5_flow_group *fg,
			  struct fs_fte *fte);

	int (*update_fte)(struct mlx5_flow_root_namespace *ns,
			  struct mlx5_flow_table *ft,
			  struct mlx5_flow_group *fg,
			  int modify_mask,
			  struct fs_fte *fte);

	int (*delete_fte)(struct mlx5_flow_root_namespace *ns,
			  struct mlx5_flow_table *ft,
			  struct fs_fte *fte);

	int (*update_root_ft)(struct mlx5_flow_root_namespace *ns,
			      struct mlx5_flow_table *ft,
			      u32 underlay_qpn,
			      bool disconnect);
};

int mlx5_cmd_fc_alloc(struct mlx5_core_dev *dev, u32 *id);
int mlx5_cmd_fc_bulk_alloc(struct mlx5_core_dev *dev,
			   enum mlx5_fc_bulk_alloc_bitmask alloc_bitmask,
			   u32 *id);
int mlx5_cmd_fc_free(struct mlx5_core_dev *dev, u32 id);
int mlx5_cmd_fc_query(struct mlx5_core_dev *dev, u32 id,
		      u64 *packets, u64 *bytes);

int mlx5_cmd_fc_get_bulk_query_out_len(int bulk_len);
int mlx5_cmd_fc_bulk_query(struct mlx5_core_dev *dev, u32 base_id, int bulk_len,
			   u32 *out);

const struct mlx5_flow_cmds *mlx5_fs_cmd_get_default(enum fs_flow_table_type type);

#endif
