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
#include <linux/idr.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/mlx5_ifc.h>
#include <linux/mlx5/vport.h>
#include <linux/mlx5/fs.h>
#include "mlx5_core.h"
#include "eswitch.h"
#include "esw/acl/ofld.h"
#include "rdma.h"
#include "en.h"
#include "fs_core.h"
#include "lib/devcom.h"
#include "lib/eq.h"
#include "lib/fs_chains.h"
#include "en_tc.h"

/* There are two match-all miss flows, one for unicast dst mac and
 * one for multicast.
 */
#define MLX5_ESW_MISS_FLOWS (2)
#define UPLINK_REP_INDEX 0

/* Per vport tables */

#define MLX5_ESW_VPORT_TABLE_SIZE 128

/* This struct is used as a key to the hash table and we need it to be packed
 * so hash result is consistent
 */
struct mlx5_vport_key {
	u32 chain;
	u16 prio;
	u16 vport;
	u16 vhca_id;
} __packed;

struct mlx5_vport_tbl_attr {
	u16 chain;
	u16 prio;
	u16 vport;
};

struct mlx5_vport_table {
	struct hlist_node hlist;
	struct mlx5_flow_table *fdb;
	u32 num_rules;
	struct mlx5_vport_key key;
};

#define MLX5_ESW_VPORT_TBL_NUM_GROUPS  4

static struct mlx5_flow_table *
esw_vport_tbl_create(struct mlx5_eswitch *esw, struct mlx5_flow_namespace *ns)
{
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_flow_table *fdb;

	ft_attr.autogroup.max_num_groups = MLX5_ESW_VPORT_TBL_NUM_GROUPS;
	ft_attr.max_fte = MLX5_ESW_VPORT_TABLE_SIZE;
	ft_attr.prio = FDB_PER_VPORT;
	fdb = mlx5_create_auto_grouped_flow_table(ns, &ft_attr);
	if (IS_ERR(fdb)) {
		esw_warn(esw->dev, "Failed to create per vport FDB Table err %ld\n",
			 PTR_ERR(fdb));
	}

	return fdb;
}

static u32 flow_attr_to_vport_key(struct mlx5_eswitch *esw,
				  struct mlx5_vport_tbl_attr *attr,
				  struct mlx5_vport_key *key)
{
	key->vport = attr->vport;
	key->chain = attr->chain;
	key->prio = attr->prio;
	key->vhca_id = MLX5_CAP_GEN(esw->dev, vhca_id);
	return jhash(key, sizeof(*key), 0);
}

/* caller must hold vports.lock */
static struct mlx5_vport_table *
esw_vport_tbl_lookup(struct mlx5_eswitch *esw, struct mlx5_vport_key *skey, u32 key)
{
	struct mlx5_vport_table *e;

	hash_for_each_possible(esw->fdb_table.offloads.vports.table, e, hlist, key)
		if (!memcmp(&e->key, skey, sizeof(*skey)))
			return e;

	return NULL;
}

static void
esw_vport_tbl_put(struct mlx5_eswitch *esw, struct mlx5_vport_tbl_attr *attr)
{
	struct mlx5_vport_table *e;
	struct mlx5_vport_key key;
	u32 hkey;

	mutex_lock(&esw->fdb_table.offloads.vports.lock);
	hkey = flow_attr_to_vport_key(esw, attr, &key);
	e = esw_vport_tbl_lookup(esw, &key, hkey);
	if (!e || --e->num_rules)
		goto out;

	hash_del(&e->hlist);
	mlx5_destroy_flow_table(e->fdb);
	kfree(e);
out:
	mutex_unlock(&esw->fdb_table.offloads.vports.lock);
}

static struct mlx5_flow_table *
esw_vport_tbl_get(struct mlx5_eswitch *esw, struct mlx5_vport_tbl_attr *attr)
{
	struct mlx5_core_dev *dev = esw->dev;
	struct mlx5_flow_namespace *ns;
	struct mlx5_flow_table *fdb;
	struct mlx5_vport_table *e;
	struct mlx5_vport_key skey;
	u32 hkey;

	mutex_lock(&esw->fdb_table.offloads.vports.lock);
	hkey = flow_attr_to_vport_key(esw, attr, &skey);
	e = esw_vport_tbl_lookup(esw, &skey, hkey);
	if (e) {
		e->num_rules++;
		goto out;
	}

	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (!e) {
		fdb = ERR_PTR(-ENOMEM);
		goto err_alloc;
	}

	ns = mlx5_get_flow_namespace(dev, MLX5_FLOW_NAMESPACE_FDB);
	if (!ns) {
		esw_warn(dev, "Failed to get FDB namespace\n");
		fdb = ERR_PTR(-ENOENT);
		goto err_ns;
	}

	fdb = esw_vport_tbl_create(esw, ns);
	if (IS_ERR(fdb))
		goto err_ns;

	e->fdb = fdb;
	e->num_rules = 1;
	e->key = skey;
	hash_add(esw->fdb_table.offloads.vports.table, &e->hlist, hkey);
out:
	mutex_unlock(&esw->fdb_table.offloads.vports.lock);
	return e->fdb;

err_ns:
	kfree(e);
err_alloc:
	mutex_unlock(&esw->fdb_table.offloads.vports.lock);
	return fdb;
}

int mlx5_esw_vport_tbl_get(struct mlx5_eswitch *esw)
{
	struct mlx5_vport_tbl_attr attr;
	struct mlx5_flow_table *fdb;
	struct mlx5_vport *vport;
	int i;

	attr.chain = 0;
	attr.prio = 1;
	mlx5_esw_for_all_vports(esw, i, vport) {
		attr.vport = vport->vport;
		fdb = esw_vport_tbl_get(esw, &attr);
		if (IS_ERR(fdb))
			goto out;
	}
	return 0;

out:
	mlx5_esw_vport_tbl_put(esw);
	return PTR_ERR(fdb);
}

void mlx5_esw_vport_tbl_put(struct mlx5_eswitch *esw)
{
	struct mlx5_vport_tbl_attr attr;
	struct mlx5_vport *vport;
	int i;

	attr.chain = 0;
	attr.prio = 1;
	mlx5_esw_for_all_vports(esw, i, vport) {
		attr.vport = vport->vport;
		esw_vport_tbl_put(esw, &attr);
	}
}

/* End: Per vport tables */

static struct mlx5_eswitch_rep *mlx5_eswitch_get_rep(struct mlx5_eswitch *esw,
						     u16 vport_num)
{
	int idx = mlx5_eswitch_vport_num_to_index(esw, vport_num);

	WARN_ON(idx > esw->total_vports - 1);
	return &esw->offloads.vport_reps[idx];
}

static void
mlx5_eswitch_set_rule_flow_source(struct mlx5_eswitch *esw,
				  struct mlx5_flow_spec *spec,
				  struct mlx5_esw_flow_attr *attr)
{
	if (MLX5_CAP_ESW_FLOWTABLE(esw->dev, flow_source) &&
	    attr && attr->in_rep && attr->in_rep->vport == MLX5_VPORT_UPLINK)
		spec->flow_context.flow_source = MLX5_FLOW_CONTEXT_FLOW_SOURCE_UPLINK;
}

static void
mlx5_eswitch_set_rule_source_port(struct mlx5_eswitch *esw,
				  struct mlx5_flow_spec *spec,
				  struct mlx5_esw_flow_attr *attr)
{
	void *misc2;
	void *misc;

	/* Use metadata matching because vport is not represented by single
	 * VHCA in dual-port RoCE mode, and matching on source vport may fail.
	 */
	if (mlx5_eswitch_vport_match_metadata_enabled(esw)) {
		misc2 = MLX5_ADDR_OF(fte_match_param, spec->match_value, misc_parameters_2);
		MLX5_SET(fte_match_set_misc2, misc2, metadata_reg_c_0,
			 mlx5_eswitch_get_vport_metadata_for_match(attr->in_mdev->priv.eswitch,
								   attr->in_rep->vport));

		misc2 = MLX5_ADDR_OF(fte_match_param, spec->match_criteria, misc_parameters_2);
		MLX5_SET(fte_match_set_misc2, misc2, metadata_reg_c_0,
			 mlx5_eswitch_get_vport_metadata_mask());

		spec->match_criteria_enable |= MLX5_MATCH_MISC_PARAMETERS_2;
	} else {
		misc = MLX5_ADDR_OF(fte_match_param, spec->match_value, misc_parameters);
		MLX5_SET(fte_match_set_misc, misc, source_port, attr->in_rep->vport);

		if (MLX5_CAP_ESW(esw->dev, merged_eswitch))
			MLX5_SET(fte_match_set_misc, misc,
				 source_eswitch_owner_vhca_id,
				 MLX5_CAP_GEN(attr->in_mdev, vhca_id));

		misc = MLX5_ADDR_OF(fte_match_param, spec->match_criteria, misc_parameters);
		MLX5_SET_TO_ONES(fte_match_set_misc, misc, source_port);
		if (MLX5_CAP_ESW(esw->dev, merged_eswitch))
			MLX5_SET_TO_ONES(fte_match_set_misc, misc,
					 source_eswitch_owner_vhca_id);

		spec->match_criteria_enable |= MLX5_MATCH_MISC_PARAMETERS;
	}
}

struct mlx5_flow_handle *
mlx5_eswitch_add_offloaded_rule(struct mlx5_eswitch *esw,
				struct mlx5_flow_spec *spec,
				struct mlx5_flow_attr *attr)
{
	struct mlx5_flow_destination dest[MLX5_MAX_FLOW_FWD_VPORTS + 1] = {};
	struct mlx5_flow_act flow_act = { .flags = FLOW_ACT_NO_APPEND, };
	struct mlx5_esw_flow_attr *esw_attr = attr->esw_attr;
	struct mlx5_fs_chains *chains = esw_chains(esw);
	bool split = !!(esw_attr->split_count);
	struct mlx5_vport_tbl_attr fwd_attr;
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_table *fdb;
	int j, i = 0;

	if (esw->mode != MLX5_ESWITCH_OFFLOADS)
		return ERR_PTR(-EOPNOTSUPP);

	flow_act.action = attr->action;
	/* if per flow vlan pop/push is emulated, don't set that into the firmware */
	if (!mlx5_eswitch_vlan_actions_supported(esw->dev, 1))
		flow_act.action &= ~(MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH |
				     MLX5_FLOW_CONTEXT_ACTION_VLAN_POP);
	else if (flow_act.action & MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH) {
		flow_act.vlan[0].ethtype = ntohs(esw_attr->vlan_proto[0]);
		flow_act.vlan[0].vid = esw_attr->vlan_vid[0];
		flow_act.vlan[0].prio = esw_attr->vlan_prio[0];
		if (flow_act.action & MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH_2) {
			flow_act.vlan[1].ethtype = ntohs(esw_attr->vlan_proto[1]);
			flow_act.vlan[1].vid = esw_attr->vlan_vid[1];
			flow_act.vlan[1].prio = esw_attr->vlan_prio[1];
		}
	}

