// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "fs_core.h"
#include "eswitch.h"
#include "en_accel/ipsec.h"
#include "esw/ipsec_fs.h"
#if IS_ENABLED(CONFIG_MLX5_CLS_ACT)
#include "en/tc_priv.h"
#endif

enum {
	MLX5_ESW_IPSEC_RX_POL_FT_LEVEL,
	MLX5_ESW_IPSEC_RX_ESP_FT_LEVEL,
	MLX5_ESW_IPSEC_RX_ESP_FT_CHK_LEVEL,
};

enum {
	MLX5_ESW_IPSEC_TX_POL_FT_LEVEL,
	MLX5_ESW_IPSEC_TX_ESP_FT_LEVEL,
	MLX5_ESW_IPSEC_TX_ESP_FT_CNT_LEVEL,
};

static void esw_ipsec_rx_status_drop_destroy(struct mlx5e_ipsec *ipsec,
					     struct mlx5e_ipsec_rx *rx)
{
	mlx5_del_flow_rules(rx->status_drop.rule);
	mlx5_destroy_flow_group(rx->status_drop.group);
	mlx5_fc_destroy(ipsec->mdev, rx->status_drop_cnt);
}

static void esw_ipsec_rx_status_pass_destroy(struct mlx5e_ipsec *ipsec,
					     struct mlx5e_ipsec_rx *rx)
{
	mlx5_del_flow_rules(rx->status.rule);
	mlx5_chains_put_table(esw_chains(ipsec->mdev->priv.eswitch), 0, 1, 0);
}

static int esw_ipsec_rx_status_drop_create(struct mlx5e_ipsec *ipsec,
					   struct mlx5e_ipsec_rx *rx)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_flow_table *ft = rx->ft.status;
	struct mlx5_core_dev *mdev = ipsec->mdev;
	struct mlx5_flow_destination dest = {};
	struct mlx5_flow_act flow_act = {};
	struct mlx5_flow_handle *rule;
	struct mlx5_fc *flow_counter;
	struct mlx5_flow_spec *spec;
	struct mlx5_flow_group *g;
	u32 *flow_group_in;
	int err = 0;

	flow_group_in = kvzalloc(inlen, GFP_KERNEL);
	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!flow_group_in || !spec) {
		err = -ENOMEM;
		goto err_out;
	}

	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, ft->max_fte - 1);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, ft->max_fte - 1);
	g = mlx5_create_flow_group(ft, flow_group_in);
	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		mlx5_core_err(mdev,
			      "Failed to add ipsec rx status drop flow group, err=%d\n", err);
		goto err_out;
	}

	flow_counter = mlx5_fc_create(mdev, false);
	if (IS_ERR(flow_counter)) {
		err = PTR_ERR(flow_counter);
		mlx5_core_err(mdev,
			      "Failed to add ipsec rx status drop rule counter, err=%d\n", err);
		goto err_cnt;
	}

	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_DROP | MLX5_FLOW_CONTEXT_ACTION_COUNT;
	dest.type = MLX5_FLOW_DESTINATION_TYPE_COUNTER;
	dest.counter_id = mlx5_fc_id(flow_counter);
	spec->flow_context.flow_source = MLX5_FLOW_CONTEXT_FLOW_SOURCE_UPLINK;
	rule = mlx5_add_flow_rules(ft, spec, &flow_act, &dest, 1);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(mdev,
			      "Failed to add ipsec rx status drop rule, err=%d\n", err);
		goto err_rule;
	}

	rx->status_drop.group = g;
	rx->status_drop.rule = rule;
	rx->status_drop_cnt = flow_counter;

	kvfree(flow_group_in);
	kvfree(spec);
	return 0;

err_rule:
	mlx5_fc_destroy(mdev, flow_counter);
err_cnt:
	mlx5_destroy_flow_group(g);
err_out:
	kvfree(flow_group_in);
	kvfree(spec);
	return err;
}

