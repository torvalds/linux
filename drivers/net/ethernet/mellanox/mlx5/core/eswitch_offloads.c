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

enum {
	FDB_FAST_PATH = 0,
	FDB_SLOW_PATH
};

struct mlx5_flow_handle *
mlx5_eswitch_add_offloaded_rule(struct mlx5_eswitch *esw,
				struct mlx5_flow_spec *spec,
				struct mlx5_esw_flow_attr *attr)
{
	struct mlx5_flow_destination dest[2] = {};
	struct mlx5_flow_act flow_act = {0};
	struct mlx5_fc *counter = NULL;
	struct mlx5_flow_handle *rule;
	void *misc;
	int i = 0;

	if (esw->mode != SRIOV_OFFLOADS)
		return ERR_PTR(-EOPNOTSUPP);

	/* per flow vlan pop/push is emulated, don't set that into the firmware */
	flow_act.action = attr->action & ~(MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH | MLX5_FLOW_CONTEXT_ACTION_VLAN_POP);

	if (flow_act.action & MLX5_FLOW_CONTEXT_ACTION_FWD_DEST) {
		dest[i].type = MLX5_FLOW_DESTINATION_TYPE_VPORT;
		dest[i].vport_num = attr->out_rep->vport;
		i++;
	}
	if (flow_act.action & MLX5_FLOW_CONTEXT_ACTION_COUNT) {
		counter = mlx5_fc_create(esw->dev, true);
		if (IS_ERR(counter)) {
			rule = ERR_CAST(counter);
			goto err_counter_alloc;
		}
		dest[i].type = MLX5_FLOW_DESTINATION_TYPE_COUNTER;
		dest[i].counter = counter;
		i++;
	}

	misc = MLX5_ADDR_OF(fte_match_param, spec->match_value, misc_parameters);
	MLX5_SET(fte_match_set_misc, misc, source_port, attr->in_rep->vport);

	misc = MLX5_ADDR_OF(fte_match_param, spec->match_criteria, misc_parameters);
	MLX5_SET_TO_ONES(fte_match_set_misc, misc, source_port);

	spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS |
				      MLX5_MATCH_MISC_PARAMETERS;
	if (flow_act.action & MLX5_FLOW_CONTEXT_ACTION_DECAP)
		spec->match_criteria_enable |= MLX5_MATCH_INNER_HEADERS;

	if (attr->action & MLX5_FLOW_CONTEXT_ACTION_MOD_HDR)
		flow_act.modify_id = attr->mod_hdr_id;

	if (attr->action & MLX5_FLOW_CONTEXT_ACTION_ENCAP)
		flow_act.encap_id = attr->encap_id;

	rule = mlx5_add_flow_rules((struct mlx5_flow_table *)esw->fdb_table.fdb,
				   spec, &flow_act, dest, i);
	if (IS_ERR(rule))
		goto err_add_rule;
	else
		esw->offloads.num_flows++;

	return rule;

err_add_rule:
	mlx5_fc_destroy(esw->dev, counter);
err_counter_alloc:
	return rule;
}

void
mlx5_eswitch_del_offloaded_rule(struct mlx5_eswitch *esw,
				struct mlx5_flow_handle *rule,
				struct mlx5_esw_flow_attr *attr)
{
	struct mlx5_fc *counter = NULL;

	counter = mlx5_flow_rule_counter(rule);
	mlx5_del_flow_rules(rule);
	mlx5_fc_destroy(esw->dev, counter);
	esw->offloads.num_flows--;
}

static int esw_set_global_vlan_pop(struct mlx5_eswitch *esw, u8 val)
{
	struct mlx5_eswitch_rep *rep;
	int vf_vport, err = 0;

	esw_debug(esw->dev, "%s applying global %s policy\n", __func__, val ? "pop" : "none");
	for (vf_vport = 1; vf_vport < esw->enabled_vports; vf_vport++) {
		rep = &esw->offloads.vport_reps[vf_vport];
		if (!rep->rep_if[REP_ETH].valid)
			continue;

		err = __mlx5_eswitch_set_vport_vlan(esw, rep->vport, 0, 0, val);
		if (err)
			goto out;
	}

out:
	return err;
}

static struct mlx5_eswitch_rep *
esw_vlan_action_get_vport(struct mlx5_esw_flow_attr *attr, bool push, bool pop)
{
	struct mlx5_eswitch_rep *in_rep, *out_rep, *vport = NULL;

	in_rep  = attr->in_rep;
	out_rep = attr->out_rep;

	if (push)
		vport = in_rep;
	else if (pop)
		vport = out_rep;
	else
		vport = in_rep;

	return vport;
}