	if (flow_act.action & MLX5_FLOW_CONTEXT_ACTION_FWD_DEST) {
		struct mlx5_flow_table *ft;

		if (attr->dest_ft) {
			flow_act.flags |= FLOW_ACT_IGNORE_FLOW_LEVEL;
			dest[i].type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
			dest[i].ft = attr->dest_ft;
			i++;
		} else if (attr->flags & MLX5_ESW_ATTR_FLAG_SLOW_PATH) {
			flow_act.flags |= FLOW_ACT_IGNORE_FLOW_LEVEL;
			dest[i].type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
			dest[i].ft = mlx5_chains_get_tc_end_ft(chains);
			i++;
		} else if (attr->dest_chain) {
			flow_act.flags |= FLOW_ACT_IGNORE_FLOW_LEVEL;
			ft = mlx5_chains_get_table(chains, attr->dest_chain,
						   1, 0);
			if (IS_ERR(ft)) {
				rule = ERR_CAST(ft);
				goto err_create_goto_table;
			}

			dest[i].type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
			dest[i].ft = ft;
			i++;
		} else {
			for (j = esw_attr->split_count; j < esw_attr->out_count; j++) {
				dest[i].type = MLX5_FLOW_DESTINATION_TYPE_VPORT;
				dest[i].vport.num = esw_attr->dests[j].rep->vport;
				dest[i].vport.vhca_id =
					MLX5_CAP_GEN(esw_attr->dests[j].mdev, vhca_id);
				if (MLX5_CAP_ESW(esw->dev, merged_eswitch))
					dest[i].vport.flags |=
						MLX5_FLOW_DEST_VPORT_VHCA_ID;
				if (esw_attr->dests[j].flags & MLX5_ESW_DEST_ENCAP) {
					flow_act.action |= MLX5_FLOW_CONTEXT_ACTION_PACKET_REFORMAT;
					flow_act.pkt_reformat =
							esw_attr->dests[j].pkt_reformat;
					dest[i].vport.flags |= MLX5_FLOW_DEST_VPORT_REFORMAT_ID;
					dest[i].vport.pkt_reformat =
						esw_attr->dests[j].pkt_reformat;
				}
				i++;
			}
		}
	}

	if (esw_attr->decap_pkt_reformat)
		flow_act.pkt_reformat = esw_attr->decap_pkt_reformat;

	if (flow_act.action & MLX5_FLOW_CONTEXT_ACTION_COUNT) {
		dest[i].type = MLX5_FLOW_DESTINATION_TYPE_COUNTER;
		dest[i].counter_id = mlx5_fc_id(attr->counter);
		i++;
	}

	if (attr->outer_match_level != MLX5_MATCH_NONE)
		spec->match_criteria_enable |= MLX5_MATCH_OUTER_HEADERS;
	if (attr->inner_match_level != MLX5_MATCH_NONE)
		spec->match_criteria_enable |= MLX5_MATCH_INNER_HEADERS;

	if (flow_act.action & MLX5_FLOW_CONTEXT_ACTION_MOD_HDR)
		flow_act.modify_hdr = attr->modify_hdr;

	if (split) {
		fwd_attr.chain = attr->chain;
		fwd_attr.prio = attr->prio;
		fwd_attr.vport = esw_attr->in_rep->vport;

		fdb = esw_vport_tbl_get(esw, &fwd_attr);
	} else {
		if (attr->chain || attr->prio)
			fdb = mlx5_chains_get_table(chains, attr->chain,
						    attr->prio, 0);
		else
			fdb = attr->ft;

		if (!(attr->flags & MLX5_ESW_ATTR_FLAG_NO_IN_PORT))
			mlx5_eswitch_set_rule_source_port(esw, spec, esw_attr);
	}
	if (IS_ERR(fdb)) {
		rule = ERR_CAST(fdb);
		goto err_esw_get;
	}

	mlx5_eswitch_set_rule_flow_source(esw, spec, esw_attr);

	if (mlx5_eswitch_termtbl_required(esw, attr, &flow_act, spec))
		rule = mlx5_eswitch_add_termtbl_rule(esw, fdb, spec, esw_attr,
						     &flow_act, dest, i);
	else
		rule = mlx5_add_flow_rules(fdb, spec, &flow_act, dest, i);
	if (IS_ERR(rule))
		goto err_add_rule;
	else
		atomic64_inc(&esw->offloads.num_flows);

	return rule;

err_add_rule:
	if (split)
		esw_vport_tbl_put(esw, &fwd_attr);
	else if (attr->chain || attr->prio)
		mlx5_chains_put_table(chains, attr->chain, attr->prio, 0);
err_esw_get:
	if (!(attr->flags & MLX5_ESW_ATTR_FLAG_SLOW_PATH) && attr->dest_chain)
		mlx5_chains_put_table(chains, attr->dest_chain, 1, 0);
err_create_goto_table:
	return rule;
}

struct mlx5_flow_handle *
mlx5_eswitch_add_fwd_rule(struct mlx5_eswitch *esw,
			  struct mlx5_flow_spec *spec,
			  struct mlx5_flow_attr *attr)
{
	struct mlx5_flow_destination dest[MLX5_MAX_FLOW_FWD_VPORTS + 1] = {};
	struct mlx5_flow_act flow_act = { .flags = FLOW_ACT_NO_APPEND, };
	struct mlx5_esw_flow_attr *esw_attr = attr->esw_attr;
	struct mlx5_fs_chains *chains = esw_chains(esw);
	struct mlx5_vport_tbl_attr fwd_attr;
	struct mlx5_flow_table *fast_fdb;
	struct mlx5_flow_table *fwd_fdb;
	struct mlx5_flow_handle *rule;
	int i;

	fast_fdb = mlx5_chains_get_table(chains, attr->chain, attr->prio, 0);
	if (IS_ERR(fast_fdb)) {
		rule = ERR_CAST(fast_fdb);
		goto err_get_fast;
	}

	fwd_attr.chain = attr->chain;
	fwd_attr.prio = attr->prio;
	fwd_attr.vport = esw_attr->in_rep->vport;
	fwd_fdb = esw_vport_tbl_get(esw, &fwd_attr);
	if (IS_ERR(fwd_fdb)) {
		rule = ERR_CAST(fwd_fdb);
		goto err_get_fwd;
	}

	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	for (i = 0; i < esw_attr->split_count; i++) {
		dest[i].type = MLX5_FLOW_DESTINATION_TYPE_VPORT;
		dest[i].vport.num = esw_attr->dests[i].rep->vport;
		dest[i].vport.vhca_id =
			MLX5_CAP_GEN(esw_attr->dests[i].mdev, vhca_id);
		if (MLX5_CAP_ESW(esw->dev, merged_eswitch))
			dest[i].vport.flags |= MLX5_FLOW_DEST_VPORT_VHCA_ID;
		if (esw_attr->dests[i].flags & MLX5_ESW_DEST_ENCAP) {
			dest[i].vport.flags |= MLX5_FLOW_DEST_VPORT_REFORMAT_ID;
			dest[i].vport.pkt_reformat = esw_attr->dests[i].pkt_reformat;
		}
	}
	dest[i].type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest[i].ft = fwd_fdb,
	i++;

	mlx5_eswitch_set_rule_source_port(esw, spec, esw_attr);

	if (attr->outer_match_level != MLX5_MATCH_NONE)
		spec->match_criteria_enable |= MLX5_MATCH_OUTER_HEADERS;

	flow_act.flags |= FLOW_ACT_IGNORE_FLOW_LEVEL;
	rule = mlx5_add_flow_rules(fast_fdb, spec, &flow_act, dest, i);

	if (IS_ERR(rule))
		goto add_err;

	atomic64_inc(&esw->offloads.num_flows);

	return rule;
add_err:
	esw_vport_tbl_put(esw, &fwd_attr);
err_get_fwd:
	mlx5_chains_put_table(chains, attr->chain, attr->prio, 0);
err_get_fast:
	return rule;
}

static void
__mlx5_eswitch_del_rule(struct mlx5_eswitch *esw,
			struct mlx5_flow_handle *rule,
			struct mlx5_flow_attr *attr,
			bool fwd_rule)
{
	struct mlx5_esw_flow_attr *esw_attr = attr->esw_attr;
	struct mlx5_fs_chains *chains = esw_chains(esw);
	bool split = (esw_attr->split_count > 0);
	struct mlx5_vport_tbl_attr fwd_attr;
	int i;

	mlx5_del_flow_rules(rule);

	if (!(attr->flags & MLX5_ESW_ATTR_FLAG_SLOW_PATH)) {
		/* unref the term table */
		for (i = 0; i < MLX5_MAX_FLOW_FWD_VPORTS; i++) {
			if (esw_attr->dests[i].termtbl)
				mlx5_eswitch_termtbl_put(esw, esw_attr->dests[i].termtbl);
		}
	}

	atomic64_dec(&esw->offloads.num_flows);

	if (fwd_rule || split) {
		fwd_attr.chain = attr->chain;
		fwd_attr.prio = attr->prio;
		fwd_attr.vport = esw_attr->in_rep->vport;
	}

	if (fwd_rule)  {
		esw_vport_tbl_put(esw, &fwd_attr);
		mlx5_chains_put_table(chains, attr->chain, attr->prio, 0);
	} else {
		if (split)
			esw_vport_tbl_put(esw, &fwd_attr);
		else if (attr->chain || attr->prio)
			mlx5_chains_put_table(chains, attr->chain, attr->prio, 0);
		if (attr->dest_chain)
			mlx5_chains_put_table(chains, attr->dest_chain, 1, 0);
	}
}

void
mlx5_eswitch_del_offloaded_rule(struct mlx5_eswitch *esw,
				struct mlx5_flow_handle *rule,
				struct mlx5_flow_attr *attr)
{
	__mlx5_eswitch_del_rule(esw, rule, attr, false);
}

void
mlx5_eswitch_del_fwd_rule(struct mlx5_eswitch *esw,
			  struct mlx5_flow_handle *rule,
			  struct mlx5_flow_attr *attr)
{
	__mlx5_eswitch_del_rule(esw, rule, attr, true);
}

static int esw_set_global_vlan_pop(struct mlx5_eswitch *esw, u8 val)
{
	struct mlx5_eswitch_rep *rep;
	int i, err = 0;

	esw_debug(esw->dev, "%s applying global %s policy\n", __func__, val ? "pop" : "none");
	mlx5_esw_for_each_host_func_rep(esw, i, rep, esw->esw_funcs.num_vfs) {
		if (atomic_read(&rep->rep_data[REP_ETH].state) != REP_LOADED)
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
	out_rep = attr->dests[0].rep;

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
	out_rep = attr->dests[0].rep;

	if (push && in_rep->vport == MLX5_VPORT_UPLINK)
		goto out_notsupp;

	if (pop && out_rep->vport == MLX5_VPORT_UPLINK)
		goto out_notsupp;

	/* vport has vlan push configured, can't offload VF --> wire rules w.o it */
	if (!push && !pop && fwd)
		if (in_rep->vlan && out_rep->vport == MLX5_VPORT_UPLINK)
			goto out_notsupp;

	/* protects against (1) setting rules with different vlans to push and
	 * (2) setting rules w.o vlans (attr->vlan = 0) && w. vlans to push (!= 0)
	 */
	if (push && in_rep->vlan_refcount && (in_rep->vlan != attr->vlan_vid[0]))
		goto out_notsupp;

	return 0;

out_notsupp:
	return -EOPNOTSUPP;
}

int mlx5_eswitch_add_vlan_action(struct mlx5_eswitch *esw,
				 struct mlx5_flow_attr *attr)
{
	struct offloads_fdb *offloads = &esw->fdb_table.offloads;
	struct mlx5_esw_flow_attr *esw_attr = attr->esw_attr;
	struct mlx5_eswitch_rep *vport = NULL;
	bool push, pop, fwd;
	int err = 0;

	/* nop if we're on the vlan push/pop non emulation mode */
	if (mlx5_eswitch_vlan_actions_supported(esw->dev, 1))
		return 0;

	push = !!(attr->action & MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH);
	pop  = !!(attr->action & MLX5_FLOW_CONTEXT_ACTION_VLAN_POP);
	fwd  = !!((attr->action & MLX5_FLOW_CONTEXT_ACTION_FWD_DEST) &&
		   !attr->dest_chain);

	mutex_lock(&esw->state_lock);

	err = esw_add_vlan_action_check(esw_attr, push, pop, fwd);
	if (err)
		goto unlock;

	attr->flags &= ~MLX5_ESW_ATTR_FLAG_VLAN_HANDLED;

	vport = esw_vlan_action_get_vport(esw_attr, push, pop);

	if (!push && !pop && fwd) {
		/* tracks VF --> wire rules without vlan push action */
		if (esw_attr->dests[0].rep->vport == MLX5_VPORT_UPLINK) {
			vport->vlan_refcount++;
			attr->flags |= MLX5_ESW_ATTR_FLAG_VLAN_HANDLED;
		}

		goto unlock;
	}

	if (!push && !pop)
		goto unlock;

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

		err = __mlx5_eswitch_set_vport_vlan(esw, vport->vport, esw_attr->vlan_vid[0],
						    0, SET_VLAN_INSERT | SET_VLAN_STRIP);
		if (err)
			goto out;
		vport->vlan = esw_attr->vlan_vid[0];
skip_set_push:
		vport->vlan_refcount++;
	}
out:
	if (!err)
		attr->flags |= MLX5_ESW_ATTR_FLAG_VLAN_HANDLED;
unlock:
	mutex_unlock(&esw->state_lock);
	return err;
}

