/*
 * Copyright (c) 2013-2015, Mellanox Technologies, Ltd.  All rights reserved.
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

#include <linux/export.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/flow_table.h>
#include "mlx5_core.h"

struct mlx5_ftg {
	struct mlx5_flow_table_group    g;
	u32				id;
	u32				start_ix;
};

struct mlx5_flow_table {
	struct mlx5_core_dev	*dev;
	u8			level;
	u8			type;
	u32			id;
	struct mutex		mutex; /* sync bitmap alloc */
	u16			num_groups;
	struct mlx5_ftg		*group;
	unsigned long		*bitmap;
	u32			size;
};

static int mlx5_set_flow_entry_cmd(struct mlx5_flow_table *ft, u32 group_ix,
				   u32 flow_index, void *flow_context)
{
	u32 out[MLX5_ST_SZ_DW(set_fte_out)];
	u32 *in;
	void *in_flow_context;
	int fcdls =
		MLX5_GET(flow_context, flow_context, destination_list_size) *
		MLX5_ST_SZ_BYTES(dest_format_struct);
	int inlen = MLX5_ST_SZ_BYTES(set_fte_in) + fcdls;
	int err;

	in = mlx5_vzalloc(inlen);
	if (!in) {
		mlx5_core_warn(ft->dev, "failed to allocate inbox\n");
		return -ENOMEM;
	}

	MLX5_SET(set_fte_in, in, table_type, ft->type);
	MLX5_SET(set_fte_in, in, table_id,   ft->id);
	MLX5_SET(set_fte_in, in, flow_index, flow_index);
	MLX5_SET(set_fte_in, in, opcode, MLX5_CMD_OP_SET_FLOW_TABLE_ENTRY);

	in_flow_context = MLX5_ADDR_OF(set_fte_in, in, flow_context);
	memcpy(in_flow_context, flow_context,
	       MLX5_ST_SZ_BYTES(flow_context) + fcdls);

	MLX5_SET(flow_context, in_flow_context, group_id,
		 ft->group[group_ix].id);

	memset(out, 0, sizeof(out));
	err = mlx5_cmd_exec_check_status(ft->dev, in, inlen, out,
					 sizeof(out));
	kvfree(in);

	return err;
}

static void mlx5_del_flow_entry_cmd(struct mlx5_flow_table *ft, u32 flow_index)
{
	u32 in[MLX5_ST_SZ_DW(delete_fte_in)];
	u32 out[MLX5_ST_SZ_DW(delete_fte_out)];

	memset(in, 0, sizeof(in));
	memset(out, 0, sizeof(out));

#define MLX5_SET_DFTEI(p, x, v) MLX5_SET(delete_fte_in, p, x, v)
	MLX5_SET_DFTEI(in, table_type, ft->type);
	MLX5_SET_DFTEI(in, table_id,   ft->id);
	MLX5_SET_DFTEI(in, flow_index, flow_index);
	MLX5_SET_DFTEI(in, opcode,     MLX5_CMD_OP_DELETE_FLOW_TABLE_ENTRY);

	mlx5_cmd_exec_check_status(ft->dev, in, sizeof(in), out, sizeof(out));
}

static void mlx5_destroy_flow_group_cmd(struct mlx5_flow_table *ft, int i)
{
	u32 in[MLX5_ST_SZ_DW(destroy_flow_group_in)];
	u32 out[MLX5_ST_SZ_DW(destroy_flow_group_out)];

	memset(in, 0, sizeof(in));
	memset(out, 0, sizeof(out));

#define MLX5_SET_DFGI(p, x, v) MLX5_SET(destroy_flow_group_in, p, x, v)
	MLX5_SET_DFGI(in, table_type, ft->type);
	MLX5_SET_DFGI(in, table_id,   ft->id);
	MLX5_SET_DFGI(in, opcode, MLX5_CMD_OP_DESTROY_FLOW_GROUP);
	MLX5_SET_DFGI(in, group_id, ft->group[i].id);
	mlx5_cmd_exec_check_status(ft->dev, in, sizeof(in), out, sizeof(out));
}