static int esw_ipsec_rx_status_pass_create(struct mlx5e_ipsec *ipsec,
					   struct mlx5e_ipsec_rx *rx,
					   struct mlx5_flow_destination *dest)
{
	struct mlx5_flow_act flow_act = {};
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_spec *spec;
	int err;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
			 misc_parameters_2.ipsec_syndrome);
	MLX5_SET(fte_match_param, spec->match_value,
		 misc_parameters_2.ipsec_syndrome, 0);
	spec->flow_context.flow_source = MLX5_FLOW_CONTEXT_FLOW_SOURCE_UPLINK;
	spec->match_criteria_enable = MLX5_MATCH_MISC_PARAMETERS_2;
	flow_act.flags = FLOW_ACT_NO_APPEND;
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST |
			  MLX5_FLOW_CONTEXT_ACTION_COUNT;
	rule = mlx5_add_flow_rules(rx->ft.status, spec, &flow_act, dest, 2);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_warn(ipsec->mdev,
			       "Failed to add ipsec rx status pass rule, err=%d\n", err);
		goto err_rule;
	}

	rx->status.rule = rule;
	kvfree(spec);
	return 0;

err_rule:
	kvfree(spec);
	return err;
}

void mlx5_esw_ipsec_rx_status_destroy(struct mlx5e_ipsec *ipsec,
				      struct mlx5e_ipsec_rx *rx)
{
	esw_ipsec_rx_status_pass_destroy(ipsec, rx);
	esw_ipsec_rx_status_drop_destroy(ipsec, rx);
}

int mlx5_esw_ipsec_rx_status_create(struct mlx5e_ipsec *ipsec,
				    struct mlx5e_ipsec_rx *rx,
				    struct mlx5_flow_destination *dest)
{
	int err;

	err = esw_ipsec_rx_status_drop_create(ipsec, rx);
	if (err)
		return err;

	err = esw_ipsec_rx_status_pass_create(ipsec, rx, dest);
	if (err)
		goto err_pass_create;

	return 0;

err_pass_create:
	esw_ipsec_rx_status_drop_destroy(ipsec, rx);
	return err;
}

void mlx5_esw_ipsec_rx_create_attr_set(struct mlx5e_ipsec *ipsec,
				       struct mlx5e_ipsec_rx_create_attr *attr)
{
	attr->prio = FDB_CRYPTO_INGRESS;
	attr->pol_level = MLX5_ESW_IPSEC_RX_POL_FT_LEVEL;
	attr->sa_level = MLX5_ESW_IPSEC_RX_ESP_FT_LEVEL;
	attr->status_level = MLX5_ESW_IPSEC_RX_ESP_FT_CHK_LEVEL;
	attr->chains_ns = MLX5_FLOW_NAMESPACE_FDB;
}

int mlx5_esw_ipsec_rx_status_pass_dest_get(struct mlx5e_ipsec *ipsec,
					   struct mlx5_flow_destination *dest)
{
	dest->type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest->ft = mlx5_chains_get_table(esw_chains(ipsec->mdev->priv.eswitch), 0, 1, 0);

	return 0;
}

int mlx5_esw_ipsec_rx_setup_modify_header(struct mlx5e_ipsec_sa_entry *sa_entry,
					  struct mlx5_flow_act *flow_act)
{
	u8 action[MLX5_UN_SZ_BYTES(set_add_copy_action_in_auto)] = {};
	struct mlx5e_ipsec *ipsec = sa_entry->ipsec;
	struct mlx5_core_dev *mdev = ipsec->mdev;
	struct mlx5_modify_hdr *modify_hdr;
	u32 mapped_id;
	int err;

	err = xa_alloc_bh(&ipsec->rx_esw->ipsec_obj_id_map, &mapped_id,
			  xa_mk_value(sa_entry->ipsec_obj_id),
			  XA_LIMIT(1, ESW_IPSEC_RX_MAPPED_ID_MASK), 0);
	if (err)
		return err;

	/* reuse tunnel bits for ipsec,
	 * tun_id is always 0 and tun_opts is mapped to ipsec_obj_id.
	 */
	MLX5_SET(set_action_in, action, action_type, MLX5_ACTION_TYPE_SET);
	MLX5_SET(set_action_in, action, field,
		 MLX5_ACTION_IN_FIELD_METADATA_REG_C_1);
	MLX5_SET(set_action_in, action, offset, ESW_ZONE_ID_BITS);
	MLX5_SET(set_action_in, action, length,
		 ESW_TUN_ID_BITS + ESW_TUN_OPTS_BITS);
	MLX5_SET(set_action_in, action, data, mapped_id);