int mlx5_eswitch_del_vlan_action(struct mlx5_eswitch *esw,
				 struct mlx5_flow_attr *attr)
{
	struct offloads_fdb *offloads = &esw->fdb_table.offloads;
	struct mlx5_esw_flow_attr *esw_attr = attr->esw_attr;
	struct mlx5_eswitch_rep *vport = NULL;
	bool push, pop, fwd;
	int err = 0;

	/* nop if we're on the vlan push/pop non emulation mode */
	if (mlx5_eswitch_vlan_actions_supported(esw->dev, 1))
		return 0;

	if (!(attr->flags & MLX5_ESW_ATTR_FLAG_VLAN_HANDLED))
		return 0;

	push = !!(attr->action & MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH);
	pop  = !!(attr->action & MLX5_FLOW_CONTEXT_ACTION_VLAN_POP);
	fwd  = !!(attr->action & MLX5_FLOW_CONTEXT_ACTION_FWD_DEST);

	mutex_lock(&esw->state_lock);

	vport = esw_vlan_action_get_vport(esw_attr, push, pop);

	if (!push && !pop && fwd) {
		/* tracks VF --> wire rules without vlan push action */
		if (esw_attr->dests[0].rep->vport == MLX5_VPORT_UPLINK)
			vport->vlan_refcount--;

		goto out;
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
		goto out;

	/* no more vlan rules, stop global vlan pop policy */
	err = esw_set_global_vlan_pop(esw, 0);

out:
	mutex_unlock(&esw->state_lock);
	return err;
}

struct mlx5_flow_handle *
mlx5_eswitch_add_send_to_vport_rule(struct mlx5_eswitch *esw, u16 vport,
				    u32 sqn)
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
	/* source vport is the esw manager */
	MLX5_SET(fte_match_set_misc, misc, source_port, esw->manager_vport);

	misc = MLX5_ADDR_OF(fte_match_param, spec->match_criteria, misc_parameters);
	MLX5_SET_TO_ONES(fte_match_set_misc, misc, source_sqn);
	MLX5_SET_TO_ONES(fte_match_set_misc, misc, source_port);

	spec->match_criteria_enable = MLX5_MATCH_MISC_PARAMETERS;
	dest.type = MLX5_FLOW_DESTINATION_TYPE_VPORT;
	dest.vport.num = vport;
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;

	flow_rule = mlx5_add_flow_rules(esw->fdb_table.offloads.slow_fdb,
					spec, &flow_act, &dest, 1);
	if (IS_ERR(flow_rule))
		esw_warn(esw->dev, "FDB: Failed to add send to vport rule err %ld\n", PTR_ERR(flow_rule));
out:
	kvfree(spec);
	return flow_rule;
}
EXPORT_SYMBOL(mlx5_eswitch_add_send_to_vport_rule);

void mlx5_eswitch_del_send_to_vport_rule(struct mlx5_flow_handle *rule)
{
	mlx5_del_flow_rules(rule);
}

static bool mlx5_eswitch_reg_c1_loopback_supported(struct mlx5_eswitch *esw)
{
	return MLX5_CAP_ESW_FLOWTABLE(esw->dev, fdb_to_vport_reg_c_id) &
	       MLX5_FDB_TO_VPORT_REG_C_1;
}

static int esw_set_passing_vport_metadata(struct mlx5_eswitch *esw, bool enable)
{
	u32 out[MLX5_ST_SZ_DW(query_esw_vport_context_out)] = {};
	u32 min[MLX5_ST_SZ_DW(modify_esw_vport_context_in)] = {};
	u32 in[MLX5_ST_SZ_DW(query_esw_vport_context_in)] = {};
	u8 curr, wanted;
	int err;

	if (!mlx5_eswitch_reg_c1_loopback_supported(esw) &&
	    !mlx5_eswitch_vport_match_metadata_enabled(esw))
		return 0;

	MLX5_SET(query_esw_vport_context_in, in, opcode,
		 MLX5_CMD_OP_QUERY_ESW_VPORT_CONTEXT);
	err = mlx5_cmd_exec_inout(esw->dev, query_esw_vport_context, in, out);
	if (err)
		return err;

	curr = MLX5_GET(query_esw_vport_context_out, out,
			esw_vport_context.fdb_to_vport_reg_c_id);
	wanted = MLX5_FDB_TO_VPORT_REG_C_0;
	if (mlx5_eswitch_reg_c1_loopback_supported(esw))
		wanted |= MLX5_FDB_TO_VPORT_REG_C_1;

	if (enable)
		curr |= wanted;
	else
		curr &= ~wanted;

	MLX5_SET(modify_esw_vport_context_in, min,
		 esw_vport_context.fdb_to_vport_reg_c_id, curr);
	MLX5_SET(modify_esw_vport_context_in, min,
		 field_select.fdb_to_vport_reg_c_id, 1);

	err = mlx5_eswitch_modify_esw_vport_context(esw->dev, 0, false, min);
	if (!err) {
		if (enable && (curr & MLX5_FDB_TO_VPORT_REG_C_1))
			esw->flags |= MLX5_ESWITCH_REG_C1_LOOPBACK_ENABLED;
		else
			esw->flags &= ~MLX5_ESWITCH_REG_C1_LOOPBACK_ENABLED;
	}

	return err;
}

static void peer_miss_rules_setup(struct mlx5_eswitch *esw,
				  struct mlx5_core_dev *peer_dev,
				  struct mlx5_flow_spec *spec,
				  struct mlx5_flow_destination *dest)
{
	void *misc;

	if (mlx5_eswitch_vport_match_metadata_enabled(esw)) {
		misc = MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
				    misc_parameters_2);
		MLX5_SET(fte_match_set_misc2, misc, metadata_reg_c_0,
			 mlx5_eswitch_get_vport_metadata_mask());

		spec->match_criteria_enable = MLX5_MATCH_MISC_PARAMETERS_2;
	} else {
		misc = MLX5_ADDR_OF(fte_match_param, spec->match_value,
				    misc_parameters);

		MLX5_SET(fte_match_set_misc, misc, source_eswitch_owner_vhca_id,
			 MLX5_CAP_GEN(peer_dev, vhca_id));

		spec->match_criteria_enable = MLX5_MATCH_MISC_PARAMETERS;

		misc = MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
				    misc_parameters);
		MLX5_SET_TO_ONES(fte_match_set_misc, misc, source_port);
		MLX5_SET_TO_ONES(fte_match_set_misc, misc,
				 source_eswitch_owner_vhca_id);
	}

	dest->type = MLX5_FLOW_DESTINATION_TYPE_VPORT;
	dest->vport.num = peer_dev->priv.eswitch->manager_vport;
	dest->vport.vhca_id = MLX5_CAP_GEN(peer_dev, vhca_id);
	dest->vport.flags |= MLX5_FLOW_DEST_VPORT_VHCA_ID;
}

static void esw_set_peer_miss_rule_source_port(struct mlx5_eswitch *esw,
					       struct mlx5_eswitch *peer_esw,
					       struct mlx5_flow_spec *spec,
					       u16 vport)
{
	void *misc;

	if (mlx5_eswitch_vport_match_metadata_enabled(esw)) {
		misc = MLX5_ADDR_OF(fte_match_param, spec->match_value,
				    misc_parameters_2);
		MLX5_SET(fte_match_set_misc2, misc, metadata_reg_c_0,
			 mlx5_eswitch_get_vport_metadata_for_match(peer_esw,
								   vport));
	} else {
		misc = MLX5_ADDR_OF(fte_match_param, spec->match_value,
				    misc_parameters);
		MLX5_SET(fte_match_set_misc, misc, source_port, vport);
	}
}

static int esw_add_fdb_peer_miss_rules(struct mlx5_eswitch *esw,
				       struct mlx5_core_dev *peer_dev)
{
	struct mlx5_flow_destination dest = {};
	struct mlx5_flow_act flow_act = {0};
	struct mlx5_flow_handle **flows;
	struct mlx5_flow_handle *flow;
	struct mlx5_flow_spec *spec;
	/* total vports is the same for both e-switches */
	int nvports = esw->total_vports;
	void *misc;
	int err, i;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	peer_miss_rules_setup(esw, peer_dev, spec, &dest);

	flows = kvzalloc(nvports * sizeof(*flows), GFP_KERNEL);
	if (!flows) {
		err = -ENOMEM;
		goto alloc_flows_err;
	}

	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	misc = MLX5_ADDR_OF(fte_match_param, spec->match_value,
			    misc_parameters);

	if (mlx5_core_is_ecpf_esw_manager(esw->dev)) {
		esw_set_peer_miss_rule_source_port(esw, peer_dev->priv.eswitch,
						   spec, MLX5_VPORT_PF);

		flow = mlx5_add_flow_rules(esw->fdb_table.offloads.slow_fdb,
					   spec, &flow_act, &dest, 1);
		if (IS_ERR(flow)) {
			err = PTR_ERR(flow);
			goto add_pf_flow_err;
		}
		flows[MLX5_VPORT_PF] = flow;
	}

	if (mlx5_ecpf_vport_exists(esw->dev)) {
		MLX5_SET(fte_match_set_misc, misc, source_port, MLX5_VPORT_ECPF);
		flow = mlx5_add_flow_rules(esw->fdb_table.offloads.slow_fdb,
					   spec, &flow_act, &dest, 1);
		if (IS_ERR(flow)) {
			err = PTR_ERR(flow);
			goto add_ecpf_flow_err;
		}
		flows[mlx5_eswitch_ecpf_idx(esw)] = flow;
	}

	mlx5_esw_for_each_vf_vport_num(esw, i, mlx5_core_max_vfs(esw->dev)) {
		esw_set_peer_miss_rule_source_port(esw,
						   peer_dev->priv.eswitch,
						   spec, i);

		flow = mlx5_add_flow_rules(esw->fdb_table.offloads.slow_fdb,
					   spec, &flow_act, &dest, 1);
		if (IS_ERR(flow)) {
			err = PTR_ERR(flow);
			goto add_vf_flow_err;
		}
		flows[i] = flow;
	}

	esw->fdb_table.offloads.peer_miss_rules = flows;

	kvfree(spec);
	return 0;

add_vf_flow_err:
	nvports = --i;
	mlx5_esw_for_each_vf_vport_num_reverse(esw, i, nvports)
		mlx5_del_flow_rules(flows[i]);

	if (mlx5_ecpf_vport_exists(esw->dev))
		mlx5_del_flow_rules(flows[mlx5_eswitch_ecpf_idx(esw)]);
add_ecpf_flow_err:
	if (mlx5_core_is_ecpf_esw_manager(esw->dev))
		mlx5_del_flow_rules(flows[MLX5_VPORT_PF]);
add_pf_flow_err:
	esw_warn(esw->dev, "FDB: Failed to add peer miss flow rule err %d\n", err);
	kvfree(flows);
alloc_flows_err:
	kvfree(spec);
	return err;
}

