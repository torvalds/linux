// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021 Mellanox Technologies. */

#include <linux/etherdevice.h>
#include <linux/idr.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/mlx5_ifc.h>
#include <linux/mlx5/vport.h>
#include <linux/mlx5/fs.h>
#include "mlx5_core.h"
#include "eswitch.h"
#include "en.h"
#include "en_tc.h"
#include "fs_core.h"
#include "esw/indir_table.h"
#include "lib/fs_chains.h"
#include "en/mod_hdr.h"

#define MLX5_ESW_INDIR_TABLE_SIZE 128
#define MLX5_ESW_INDIR_TABLE_RECIRC_IDX_MAX (MLX5_ESW_INDIR_TABLE_SIZE - 2)
#define MLX5_ESW_INDIR_TABLE_FWD_IDX (MLX5_ESW_INDIR_TABLE_SIZE - 1)

struct mlx5_esw_indir_table_rule {
	struct list_head list;
	struct mlx5_flow_handle *handle;
	union {
		__be32 v4;
		struct in6_addr v6;
	} dst_ip;
	u32 vni;
	struct mlx5_modify_hdr *mh;
	refcount_t refcnt;
};

struct mlx5_esw_indir_table_entry {
	struct hlist_node hlist;
	struct mlx5_flow_table *ft;
	struct mlx5_flow_group *recirc_grp;
	struct mlx5_flow_group *fwd_grp;
	struct mlx5_flow_handle *fwd_rule;
	struct list_head recirc_rules;
	int recirc_cnt;
	int fwd_ref;

	u16 vport;
	u8 ip_version;
};

struct mlx5_esw_indir_table {
	struct mutex lock; /* protects table */
	DECLARE_HASHTABLE(table, 8);
};

struct mlx5_esw_indir_table *
mlx5_esw_indir_table_init(void)
{
	struct mlx5_esw_indir_table *indir = kvzalloc(sizeof(*indir), GFP_KERNEL);

	if (!indir)
		return ERR_PTR(-ENOMEM);

	mutex_init(&indir->lock);
	hash_init(indir->table);
	return indir;
}

void
mlx5_esw_indir_table_destroy(struct mlx5_esw_indir_table *indir)
{
	mutex_destroy(&indir->lock);
	kvfree(indir);
}

bool
mlx5_esw_indir_table_needed(struct mlx5_eswitch *esw,
			    struct mlx5_flow_attr *attr,
			    u16 vport_num,
			    struct mlx5_core_dev *dest_mdev)
{
	struct mlx5_esw_flow_attr *esw_attr = attr->esw_attr;

	/* Use indirect table for all IP traffic from UL to VF with vport
	 * destination when source rewrite flag is set.
	 */
	return esw_attr->in_rep->vport == MLX5_VPORT_UPLINK &&
		mlx5_eswitch_is_vf_vport(esw, vport_num) &&
		esw->dev == dest_mdev &&
		attr->ip_version &&
		attr->flags & MLX5_ATTR_FLAG_SRC_REWRITE;
}

u16
mlx5_esw_indir_table_decap_vport(struct mlx5_flow_attr *attr)
{
	struct mlx5_esw_flow_attr *esw_attr = attr->esw_attr;

	return esw_attr->rx_tun_attr ? esw_attr->rx_tun_attr->decap_vport : 0;
}

static struct mlx5_esw_indir_table_rule *
mlx5_esw_indir_table_rule_lookup(struct mlx5_esw_indir_table_entry *e,
				 struct mlx5_esw_flow_attr *attr)
{
	struct mlx5_esw_indir_table_rule *rule;

	list_for_each_entry(rule, &e->recirc_rules, list)
		if (rule->vni == attr->rx_tun_attr->vni &&
		    !memcmp(&rule->dst_ip, &attr->rx_tun_attr->dst_ip,
			    sizeof(attr->rx_tun_attr->dst_ip)))
			goto found;
	return NULL;

found:
	refcount_inc(&rule->refcnt);
	return rule;
}