static int esw_add_vlan_action_check(struct mlx5_esw_flow_attr *attr,
				     bool push, bool pop, bool fwd)
{
	struct mlx5_eswitch_rep *in_rep, *out_rep;

	if ((push || pop) && !fwd)
		goto out_notsupp;

	in_rep  = attr->in_rep;
	out_rep = attr->out_rep;

	if (push && in_rep->vport == FDB_UPLINK_VPORT)
		goto out_notsupp;

	if (pop && out_rep->vport == FDB_UPLINK_VPORT)
		goto out_notsupp;

	/* vport has vlan push configured, can't offload VF --> wire rules w.o it */
	if (!push && !pop && fwd)
		if (in_rep->vlan && out_rep->vport == FDB_UPLINK_VPORT)
			goto out_notsupp;

	/* protects against (1) setting rules with different vlans to push and
	 * (2) setting rules w.o vlans (attr->vlan = 0) && w. vlans to push (!= 0)
	 */
	if (push && in_rep->vlan_refcount && (in_rep->vlan != attr->vlan))
		goto out_notsupp;

	return 0;

out_notsupp:
	return -EOPNOTSUPP;
}

int mlx5_eswitch_add_vlan_action(struct mlx5_eswitch *esw,
				 struct mlx5_esw_flow_attr *attr)
{
	struct offloads_fdb *offloads = &esw->fdb_table.offloads;
	struct mlx5_eswitch_rep *vport = NULL;
	bool push, pop, fwd;
	int err = 0;

	push = !!(attr->action & MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH);
	pop  = !!(attr->action & MLX5_FLOW_CONTEXT_ACTION_VLAN_POP);
	fwd  = !!(attr->action & MLX5_FLOW_CONTEXT_ACTION_FWD_DEST);

	err = esw_add_vlan_action_check(attr, push, pop, fwd);
	if (err)
		return err;

	attr->vlan_handled = false;

	vport = esw_vlan_action_get_vport(attr, push, pop);

	if (!push && !pop && fwd) {
		/* tracks VF --> wire rules without vlan push action */
		if (attr->out_rep->vport == FDB_UPLINK_VPORT) {
			vport->vlan_refcount++;
			attr->vlan_handled = true;
		}

		return 0;
	}

	if (!push && !pop)
		return 0;

	if (!(offloads->vlan_push_pop_refcount)) {
		/* it's the 1st vlan rule, apply global vlan pop policy */
		err = esw_set_global_vlan_pop(esw, SET_VLAN_STRIP);
		if (err)
			goto out;
	}
	offloads->vlan_push_pop_refcount++;

	if (push) {
		if (vport->vlan_refcount)
			goto skip_set_push;

		err = __mlx5_eswitch_set_vport_vlan(esw, vport->vport, attr->vlan, 0,
						    SET_VLAN_INSERT | SET_VLAN_STRIP);
		if (err)
			goto out;
		vport->vlan = attr->vlan;
skip_set_push:
		vport->vlan_refcount++;
	}
out:
	if (!err)
		attr->vlan_handled = true;
	return err;
}

int mlx5_eswitch_del_vlan_action(struct mlx5_eswitch *esw,
				 struct mlx5_esw_flow_attr *attr)
{
	struct offloads_fdb *offloads = &esw->fdb_table.offloads;
	struct mlx5_eswitch_rep *vport = NULL;
	bool push, pop, fwd;
	int err = 0;

	if (!attr->vlan_handled)
		return 0;

	push = !!(attr->action & MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH);
	pop  = !!(attr->action & MLX5_FLOW_CONTEXT_ACTION_VLAN_POP);
	fwd  = !!(attr->action & MLX5_FLOW_CONTEXT_ACTION_FWD_DEST);

	vport = esw_vlan_action_get_vport(attr, push, pop);

	if (!push && !pop && fwd) {
		/* tracks VF --> wire rules without vlan push action */
		if (attr->out_rep->vport == FDB_UPLINK_VPORT)
			vport->vlan_refcount--;

		return 0;
	}

	if (push) {
		vport->vlan_refcount--;
		if (vport->vlan_refcount)
			goto skip_unset_push;

		vport->vlan = 0;
		err = __mlx5_eswitch_set_vport_vlan(esw, vport->vport,
						    0, 0, SET_VLAN_STRIP);
		if (err)
			goto out;
	}

skip_unset_push:
	offloads->vlan_push_pop_refcount--;
	if (offloads->vlan_push_pop_refcount)
		return 0;

	/* no more vlan rules, stop global vlan pop policy */
	err = esw_set_global_vlan_pop(esw, 0);

out:
	return err;
}