static void esw_del_fdb_peer_miss_rules(struct mlx5_eswitch *esw)
{
	struct mlx5_flow_handle **flows;
	int i;

	flows = esw->fdb_table.offloads.peer_miss_rules;

	mlx5_esw_for_each_vf_vport_num_reverse(esw, i,
					       mlx5_core_max_vfs(esw->dev))
		mlx5_del_flow_rules(flows[i]);

	if (mlx5_ecpf_vport_exists(esw->dev))
		mlx5_del_flow_rules(flows[mlx5_eswitch_ecpf_idx(esw)]);

	if (mlx5_core_is_ecpf_esw_manager(esw->dev))
		mlx5_del_flow_rules(flows[MLX5_VPORT_PF]);

	kvfree(flows);
}

static int esw_add_fdb_miss_rule(struct mlx5_eswitch *esw)
{
	struct mlx5_flow_act flow_act = {0};
	struct mlx5_flow_destination dest = {};
	struct mlx5_flow_handle *flow_rule = NULL;
	struct mlx5_flow_spec *spec;
	void *headers_c;
	void *headers_v;
	int err = 0;
	u8 *dmac_c;
	u8 *dmac_v;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec) {
		err = -ENOMEM;
		goto out;
	}

	spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
	headers_c = MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
				 outer_headers);
	dmac_c = MLX5_ADDR_OF(fte_match_param, headers_c,
			      outer_headers.dmac_47_16);
	dmac_c[0] = 0x01;

	dest.type = MLX5_FLOW_DESTINATION_TYPE_VPORT;
	dest.vport.num = esw->manager_vport;
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;

	flow_rule = mlx5_add_flow_rules(esw->fdb_table.offloads.slow_fdb,
					spec, &flow_act, &dest, 1);
	if (IS_ERR(flow_rule)) {
		err = PTR_ERR(flow_rule);
		esw_warn(esw->dev,  "FDB: Failed to add unicast miss flow rule err %d\n", err);
		goto out;
	}

	esw->fdb_table.offloads.miss_rule_uni = flow_rule;

	headers_v = MLX5_ADDR_OF(fte_match_param, spec->match_value,
				 outer_headers);
	dmac_v = MLX5_ADDR_OF(fte_match_param, headers_v,
			      outer_headers.dmac_47_16);
	dmac_v[0] = 0x01;
	flow_rule = mlx5_add_flow_rules(esw->fdb_table.offloads.slow_fdb,
					spec, &flow_act, &dest, 1);
	if (IS_ERR(flow_rule)) {
		err = PTR_ERR(flow_rule);
		esw_warn(esw->dev, "FDB: Failed to add multicast miss flow rule err %d\n", err);
		mlx5_del_flow_rules(esw->fdb_table.offloads.miss_rule_uni);
		goto out;
	}

	esw->fdb_table.offloads.miss_rule_multi = flow_rule;

out:
	kvfree(spec);
	return err;
}

struct mlx5_flow_handle *
esw_add_restore_rule(struct mlx5_eswitch *esw, u32 tag)
{
	struct mlx5_flow_act flow_act = { .flags = FLOW_ACT_NO_APPEND, };
	struct mlx5_flow_table *ft = esw->offloads.ft_offloads_restore;
	struct mlx5_flow_context *flow_context;
	struct mlx5_flow_handle *flow_rule;
	struct mlx5_flow_destination dest;
	struct mlx5_flow_spec *spec;
	void *misc;

	if (!mlx5_eswitch_reg_c1_loopback_supported(esw))
		return ERR_PTR(-EOPNOTSUPP);

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return ERR_PTR(-ENOMEM);

	misc = MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
			    misc_parameters_2);
	MLX5_SET(fte_match_set_misc2, misc, metadata_reg_c_0,
		 ESW_CHAIN_TAG_METADATA_MASK);
	misc = MLX5_ADDR_OF(fte_match_param, spec->match_value,
			    misc_parameters_2);
	MLX5_SET(fte_match_set_misc2, misc, metadata_reg_c_0, tag);
	spec->match_criteria_enable = MLX5_MATCH_MISC_PARAMETERS_2;
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST |
			  MLX5_FLOW_CONTEXT_ACTION_MOD_HDR;
	flow_act.modify_hdr = esw->offloads.restore_copy_hdr_id;

	flow_context = &spec->flow_context;
	flow_context->flags |= FLOW_CONTEXT_HAS_TAG;
	flow_context->flow_tag = tag;
	dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest.ft = esw->offloads.ft_offloads;

	flow_rule = mlx5_add_flow_rules(ft, spec, &flow_act, &dest, 1);
	kfree(spec);

	if (IS_ERR(flow_rule))
		esw_warn(esw->dev,
			 "Failed to create restore rule for tag: %d, err(%d)\n",
			 tag, (int)PTR_ERR(flow_rule));

	return flow_rule;
}

u32
esw_get_max_restore_tag(struct mlx5_eswitch *esw)
{
	return ESW_CHAIN_TAG_METADATA_MASK;
}

#define MAX_PF_SQ 256
#define MAX_SQ_NVPORTS 32

static void esw_set_flow_group_source_port(struct mlx5_eswitch *esw,
					   u32 *flow_group_in)
{
	void *match_criteria = MLX5_ADDR_OF(create_flow_group_in,
					    flow_group_in,
					    match_criteria);

	if (mlx5_eswitch_vport_match_metadata_enabled(esw)) {
		MLX5_SET(create_flow_group_in, flow_group_in,
			 match_criteria_enable,
			 MLX5_MATCH_MISC_PARAMETERS_2);

		MLX5_SET(fte_match_param, match_criteria,
			 misc_parameters_2.metadata_reg_c_0,
			 mlx5_eswitch_get_vport_metadata_mask());
	} else {
		MLX5_SET(create_flow_group_in, flow_group_in,
			 match_criteria_enable,
			 MLX5_MATCH_MISC_PARAMETERS);

		MLX5_SET_TO_ONES(fte_match_param, match_criteria,
				 misc_parameters.source_port);
	}
}

#if IS_ENABLED(CONFIG_MLX5_CLS_ACT)
#define fdb_modify_header_fwd_to_table_supported(esw) \
	(MLX5_CAP_ESW_FLOWTABLE((esw)->dev, fdb_modify_header_fwd_to_table))
static void esw_init_chains_offload_flags(struct mlx5_eswitch *esw, u32 *flags)
{
	struct mlx5_core_dev *dev = esw->dev;

	if (MLX5_CAP_ESW_FLOWTABLE_FDB(dev, ignore_flow_level))
		*flags |= MLX5_CHAINS_IGNORE_FLOW_LEVEL_SUPPORTED;

	if (!MLX5_CAP_ESW_FLOWTABLE(dev, multi_fdb_encap) &&
	    esw->offloads.encap != DEVLINK_ESWITCH_ENCAP_MODE_NONE) {
		*flags &= ~MLX5_CHAINS_AND_PRIOS_SUPPORTED;
		esw_warn(dev, "Tc chains and priorities offload aren't supported, update firmware if needed\n");
	} else if (!mlx5_eswitch_reg_c1_loopback_enabled(esw)) {
		*flags &= ~MLX5_CHAINS_AND_PRIOS_SUPPORTED;
		esw_warn(dev, "Tc chains and priorities offload aren't supported\n");
	} else if (!fdb_modify_header_fwd_to_table_supported(esw)) {
		/* Disabled when ttl workaround is needed, e.g
		 * when ESWITCH_IPV4_TTL_MODIFY_ENABLE = true in mlxconfig
		 */
		esw_warn(dev,
			 "Tc chains and priorities offload aren't supported, check firmware version, or mlxconfig settings\n");
		*flags &= ~MLX5_CHAINS_AND_PRIOS_SUPPORTED;
	} else {
		*flags |= MLX5_CHAINS_AND_PRIOS_SUPPORTED;
		esw_info(dev, "Supported tc chains and prios offload\n");
	}

	if (esw->offloads.encap != DEVLINK_ESWITCH_ENCAP_MODE_NONE)
		*flags |= MLX5_CHAINS_FT_TUNNEL_SUPPORTED;
}

static int
esw_chains_create(struct mlx5_eswitch *esw, struct mlx5_flow_table *miss_fdb)
{
	struct mlx5_core_dev *dev = esw->dev;
	struct mlx5_flow_table *nf_ft, *ft;
	struct mlx5_chains_attr attr = {};
	struct mlx5_fs_chains *chains;
	u32 fdb_max;
	int err;

	fdb_max = 1 << MLX5_CAP_ESW_FLOWTABLE_FDB(dev, log_max_ft_size);

	esw_init_chains_offload_flags(esw, &attr.flags);
	attr.ns = MLX5_FLOW_NAMESPACE_FDB;
	attr.max_ft_sz = fdb_max;
	attr.max_grp_num = esw->params.large_group_num;
	attr.default_ft = miss_fdb;
	attr.max_restore_tag = esw_get_max_restore_tag(esw);

	chains = mlx5_chains_create(dev, &attr);
	if (IS_ERR(chains)) {
		err = PTR_ERR(chains);
		esw_warn(dev, "Failed to create fdb chains err(%d)\n", err);
		return err;
	}

	esw->fdb_table.offloads.esw_chains_priv = chains;

	/* Create tc_end_ft which is the always created ft chain */
	nf_ft = mlx5_chains_get_table(chains, mlx5_chains_get_nf_ft_chain(chains),
				      1, 0);
	if (IS_ERR(nf_ft)) {
		err = PTR_ERR(nf_ft);
		goto nf_ft_err;
	}

	/* Always open the root for fast path */
	ft = mlx5_chains_get_table(chains, 0, 1, 0);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		goto level_0_err;
	}

	/* Open level 1 for split fdb rules now if prios isn't supported  */
	if (!mlx5_chains_prios_supported(chains)) {
		err = mlx5_esw_vport_tbl_get(esw);
		if (err)
			goto level_1_err;
	}

	mlx5_chains_set_end_ft(chains, nf_ft);

	return 0;

level_1_err:
	mlx5_chains_put_table(chains, 0, 1, 0);
level_0_err:
	mlx5_chains_put_table(chains, mlx5_chains_get_nf_ft_chain(chains), 1, 0);
nf_ft_err:
	mlx5_chains_destroy(chains);
	esw->fdb_table.offloads.esw_chains_priv = NULL;

	return err;
}

static void
esw_chains_destroy(struct mlx5_eswitch *esw, struct mlx5_fs_chains *chains)
{
	if (!mlx5_chains_prios_supported(chains))
		mlx5_esw_vport_tbl_put(esw);
	mlx5_chains_put_table(chains, 0, 1, 0);
	mlx5_chains_put_table(chains, mlx5_chains_get_nf_ft_chain(chains), 1, 0);
	mlx5_chains_destroy(chains);
}

#else /* CONFIG_MLX5_CLS_ACT */

static int
esw_chains_create(struct mlx5_eswitch *esw, struct mlx5_flow_table *miss_fdb)
{ return 0; }

static void
esw_chains_destroy(struct mlx5_eswitch *esw, struct mlx5_fs_chains *chains)
{}

#endif