static int mlx5_esw_indir_table_rule_get(struct mlx5_eswitch *esw,
					 struct mlx5_flow_attr *attr,
					 struct mlx5_flow_spec *spec,
					 struct mlx5_esw_indir_table_entry *e)
{
	struct mlx5_esw_flow_attr *esw_attr = attr->esw_attr;
	struct mlx5_fs_chains *chains = esw_chains(esw);
	struct mlx5e_tc_mod_hdr_acts mod_acts = {};
	struct mlx5_flow_destination dest = {};
	struct mlx5_esw_indir_table_rule *rule;
	struct mlx5_flow_act flow_act = {};
	struct mlx5_flow_spec *rule_spec;
	struct mlx5_flow_handle *handle;
	int err = 0;
	u32 data;

	rule = mlx5_esw_indir_table_rule_lookup(e, esw_attr);
	if (rule)
		return 0;

	if (e->recirc_cnt == MLX5_ESW_INDIR_TABLE_RECIRC_IDX_MAX)
		return -EINVAL;

	rule_spec = kvzalloc(sizeof(*rule_spec), GFP_KERNEL);
	if (!rule_spec)
		return -ENOMEM;

	rule = kzalloc(sizeof(*rule), GFP_KERNEL);
	if (!rule) {
		err = -ENOMEM;
		goto out;
	}

	rule_spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS |
					   MLX5_MATCH_MISC_PARAMETERS |
					   MLX5_MATCH_MISC_PARAMETERS_2;
	if (MLX5_CAP_FLOWTABLE_NIC_RX(esw->dev, ft_field_support.outer_ip_version)) {
		MLX5_SET(fte_match_param, rule_spec->match_criteria,
			 outer_headers.ip_version, 0xf);
		MLX5_SET(fte_match_param, rule_spec->match_value, outer_headers.ip_version,
			 attr->ip_version);
	} else if (attr->ip_version) {
		MLX5_SET_TO_ONES(fte_match_param, rule_spec->match_criteria,
				 outer_headers.ethertype);
		MLX5_SET(fte_match_param, rule_spec->match_value, outer_headers.ethertype,
			 (attr->ip_version == 4 ? ETH_P_IP : ETH_P_IPV6));
	} else {
		err = -EOPNOTSUPP;
		goto err_ethertype;
	}

	if (attr->ip_version == 4) {
		MLX5_SET_TO_ONES(fte_match_param, rule_spec->match_criteria,
				 outer_headers.dst_ipv4_dst_ipv6.ipv4_layout.ipv4);
		MLX5_SET(fte_match_param, rule_spec->match_value,
			 outer_headers.dst_ipv4_dst_ipv6.ipv4_layout.ipv4,
			 ntohl(esw_attr->rx_tun_attr->dst_ip.v4));
	} else if (attr->ip_version == 6) {
		int len = sizeof(struct in6_addr);

		memset(MLX5_ADDR_OF(fte_match_param, rule_spec->match_criteria,
				    outer_headers.dst_ipv4_dst_ipv6.ipv6_layout.ipv6),
		       0xff, len);
		memcpy(MLX5_ADDR_OF(fte_match_param, rule_spec->match_value,
				    outer_headers.dst_ipv4_dst_ipv6.ipv6_layout.ipv6),
		       &esw_attr->rx_tun_attr->dst_ip.v6, len);
	}

	MLX5_SET_TO_ONES(fte_match_param, rule_spec->match_criteria,
			 misc_parameters.vxlan_vni);
	MLX5_SET(fte_match_param, rule_spec->match_value, misc_parameters.vxlan_vni,
		 MLX5_GET(fte_match_param, spec->match_value, misc_parameters.vxlan_vni));

	MLX5_SET(fte_match_param, rule_spec->match_criteria,
		 misc_parameters_2.metadata_reg_c_0, mlx5_eswitch_get_vport_metadata_mask());
	MLX5_SET(fte_match_param, rule_spec->match_value, misc_parameters_2.metadata_reg_c_0,
		 mlx5_eswitch_get_vport_metadata_for_match(esw_attr->in_mdev->priv.eswitch,
							   MLX5_VPORT_UPLINK));

