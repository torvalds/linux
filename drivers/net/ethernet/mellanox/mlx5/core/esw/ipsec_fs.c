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

	err = xa_alloc_bh(&ipsec->ipsec_obj_id_map, &mapped_id,
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
	xa_erase_bh(&ipsec->ipsec_obj_id_map, mapped_id);
	return err;
}

void mlx5_esw_ipsec_rx_id_mapping_remove(struct mlx5e_ipsec_sa_entry *sa_entry)
{
	struct mlx5e_ipsec *ipsec = sa_entry->ipsec;

	if (sa_entry->rx_mapped_id)
		xa_erase_bh(&ipsec->ipsec_obj_id_map,
			    sa_entry->rx_mapped_id);
}

int mlx5_esw_ipsec_rx_ipsec_obj_id_search(struct mlx5e_priv *priv, u32 id,
					  u32 *ipsec_obj_id)
{
	struct mlx5e_ipsec *ipsec = priv->ipsec;
	void *val;

	val = xa_load(&ipsec->ipsec_obj_id_map, id);
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

	mlx5_esw_for_each_rep(esw, i, rep) {
		if (atomic_read(&rep->rep_data[REP_ETH].state) != REP_LOADED)
			continue;

		rpriv = rep->rep_data[REP_ETH].priv;
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