static int esw_create_offloads_fdb_tables(struct mlx5_eswitch *esw)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_core_dev *dev = esw->dev;
	struct mlx5_flow_namespace *root_ns;
	struct mlx5_flow_table *fdb = NULL;
	u32 flags = 0, *flow_group_in;
	int table_size, ix, err = 0;
	struct mlx5_flow_group *g;
	void *match_criteria;
	u8 *dmac;

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
	esw->fdb_table.offloads.ns = root_ns;
	err = mlx5_flow_namespace_set_mode(root_ns,
					   esw->dev->priv.steering->mode);
	if (err) {
		esw_warn(dev, "Failed to set FDB namespace steering mode\n");
		goto ns_err;
	}

	table_size = esw->total_vports * MAX_SQ_NVPORTS + MAX_PF_SQ +
		MLX5_ESW_MISS_FLOWS + esw->total_vports;

	/* create the slow path fdb with encap set, so further table instances
	 * can be created at run time while VFs are probed if the FW allows that.
	 */
	if (esw->offloads.encap != DEVLINK_ESWITCH_ENCAP_MODE_NONE)
		flags |= (MLX5_FLOW_TABLE_TUNNEL_EN_REFORMAT |
			  MLX5_FLOW_TABLE_TUNNEL_EN_DECAP);

	ft_attr.flags = flags;
	ft_attr.max_fte = table_size;
	ft_attr.prio = FDB_SLOW_PATH;

	fdb = mlx5_create_flow_table(root_ns, &ft_attr);
	if (IS_ERR(fdb)) {
		err = PTR_ERR(fdb);
		esw_warn(dev, "Failed to create slow path FDB Table err %d\n", err);
		goto slow_fdb_err;
	}
	esw->fdb_table.offloads.slow_fdb = fdb;

	err = esw_chains_create(esw, fdb);
	if (err) {
		esw_warn(dev, "Failed to open fdb chains err(%d)\n", err);
		goto fdb_chains_err;
	}

	/* create send-to-vport group */
	MLX5_SET(create_flow_group_in, flow_group_in, match_criteria_enable,
		 MLX5_MATCH_MISC_PARAMETERS);

	match_criteria = MLX5_ADDR_OF(create_flow_group_in, flow_group_in, match_criteria);

	MLX5_SET_TO_ONES(fte_match_param, match_criteria, misc_parameters.source_sqn);
	MLX5_SET_TO_ONES(fte_match_param, match_criteria, misc_parameters.source_port);

	ix = esw->total_vports * MAX_SQ_NVPORTS + MAX_PF_SQ;
	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, 0);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, ix - 1);

	g = mlx5_create_flow_group(fdb, flow_group_in);
	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		esw_warn(dev, "Failed to create send-to-vport flow group err(%d)\n", err);
		goto send_vport_err;
	}
	esw->fdb_table.offloads.send_to_vport_grp = g;

	if (MLX5_CAP_ESW(esw->dev, merged_eswitch)) {
		/* create peer esw miss group */
		memset(flow_group_in, 0, inlen);

		esw_set_flow_group_source_port(esw, flow_group_in);

		if (!mlx5_eswitch_vport_match_metadata_enabled(esw)) {
			match_criteria = MLX5_ADDR_OF(create_flow_group_in,
						      flow_group_in,
						      match_criteria);

			MLX5_SET_TO_ONES(fte_match_param, match_criteria,
					 misc_parameters.source_eswitch_owner_vhca_id);

			MLX5_SET(create_flow_group_in, flow_group_in,
				 source_eswitch_owner_vhca_id_valid, 1);
		}

		MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, ix);
		MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index,
			 ix + esw->total_vports - 1);
		ix += esw->total_vports;

		g = mlx5_create_flow_group(fdb, flow_group_in);
		if (IS_ERR(g)) {
			err = PTR_ERR(g);
			esw_warn(dev, "Failed to create peer miss flow group err(%d)\n", err);
			goto peer_miss_err;
		}
		esw->fdb_table.offloads.peer_miss_grp = g;
	}

	/* create miss group */
	memset(flow_group_in, 0, inlen);
	MLX5_SET(create_flow_group_in, flow_group_in, match_criteria_enable,
		 MLX5_MATCH_OUTER_HEADERS);
	match_criteria = MLX5_ADDR_OF(create_flow_group_in, flow_group_in,
				      match_criteria);
	dmac = MLX5_ADDR_OF(fte_match_param, match_criteria,
			    outer_headers.dmac_47_16);
	dmac[0] = 0x01;

	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, ix);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index,
		 ix + MLX5_ESW_MISS_FLOWS);

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

	kvfree(flow_group_in);
	return 0;

miss_rule_err:
	mlx5_destroy_flow_group(esw->fdb_table.offloads.miss_grp);
miss_err:
	if (MLX5_CAP_ESW(esw->dev, merged_eswitch))
		mlx5_destroy_flow_group(esw->fdb_table.offloads.peer_miss_grp);
peer_miss_err:
	mlx5_destroy_flow_group(esw->fdb_table.offloads.send_to_vport_grp);
send_vport_err:
	esw_chains_destroy(esw, esw_chains(esw));
fdb_chains_err:
	mlx5_destroy_flow_table(esw->fdb_table.offloads.slow_fdb);
slow_fdb_err:
	/* Holds true only as long as DMFS is the default */
	mlx5_flow_namespace_set_mode(root_ns, MLX5_FLOW_STEERING_MODE_DMFS);
ns_err:
	kvfree(flow_group_in);
	return err;
}

static void esw_destroy_offloads_fdb_tables(struct mlx5_eswitch *esw)
{
	if (!esw->fdb_table.offloads.slow_fdb)
		return;

	esw_debug(esw->dev, "Destroy offloads FDB Tables\n");
	mlx5_del_flow_rules(esw->fdb_table.offloads.miss_rule_multi);
	mlx5_del_flow_rules(esw->fdb_table.offloads.miss_rule_uni);
	mlx5_destroy_flow_group(esw->fdb_table.offloads.send_to_vport_grp);
	if (MLX5_CAP_ESW(esw->dev, merged_eswitch))
		mlx5_destroy_flow_group(esw->fdb_table.offloads.peer_miss_grp);
	mlx5_destroy_flow_group(esw->fdb_table.offloads.miss_grp);

	esw_chains_destroy(esw, esw_chains(esw));

	mlx5_destroy_flow_table(esw->fdb_table.offloads.slow_fdb);
	/* Holds true only as long as DMFS is the default */
	mlx5_flow_namespace_set_mode(esw->fdb_table.offloads.ns,
				     MLX5_FLOW_STEERING_MODE_DMFS);
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

	ft_attr.max_fte = esw->total_vports + MLX5_ESW_MISS_FLOWS;
	ft_attr.prio = 1;

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
	u32 *flow_group_in;
	int nvports;
	int err = 0;

	nvports = esw->total_vports + MLX5_ESW_MISS_FLOWS;
	flow_group_in = kvzalloc(inlen, GFP_KERNEL);
	if (!flow_group_in)
		return -ENOMEM;

	/* create vport rx group */
	esw_set_flow_group_source_port(esw, flow_group_in);

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
	kvfree(flow_group_in);
	return err;
}

static void esw_destroy_vport_rx_group(struct mlx5_eswitch *esw)
{
	mlx5_destroy_flow_group(esw->offloads.vport_rx_group);
}

struct mlx5_flow_handle *
mlx5_eswitch_create_vport_rx_rule(struct mlx5_eswitch *esw, u16 vport,
				  struct mlx5_flow_destination *dest)
{
	struct mlx5_flow_act flow_act = {0};
	struct mlx5_flow_handle *flow_rule;
	struct mlx5_flow_spec *spec;
	void *misc;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec) {
		flow_rule = ERR_PTR(-ENOMEM);
		goto out;
	}

	if (mlx5_eswitch_vport_match_metadata_enabled(esw)) {
		misc = MLX5_ADDR_OF(fte_match_param, spec->match_value, misc_parameters_2);
		MLX5_SET(fte_match_set_misc2, misc, metadata_reg_c_0,
			 mlx5_eswitch_get_vport_metadata_for_match(esw, vport));

		misc = MLX5_ADDR_OF(fte_match_param, spec->match_criteria, misc_parameters_2);
		MLX5_SET(fte_match_set_misc2, misc, metadata_reg_c_0,
			 mlx5_eswitch_get_vport_metadata_mask());

		spec->match_criteria_enable = MLX5_MATCH_MISC_PARAMETERS_2;
	} else {
		misc = MLX5_ADDR_OF(fte_match_param, spec->match_value, misc_parameters);
		MLX5_SET(fte_match_set_misc, misc, source_port, vport);

		misc = MLX5_ADDR_OF(fte_match_param, spec->match_criteria, misc_parameters);
		MLX5_SET_TO_ONES(fte_match_set_misc, misc, source_port);

		spec->match_criteria_enable = MLX5_MATCH_MISC_PARAMETERS;
	}

	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	flow_rule = mlx5_add_flow_rules(esw->offloads.ft_offloads, spec,
					&flow_act, dest, 1);
	if (IS_ERR(flow_rule)) {
		esw_warn(esw->dev, "fs offloads: Failed to add vport rx rule err %ld\n", PTR_ERR(flow_rule));
		goto out;
	}

out:
	kvfree(spec);
	return flow_rule;
}


static int mlx5_eswitch_inline_mode_get(const struct mlx5_eswitch *esw, u8 *mode)
{
	u8 prev_mlx5_mode, mlx5_mode = MLX5_INLINE_MODE_L2;
	struct mlx5_core_dev *dev = esw->dev;
	int vport;

	if (!MLX5_CAP_GEN(dev, vport_group_manager))
		return -EOPNOTSUPP;

	if (esw->mode == MLX5_ESWITCH_NONE)
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
	mlx5_query_nic_vport_min_inline(dev, esw->first_host_vport, &prev_mlx5_mode);
	mlx5_esw_for_each_host_func_vport(esw, vport, esw->esw_funcs.num_vfs) {
		mlx5_query_nic_vport_min_inline(dev, vport, &mlx5_mode);
		if (prev_mlx5_mode != mlx5_mode)
			return -EINVAL;
		prev_mlx5_mode = mlx5_mode;
	}

out:
	*mode = mlx5_mode;
	return 0;
}

static void esw_destroy_restore_table(struct mlx5_eswitch *esw)
{
	struct mlx5_esw_offload *offloads = &esw->offloads;

	if (!mlx5_eswitch_reg_c1_loopback_supported(esw))
		return;

	mlx5_modify_header_dealloc(esw->dev, offloads->restore_copy_hdr_id);
	mlx5_destroy_flow_group(offloads->restore_group);
	mlx5_destroy_flow_table(offloads->ft_offloads_restore);
}

static int esw_create_restore_table(struct mlx5_eswitch *esw)
{
	u8 modact[MLX5_UN_SZ_BYTES(set_add_copy_action_in_auto)] = {};
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_core_dev *dev = esw->dev;
	struct mlx5_flow_namespace *ns;
	struct mlx5_modify_hdr *mod_hdr;
	void *match_criteria, *misc;
	struct mlx5_flow_table *ft;
	struct mlx5_flow_group *g;
	u32 *flow_group_in;
	int err = 0;

	if (!mlx5_eswitch_reg_c1_loopback_supported(esw))
		return 0;

	ns = mlx5_get_flow_namespace(dev, MLX5_FLOW_NAMESPACE_OFFLOADS);
	if (!ns) {
		esw_warn(esw->dev, "Failed to get offloads flow namespace\n");
		return -EOPNOTSUPP;
	}

	flow_group_in = kvzalloc(inlen, GFP_KERNEL);
	if (!flow_group_in) {
		err = -ENOMEM;
		goto out_free;
	}

	ft_attr.max_fte = 1 << ESW_CHAIN_TAG_METADATA_BITS;
	ft = mlx5_create_flow_table(ns, &ft_attr);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		esw_warn(esw->dev, "Failed to create restore table, err %d\n",
			 err);
		goto out_free;
	}

	memset(flow_group_in, 0, inlen);
	match_criteria = MLX5_ADDR_OF(create_flow_group_in, flow_group_in,
				      match_criteria);
	misc = MLX5_ADDR_OF(fte_match_param, match_criteria,
			    misc_parameters_2);

	MLX5_SET(fte_match_set_misc2, misc, metadata_reg_c_0,
		 ESW_CHAIN_TAG_METADATA_MASK);
	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, 0);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index,
		 ft_attr.max_fte - 1);
	MLX5_SET(create_flow_group_in, flow_group_in, match_criteria_enable,
		 MLX5_MATCH_MISC_PARAMETERS_2);
	g = mlx5_create_flow_group(ft, flow_group_in);
	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		esw_warn(dev, "Failed to create restore flow group, err: %d\n",
			 err);
		goto err_group;
	}

	MLX5_SET(copy_action_in, modact, action_type, MLX5_ACTION_TYPE_COPY);
	MLX5_SET(copy_action_in, modact, src_field,
		 MLX5_ACTION_IN_FIELD_METADATA_REG_C_1);
	MLX5_SET(copy_action_in, modact, dst_field,
		 MLX5_ACTION_IN_FIELD_METADATA_REG_B);
	mod_hdr = mlx5_modify_header_alloc(esw->dev,
					   MLX5_FLOW_NAMESPACE_KERNEL, 1,
					   modact);
	if (IS_ERR(mod_hdr)) {
		err = PTR_ERR(mod_hdr);
		esw_warn(dev, "Failed to create restore mod header, err: %d\n",
			 err);
		goto err_mod_hdr;
	}

	esw->offloads.ft_offloads_restore = ft;
	esw->offloads.restore_group = g;
	esw->offloads.restore_copy_hdr_id = mod_hdr;

	kvfree(flow_group_in);

	return 0;