	/* Modify flow source to recirculate packet */
	data = mlx5_eswitch_get_vport_metadata_for_set(esw, esw_attr->rx_tun_attr->decap_vport);
	err = mlx5e_tc_match_to_reg_set(esw->dev, &mod_acts, MLX5_FLOW_NAMESPACE_FDB,
					VPORT_TO_REG, data);
	if (err)
		goto err_mod_hdr_regc0;

	err = mlx5e_tc_match_to_reg_set(esw->dev, &mod_acts, MLX5_FLOW_NAMESPACE_FDB,
					TUNNEL_TO_REG, ESW_TUN_SLOW_TABLE_GOTO_VPORT);
	if (err)
		goto err_mod_hdr_regc1;

	flow_act.modify_hdr = mlx5_modify_header_alloc(esw->dev, MLX5_FLOW_NAMESPACE_FDB,
						       mod_acts.num_actions, mod_acts.actions);
	if (IS_ERR(flow_act.modify_hdr)) {
		err = PTR_ERR(flow_act.modify_hdr);
		goto err_mod_hdr_alloc;
	}

	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST | MLX5_FLOW_CONTEXT_ACTION_MOD_HDR;
	flow_act.flags = FLOW_ACT_IGNORE_FLOW_LEVEL | FLOW_ACT_NO_APPEND;
	dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest.ft = mlx5_chains_get_table(chains, 0, 1, 0);
	if (IS_ERR(dest.ft)) {
		err = PTR_ERR(dest.ft);
		goto err_table;
	}
	handle = mlx5_add_flow_rules(e->ft, rule_spec, &flow_act, &dest, 1);
	if (IS_ERR(handle)) {
		err = PTR_ERR(handle);
		goto err_handle;
	}

	mlx5e_mod_hdr_dealloc(&mod_acts);
	rule->handle = handle;
	rule->vni = esw_attr->rx_tun_attr->vni;
	rule->mh = flow_act.modify_hdr;
	memcpy(&rule->dst_ip, &esw_attr->rx_tun_attr->dst_ip,
	       sizeof(esw_attr->rx_tun_attr->dst_ip));
	refcount_set(&rule->refcnt, 1);
	list_add(&rule->list, &e->recirc_rules);
	e->recirc_cnt++;
	goto out;

err_handle:
	mlx5_chains_put_table(chains, 0, 1, 0);
err_table:
	mlx5_modify_header_dealloc(esw->dev, flow_act.modify_hdr);
err_mod_hdr_alloc:
err_mod_hdr_regc1:
	mlx5e_mod_hdr_dealloc(&mod_acts);
err_mod_hdr_regc0:
err_ethertype:
	kfree(rule);
out:
	kvfree(rule_spec);
	return err;
}

static void mlx5_esw_indir_table_rule_put(struct mlx5_eswitch *esw,
					  struct mlx5_flow_attr *attr,
					  struct mlx5_esw_indir_table_entry *e)
{
	struct mlx5_esw_flow_attr *esw_attr = attr->esw_attr;
	struct mlx5_fs_chains *chains = esw_chains(esw);
	struct mlx5_esw_indir_table_rule *rule;

	list_for_each_entry(rule, &e->recirc_rules, list)
		if (rule->vni == esw_attr->rx_tun_attr->vni &&
		    !memcmp(&rule->dst_ip, &esw_attr->rx_tun_attr->dst_ip,
			    sizeof(esw_attr->rx_tun_attr->dst_ip)))
			goto found;

	return;

found:
	if (!refcount_dec_and_test(&rule->refcnt))
		return;

	mlx5_del_flow_rules(rule->handle);
	mlx5_chains_put_table(chains, 0, 1, 0);
	mlx5_modify_header_dealloc(esw->dev, rule->mh);
	list_del(&rule->list);
	kfree(rule);
	e->recirc_cnt--;
}

static int mlx5_create_indir_recirc_group(struct mlx5_eswitch *esw,
					  struct mlx5_flow_attr *attr,
					  struct mlx5_flow_spec *spec,
					  struct mlx5_esw_indir_table_entry *e)
{
	int err = 0, inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	u32 *in, *match;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(create_flow_group_in, in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS |
		 MLX5_MATCH_MISC_PARAMETERS | MLX5_MATCH_MISC_PARAMETERS_2);
	match = MLX5_ADDR_OF(create_flow_group_in, in, match_criteria);