struct mlx5_flow_handle *
mlx5_eswitch_add_send_to_vport_rule(struct mlx5_eswitch *esw, int vport, u32 sqn)
{
	struct mlx5_flow_act flow_act = {0};
	struct mlx5_flow_destination dest = {};
	struct mlx5_flow_handle *flow_rule;
	struct mlx5_flow_spec *spec;
	void *misc;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec) {
		flow_rule = ERR_PTR(-ENOMEM);
		goto out;
	}

	misc = MLX5_ADDR_OF(fte_match_param, spec->match_value, misc_parameters);
	MLX5_SET(fte_match_set_misc, misc, source_sqn, sqn);
	MLX5_SET(fte_match_set_misc, misc, source_port, 0x0); /* source vport is 0 */

	misc = MLX5_ADDR_OF(fte_match_param, spec->match_criteria, misc_parameters);
	MLX5_SET_TO_ONES(fte_match_set_misc, misc, source_sqn);
	MLX5_SET_TO_ONES(fte_match_set_misc, misc, source_port);

	spec->match_criteria_enable = MLX5_MATCH_MISC_PARAMETERS;
	dest.type = MLX5_FLOW_DESTINATION_TYPE_VPORT;
	dest.vport_num = vport;
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;

	flow_rule = mlx5_add_flow_rules(esw->fdb_table.offloads.fdb, spec,
					&flow_act, &dest, 1);
	if (IS_ERR(flow_rule))
		esw_warn(esw->dev, "FDB: Failed to add send to vport rule err %ld\n", PTR_ERR(flow_rule));
out:
	kvfree(spec);
	return flow_rule;
}

void mlx5_eswitch_del_send_to_vport_rule(struct mlx5_flow_handle *rule)
{
	mlx5_del_flow_rules(rule);
}

static int esw_add_fdb_miss_rule(struct mlx5_eswitch *esw)
{
	struct mlx5_flow_act flow_act = {0};
	struct mlx5_flow_destination dest = {};
	struct mlx5_flow_handle *flow_rule = NULL;
	struct mlx5_flow_spec *spec;
	int err = 0;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec) {
		err = -ENOMEM;
		goto out;
	}

	dest.type = MLX5_FLOW_DESTINATION_TYPE_VPORT;
	dest.vport_num = 0;
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;

	flow_rule = mlx5_add_flow_rules(esw->fdb_table.offloads.fdb, spec,
					&flow_act, &dest, 1);
	if (IS_ERR(flow_rule)) {
		err = PTR_ERR(flow_rule);
		esw_warn(esw->dev,  "FDB: Failed to add miss flow rule err %d\n", err);
		goto out;
	}

	esw->fdb_table.offloads.miss_rule = flow_rule;
out:
	kvfree(spec);
	return err;
}

#define ESW_OFFLOADS_NUM_GROUPS  4

static int esw_create_offloads_fast_fdb_table(struct mlx5_eswitch *esw)
{
	struct mlx5_core_dev *dev = esw->dev;
	struct mlx5_flow_namespace *root_ns;
	struct mlx5_flow_table *fdb = NULL;
	int esw_size, err = 0;
	u32 flags = 0;
	u32 max_flow_counter = (MLX5_CAP_GEN(dev, max_flow_counter_31_16) << 16) |
				MLX5_CAP_GEN(dev, max_flow_counter_15_0);

	root_ns = mlx5_get_flow_namespace(dev, MLX5_FLOW_NAMESPACE_FDB);
	if (!root_ns) {
		esw_warn(dev, "Failed to get FDB flow namespace\n");
		err = -EOPNOTSUPP;
		goto out;
	}

	esw_debug(dev, "Create offloads FDB table, min (max esw size(2^%d), max counters(%d)*groups(%d))\n",
		  MLX5_CAP_ESW_FLOWTABLE_FDB(dev, log_max_ft_size),
		  max_flow_counter, ESW_OFFLOADS_NUM_GROUPS);

	esw_size = min_t(int, max_flow_counter * ESW_OFFLOADS_NUM_GROUPS,
			 1 << MLX5_CAP_ESW_FLOWTABLE_FDB(dev, log_max_ft_size));

	if (esw->offloads.encap != DEVLINK_ESWITCH_ENCAP_MODE_NONE)
		flags |= MLX5_FLOW_TABLE_TUNNEL_EN;

	fdb = mlx5_create_auto_grouped_flow_table(root_ns, FDB_FAST_PATH,
						  esw_size,
						  ESW_OFFLOADS_NUM_GROUPS, 0,
						  flags);
	if (IS_ERR(fdb)) {
		err = PTR_ERR(fdb);
		esw_warn(dev, "Failed to create Fast path FDB Table err %d\n", err);
		goto out;
	}
	esw->fdb_table.fdb = fdb;

out:
	return err;
}

static void esw_destroy_offloads_fast_fdb_table(struct mlx5_eswitch *esw)
{
	mlx5_destroy_flow_table(esw->fdb_table.fdb);
}

#define MAX_PF_SQ 256