err_mod_hdr:
	mlx5_destroy_flow_group(g);
err_group:
	mlx5_destroy_flow_table(ft);
out_free:
	kvfree(flow_group_in);

	return err;
}

static int esw_offloads_start(struct mlx5_eswitch *esw,
			      struct netlink_ext_ack *extack)
{
	int err, err1;

	mlx5_eswitch_disable_locked(esw, false);
	err = mlx5_eswitch_enable_locked(esw, MLX5_ESWITCH_OFFLOADS,
					 esw->dev->priv.sriov.num_vfs);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Failed setting eswitch to offloads");
		err1 = mlx5_eswitch_enable_locked(esw, MLX5_ESWITCH_LEGACY,
						  MLX5_ESWITCH_IGNORE_NUM_VFS);
		if (err1) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Failed setting eswitch back to legacy");
		}
	}
	if (esw->offloads.inline_mode == MLX5_INLINE_MODE_NONE) {
		if (mlx5_eswitch_inline_mode_get(esw,
						 &esw->offloads.inline_mode)) {
			esw->offloads.inline_mode = MLX5_INLINE_MODE_L2;
			NL_SET_ERR_MSG_MOD(extack,
					   "Inline mode is different between vports");
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
	int total_vports = esw->total_vports;
	struct mlx5_eswitch_rep *rep;
	int vport_index;
	u8 rep_type;

	esw->offloads.vport_reps = kcalloc(total_vports,
					   sizeof(struct mlx5_eswitch_rep),
					   GFP_KERNEL);
	if (!esw->offloads.vport_reps)
		return -ENOMEM;

	mlx5_esw_for_all_reps(esw, vport_index, rep) {
		rep->vport = mlx5_eswitch_index_to_vport_num(esw, vport_index);
		rep->vport_index = vport_index;

		for (rep_type = 0; rep_type < NUM_REP_TYPES; rep_type++)
			atomic_set(&rep->rep_data[rep_type].state,
				   REP_UNREGISTERED);
	}

	return 0;
}

static void __esw_offloads_unload_rep(struct mlx5_eswitch *esw,
				      struct mlx5_eswitch_rep *rep, u8 rep_type)
{
	if (atomic_cmpxchg(&rep->rep_data[rep_type].state,
			   REP_LOADED, REP_REGISTERED) == REP_LOADED)
		esw->offloads.rep_ops[rep_type]->unload(rep);
}

static void __unload_reps_all_vport(struct mlx5_eswitch *esw, u8 rep_type)
{
	struct mlx5_eswitch_rep *rep;
	int i;

	mlx5_esw_for_each_vf_rep_reverse(esw, i, rep, esw->esw_funcs.num_vfs)
		__esw_offloads_unload_rep(esw, rep, rep_type);

	if (mlx5_ecpf_vport_exists(esw->dev)) {
		rep = mlx5_eswitch_get_rep(esw, MLX5_VPORT_ECPF);
		__esw_offloads_unload_rep(esw, rep, rep_type);
	}

	if (mlx5_core_is_ecpf_esw_manager(esw->dev)) {
		rep = mlx5_eswitch_get_rep(esw, MLX5_VPORT_PF);
		__esw_offloads_unload_rep(esw, rep, rep_type);
	}

	rep = mlx5_eswitch_get_rep(esw, MLX5_VPORT_UPLINK);
	__esw_offloads_unload_rep(esw, rep, rep_type);
}

int esw_offloads_load_rep(struct mlx5_eswitch *esw, u16 vport_num)
{
	struct mlx5_eswitch_rep *rep;
	int rep_type;
	int err;

	if (esw->mode != MLX5_ESWITCH_OFFLOADS)
		return 0;

	rep = mlx5_eswitch_get_rep(esw, vport_num);
	for (rep_type = 0; rep_type < NUM_REP_TYPES; rep_type++)
		if (atomic_cmpxchg(&rep->rep_data[rep_type].state,
				   REP_REGISTERED, REP_LOADED) == REP_REGISTERED) {
			err = esw->offloads.rep_ops[rep_type]->load(esw->dev, rep);
			if (err)
				goto err_reps;
		}

	return 0;

err_reps:
	atomic_set(&rep->rep_data[rep_type].state, REP_REGISTERED);
	for (--rep_type; rep_type >= 0; rep_type--)
		__esw_offloads_unload_rep(esw, rep, rep_type);
	return err;
}

void esw_offloads_unload_rep(struct mlx5_eswitch *esw, u16 vport_num)
{
	struct mlx5_eswitch_rep *rep;
	int rep_type;

	if (esw->mode != MLX5_ESWITCH_OFFLOADS)
		return;

	rep = mlx5_eswitch_get_rep(esw, vport_num);
	for (rep_type = NUM_REP_TYPES - 1; rep_type >= 0; rep_type--)
		__esw_offloads_unload_rep(esw, rep, rep_type);
}

#define ESW_OFFLOADS_DEVCOM_PAIR	(0)
#define ESW_OFFLOADS_DEVCOM_UNPAIR	(1)

static int mlx5_esw_offloads_pair(struct mlx5_eswitch *esw,
				  struct mlx5_eswitch *peer_esw)
{
	int err;

	err = esw_add_fdb_peer_miss_rules(esw, peer_esw->dev);
	if (err)
		return err;

	return 0;
}

static void mlx5_esw_offloads_unpair(struct mlx5_eswitch *esw)
{
#if IS_ENABLED(CONFIG_MLX5_CLS_ACT)
	mlx5e_tc_clean_fdb_peer_flows(esw);
#endif
	esw_del_fdb_peer_miss_rules(esw);
}

static int mlx5_esw_offloads_set_ns_peer(struct mlx5_eswitch *esw,
					 struct mlx5_eswitch *peer_esw,
					 bool pair)
{
	struct mlx5_flow_root_namespace *peer_ns;
	struct mlx5_flow_root_namespace *ns;
	int err;

	peer_ns = peer_esw->dev->priv.steering->fdb_root_ns;
	ns = esw->dev->priv.steering->fdb_root_ns;

	if (pair) {
		err = mlx5_flow_namespace_set_peer(ns, peer_ns);
		if (err)
			return err;

		err = mlx5_flow_namespace_set_peer(peer_ns, ns);
		if (err) {
			mlx5_flow_namespace_set_peer(ns, NULL);
			return err;
		}
	} else {
		mlx5_flow_namespace_set_peer(ns, NULL);
		mlx5_flow_namespace_set_peer(peer_ns, NULL);
	}

	return 0;
}

static int mlx5_esw_offloads_devcom_event(int event,
					  void *my_data,
					  void *event_data)
{
	struct mlx5_eswitch *esw = my_data;
	struct mlx5_devcom *devcom = esw->dev->priv.devcom;
	struct mlx5_eswitch *peer_esw = event_data;
	int err;

	switch (event) {
	case ESW_OFFLOADS_DEVCOM_PAIR:
		if (mlx5_eswitch_vport_match_metadata_enabled(esw) !=
		    mlx5_eswitch_vport_match_metadata_enabled(peer_esw))
			break;

		err = mlx5_esw_offloads_set_ns_peer(esw, peer_esw, true);
		if (err)
			goto err_out;
		err = mlx5_esw_offloads_pair(esw, peer_esw);
		if (err)
			goto err_peer;

		err = mlx5_esw_offloads_pair(peer_esw, esw);
		if (err)
			goto err_pair;

		mlx5_devcom_set_paired(devcom, MLX5_DEVCOM_ESW_OFFLOADS, true);
		break;

	case ESW_OFFLOADS_DEVCOM_UNPAIR:
		if (!mlx5_devcom_is_paired(devcom, MLX5_DEVCOM_ESW_OFFLOADS))
			break;

		mlx5_devcom_set_paired(devcom, MLX5_DEVCOM_ESW_OFFLOADS, false);
		mlx5_esw_offloads_unpair(peer_esw);
		mlx5_esw_offloads_unpair(esw);
		mlx5_esw_offloads_set_ns_peer(esw, peer_esw, false);
		break;
	}

	return 0;

err_pair:
	mlx5_esw_offloads_unpair(esw);
err_peer:
	mlx5_esw_offloads_set_ns_peer(esw, peer_esw, false);
err_out:
	mlx5_core_err(esw->dev, "esw offloads devcom event failure, event %u err %d",
		      event, err);
	return err;
}

static void esw_offloads_devcom_init(struct mlx5_eswitch *esw)
{
	struct mlx5_devcom *devcom = esw->dev->priv.devcom;

	INIT_LIST_HEAD(&esw->offloads.peer_flows);
	mutex_init(&esw->offloads.peer_mutex);

	if (!MLX5_CAP_ESW(esw->dev, merged_eswitch))
		return;

	mlx5_devcom_register_component(devcom,
				       MLX5_DEVCOM_ESW_OFFLOADS,
				       mlx5_esw_offloads_devcom_event,
				       esw);

	mlx5_devcom_send_event(devcom,
			       MLX5_DEVCOM_ESW_OFFLOADS,
			       ESW_OFFLOADS_DEVCOM_PAIR, esw);
}

static void esw_offloads_devcom_cleanup(struct mlx5_eswitch *esw)
{
	struct mlx5_devcom *devcom = esw->dev->priv.devcom;

	if (!MLX5_CAP_ESW(esw->dev, merged_eswitch))
		return;

	mlx5_devcom_send_event(devcom, MLX5_DEVCOM_ESW_OFFLOADS,
			       ESW_OFFLOADS_DEVCOM_UNPAIR, esw);

	mlx5_devcom_unregister_component(devcom, MLX5_DEVCOM_ESW_OFFLOADS);
}

static bool
esw_check_vport_match_metadata_supported(const struct mlx5_eswitch *esw)
{
	if (!MLX5_CAP_ESW(esw->dev, esw_uplink_ingress_acl))
		return false;

	if (!(MLX5_CAP_ESW_FLOWTABLE(esw->dev, fdb_to_vport_reg_c_id) &
	      MLX5_FDB_TO_VPORT_REG_C_0))
		return false;

	if (!MLX5_CAP_ESW_FLOWTABLE(esw->dev, flow_source))
		return false;

	if (mlx5_core_is_ecpf_esw_manager(esw->dev) ||
	    mlx5_ecpf_vport_exists(esw->dev))
		return false;

	return true;
}

u32 mlx5_esw_match_metadata_alloc(struct mlx5_eswitch *esw)
{
	u32 num_vports = GENMASK(ESW_VPORT_BITS - 1, 0) - 1;
	u32 vhca_id_mask = GENMASK(ESW_VHCA_ID_BITS - 1, 0);
	u32 vhca_id = MLX5_CAP_GEN(esw->dev, vhca_id);
	u32 start;
	u32 end;
	int id;

	/* Make sure the vhca_id fits the ESW_VHCA_ID_BITS */
	WARN_ON_ONCE(vhca_id >= BIT(ESW_VHCA_ID_BITS));

	/* Trim vhca_id to ESW_VHCA_ID_BITS */
	vhca_id &= vhca_id_mask;

	start = (vhca_id << ESW_VPORT_BITS);
	end = start + num_vports;
	if (!vhca_id)
		start += 1; /* zero is reserved/invalid metadata */
	id = ida_alloc_range(&esw->offloads.vport_metadata_ida, start, end, GFP_KERNEL);

	return (id < 0) ? 0 : id;
}

void mlx5_esw_match_metadata_free(struct mlx5_eswitch *esw, u32 metadata)
{
	ida_free(&esw->offloads.vport_metadata_ida, metadata);
}

static int esw_offloads_vport_metadata_setup(struct mlx5_eswitch *esw,
					     struct mlx5_vport *vport)
{
	vport->default_metadata = mlx5_esw_match_metadata_alloc(esw);
	vport->metadata = vport->default_metadata;
	return vport->metadata ? 0 : -ENOSPC;
}

static void esw_offloads_vport_metadata_cleanup(struct mlx5_eswitch *esw,
						struct mlx5_vport *vport)
{
	if (!vport->default_metadata)
		return;

	WARN_ON(vport->metadata != vport->default_metadata);
	mlx5_esw_match_metadata_free(esw, vport->default_metadata);
}

static void esw_offloads_metadata_uninit(struct mlx5_eswitch *esw)
{
	struct mlx5_vport *vport;
	int i;

	if (!mlx5_eswitch_vport_match_metadata_enabled(esw))
		return;

	mlx5_esw_for_all_vports_reverse(esw, i, vport)
		esw_offloads_vport_metadata_cleanup(esw, vport);
}

static int esw_offloads_metadata_init(struct mlx5_eswitch *esw)
{
	struct mlx5_vport *vport;
	int err;
	int i;

	if (!mlx5_eswitch_vport_match_metadata_enabled(esw))
		return 0;

	mlx5_esw_for_all_vports(esw, i, vport) {
		err = esw_offloads_vport_metadata_setup(esw, vport);
		if (err)
			goto metadata_err;
	}

	return 0;

metadata_err:
	esw_offloads_metadata_uninit(esw);
	return err;
}

int
esw_vport_create_offloads_acl_tables(struct mlx5_eswitch *esw,
				     struct mlx5_vport *vport)
{
	int err;

	err = esw_acl_ingress_ofld_setup(esw, vport);
	if (err)
		return err;

	if (mlx5_eswitch_is_vf_vport(esw, vport->vport)) {
		err = esw_acl_egress_ofld_setup(esw, vport);
		if (err)
			goto egress_err;
	}

	return 0;

egress_err:
	esw_acl_ingress_ofld_cleanup(esw, vport);
	return err;
}

void
esw_vport_destroy_offloads_acl_tables(struct mlx5_eswitch *esw,
				      struct mlx5_vport *vport)
{
	esw_acl_egress_ofld_cleanup(vport);
	esw_acl_ingress_ofld_cleanup(esw, vport);
}

static int esw_create_uplink_offloads_acl_tables(struct mlx5_eswitch *esw)
{
	struct mlx5_vport *vport;

	vport = mlx5_eswitch_get_vport(esw, MLX5_VPORT_UPLINK);
	return esw_vport_create_offloads_acl_tables(esw, vport);
}

static void esw_destroy_uplink_offloads_acl_tables(struct mlx5_eswitch *esw)
{
	struct mlx5_vport *vport;

	vport = mlx5_eswitch_get_vport(esw, MLX5_VPORT_UPLINK);
	esw_vport_destroy_offloads_acl_tables(esw, vport);
}

static int esw_offloads_steering_init(struct mlx5_eswitch *esw)
{
	int err;

	memset(&esw->fdb_table.offloads, 0, sizeof(struct offloads_fdb));
	mutex_init(&esw->fdb_table.offloads.vports.lock);
	hash_init(esw->fdb_table.offloads.vports.table);

	err = esw_create_uplink_offloads_acl_tables(esw);
	if (err)
		goto create_acl_err;

	err = esw_create_offloads_table(esw);
	if (err)
		goto create_offloads_err;

	err = esw_create_restore_table(esw);
	if (err)
		goto create_restore_err;

	err = esw_create_offloads_fdb_tables(esw);
	if (err)
		goto create_fdb_err;

	err = esw_create_vport_rx_group(esw);
	if (err)
		goto create_fg_err;

	return 0;

create_fg_err:
	esw_destroy_offloads_fdb_tables(esw);
create_fdb_err:
	esw_destroy_restore_table(esw);
create_restore_err:
	esw_destroy_offloads_table(esw);
create_offloads_err:
	esw_destroy_uplink_offloads_acl_tables(esw);
create_acl_err:
	mutex_destroy(&esw->fdb_table.offloads.vports.lock);
	return err;
}

static void esw_offloads_steering_cleanup(struct mlx5_eswitch *esw)
{
	esw_destroy_vport_rx_group(esw);
	esw_destroy_offloads_fdb_tables(esw);
	esw_destroy_restore_table(esw);
	esw_destroy_offloads_table(esw);
	esw_destroy_uplink_offloads_acl_tables(esw);
	mutex_destroy(&esw->fdb_table.offloads.vports.lock);
}

static void
esw_vfs_changed_event_handler(struct mlx5_eswitch *esw, const u32 *out)
{
	bool host_pf_disabled;
	u16 new_num_vfs;

	new_num_vfs = MLX5_GET(query_esw_functions_out, out,
			       host_params_context.host_num_of_vfs);
	host_pf_disabled = MLX5_GET(query_esw_functions_out, out,
				    host_params_context.host_pf_disabled);

	if (new_num_vfs == esw->esw_funcs.num_vfs || host_pf_disabled)
		return;

	/* Number of VFs can only change from "0 to x" or "x to 0". */
	if (esw->esw_funcs.num_vfs > 0) {
		mlx5_eswitch_unload_vf_vports(esw, esw->esw_funcs.num_vfs);
	} else {
		int err;

		err = mlx5_eswitch_load_vf_vports(esw, new_num_vfs,
						  MLX5_VPORT_UC_ADDR_CHANGE);
		if (err)
			return;
	}
	esw->esw_funcs.num_vfs = new_num_vfs;
}

static void esw_functions_changed_event_handler(struct work_struct *work)
{
	struct mlx5_host_work *host_work;
	struct mlx5_eswitch *esw;
	const u32 *out;

	host_work = container_of(work, struct mlx5_host_work, work);
	esw = host_work->esw;

	out = mlx5_esw_query_functions(esw->dev);
	if (IS_ERR(out))
		goto out;

	esw_vfs_changed_event_handler(esw, out);
	kvfree(out);
out:
	kfree(host_work);
}

int mlx5_esw_funcs_changed_handler(struct notifier_block *nb, unsigned long type, void *data)
{
	struct mlx5_esw_functions *esw_funcs;
	struct mlx5_host_work *host_work;
	struct mlx5_eswitch *esw;

	host_work = kzalloc(sizeof(*host_work), GFP_ATOMIC);
	if (!host_work)
		return NOTIFY_DONE;

	esw_funcs = mlx5_nb_cof(nb, struct mlx5_esw_functions, nb);
	esw = container_of(esw_funcs, struct mlx5_eswitch, esw_funcs);

	host_work->esw = esw;

	INIT_WORK(&host_work->work, esw_functions_changed_event_handler);
	queue_work(esw->work_queue, &host_work->work);

	return NOTIFY_OK;
}

static int mlx5_esw_host_number_init(struct mlx5_eswitch *esw)
{
	const u32 *query_host_out;

	if (!mlx5_core_is_ecpf_esw_manager(esw->dev))
		return 0;

	query_host_out = mlx5_esw_query_functions(esw->dev);
	if (IS_ERR(query_host_out))
		return PTR_ERR(query_host_out);

	/* Mark non local controller with non zero controller number. */
	esw->offloads.host_number = MLX5_GET(query_esw_functions_out, query_host_out,
					     host_params_context.host_number);
	kvfree(query_host_out);
	return 0;
}

int esw_offloads_enable(struct mlx5_eswitch *esw)
{
	struct mlx5_vport *vport;
	int err, i;

	if (MLX5_CAP_ESW_FLOWTABLE_FDB(esw->dev, reformat) &&
	    MLX5_CAP_ESW_FLOWTABLE_FDB(esw->dev, decap))
		esw->offloads.encap = DEVLINK_ESWITCH_ENCAP_MODE_BASIC;
	else
		esw->offloads.encap = DEVLINK_ESWITCH_ENCAP_MODE_NONE;

	mutex_init(&esw->offloads.termtbl_mutex);
	mlx5_rdma_enable_roce(esw->dev);

	err = mlx5_esw_host_number_init(esw);
	if (err)
		goto err_metadata;

	if (esw_check_vport_match_metadata_supported(esw))
		esw->flags |= MLX5_ESWITCH_VPORT_MATCH_METADATA;

	err = esw_offloads_metadata_init(esw);
	if (err)
		goto err_metadata;

	err = esw_set_passing_vport_metadata(esw, true);
	if (err)
		goto err_vport_metadata;

	err = esw_offloads_steering_init(esw);
	if (err)
		goto err_steering_init;

	/* Representor will control the vport link state */
	mlx5_esw_for_each_vf_vport(esw, i, vport, esw->esw_funcs.num_vfs)
		vport->info.link_state = MLX5_VPORT_ADMIN_STATE_DOWN;

	/* Uplink vport rep must load first. */
	err = esw_offloads_load_rep(esw, MLX5_VPORT_UPLINK);
	if (err)
		goto err_uplink;

	err = mlx5_eswitch_enable_pf_vf_vports(esw, MLX5_VPORT_UC_ADDR_CHANGE);
	if (err)
		goto err_vports;

	esw_offloads_devcom_init(esw);

	return 0;

err_vports:
	esw_offloads_unload_rep(esw, MLX5_VPORT_UPLINK);
err_uplink:
	esw_offloads_steering_cleanup(esw);
err_steering_init:
	esw_set_passing_vport_metadata(esw, false);
err_vport_metadata:
	esw_offloads_metadata_uninit(esw);
err_metadata:
	esw->flags &= ~MLX5_ESWITCH_VPORT_MATCH_METADATA;
	mlx5_rdma_disable_roce(esw->dev);
	mutex_destroy(&esw->offloads.termtbl_mutex);
	return err;
}

static int esw_offloads_stop(struct mlx5_eswitch *esw,
			     struct netlink_ext_ack *extack)
{
	int err, err1;

	mlx5_eswitch_disable_locked(esw, false);
	err = mlx5_eswitch_enable_locked(esw, MLX5_ESWITCH_LEGACY,
					 MLX5_ESWITCH_IGNORE_NUM_VFS);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Failed setting eswitch to legacy");
		err1 = mlx5_eswitch_enable_locked(esw, MLX5_ESWITCH_OFFLOADS,
						  MLX5_ESWITCH_IGNORE_NUM_VFS);
		if (err1) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Failed setting eswitch back to offloads");
		}
	}

	return err;
}