	if (MLX5_CAP_FLOWTABLE_NIC_RX(esw->dev, ft_field_support.outer_ip_version))
		MLX5_SET(fte_match_param, match, outer_headers.ip_version, 0xf);
	else
		MLX5_SET_TO_ONES(fte_match_param, match, outer_headers.ethertype);

	if (attr->ip_version == 4) {
		MLX5_SET_TO_ONES(fte_match_param, match,
				 outer_headers.dst_ipv4_dst_ipv6.ipv4_layout.ipv4);
	} else if (attr->ip_version == 6) {
		memset(MLX5_ADDR_OF(fte_match_param, match,
				    outer_headers.dst_ipv4_dst_ipv6.ipv6_layout.ipv6),
		       0xff, sizeof(struct in6_addr));
	} else {
		err = -EOPNOTSUPP;
		goto out;
	}

	MLX5_SET_TO_ONES(fte_match_param, match, misc_parameters.vxlan_vni);
	MLX5_SET(fte_match_param, match, misc_parameters_2.metadata_reg_c_0,
		 mlx5_eswitch_get_vport_metadata_mask());
	MLX5_SET(create_flow_group_in, in, start_flow_index, 0);
	MLX5_SET(create_flow_group_in, in, end_flow_index, MLX5_ESW_INDIR_TABLE_RECIRC_IDX_MAX);
	e->recirc_grp = mlx5_create_flow_group(e->ft, in);
	if (IS_ERR(e->recirc_grp)) {
		err = PTR_ERR(e->recirc_grp);
		goto out;
	}

	INIT_LIST_HEAD(&e->recirc_rules);
	e->recirc_cnt = 0;

out:
	kvfree(in);
	return err;
}

static int mlx5_create_indir_fwd_group(struct mlx5_eswitch *esw,
				       struct mlx5_esw_indir_table_entry *e)
{
	int err = 0, inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_flow_destination dest = {};
	struct mlx5_flow_act flow_act = {};
	struct mlx5_flow_spec *spec;
	u32 *in;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec) {
		kvfree(in);
		return -ENOMEM;
	}

	/* Hold one entry */
	MLX5_SET(create_flow_group_in, in, start_flow_index, MLX5_ESW_INDIR_TABLE_FWD_IDX);
	MLX5_SET(create_flow_group_in, in, end_flow_index, MLX5_ESW_INDIR_TABLE_FWD_IDX);
	e->fwd_grp = mlx5_create_flow_group(e->ft, in);
	if (IS_ERR(e->fwd_grp)) {
		err = PTR_ERR(e->fwd_grp);
		goto err_out;
	}

	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	dest.type = MLX5_FLOW_DESTINATION_TYPE_VPORT;
	dest.vport.num = e->vport;
	dest.vport.vhca_id = MLX5_CAP_GEN(esw->dev, vhca_id);
	dest.vport.flags = MLX5_FLOW_DEST_VPORT_VHCA_ID;
	e->fwd_rule = mlx5_add_flow_rules(e->ft, spec, &flow_act, &dest, 1);
	if (IS_ERR(e->fwd_rule)) {
		mlx5_destroy_flow_group(e->fwd_grp);
		err = PTR_ERR(e->fwd_rule);
	}

err_out:
	kvfree(spec);
	kvfree(in);
	return err;
}

static struct mlx5_esw_indir_table_entry *
mlx5_esw_indir_table_entry_create(struct mlx5_eswitch *esw, struct mlx5_flow_attr *attr,
				  struct mlx5_flow_spec *spec, u16 vport, bool decap)
{
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_flow_namespace *root_ns;
	struct mlx5_esw_indir_table_entry *e;
	struct mlx5_flow_table *ft;
	int err = 0;

	root_ns = mlx5_get_flow_namespace(esw->dev, MLX5_FLOW_NAMESPACE_FDB);
	if (!root_ns)
		return ERR_PTR(-ENOENT);

	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (!e)
		return ERR_PTR(-ENOMEM);