static int esw_create_offloads_fdb_tables(struct mlx5_eswitch *esw, int nvports)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_core_dev *dev = esw->dev;
	struct mlx5_flow_namespace *root_ns;
	struct mlx5_flow_table *fdb = NULL;
	int table_size, ix, err = 0;
	struct mlx5_flow_group *g;
	void *match_criteria;
	u32 *flow_group_in;

	esw_debug(esw->dev, "Create offloads FDB Tables\n");
	flow_group_in = kvzalloc(inlen, GFP_KERNEL);
	if (!flow_group_in)
		return -ENOMEM;

	root_ns = mlx5_get_flow_namespace(dev, MLX5_FLOW_NAMESPACE_FDB);
	if (!root_ns) {
		esw_warn(dev, "Failed to get FDB flow namespace\n");
		err = -EOPNOTSUPP;
		goto ns_err;
	}

	err = esw_create_offloads_fast_fdb_table(esw);
	if (err)
		goto fast_fdb_err;

	table_size = nvports + MAX_PF_SQ + 1;

	ft_attr.max_fte = table_size;
	ft_attr.prio = FDB_SLOW_PATH;

	fdb = mlx5_create_flow_table(root_ns, &ft_attr);
	if (IS_ERR(fdb)) {
		err = PTR_ERR(fdb);
		esw_warn(dev, "Failed to create slow path FDB Table err %d\n", err);
		goto slow_fdb_err;
	}
	esw->fdb_table.offloads.fdb = fdb;

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
	mlx5_destroy_flow_table(esw->fdb_table.offloads.fdb);
slow_fdb_err:
	mlx5_destroy_flow_table(esw->fdb_table.fdb);
fast_fdb_err:
ns_err:
	kvfree(flow_group_in);
	return err;
}

static void esw_destroy_offloads_fdb_tables(struct mlx5_eswitch *esw)
{
	if (!esw->fdb_table.fdb)
		return;

	esw_debug(esw->dev, "Destroy offloads FDB Tables\n");
	mlx5_del_flow_rules(esw->fdb_table.offloads.miss_rule);
	mlx5_destroy_flow_group(esw->fdb_table.offloads.send_to_vport_grp);
	mlx5_destroy_flow_group(esw->fdb_table.offloads.miss_grp);

	mlx5_destroy_flow_table(esw->fdb_table.offloads.fdb);
	esw_destroy_offloads_fast_fdb_table(esw);
}

static int esw_create_offloads_table(struct mlx5_eswitch *esw)
{
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_core_dev *dev = esw->dev;
	struct mlx5_flow_table *ft_offloads;
	struct mlx5_flow_namespace *ns;
	int err = 0;

	ns = mlx5_get_flow_namespace(dev, MLX5_FLOW_NAMESPACE_OFFLOADS);
	if (!ns) {
		esw_warn(esw->dev, "Failed to get offloads flow namespace\n");
		return -EOPNOTSUPP;
	}

	ft_attr.max_fte = dev->priv.sriov.num_vfs + 2;

	ft_offloads = mlx5_create_flow_table(ns, &ft_attr);
	if (IS_ERR(ft_offloads)) {
		err = PTR_ERR(ft_offloads);
		esw_warn(esw->dev, "Failed to create offloads table, err %d\n", err);
		return err;
	}

	esw->offloads.ft_offloads = ft_offloads;
	return 0;
}

static void esw_destroy_offloads_table(struct mlx5_eswitch *esw)
{
	struct mlx5_esw_offload *offloads = &esw->offloads;

	mlx5_destroy_flow_table(offloads->ft_offloads);
}

static int esw_create_vport_rx_group(struct mlx5_eswitch *esw)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_flow_group *g;
	struct mlx5_priv *priv = &esw->dev->priv;
	u32 *flow_group_in;
	void *match_criteria, *misc;
	int err = 0;
	int nvports = priv->sriov.num_vfs + 2;

	flow_group_in = kvzalloc(inlen, GFP_KERNEL);
	if (!flow_group_in)
		return -ENOMEM;

	/* create vport rx group */
	memset(flow_group_in, 0, inlen);
	MLX5_SET(create_flow_group_in, flow_group_in, match_criteria_enable,
		 MLX5_MATCH_MISC_PARAMETERS);

	match_criteria = MLX5_ADDR_OF(create_flow_group_in, flow_group_in, match_criteria);
	misc = MLX5_ADDR_OF(fte_match_param, match_criteria, misc_parameters);
	MLX5_SET_TO_ONES(fte_match_set_misc, misc, source_port);

	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, 0);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, nvports - 1);

	g = mlx5_create_flow_group(esw->offloads.ft_offloads, flow_group_in);

	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		mlx5_core_warn(esw->dev, "Failed to create vport rx group err %d\n", err);
		goto out;
	}

	esw->offloads.vport_rx_group = g;