void esw_offloads_disable(struct mlx5_eswitch *esw)
{
	esw_offloads_devcom_cleanup(esw);
	mlx5_eswitch_disable_pf_vf_vports(esw);
	esw_offloads_unload_rep(esw, MLX5_VPORT_UPLINK);
	esw_set_passing_vport_metadata(esw, false);
	esw_offloads_steering_cleanup(esw);
	esw_offloads_metadata_uninit(esw);
	esw->flags &= ~MLX5_ESWITCH_VPORT_MATCH_METADATA;
	mlx5_rdma_disable_roce(esw->dev);
	mutex_destroy(&esw->offloads.termtbl_mutex);
	esw->offloads.encap = DEVLINK_ESWITCH_ENCAP_MODE_NONE;
}

static int esw_mode_from_devlink(u16 mode, u16 *mlx5_mode)
{
	switch (mode) {
	case DEVLINK_ESWITCH_MODE_LEGACY:
		*mlx5_mode = MLX5_ESWITCH_LEGACY;
		break;
	case DEVLINK_ESWITCH_MODE_SWITCHDEV:
		*mlx5_mode = MLX5_ESWITCH_OFFLOADS;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int esw_mode_to_devlink(u16 mlx5_mode, u16 *mode)
{
	switch (mlx5_mode) {
	case MLX5_ESWITCH_LEGACY:
		*mode = DEVLINK_ESWITCH_MODE_LEGACY;
		break;
	case MLX5_ESWITCH_OFFLOADS:
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

static int eswitch_devlink_esw_mode_check(const struct mlx5_eswitch *esw)
{
	/* devlink commands in NONE eswitch mode are currently supported only
	 * on ECPF.
	 */
	return (esw->mode == MLX5_ESWITCH_NONE &&
		!mlx5_core_is_ecpf_esw_manager(esw->dev)) ? -EOPNOTSUPP : 0;
}

int mlx5_devlink_eswitch_mode_set(struct devlink *devlink, u16 mode,
				  struct netlink_ext_ack *extack)
{
	u16 cur_mlx5_mode, mlx5_mode = 0;
	struct mlx5_eswitch *esw;
	int err = 0;

	esw = mlx5_devlink_eswitch_get(devlink);
	if (IS_ERR(esw))
		return PTR_ERR(esw);

	if (esw_mode_from_devlink(mode, &mlx5_mode))
		return -EINVAL;

	mutex_lock(&esw->mode_lock);
	cur_mlx5_mode = esw->mode;
	if (cur_mlx5_mode == mlx5_mode)
		goto unlock;

	if (mode == DEVLINK_ESWITCH_MODE_SWITCHDEV)
		err = esw_offloads_start(esw, extack);
	else if (mode == DEVLINK_ESWITCH_MODE_LEGACY)
		err = esw_offloads_stop(esw, extack);
	else
		err = -EINVAL;

unlock:
	mutex_unlock(&esw->mode_lock);
	return err;
}

int mlx5_devlink_eswitch_mode_get(struct devlink *devlink, u16 *mode)
{
	struct mlx5_eswitch *esw;
	int err;

	esw = mlx5_devlink_eswitch_get(devlink);
	if (IS_ERR(esw))
		return PTR_ERR(esw);

	mutex_lock(&esw->mode_lock);
	err = eswitch_devlink_esw_mode_check(esw);
	if (err)
		goto unlock;

	err = esw_mode_to_devlink(esw->mode, mode);
unlock:
	mutex_unlock(&esw->mode_lock);
	return err;
}

int mlx5_devlink_eswitch_inline_mode_set(struct devlink *devlink, u8 mode,
					 struct netlink_ext_ack *extack)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	int err, vport, num_vport;
	struct mlx5_eswitch *esw;
	u8 mlx5_mode;

	esw = mlx5_devlink_eswitch_get(devlink);
	if (IS_ERR(esw))
		return PTR_ERR(esw);

	mutex_lock(&esw->mode_lock);
	err = eswitch_devlink_esw_mode_check(esw);
	if (err)
		goto out;

	switch (MLX5_CAP_ETH(dev, wqe_inline_mode)) {
	case MLX5_CAP_INLINE_MODE_NOT_REQUIRED:
		if (mode == DEVLINK_ESWITCH_INLINE_MODE_NONE)
			goto out;
		fallthrough;
	case MLX5_CAP_INLINE_MODE_L2:
		NL_SET_ERR_MSG_MOD(extack, "Inline mode can't be set");
		err = -EOPNOTSUPP;
		goto out;
	case MLX5_CAP_INLINE_MODE_VPORT_CONTEXT:
		break;
	}

	if (atomic64_read(&esw->offloads.num_flows) > 0) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Can't set inline mode when flows are configured");
		err = -EOPNOTSUPP;
		goto out;
	}

	err = esw_inline_mode_from_devlink(mode, &mlx5_mode);
	if (err)
		goto out;

	mlx5_esw_for_each_host_func_vport(esw, vport, esw->esw_funcs.num_vfs) {
		err = mlx5_modify_nic_vport_min_inline(dev, vport, mlx5_mode);
		if (err) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Failed to set min inline on vport");
			goto revert_inline_mode;
		}
	}

	esw->offloads.inline_mode = mlx5_mode;
	mutex_unlock(&esw->mode_lock);
	return 0;

revert_inline_mode:
	num_vport = --vport;
	mlx5_esw_for_each_host_func_vport_reverse(esw, vport, num_vport)
		mlx5_modify_nic_vport_min_inline(dev,
						 vport,
						 esw->offloads.inline_mode);
out:
	mutex_unlock(&esw->mode_lock);
	return err;
}

int mlx5_devlink_eswitch_inline_mode_get(struct devlink *devlink, u8 *mode)
{
	struct mlx5_eswitch *esw;
	int err;

	esw = mlx5_devlink_eswitch_get(devlink);
	if (IS_ERR(esw))
		return PTR_ERR(esw);

	mutex_lock(&esw->mode_lock);
	err = eswitch_devlink_esw_mode_check(esw);
	if (err)
		goto unlock;

	err = esw_inline_mode_to_devlink(esw->offloads.inline_mode, mode);
unlock:
	mutex_unlock(&esw->mode_lock);
	return err;
}

int mlx5_devlink_eswitch_encap_mode_set(struct devlink *devlink,
					enum devlink_eswitch_encap_mode encap,
					struct netlink_ext_ack *extack)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	struct mlx5_eswitch *esw;
	int err;

	esw = mlx5_devlink_eswitch_get(devlink);
	if (IS_ERR(esw))
		return PTR_ERR(esw);

	mutex_lock(&esw->mode_lock);
	err = eswitch_devlink_esw_mode_check(esw);
	if (err)
		goto unlock;

	if (encap != DEVLINK_ESWITCH_ENCAP_MODE_NONE &&
	    (!MLX5_CAP_ESW_FLOWTABLE_FDB(dev, reformat) ||
	     !MLX5_CAP_ESW_FLOWTABLE_FDB(dev, decap))) {
		err = -EOPNOTSUPP;
		goto unlock;
	}

	if (encap && encap != DEVLINK_ESWITCH_ENCAP_MODE_BASIC) {
		err = -EOPNOTSUPP;
		goto unlock;
	}

	if (esw->mode == MLX5_ESWITCH_LEGACY) {
		esw->offloads.encap = encap;
		goto unlock;
	}

	if (esw->offloads.encap == encap)
		goto unlock;

	if (atomic64_read(&esw->offloads.num_flows) > 0) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Can't set encapsulation when flows are configured");
		err = -EOPNOTSUPP;
		goto unlock;
	}

	esw_destroy_offloads_fdb_tables(esw);

	esw->offloads.encap = encap;

	err = esw_create_offloads_fdb_tables(esw);

	if (err) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Failed re-creating fast FDB table");
		esw->offloads.encap = !encap;
		(void)esw_create_offloads_fdb_tables(esw);
	}