	ft_attr.prio = FDB_TC_OFFLOAD;
	ft_attr.max_fte = MLX5_ESW_INDIR_TABLE_SIZE;
	ft_attr.flags = MLX5_FLOW_TABLE_UNMANAGED;
	ft_attr.level = 1;

	ft = mlx5_create_flow_table(root_ns, &ft_attr);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		goto tbl_err;
	}
	e->ft = ft;
	e->vport = vport;
	e->ip_version = attr->ip_version;
	e->fwd_ref = !decap;

	err = mlx5_create_indir_recirc_group(esw, attr, spec, e);
	if (err)
		goto recirc_grp_err;

	if (decap) {
		err = mlx5_esw_indir_table_rule_get(esw, attr, spec, e);
		if (err)
			goto recirc_rule_err;
	}

	err = mlx5_create_indir_fwd_group(esw, e);
	if (err)
		goto fwd_grp_err;

	hash_add(esw->fdb_table.offloads.indir->table, &e->hlist,
		 vport << 16 | attr->ip_version);

	return e;

fwd_grp_err:
	if (decap)
		mlx5_esw_indir_table_rule_put(esw, attr, e);
recirc_rule_err:
	mlx5_destroy_flow_group(e->recirc_grp);
recirc_grp_err:
	mlx5_destroy_flow_table(e->ft);
tbl_err:
	kfree(e);
	return ERR_PTR(err);
}

static struct mlx5_esw_indir_table_entry *
mlx5_esw_indir_table_entry_lookup(struct mlx5_eswitch *esw, u16 vport, u8 ip_version)
{
	struct mlx5_esw_indir_table_entry *e;
	u32 key = vport << 16 | ip_version;

	hash_for_each_possible(esw->fdb_table.offloads.indir->table, e, hlist, key)
		if (e->vport == vport && e->ip_version == ip_version)
			return e;

	return NULL;
}

struct mlx5_flow_table *mlx5_esw_indir_table_get(struct mlx5_eswitch *esw,
						 struct mlx5_flow_attr *attr,
						 struct mlx5_flow_spec *spec,
						 u16 vport, bool decap)
{
	struct mlx5_esw_indir_table_entry *e;
	int err;

	mutex_lock(&esw->fdb_table.offloads.indir->lock);
	e = mlx5_esw_indir_table_entry_lookup(esw, vport, attr->ip_version);
	if (e) {
		if (!decap) {
			e->fwd_ref++;
		} else {
			err = mlx5_esw_indir_table_rule_get(esw, attr, spec, e);
			if (err)
				goto out_err;
		}
	} else {
		e = mlx5_esw_indir_table_entry_create(esw, attr, spec, vport, decap);
		if (IS_ERR(e)) {
			err = PTR_ERR(e);
			esw_warn(esw->dev, "Failed to create indirection table, err %d.\n", err);
			goto out_err;
		}
	}
	mutex_unlock(&esw->fdb_table.offloads.indir->lock);
	return e->ft;

out_err:
	mutex_unlock(&esw->fdb_table.offloads.indir->lock);
	return ERR_PTR(err);
}

void mlx5_esw_indir_table_put(struct mlx5_eswitch *esw,
			      struct mlx5_flow_attr *attr,
			      u16 vport, bool decap)
{
	struct mlx5_esw_indir_table_entry *e;

	mutex_lock(&esw->fdb_table.offloads.indir->lock);
	e = mlx5_esw_indir_table_entry_lookup(esw, vport, attr->ip_version);
	if (!e)
		goto out;

	if (!decap)
		e->fwd_ref--;
	else
		mlx5_esw_indir_table_rule_put(esw, attr, e);

	if (e->fwd_ref || e->recirc_cnt)
		goto out;

	hash_del(&e->hlist);
	mlx5_destroy_flow_group(e->recirc_grp);
	mlx5_del_flow_rules(e->fwd_rule);
	mlx5_destroy_flow_group(e->fwd_grp);
	mlx5_destroy_flow_table(e->ft);
	kfree(e);
out:
	mutex_unlock(&esw->fdb_table.offloads.indir->lock);
}