static int mlx5_create_flow_group_cmd(struct mlx5_flow_table *ft, int i)
{
	u32 out[MLX5_ST_SZ_DW(create_flow_group_out)];
	u32 *in;
	void *in_match_criteria;
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_flow_table_group *g = &ft->group[i].g;
	u32 start_ix = ft->group[i].start_ix;
	u32 end_ix = start_ix + (1 << g->log_sz) - 1;
	int err;

	in = mlx5_vzalloc(inlen);
	if (!in) {
		mlx5_core_warn(ft->dev, "failed to allocate inbox\n");
		return -ENOMEM;
	}
	in_match_criteria = MLX5_ADDR_OF(create_flow_group_in, in,
					 match_criteria);

	memset(out, 0, sizeof(out));

#define MLX5_SET_CFGI(p, x, v) MLX5_SET(create_flow_group_in, p, x, v)
	MLX5_SET_CFGI(in, table_type,            ft->type);
	MLX5_SET_CFGI(in, table_id,              ft->id);
	MLX5_SET_CFGI(in, opcode,                MLX5_CMD_OP_CREATE_FLOW_GROUP);
	MLX5_SET_CFGI(in, start_flow_index,      start_ix);
	MLX5_SET_CFGI(in, end_flow_index,        end_ix);
	MLX5_SET_CFGI(in, match_criteria_enable, g->match_criteria_enable);

	memcpy(in_match_criteria, g->match_criteria,
	       MLX5_ST_SZ_BYTES(fte_match_param));

	err = mlx5_cmd_exec_check_status(ft->dev, in, inlen, out,
					 sizeof(out));
	if (!err)
		ft->group[i].id = MLX5_GET(create_flow_group_out, out,
					   group_id);

	kvfree(in);

	return err;
}

static void mlx5_destroy_flow_table_groups(struct mlx5_flow_table *ft)
{
	int i;

	for (i = 0; i < ft->num_groups; i++)
		mlx5_destroy_flow_group_cmd(ft, i);
}

static int mlx5_create_flow_table_groups(struct mlx5_flow_table *ft)
{
	int err;
	int i;

	for (i = 0; i < ft->num_groups; i++) {
		err = mlx5_create_flow_group_cmd(ft, i);
		if (err)
			goto err_destroy_flow_table_groups;
	}

	return 0;

err_destroy_flow_table_groups:
	for (i--; i >= 0; i--)
		mlx5_destroy_flow_group_cmd(ft, i);

	return err;
}

static int mlx5_create_flow_table_cmd(struct mlx5_flow_table *ft)
{
	u32 in[MLX5_ST_SZ_DW(create_flow_table_in)];
	u32 out[MLX5_ST_SZ_DW(create_flow_table_out)];
	int err;

	memset(in, 0, sizeof(in));

	MLX5_SET(create_flow_table_in, in, table_type, ft->type);
	MLX5_SET(create_flow_table_in, in, level,      ft->level);
	MLX5_SET(create_flow_table_in, in, log_size,   order_base_2(ft->size));

	MLX5_SET(create_flow_table_in, in, opcode,
		 MLX5_CMD_OP_CREATE_FLOW_TABLE);

	memset(out, 0, sizeof(out));
	err = mlx5_cmd_exec_check_status(ft->dev, in, sizeof(in), out,
					 sizeof(out));
	if (err)
		return err;

	ft->id = MLX5_GET(create_flow_table_out, out, table_id);

	return 0;
}

static void mlx5_destroy_flow_table_cmd(struct mlx5_flow_table *ft)
{
	u32 in[MLX5_ST_SZ_DW(destroy_flow_table_in)];
	u32 out[MLX5_ST_SZ_DW(destroy_flow_table_out)];

	memset(in, 0, sizeof(in));
	memset(out, 0, sizeof(out));

#define MLX5_SET_DFTI(p, x, v) MLX5_SET(destroy_flow_table_in, p, x, v)
	MLX5_SET_DFTI(in, table_type, ft->type);
	MLX5_SET_DFTI(in, table_id,   ft->id);
	MLX5_SET_DFTI(in, opcode, MLX5_CMD_OP_DESTROY_FLOW_TABLE);

	mlx5_cmd_exec_check_status(ft->dev, in, sizeof(in), out, sizeof(out));
}

static int mlx5_find_group(struct mlx5_flow_table *ft, u8 match_criteria_enable,
			   u32 *match_criteria, int *group_ix)
{
	void *mc_outer = MLX5_ADDR_OF(fte_match_param, match_criteria,
				      outer_headers);
	void *mc_misc  = MLX5_ADDR_OF(fte_match_param, match_criteria,
				      misc_parameters);
	void *mc_inner = MLX5_ADDR_OF(fte_match_param, match_criteria,
				      inner_headers);
	int mc_outer_sz = MLX5_ST_SZ_BYTES(fte_match_set_lyr_2_4);
	int mc_misc_sz  = MLX5_ST_SZ_BYTES(fte_match_set_misc);
	int mc_inner_sz = MLX5_ST_SZ_BYTES(fte_match_set_lyr_2_4);
	int i;

	for (i = 0; i < ft->num_groups; i++) {
		struct mlx5_flow_table_group *g = &ft->group[i].g;
		void *gmc_outer = MLX5_ADDR_OF(fte_match_param,
					       g->match_criteria,
					       outer_headers);
		void *gmc_misc  = MLX5_ADDR_OF(fte_match_param,
					       g->match_criteria,
					       misc_parameters);
		void *gmc_inner = MLX5_ADDR_OF(fte_match_param,
					       g->match_criteria,
					       inner_headers);

		if (g->match_criteria_enable != match_criteria_enable)
			continue;

		if (match_criteria_enable & MLX5_MATCH_OUTER_HEADERS)
			if (memcmp(mc_outer, gmc_outer, mc_outer_sz))
				continue;

		if (match_criteria_enable & MLX5_MATCH_MISC_PARAMETERS)
			if (memcmp(mc_misc, gmc_misc, mc_misc_sz))
				continue;

		if (match_criteria_enable & MLX5_MATCH_INNER_HEADERS)
			if (memcmp(mc_inner, gmc_inner, mc_inner_sz))
				continue;

		*group_ix = i;
		return 0;
	}

	return -EINVAL;
}

