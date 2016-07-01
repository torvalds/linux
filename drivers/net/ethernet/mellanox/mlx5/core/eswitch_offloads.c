/*
 * Copyright (c) 2016, Mellanox Technologies. All rights reserved.
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

#include <linux/etherdevice.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/mlx5_ifc.h>
#include <linux/mlx5/vport.h>
#include <linux/mlx5/fs.h>
#include "mlx5_core.h"
#include "eswitch.h"

struct mlx5_flow_rule *
mlx5_eswitch_add_send_to_vport_rule(struct mlx5_eswitch *esw, int vport, u32 sqn)
{
	struct mlx5_flow_destination dest;
	struct mlx5_flow_rule *flow_rule;
	int match_header = MLX5_MATCH_MISC_PARAMETERS;
	u32 *match_v, *match_c;
	void *misc;

	match_v = kzalloc(MLX5_ST_SZ_BYTES(fte_match_param), GFP_KERNEL);
	match_c = kzalloc(MLX5_ST_SZ_BYTES(fte_match_param), GFP_KERNEL);
	if (!match_v || !match_c) {
		esw_warn(esw->dev, "FDB: Failed to alloc match parameters\n");
		flow_rule = ERR_PTR(-ENOMEM);
		goto out;
	}

	misc = MLX5_ADDR_OF(fte_match_param, match_v, misc_parameters);
	MLX5_SET(fte_match_set_misc, misc, source_sqn, sqn);
	MLX5_SET(fte_match_set_misc, misc, source_port, 0x0); /* source vport is 0 */

	misc = MLX5_ADDR_OF(fte_match_param, match_c, misc_parameters);
	MLX5_SET_TO_ONES(fte_match_set_misc, misc, source_sqn);
	MLX5_SET_TO_ONES(fte_match_set_misc, misc, source_port);

	dest.type = MLX5_FLOW_DESTINATION_TYPE_VPORT;
	dest.vport_num = vport;

	flow_rule = mlx5_add_flow_rule(esw->fdb_table.fdb, match_header, match_c,
				       match_v, MLX5_FLOW_CONTEXT_ACTION_FWD_DEST,
				       0, &dest);
	if (IS_ERR(flow_rule))
		esw_warn(esw->dev, "FDB: Failed to add send to vport rule err %ld\n", PTR_ERR(flow_rule));
out:
	kfree(match_v);
	kfree(match_c);
	return flow_rule;
}

static int esw_add_fdb_miss_rule(struct mlx5_eswitch *esw)
{
	struct mlx5_flow_destination dest;
	struct mlx5_flow_rule *flow_rule = NULL;
	u32 *match_v, *match_c;
	int err = 0;

	match_v = kzalloc(MLX5_ST_SZ_BYTES(fte_match_param), GFP_KERNEL);
	match_c = kzalloc(MLX5_ST_SZ_BYTES(fte_match_param), GFP_KERNEL);
	if (!match_v || !match_c) {
		esw_warn(esw->dev, "FDB: Failed to alloc match parameters\n");
		err = -ENOMEM;
		goto out;
	}

	dest.type = MLX5_FLOW_DESTINATION_TYPE_VPORT;
	dest.vport_num = 0;

	flow_rule = mlx5_add_flow_rule(esw->fdb_table.fdb, 0, match_c, match_v,
				       MLX5_FLOW_CONTEXT_ACTION_FWD_DEST, 0, &dest);
	if (IS_ERR(flow_rule)) {
		err = PTR_ERR(flow_rule);
		esw_warn(esw->dev,  "FDB: Failed to add miss flow rule err %d\n", err);
		goto out;
	}

	esw->fdb_table.offloads.miss_rule = flow_rule;
out:
	kfree(match_v);
	kfree(match_c);
	return err;
}

#define MAX_PF_SQ 256

int esw_create_offloads_fdb_table(struct mlx5_eswitch *esw, int nvports)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_core_dev *dev = esw->dev;
	struct mlx5_flow_namespace *root_ns;
	struct mlx5_flow_table *fdb = NULL;
	struct mlx5_flow_group *g;
	u32 *flow_group_in;
	void *match_criteria;
	int table_size, ix, err = 0;

	flow_group_in = mlx5_vzalloc(inlen);
	if (!flow_group_in)
		return -ENOMEM;

	root_ns = mlx5_get_flow_namespace(dev, MLX5_FLOW_NAMESPACE_FDB);
	if (!root_ns) {
		esw_warn(dev, "Failed to get FDB flow namespace\n");
		goto ns_err;
	}

	esw_debug(dev, "Create offloads FDB table, log_max_size(%d)\n",
		  MLX5_CAP_ESW_FLOWTABLE_FDB(dev, log_max_ft_size));

	table_size = nvports + MAX_PF_SQ + 1;
	fdb = mlx5_create_flow_table(root_ns, 0, table_size, 0);
	if (IS_ERR(fdb)) {
		err = PTR_ERR(fdb);
		esw_warn(dev, "Failed to create FDB Table err %d\n", err);
		goto fdb_err;
	}
	esw->fdb_table.fdb = fdb;

	/* create send-to-vport group */
	memset(flow_group_in, 0, inlen);
	MLX5_SET(create_flow_group_in, flow_group_in, match_criteria_enable,
		 MLX5_MATCH_MISC_PARAMETERS);

	match_criteria = MLX5_ADDR_OF(create_flow_group_in, flow_group_in, match_criteria);

	MLX5_SET_TO_ONES(fte_match_param, match_criteria, misc_parameters.source_sqn);
	MLX5_SET_TO_ONES(fte_match_param, match_criteria, misc_parameters.source_port);

	ix = nvports + MAX_PF_SQ;
	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, 0);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, ix - 1);

	g = mlx5_create_flow_group(fdb, flow_group_in);
	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		esw_warn(dev, "Failed to create send-to-vport flow group err(%d)\n", err);
		goto send_vport_err;
	}
	esw->fdb_table.offloads.send_to_vport_grp = g;

	/* create miss group */
	memset(flow_group_in, 0, inlen);
	MLX5_SET(create_flow_group_in, flow_group_in, match_criteria_enable, 0);

	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, ix);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, ix + 1);

	g = mlx5_create_flow_group(fdb, flow_group_in);
	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		esw_warn(dev, "Failed to create miss flow group err(%d)\n", err);
		goto miss_err;
	}
	esw->fdb_table.offloads.miss_grp = g;

	err = esw_add_fdb_miss_rule(esw);
	if (err)
		goto miss_rule_err;

	return 0;

miss_rule_err:
	mlx5_destroy_flow_group(esw->fdb_table.offloads.miss_grp);
miss_err:
	mlx5_destroy_flow_group(esw->fdb_table.offloads.send_to_vport_grp);
send_vport_err:
	mlx5_destroy_flow_table(fdb);
fdb_err:
ns_err:
	kvfree(flow_group_in);
	return err;
}

void esw_destroy_offloads_fdb_table(struct mlx5_eswitch *esw)
{
	if (!esw->fdb_table.fdb)
		return;

	esw_debug(esw->dev, "Destroy offloads FDB Table\n");
	mlx5_del_flow_rule(esw->fdb_table.offloads.miss_rule);
	mlx5_destroy_flow_group(esw->fdb_table.offloads.send_to_vport_grp);
	mlx5_destroy_flow_group(esw->fdb_table.offloads.miss_grp);

	mlx5_destroy_flow_table(esw->fdb_table.fdb);
}