unlock:
	mutex_unlock(&esw->mode_lock);
	return err;
}

int mlx5_devlink_eswitch_encap_mode_get(struct devlink *devlink,
					enum devlink_eswitch_encap_mode *encap)
{
	struct mlx5_eswitch *esw;
	int err;

	esw = mlx5_devlink_eswitch_get(devlink);
	if (IS_ERR(esw))
		return PTR_ERR(esw);


	mutex_lock(&esw->mode_lock);
	err = eswitch_devlink_esw_mode_check(esw);
	if (err)
		goto unlock;

	*encap = esw->offloads.encap;
unlock:
	mutex_unlock(&esw->mode_lock);
	return 0;
}

static bool
mlx5_eswitch_vport_has_rep(const struct mlx5_eswitch *esw, u16 vport_num)
{
	/* Currently, only ECPF based device has representor for host PF. */
	if (vport_num == MLX5_VPORT_PF &&
	    !mlx5_core_is_ecpf_esw_manager(esw->dev))
		return false;

	if (vport_num == MLX5_VPORT_ECPF &&
	    !mlx5_ecpf_vport_exists(esw->dev))
		return false;

	return true;
}

void mlx5_eswitch_register_vport_reps(struct mlx5_eswitch *esw,
				      const struct mlx5_eswitch_rep_ops *ops,
				      u8 rep_type)
{
	struct mlx5_eswitch_rep_data *rep_data;
	struct mlx5_eswitch_rep *rep;
	int i;

	esw->offloads.rep_ops[rep_type] = ops;
	mlx5_esw_for_all_reps(esw, i, rep) {
		if (likely(mlx5_eswitch_vport_has_rep(esw, i))) {
			rep_data = &rep->rep_data[rep_type];
			atomic_set(&rep_data->state, REP_REGISTERED);
		}
	}
}
EXPORT_SYMBOL(mlx5_eswitch_register_vport_reps);

void mlx5_eswitch_unregister_vport_reps(struct mlx5_eswitch *esw, u8 rep_type)
{
	struct mlx5_eswitch_rep *rep;
	int i;

	if (esw->mode == MLX5_ESWITCH_OFFLOADS)
		__unload_reps_all_vport(esw, rep_type);

	mlx5_esw_for_all_reps(esw, i, rep)
		atomic_set(&rep->rep_data[rep_type].state, REP_UNREGISTERED);
}
EXPORT_SYMBOL(mlx5_eswitch_unregister_vport_reps);

void *mlx5_eswitch_get_uplink_priv(struct mlx5_eswitch *esw, u8 rep_type)
{
	struct mlx5_eswitch_rep *rep;

	rep = mlx5_eswitch_get_rep(esw, MLX5_VPORT_UPLINK);
	return rep->rep_data[rep_type].priv;
}

void *mlx5_eswitch_get_proto_dev(struct mlx5_eswitch *esw,
				 u16 vport,
				 u8 rep_type)
{
	struct mlx5_eswitch_rep *rep;

	rep = mlx5_eswitch_get_rep(esw, vport);

	if (atomic_read(&rep->rep_data[rep_type].state) == REP_LOADED &&
	    esw->offloads.rep_ops[rep_type]->get_proto_dev)
		return esw->offloads.rep_ops[rep_type]->get_proto_dev(rep);
	return NULL;
}
EXPORT_SYMBOL(mlx5_eswitch_get_proto_dev);

void *mlx5_eswitch_uplink_get_proto_dev(struct mlx5_eswitch *esw, u8 rep_type)
{
	return mlx5_eswitch_get_proto_dev(esw, MLX5_VPORT_UPLINK, rep_type);
}
EXPORT_SYMBOL(mlx5_eswitch_uplink_get_proto_dev);

struct mlx5_eswitch_rep *mlx5_eswitch_vport_rep(struct mlx5_eswitch *esw,
						u16 vport)
{
	return mlx5_eswitch_get_rep(esw, vport);
}
EXPORT_SYMBOL(mlx5_eswitch_vport_rep);

bool mlx5_eswitch_is_vf_vport(const struct mlx5_eswitch *esw, u16 vport_num)
{
	return vport_num >= MLX5_VPORT_FIRST_VF &&
	       vport_num <= esw->dev->priv.sriov.max_vfs;
}

bool mlx5_eswitch_reg_c1_loopback_enabled(const struct mlx5_eswitch *esw)
{
	return !!(esw->flags & MLX5_ESWITCH_REG_C1_LOOPBACK_ENABLED);
}
EXPORT_SYMBOL(mlx5_eswitch_reg_c1_loopback_enabled);

bool mlx5_eswitch_vport_match_metadata_enabled(const struct mlx5_eswitch *esw)
{
	return !!(esw->flags & MLX5_ESWITCH_VPORT_MATCH_METADATA);
}
EXPORT_SYMBOL(mlx5_eswitch_vport_match_metadata_enabled);

u32 mlx5_eswitch_get_vport_metadata_for_match(struct mlx5_eswitch *esw,
					      u16 vport_num)
{
	struct mlx5_vport *vport = mlx5_eswitch_get_vport(esw, vport_num);

	if (WARN_ON_ONCE(IS_ERR(vport)))
		return 0;

	return vport->metadata << (32 - ESW_SOURCE_PORT_METADATA_BITS);
}
EXPORT_SYMBOL(mlx5_eswitch_get_vport_metadata_for_match);