	modify_hdr = mlx5_modify_header_alloc(mdev, MLX5_FLOW_NAMESPACE_FDB,
					      1, action);
	if (IS_ERR(modify_hdr)) {
		err = PTR_ERR(modify_hdr);
		goto err_header_alloc;
	}

	sa_entry->rx_mapped_id = mapped_id;
	flow_act->modify_hdr = modify_hdr;
	flow_act->action |= MLX5_FLOW_CONTEXT_ACTION_MOD_HDR;

	return 0;

err_header_alloc:
	xa_erase_bh(&ipsec->rx_esw->ipsec_obj_id_map, mapped_id);
	return err;
}

void mlx5_esw_ipsec_rx_id_mapping_remove(struct mlx5e_ipsec_sa_entry *sa_entry)
{
	struct mlx5e_ipsec *ipsec = sa_entry->ipsec;

	if (sa_entry->rx_mapped_id)
		xa_erase_bh(&ipsec->rx_esw->ipsec_obj_id_map,
			    sa_entry->rx_mapped_id);
}

int mlx5_esw_ipsec_rx_ipsec_obj_id_search(struct mlx5e_priv *priv, u32 id,
					  u32 *ipsec_obj_id)
{
	struct mlx5e_ipsec *ipsec = priv->ipsec;
	void *val;

	val = xa_load(&ipsec->rx_esw->ipsec_obj_id_map, id);
	if (!val)
		return -ENOENT;

	*ipsec_obj_id = xa_to_value(val);

	return 0;
}

void mlx5_esw_ipsec_tx_create_attr_set(struct mlx5e_ipsec *ipsec,
				       struct mlx5e_ipsec_tx_create_attr *attr)
{
	attr->prio = FDB_CRYPTO_EGRESS;
	attr->pol_level = MLX5_ESW_IPSEC_TX_POL_FT_LEVEL;
	attr->sa_level = MLX5_ESW_IPSEC_TX_ESP_FT_LEVEL;
	attr->cnt_level = MLX5_ESW_IPSEC_TX_ESP_FT_CNT_LEVEL;
	attr->chains_ns = MLX5_FLOW_NAMESPACE_FDB;
}

#if IS_ENABLED(CONFIG_MLX5_CLS_ACT)
static int mlx5_esw_ipsec_modify_flow_dests(struct mlx5_eswitch *esw,
					    struct mlx5e_tc_flow *flow)
{
	struct mlx5_esw_flow_attr *esw_attr;
	struct mlx5_flow_attr *attr;
	int err;

	attr = flow->attr;
	esw_attr = attr->esw_attr;
	if (esw_attr->out_count - esw_attr->split_count > 1)
		return 0;

	err = mlx5_eswitch_restore_ipsec_rule(esw, flow->rule[0], esw_attr,
					      esw_attr->out_count - 1);

	return err;
}
#endif

void mlx5_esw_ipsec_restore_dest_uplink(struct mlx5_core_dev *mdev)
{
#if IS_ENABLED(CONFIG_MLX5_CLS_ACT)
	struct mlx5_eswitch *esw = mdev->priv.eswitch;
	struct mlx5_eswitch_rep *rep;
	struct mlx5e_rep_priv *rpriv;
	struct rhashtable_iter iter;
	struct mlx5e_tc_flow *flow;
	unsigned long i;
	int err;

	xa_for_each(&esw->offloads.vport_reps, i, rep) {
		rpriv = rep->rep_data[REP_ETH].priv;
		if (!rpriv || !rpriv->netdev)
			continue;

		rhashtable_walk_enter(&rpriv->tc_ht, &iter);
		rhashtable_walk_start(&iter);
		while ((flow = rhashtable_walk_next(&iter)) != NULL) {
			if (IS_ERR(flow))
				continue;

			err = mlx5_esw_ipsec_modify_flow_dests(esw, flow);
			if (err)
				mlx5_core_warn_once(mdev,
						    "Failed to modify flow dests for IPsec");
		}
		rhashtable_walk_stop(&iter);
		rhashtable_walk_exit(&iter);
	}
#endif
}