out:
	kfree(flow_group_in);
	return err;
}

static void esw_destroy_vport_rx_group(struct mlx5_eswitch *esw)
{
	mlx5_destroy_flow_group(esw->offloads.vport_rx_group);
}

struct mlx5_flow_handle *
mlx5_eswitch_create_vport_rx_rule(struct mlx5_eswitch *esw, int vport, u32 tirn)
{
	struct mlx5_flow_act flow_act = {0};
	struct mlx5_flow_destination dest = {};
	struct mlx5_flow_handle *flow_rule;
	struct mlx5_flow_spec *spec;
	void *misc;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec) {
		flow_rule = ERR_PTR(-ENOMEM);
		goto out;
	}

	misc = MLX5_ADDR_OF(fte_match_param, spec->match_value, misc_parameters);
	MLX5_SET(fte_match_set_misc, misc, source_port, vport);

	misc = MLX5_ADDR_OF(fte_match_param, spec->match_criteria, misc_parameters);
	MLX5_SET_TO_ONES(fte_match_set_misc, misc, source_port);

	spec->match_criteria_enable = MLX5_MATCH_MISC_PARAMETERS;
	dest.type = MLX5_FLOW_DESTINATION_TYPE_TIR;
	dest.tir_num = tirn;

	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	flow_rule = mlx5_add_flow_rules(esw->offloads.ft_offloads, spec,
					&flow_act, &dest, 1);
	if (IS_ERR(flow_rule)) {
		esw_warn(esw->dev, "fs offloads: Failed to add vport rx rule err %ld\n", PTR_ERR(flow_rule));
		goto out;
	}

out:
	kvfree(spec);
	return flow_rule;
}

static int esw_offloads_start(struct mlx5_eswitch *esw)
{
	int err, err1, num_vfs = esw->dev->priv.sriov.num_vfs;

	if (esw->mode != SRIOV_LEGACY) {
		esw_warn(esw->dev, "Can't set offloads mode, SRIOV legacy not enabled\n");
		return -EINVAL;
	}

	mlx5_eswitch_disable_sriov(esw);
	err = mlx5_eswitch_enable_sriov(esw, num_vfs, SRIOV_OFFLOADS);
	if (err) {
		esw_warn(esw->dev, "Failed setting eswitch to offloads, err %d\n", err);
		err1 = mlx5_eswitch_enable_sriov(esw, num_vfs, SRIOV_LEGACY);
		if (err1)
			esw_warn(esw->dev, "Failed setting eswitch back to legacy, err %d\n", err1);
	}
	if (esw->offloads.inline_mode == MLX5_INLINE_MODE_NONE) {
		if (mlx5_eswitch_inline_mode_get(esw,
						 num_vfs,
						 &esw->offloads.inline_mode)) {
			esw->offloads.inline_mode = MLX5_INLINE_MODE_L2;
			esw_warn(esw->dev, "Inline mode is different between vports\n");
		}
	}
	return err;
}

void esw_offloads_cleanup_reps(struct mlx5_eswitch *esw)
{
	kfree(esw->offloads.vport_reps);
}

int esw_offloads_init_reps(struct mlx5_eswitch *esw)
{
	int total_vfs = MLX5_TOTAL_VPORTS(esw->dev);
	struct mlx5_core_dev *dev = esw->dev;
	struct mlx5_esw_offload *offloads;
	struct mlx5_eswitch_rep *rep;
	u8 hw_id[ETH_ALEN];
	int vport;

	esw->offloads.vport_reps = kcalloc(total_vfs,
					   sizeof(struct mlx5_eswitch_rep),
					   GFP_KERNEL);
	if (!esw->offloads.vport_reps)
		return -ENOMEM;

	offloads = &esw->offloads;
	mlx5_query_nic_vport_mac_address(dev, 0, hw_id);

	for (vport = 0; vport < total_vfs; vport++) {
		rep = &offloads->vport_reps[vport];

		rep->vport = vport;
		ether_addr_copy(rep->hw_id, hw_id);
	}

	offloads->vport_reps[0].vport = FDB_UPLINK_VPORT;

	return 0;
}

static void esw_offloads_unload_reps_type(struct mlx5_eswitch *esw, int nvports,
					  u8 rep_type)
{
	struct mlx5_eswitch_rep *rep;
	int vport;

	for (vport = nvports - 1; vport >= 0; vport--) {
		rep = &esw->offloads.vport_reps[vport];
		if (!rep->rep_if[rep_type].valid)
			continue;

		rep->rep_if[rep_type].unload(rep);
	}
}

static void esw_offloads_unload_reps(struct mlx5_eswitch *esw, int nvports)
{
	u8 rep_type = NUM_REP_TYPES;

	while (rep_type-- > 0)
		esw_offloads_unload_reps_type(esw, nvports, rep_type);
}