static int alloc_flow_index(struct mlx5_flow_table *ft, int group_ix, u32 *ix)
{
	struct mlx5_ftg *g = &ft->group[group_ix];
	int err = 0;

	mutex_lock(&ft->mutex);

	*ix = find_next_zero_bit(ft->bitmap, ft->size, g->start_ix);
	if (*ix >= (g->start_ix + (1 << g->g.log_sz)))
		err = -ENOSPC;
	else
		__set_bit(*ix, ft->bitmap);

	mutex_unlock(&ft->mutex);

	return err;
}

static void mlx5_free_flow_index(struct mlx5_flow_table *ft, u32 ix)
{
	__clear_bit(ix, ft->bitmap);
}

int mlx5_add_flow_table_entry(void *flow_table, u8 match_criteria_enable,
			      void *match_criteria, void *flow_context,
			      u32 *flow_index)
{
	struct mlx5_flow_table *ft = flow_table;
	int group_ix;
	int err;

	err = mlx5_find_group(ft, match_criteria_enable, match_criteria,
			      &group_ix);
	if (err) {
		mlx5_core_warn(ft->dev, "mlx5_find_group failed\n");
		return err;
	}

	err = alloc_flow_index(ft, group_ix, flow_index);
	if (err) {
		mlx5_core_warn(ft->dev, "alloc_flow_index failed\n");
		return err;
	}

	return mlx5_set_flow_entry_cmd(ft, group_ix, *flow_index, flow_context);
}
EXPORT_SYMBOL(mlx5_add_flow_table_entry);

void mlx5_del_flow_table_entry(void *flow_table, u32 flow_index)
{
	struct mlx5_flow_table *ft = flow_table;

	mlx5_del_flow_entry_cmd(ft, flow_index);
	mlx5_free_flow_index(ft, flow_index);
}
EXPORT_SYMBOL(mlx5_del_flow_table_entry);

void *mlx5_create_flow_table(struct mlx5_core_dev *dev, u8 level, u8 table_type,
			     u16 num_groups,
			     struct mlx5_flow_table_group *group)
{
	struct mlx5_flow_table *ft;
	u32 start_ix = 0;
	u32 ft_size = 0;
	void *gr;
	void *bm;
	int err;
	int i;

	for (i = 0; i < num_groups; i++)
		ft_size += (1 << group[i].log_sz);

	ft = kzalloc(sizeof(*ft), GFP_KERNEL);
	gr = kcalloc(num_groups, sizeof(struct mlx5_ftg), GFP_KERNEL);
	bm = kcalloc(BITS_TO_LONGS(ft_size), sizeof(uintptr_t), GFP_KERNEL);
	if (!ft || !gr || !bm)
		goto err_free_ft;

	ft->group	= gr;
	ft->bitmap	= bm;
	ft->num_groups	= num_groups;
	ft->level	= level;
	ft->type	= table_type;
	ft->size	= ft_size;
	ft->dev		= dev;
	mutex_init(&ft->mutex);

	for (i = 0; i < ft->num_groups; i++) {
		memcpy(&ft->group[i].g, &group[i], sizeof(*group));
		ft->group[i].start_ix = start_ix;
		start_ix += 1 << group[i].log_sz;
	}

	err = mlx5_create_flow_table_cmd(ft);
	if (err)
		goto err_free_ft;

	err = mlx5_create_flow_table_groups(ft);
	if (err)
		goto err_destroy_flow_table_cmd;

	return ft;

err_destroy_flow_table_cmd:
	mlx5_destroy_flow_table_cmd(ft);

err_free_ft:
	mlx5_core_warn(dev, "failed to alloc flow table\n");
	kfree(bm);
	kfree(gr);
	kfree(ft);

	return NULL;
}
EXPORT_SYMBOL(mlx5_create_flow_table);

void mlx5_destroy_flow_table(void *flow_table)
{
	struct mlx5_flow_table *ft = flow_table;

	mlx5_destroy_flow_table_groups(ft);
	mlx5_destroy_flow_table_cmd(ft);
	kfree(ft->bitmap);
	kfree(ft->group);
	kfree(ft);
}
EXPORT_SYMBOL(mlx5_destroy_flow_table);

u32 mlx5_get_flow_table_id(void *flow_table)
{
	struct mlx5_flow_table *ft = flow_table;

	return ft->id;
}
EXPORT_SYMBOL(mlx5_get_flow_table_id);