static int esw_offloads_load_reps_type(struct mlx5_eswitch *esw, int nvports,
				       u8 rep_type)
{
	struct mlx5_eswitch_rep *rep;
	int vport;
	int err;

	for (vport = 0; vport < nvports; vport++) {
		rep = &esw->offloads.vport_reps[vport];
		if (!rep->rep_if[rep_type].valid)
			continue;

		err = rep->rep_if[rep_type].load(esw->dev, rep);
		if (err)
			goto err_reps;
	}

	return 0;

err_reps:
	esw_offloads_unload_reps_type(esw, vport, rep_type);
	return err;
}

static int esw_offloads_load_reps(struct mlx5_eswitch *esw, int nvports)
{
	u8 rep_type = 0;
	int err;

	for (rep_type = 0; rep_type < NUM_REP_TYPES; rep_type++) {
		err = esw_offloads_load_reps_type(esw, nvports, rep_type);
		if (err)
			goto err_reps;
	}

	return err;

err_reps:
	while (rep_type-- > 0)
		esw_offloads_unload_reps_type(esw, nvports, rep_type);
	return err;
}

int esw_offloads_init(struct mlx5_eswitch *esw, int nvports)
{
	int err;

	/* disable PF RoCE so missed packets don't go through RoCE steering */
	mlx5_dev_list_lock();
	mlx5_remove_dev_by_protocol(esw->dev, MLX5_INTERFACE_PROTOCOL_IB);
	mlx5_dev_list_unlock();

	err = esw_create_offloads_fdb_tables(esw, nvports);
	if (err)
		goto create_fdb_err;

	err = esw_create_offloads_table(esw);
	if (err)
		goto create_ft_err;

	err = esw_create_vport_rx_group(esw);
	if (err)
		goto create_fg_err;

	err = esw_offloads_load_reps(esw, nvports);
	if (err)
		goto err_reps;

	return 0;

err_reps:
	esw_destroy_vport_rx_group(esw);

create_fg_err:
	esw_destroy_offloads_table(esw);

create_ft_err:
	esw_destroy_offloads_fdb_tables(esw);

create_fdb_err:
	/* enable back PF RoCE */
	mlx5_dev_list_lock();
	mlx5_add_dev_by_protocol(esw->dev, MLX5_INTERFACE_PROTOCOL_IB);
	mlx5_dev_list_unlock();

	return err;
}

static int esw_offloads_stop(struct mlx5_eswitch *esw)
{
	int err, err1, num_vfs = esw->dev->priv.sriov.num_vfs;

	mlx5_eswitch_disable_sriov(esw);
	err = mlx5_eswitch_enable_sriov(esw, num_vfs, SRIOV_LEGACY);
	if (err) {
		esw_warn(esw->dev, "Failed setting eswitch to legacy, err %d\n", err);
		err1 = mlx5_eswitch_enable_sriov(esw, num_vfs, SRIOV_OFFLOADS);
		if (err1)
			esw_warn(esw->dev, "Failed setting eswitch back to offloads, err %d\n", err);
	}

	/* enable back PF RoCE */
	mlx5_dev_list_lock();
	mlx5_add_dev_by_protocol(esw->dev, MLX5_INTERFACE_PROTOCOL_IB);
	mlx5_dev_list_unlock();

	return err;
}

void esw_offloads_cleanup(struct mlx5_eswitch *esw, int nvports)
{
	esw_offloads_unload_reps(esw, nvports);
	esw_destroy_vport_rx_group(esw);
	esw_destroy_offloads_table(esw);
	esw_destroy_offloads_fdb_tables(esw);
}

static int esw_mode_from_devlink(u16 mode, u16 *mlx5_mode)
{
	switch (mode) {
	case DEVLINK_ESWITCH_MODE_LEGACY:
		*mlx5_mode = SRIOV_LEGACY;
		break;
	case DEVLINK_ESWITCH_MODE_SWITCHDEV:
		*mlx5_mode = SRIOV_OFFLOADS;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int esw_mode_to_devlink(u16 mlx5_mode, u16 *mode)
{
	switch (mlx5_mode) {
	case SRIOV_LEGACY:
		*mode = DEVLINK_ESWITCH_MODE_LEGACY;
		break;
	case SRIOV_OFFLOADS:
		*mode = DEVLINK_ESWITCH_MODE_SWITCHDEV;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int esw_inline_mode_from_devlink(u8 mode, u8 *mlx5_mode)
{
	switch (mode) {
	case DEVLINK_ESWITCH_INLINE_MODE_NONE:
		*mlx5_mode = MLX5_INLINE_MODE_NONE;
		break;
	case DEVLINK_ESWITCH_INLINE_MODE_LINK:
		*mlx5_mode = MLX5_INLINE_MODE_L2;
		break;
	case DEVLINK_ESWITCH_INLINE_MODE_NETWORK:
		*mlx5_mode = MLX5_INLINE_MODE_IP;
		break;
	case DEVLINK_ESWITCH_INLINE_MODE_TRANSPORT:
		*mlx5_mode = MLX5_INLINE_MODE_TCP_UDP;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int esw_inline_mode_to_devlink(u8 mlx5_mode, u8 *mode)
{
	switch (mlx5_mode) {
	case MLX5_INLINE_MODE_NONE:
		*mode = DEVLINK_ESWITCH_INLINE_MODE_NONE;
		break;
	case MLX5_INLINE_MODE_L2:
		*mode = DEVLINK_ESWITCH_INLINE_MODE_LINK;
		break;
	case MLX5_INLINE_MODE_IP:
		*mode = DEVLINK_ESWITCH_INLINE_MODE_NETWORK;
		break;
	case MLX5_INLINE_MODE_TCP_UDP:
		*mode = DEVLINK_ESWITCH_INLINE_MODE_TRANSPORT;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int mlx5_devlink_eswitch_check(struct devlink *devlink)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);

	if (MLX5_CAP_GEN(dev, port_type) != MLX5_CAP_PORT_TYPE_ETH)
		return -EOPNOTSUPP;

	if (!MLX5_CAP_GEN(dev, vport_group_manager))
		return -EOPNOTSUPP;

	if (dev->priv.eswitch->mode == SRIOV_NONE)
		return -EOPNOTSUPP;

	return 0;
}

int mlx5_devlink_eswitch_mode_set(struct devlink *devlink, u16 mode)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	u16 cur_mlx5_mode, mlx5_mode = 0;
	int err;

	err = mlx5_devlink_eswitch_check(devlink);
	if (err)
		return err;

	cur_mlx5_mode = dev->priv.eswitch->mode;

	if (esw_mode_from_devlink(mode, &mlx5_mode))
		return -EINVAL;

	if (cur_mlx5_mode == mlx5_mode)
		return 0;

	if (mode == DEVLINK_ESWITCH_MODE_SWITCHDEV)
		return esw_offloads_start(dev->priv.eswitch);
	else if (mode == DEVLINK_ESWITCH_MODE_LEGACY)
		return esw_offloads_stop(dev->priv.eswitch);
	else
		return -EINVAL;
}

int mlx5_devlink_eswitch_mode_get(struct devlink *devlink, u16 *mode)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	int err;

	err = mlx5_devlink_eswitch_check(devlink);
	if (err)
		return err;

	return esw_mode_to_devlink(dev->priv.eswitch->mode, mode);
}

int mlx5_devlink_eswitch_inline_mode_set(struct devlink *devlink, u8 mode)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	struct mlx5_eswitch *esw = dev->priv.eswitch;
	int err, vport;
	u8 mlx5_mode;

	err = mlx5_devlink_eswitch_check(devlink);
	if (err)
		return err;

	switch (MLX5_CAP_ETH(dev, wqe_inline_mode)) {
	case MLX5_CAP_INLINE_MODE_NOT_REQUIRED:
		if (mode == DEVLINK_ESWITCH_INLINE_MODE_NONE)
			return 0;
		/* fall through */
	case MLX5_CAP_INLINE_MODE_L2:
		esw_warn(dev, "Inline mode can't be set\n");
		return -EOPNOTSUPP;
	case MLX5_CAP_INLINE_MODE_VPORT_CONTEXT:
		break;
	}

	if (esw->offloads.num_flows > 0) {
		esw_warn(dev, "Can't set inline mode when flows are configured\n");
		return -EOPNOTSUPP;
	}

	err = esw_inline_mode_from_devlink(mode, &mlx5_mode);
	if (err)
		goto out;

	for (vport = 1; vport < esw->enabled_vports; vport++) {
		err = mlx5_modify_nic_vport_min_inline(dev, vport, mlx5_mode);
		if (err) {
			esw_warn(dev, "Failed to set min inline on vport %d\n",
				 vport);
			goto revert_inline_mode;
		}
	}

	esw->offloads.inline_mode = mlx5_mode;
	return 0;

revert_inline_mode:
	while (--vport > 0)
		mlx5_modify_nic_vport_min_inline(dev,
						 vport,
						 esw->offloads.inline_mode);
out:
	return err;
}

int mlx5_devlink_eswitch_inline_mode_get(struct devlink *devlink, u8 *mode)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	struct mlx5_eswitch *esw = dev->priv.eswitch;
	int err;

	err = mlx5_devlink_eswitch_check(devlink);
	if (err)
		return err;

	return esw_inline_mode_to_devlink(esw->offloads.inline_mode, mode);
}

int mlx5_eswitch_inline_mode_get(struct mlx5_eswitch *esw, int nvfs, u8 *mode)
{
	u8 prev_mlx5_mode, mlx5_mode = MLX5_INLINE_MODE_L2;
	struct mlx5_core_dev *dev = esw->dev;
	int vport;

	if (!MLX5_CAP_GEN(dev, vport_group_manager))
		return -EOPNOTSUPP;

	if (esw->mode == SRIOV_NONE)
		return -EOPNOTSUPP;

	switch (MLX5_CAP_ETH(dev, wqe_inline_mode)) {
	case MLX5_CAP_INLINE_MODE_NOT_REQUIRED:
		mlx5_mode = MLX5_INLINE_MODE_NONE;
		goto out;
	case MLX5_CAP_INLINE_MODE_L2:
		mlx5_mode = MLX5_INLINE_MODE_L2;
		goto out;
	case MLX5_CAP_INLINE_MODE_VPORT_CONTEXT:
		goto query_vports;
	}

query_vports:
	for (vport = 1; vport <= nvfs; vport++) {
		mlx5_query_nic_vport_min_inline(dev, vport, &mlx5_mode);
		if (vport > 1 && prev_mlx5_mode != mlx5_mode)
			return -EINVAL;
		prev_mlx5_mode = mlx5_mode;
	}

out:
	*mode = mlx5_mode;
	return 0;
}

int mlx5_devlink_eswitch_encap_mode_set(struct devlink *devlink, u8 encap)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	struct mlx5_eswitch *esw = dev->priv.eswitch;
	int err;

	err = mlx5_devlink_eswitch_check(devlink);
	if (err)
		return err;

	if (encap != DEVLINK_ESWITCH_ENCAP_MODE_NONE &&
	    (!MLX5_CAP_ESW_FLOWTABLE_FDB(dev, encap) ||
	     !MLX5_CAP_ESW_FLOWTABLE_FDB(dev, decap)))
		return -EOPNOTSUPP;

	if (encap && encap != DEVLINK_ESWITCH_ENCAP_MODE_BASIC)
		return -EOPNOTSUPP;

	if (esw->mode == SRIOV_LEGACY) {
		esw->offloads.encap = encap;
		return 0;
	}

	if (esw->offloads.encap == encap)
		return 0;

	if (esw->offloads.num_flows > 0) {
		esw_warn(dev, "Can't set encapsulation when flows are configured\n");
		return -EOPNOTSUPP;
	}

	esw_destroy_offloads_fast_fdb_table(esw);

	esw->offloads.encap = encap;
	err = esw_create_offloads_fast_fdb_table(esw);
	if (err) {
		esw_warn(esw->dev, "Failed re-creating fast FDB table, err %d\n", err);
		esw->offloads.encap = !encap;
		(void)esw_create_offloads_fast_fdb_table(esw);
	}
	return err;
}

int mlx5_devlink_eswitch_encap_mode_get(struct devlink *devlink, u8 *encap)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	struct mlx5_eswitch *esw = dev->priv.eswitch;
	int err;

	err = mlx5_devlink_eswitch_check(devlink);
	if (err)
		return err;

	*encap = esw->offloads.encap;
	return 0;
}

void mlx5_eswitch_register_vport_rep(struct mlx5_eswitch *esw,
				     int vport_index,
				     struct mlx5_eswitch_rep_if *__rep_if,
				     u8 rep_type)
{
	struct mlx5_esw_offload *offloads = &esw->offloads;
	struct mlx5_eswitch_rep_if *rep_if;

	rep_if = &offloads->vport_reps[vport_index].rep_if[rep_type];

	rep_if->load   = __rep_if->load;
	rep_if->unload = __rep_if->unload;
	rep_if->priv = __rep_if->priv;

	rep_if->valid = true;
}

void mlx5_eswitch_unregister_vport_rep(struct mlx5_eswitch *esw,
				       int vport_index, u8 rep_type)
{
	struct mlx5_esw_offload *offloads = &esw->offloads;
	struct mlx5_eswitch_rep *rep;

	rep = &offloads->vport_reps[vport_index];

	if (esw->mode == SRIOV_OFFLOADS && esw->vports[vport_index].enabled)
		rep->rep_if[rep_type].unload(rep);

	rep->rep_if[rep_type].valid = false;
}

void *mlx5_eswitch_get_uplink_priv(struct mlx5_eswitch *esw, u8 rep_type)
{
#define UPLINK_REP_INDEX 0
	struct mlx5_esw_offload *offloads = &esw->offloads;
	struct mlx5_eswitch_rep *rep;

	rep = &offloads->vport_reps[UPLINK_REP_INDEX];
	return rep->rep_if[rep_type].priv;
}
