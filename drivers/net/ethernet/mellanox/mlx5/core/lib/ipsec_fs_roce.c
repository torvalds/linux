// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include "fs_core.h"
#include "fs_cmd.h"
#include "en.h"
#include "lib/ipsec_fs_roce.h"
#include "mlx5_core.h"
#include <linux/random.h>

struct mlx5_ipsec_miss {
	struct mlx5_flow_group *group;
	struct mlx5_flow_handle *rule;
};

struct mlx5_ipsec_rx_roce {
	struct mlx5_flow_group *g;
	struct mlx5_flow_table *ft;
	struct mlx5_flow_handle *rule;
	struct mlx5_ipsec_miss roce_miss;
	struct mlx5_flow_table *nic_master_ft;
	struct mlx5_flow_group *nic_master_group;
	struct mlx5_flow_handle *nic_master_rule;
	struct mlx5_flow_table *goto_alias_ft;
	u32 alias_id;
	char key[ACCESS_KEY_LEN];

	struct mlx5_flow_table *ft_rdma;
	struct mlx5_flow_namespace *ns_rdma;
};

struct mlx5_ipsec_tx_roce {
	struct mlx5_flow_group *g;
	struct mlx5_flow_table *ft;
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_table *goto_alias_ft;
	u32 alias_id;
	char key[ACCESS_KEY_LEN];
	struct mlx5_flow_namespace *ns;
};

struct mlx5_ipsec_fs {
	struct mlx5_ipsec_rx_roce ipv4_rx;
	struct mlx5_ipsec_rx_roce ipv6_rx;
	struct mlx5_ipsec_tx_roce tx;
	struct mlx5_devcom_comp_dev **devcom;
};

static void ipsec_fs_roce_setup_udp_dport(struct mlx5_flow_spec *spec,
					  u16 dport)
{
	spec->match_criteria_enable |= MLX5_MATCH_OUTER_HEADERS;
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.ip_protocol);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.ip_protocol, IPPROTO_UDP);
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.udp_dport);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.udp_dport, dport);
}

static bool ipsec_fs_create_alias_supported_one(struct mlx5_core_dev *mdev)
{
	u64 obj_allowed = MLX5_CAP_GEN_2_64(mdev, allowed_object_for_other_vhca_access);
	u32 obj_supp = MLX5_CAP_GEN_2(mdev, cross_vhca_object_to_object_supported);

	if (!(obj_supp &
	    MLX5_CROSS_VHCA_OBJ_TO_OBJ_SUPPORTED_LOCAL_FLOW_TABLE_TO_REMOTE_FLOW_TABLE_MISS))
		return false;

	if (!(obj_allowed & MLX5_ALLOWED_OBJ_FOR_OTHER_VHCA_ACCESS_FLOW_TABLE))
		return false;

	return true;
}

static bool ipsec_fs_create_alias_supported(struct mlx5_core_dev *mdev,
					    struct mlx5_core_dev *master_mdev)
{
	if (ipsec_fs_create_alias_supported_one(mdev) &&
	    ipsec_fs_create_alias_supported_one(master_mdev))
		return true;

	return false;
}

static int ipsec_fs_create_aliased_ft(struct mlx5_core_dev *ibv_owner,
				      struct mlx5_core_dev *ibv_allowed,
				      struct mlx5_flow_table *ft,
				      u32 *obj_id, char *alias_key, bool from_event)
{
	u32 aliased_object_id = (ft->type << FT_ID_FT_TYPE_OFFSET) | ft->id;
	u16 vhca_id_to_be_accessed = MLX5_CAP_GEN(ibv_owner, vhca_id);
	struct mlx5_cmd_allow_other_vhca_access_attr allow_attr = {};
	struct mlx5_cmd_alias_obj_create_attr alias_attr = {};
	int ret;
	int i;

	if (!ipsec_fs_create_alias_supported(ibv_owner, ibv_allowed))
		return -EOPNOTSUPP;

	for (i = 0; i < ACCESS_KEY_LEN; i++)
		if (!from_event)
			alias_key[i] = get_random_u64() & 0xFF;

	memcpy(allow_attr.access_key, alias_key, ACCESS_KEY_LEN);
	allow_attr.obj_type = MLX5_GENERAL_OBJECT_TYPES_FLOW_TABLE_ALIAS;
	allow_attr.obj_id = aliased_object_id;

	if (!from_event) {
		ret = mlx5_cmd_allow_other_vhca_access(ibv_owner, &allow_attr);
		if (ret) {
			mlx5_core_err(ibv_owner, "Failed to allow other vhca access err=%d\n",
				      ret);
			return ret;
		}
	}

	memcpy(alias_attr.access_key, alias_key, ACCESS_KEY_LEN);
	alias_attr.obj_id = aliased_object_id;
	alias_attr.obj_type = MLX5_GENERAL_OBJECT_TYPES_FLOW_TABLE_ALIAS;
	alias_attr.vhca_id = vhca_id_to_be_accessed;
	ret = mlx5_cmd_alias_obj_create(ibv_allowed, &alias_attr, obj_id);
	if (ret) {
		mlx5_core_err(ibv_allowed, "Failed to create alias object err=%d\n",
			      ret);
		return ret;
	}

	return 0;
}

static int
ipsec_fs_roce_rx_rule_setup(struct mlx5_core_dev *mdev,
			    struct mlx5_flow_destination *default_dst,
			    struct mlx5_ipsec_rx_roce *roce)
{
	bool is_mpv_slave = mlx5_core_is_mp_slave(mdev);
	struct mlx5_flow_destination dst = {};
	MLX5_DECLARE_FLOW_ACT(flow_act);
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_spec *spec;
	int err = 0;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	ipsec_fs_roce_setup_udp_dport(spec, ROCE_V2_UDP_DPORT);

	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	if (is_mpv_slave) {
		dst.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
		dst.ft = roce->goto_alias_ft;
	} else {
		dst.type = MLX5_FLOW_DESTINATION_TYPE_TABLE_TYPE;
		dst.ft = roce->ft_rdma;
	}
	rule = mlx5_add_flow_rules(roce->ft, spec, &flow_act, &dst, 1);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(mdev, "Fail to add RX RoCE IPsec rule err=%d\n",
			      err);
		goto out;
	}

	roce->rule = rule;

	memset(spec, 0, sizeof(*spec));
	if (default_dst->type == MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE)
		flow_act.flags |= FLOW_ACT_IGNORE_FLOW_LEVEL;
	rule = mlx5_add_flow_rules(roce->ft, spec, &flow_act, default_dst, 1);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(mdev, "Fail to add RX RoCE IPsec miss rule err=%d\n",
			      err);
		goto fail_add_default_rule;
	}

	roce->roce_miss.rule = rule;

	if (!is_mpv_slave)
		goto out;

	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	if (default_dst->type == MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE)
		flow_act.flags &= ~FLOW_ACT_IGNORE_FLOW_LEVEL;
	dst.type = MLX5_FLOW_DESTINATION_TYPE_TABLE_TYPE;
	dst.ft = roce->ft_rdma;
	rule = mlx5_add_flow_rules(roce->nic_master_ft, NULL, &flow_act, &dst,
				   1);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(mdev, "Fail to add RX RoCE IPsec rule for alias err=%d\n",
			      err);
		goto fail_add_nic_master_rule;
	}
	roce->nic_master_rule = rule;

	kvfree(spec);
	return 0;

fail_add_nic_master_rule:
	mlx5_del_flow_rules(roce->roce_miss.rule);
fail_add_default_rule:
	mlx5_del_flow_rules(roce->rule);
out:
	kvfree(spec);
	return err;
}

static int ipsec_fs_roce_tx_rule_setup(struct mlx5_core_dev *mdev,
				       struct mlx5_ipsec_tx_roce *roce,
				       struct mlx5_flow_table *pol_ft)
{
	struct mlx5_flow_destination dst = {};
	MLX5_DECLARE_FLOW_ACT(flow_act);
	struct mlx5_flow_handle *rule;
	int err = 0;

	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	dst.type = MLX5_FLOW_DESTINATION_TYPE_TABLE_TYPE;
	dst.ft = pol_ft;
	rule = mlx5_add_flow_rules(roce->ft, NULL, &flow_act, &dst,
				   1);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(mdev, "Fail to add TX RoCE IPsec rule err=%d\n",
			      err);
		goto out;
	}
	roce->rule = rule;

out:
	return err;
}

static int ipsec_fs_roce_tx_mpv_rule_setup(struct mlx5_core_dev *mdev,
					   struct mlx5_ipsec_tx_roce *roce,
					   struct mlx5_flow_table *pol_ft)
{
	struct mlx5_flow_destination dst = {};
	MLX5_DECLARE_FLOW_ACT(flow_act);
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_spec *spec;
	int err = 0;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	spec->match_criteria_enable = MLX5_MATCH_MISC_PARAMETERS;
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, misc_parameters.source_vhca_port);
	MLX5_SET(fte_match_param, spec->match_value, misc_parameters.source_vhca_port,
		 MLX5_CAP_GEN(mdev, native_port_num));

	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	dst.type = MLX5_FLOW_DESTINATION_TYPE_TABLE_TYPE;
	dst.ft = roce->goto_alias_ft;
	rule = mlx5_add_flow_rules(roce->ft, spec, &flow_act, &dst, 1);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(mdev, "Fail to add TX RoCE IPsec rule err=%d\n",
			      err);
		goto out;
	}
	roce->rule = rule;

	/* No need for miss rule, since on miss we go to next PRIO, in which
	 * if master is configured, he will catch the traffic to go to his
	 * encryption table.
	 */

out:
	kvfree(spec);
	return err;
}

#define MLX5_TX_ROCE_GROUP_SIZE BIT(0)
#define MLX5_IPSEC_RDMA_TX_FT_LEVEL 0
#define MLX5_IPSEC_NIC_GOTO_ALIAS_FT_LEVEL 3 /* Since last used level in NIC ipsec is 2 */

static int ipsec_fs_roce_tx_mpv_create_ft(struct mlx5_core_dev *mdev,
					  struct mlx5_ipsec_tx_roce *roce,
					  struct mlx5_flow_table *pol_ft,
					  struct mlx5e_priv *peer_priv,
					  bool from_event)
{
	struct mlx5_flow_namespace *roce_ns, *nic_ns;
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_flow_table next_ft;
	struct mlx5_flow_table *ft;
	int err;

	roce_ns = mlx5_get_flow_namespace(peer_priv->mdev, MLX5_FLOW_NAMESPACE_RDMA_TX_IPSEC);
	if (!roce_ns)
		return -EOPNOTSUPP;

	nic_ns = mlx5_get_flow_namespace(peer_priv->mdev, MLX5_FLOW_NAMESPACE_EGRESS_IPSEC);
	if (!nic_ns)
		return -EOPNOTSUPP;

	err = ipsec_fs_create_aliased_ft(mdev, peer_priv->mdev, pol_ft, &roce->alias_id, roce->key,
					 from_event);
	if (err)
		return err;

	next_ft.id = roce->alias_id;
	ft_attr.max_fte = 1;
	ft_attr.next_ft = &next_ft;
	ft_attr.level = MLX5_IPSEC_NIC_GOTO_ALIAS_FT_LEVEL;
	ft_attr.flags = MLX5_FLOW_TABLE_UNMANAGED;
	ft = mlx5_create_flow_table(nic_ns, &ft_attr);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		mlx5_core_err(mdev, "Fail to create RoCE IPsec goto alias ft err=%d\n", err);
		goto destroy_alias;
	}

	roce->goto_alias_ft = ft;

	memset(&ft_attr, 0, sizeof(ft_attr));
	ft_attr.max_fte = 1;
	ft_attr.level = MLX5_IPSEC_RDMA_TX_FT_LEVEL;
	ft = mlx5_create_flow_table(roce_ns, &ft_attr);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		mlx5_core_err(mdev, "Fail to create RoCE IPsec tx ft err=%d\n", err);
		goto destroy_alias_ft;
	}

	roce->ft = ft;

	return 0;

destroy_alias_ft:
	mlx5_destroy_flow_table(roce->goto_alias_ft);
destroy_alias:
	mlx5_cmd_alias_obj_destroy(peer_priv->mdev, roce->alias_id,
				   MLX5_GENERAL_OBJECT_TYPES_FLOW_TABLE_ALIAS);
	return err;
}

static int ipsec_fs_roce_tx_mpv_create_group_rules(struct mlx5_core_dev *mdev,
						   struct mlx5_ipsec_tx_roce *roce,
						   struct mlx5_flow_table *pol_ft,
						   u32 *in)
{
	struct mlx5_flow_group *g;
	int ix = 0;
	int err;
	u8 *mc;

	mc = MLX5_ADDR_OF(create_flow_group_in, in, match_criteria);
	MLX5_SET_TO_ONES(fte_match_param, mc, misc_parameters.source_vhca_port);
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_MISC_PARAMETERS);

	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5_TX_ROCE_GROUP_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	g = mlx5_create_flow_group(roce->ft, in);
	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		mlx5_core_err(mdev, "Fail to create RoCE IPsec tx group err=%d\n", err);
		return err;
	}
	roce->g = g;

	err = ipsec_fs_roce_tx_mpv_rule_setup(mdev, roce, pol_ft);
	if (err) {
		mlx5_core_err(mdev, "Fail to create RoCE IPsec tx rules err=%d\n", err);
		goto destroy_group;
	}

	return 0;

destroy_group:
	mlx5_destroy_flow_group(roce->g);
	return err;
}

static int ipsec_fs_roce_tx_mpv_create(struct mlx5_core_dev *mdev,
				       struct mlx5_ipsec_fs *ipsec_roce,
				       struct mlx5_flow_table *pol_ft,
				       u32 *in, bool from_event)
{
	struct mlx5_devcom_comp_dev *tmp = NULL;
	struct mlx5_ipsec_tx_roce *roce;
	struct mlx5e_priv *peer_priv;
	int err;

	if (!mlx5_devcom_for_each_peer_begin(*ipsec_roce->devcom))
		return -EOPNOTSUPP;

	peer_priv = mlx5_devcom_get_next_peer_data(*ipsec_roce->devcom, &tmp);
	if (!peer_priv || !peer_priv->ipsec) {
		mlx5_core_err(mdev, "IPsec not supported on master device\n");
		err = -EOPNOTSUPP;
		goto release_peer;
	}

	roce = &ipsec_roce->tx;

	err = ipsec_fs_roce_tx_mpv_create_ft(mdev, roce, pol_ft, peer_priv, from_event);
	if (err) {
		mlx5_core_err(mdev, "Fail to create RoCE IPsec tables err=%d\n", err);
		goto release_peer;
	}

	err = ipsec_fs_roce_tx_mpv_create_group_rules(mdev, roce, pol_ft, in);
	if (err) {
		mlx5_core_err(mdev, "Fail to create RoCE IPsec tx group/rule err=%d\n", err);
		goto destroy_tables;
	}

	mlx5_devcom_for_each_peer_end(*ipsec_roce->devcom);
	return 0;

destroy_tables:
	mlx5_destroy_flow_table(roce->ft);
	mlx5_destroy_flow_table(roce->goto_alias_ft);
	mlx5_cmd_alias_obj_destroy(peer_priv->mdev, roce->alias_id,
				   MLX5_GENERAL_OBJECT_TYPES_FLOW_TABLE_ALIAS);
release_peer:
	mlx5_devcom_for_each_peer_end(*ipsec_roce->devcom);
	return err;
}

static void roce_rx_mpv_destroy_tables(struct mlx5_core_dev *mdev, struct mlx5_ipsec_rx_roce *roce)
{
	mlx5_destroy_flow_table(roce->goto_alias_ft);
	mlx5_cmd_alias_obj_destroy(mdev, roce->alias_id,
				   MLX5_GENERAL_OBJECT_TYPES_FLOW_TABLE_ALIAS);
	mlx5_destroy_flow_group(roce->nic_master_group);
	mlx5_destroy_flow_table(roce->nic_master_ft);
}

#define MLX5_RX_ROCE_GROUP_SIZE BIT(0)
#define MLX5_IPSEC_RX_IPV4_FT_LEVEL 3
#define MLX5_IPSEC_RX_IPV6_FT_LEVEL 2

static int ipsec_fs_roce_rx_mpv_create(struct mlx5_core_dev *mdev,
				       struct mlx5_ipsec_fs *ipsec_roce,
				       struct mlx5_flow_namespace *ns,
				       u32 family, u32 level, u32 prio)
{
	struct mlx5_flow_namespace *roce_ns, *nic_ns;
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_devcom_comp_dev *tmp = NULL;
	struct mlx5_ipsec_rx_roce *roce;
	struct mlx5_flow_table next_ft;
	struct mlx5_flow_table *ft;
	struct mlx5_flow_group *g;
	struct mlx5e_priv *peer_priv;
	int ix = 0;
	u32 *in;
	int err;

	roce = (family == AF_INET) ? &ipsec_roce->ipv4_rx :
				     &ipsec_roce->ipv6_rx;

	if (!mlx5_devcom_for_each_peer_begin(*ipsec_roce->devcom))
		return -EOPNOTSUPP;

	peer_priv = mlx5_devcom_get_next_peer_data(*ipsec_roce->devcom, &tmp);
	if (!peer_priv || !peer_priv->ipsec) {
		mlx5_core_err(mdev, "IPsec not supported on master device\n");
		err = -EOPNOTSUPP;
		goto release_peer;
	}

	roce_ns = mlx5_get_flow_namespace(peer_priv->mdev, MLX5_FLOW_NAMESPACE_RDMA_RX_IPSEC);
	if (!roce_ns) {
		err = -EOPNOTSUPP;
		goto release_peer;
	}

	nic_ns = mlx5_get_flow_namespace(peer_priv->mdev, MLX5_FLOW_NAMESPACE_KERNEL);
	if (!nic_ns) {
		err = -EOPNOTSUPP;
		goto release_peer;
	}

	in = kvzalloc(MLX5_ST_SZ_BYTES(create_flow_group_in), GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		goto release_peer;
	}

	ft_attr.level = (family == AF_INET) ? MLX5_IPSEC_RX_IPV4_FT_LEVEL :
					      MLX5_IPSEC_RX_IPV6_FT_LEVEL;
	ft_attr.max_fte = 1;
	ft = mlx5_create_flow_table(roce_ns, &ft_attr);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		mlx5_core_err(mdev, "Fail to create RoCE IPsec rx ft at rdma master err=%d\n", err);
		goto free_in;
	}

	roce->ft_rdma = ft;

	ft_attr.max_fte = 1;
	ft_attr.prio = prio;
	ft_attr.level = level + 2;
	ft = mlx5_create_flow_table(nic_ns, &ft_attr);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		mlx5_core_err(mdev, "Fail to create RoCE IPsec rx ft at NIC master err=%d\n", err);
		goto destroy_ft_rdma;
	}
	roce->nic_master_ft = ft;

	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += 1;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	g = mlx5_create_flow_group(roce->nic_master_ft, in);
	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		mlx5_core_err(mdev, "Fail to create RoCE IPsec rx group aliased err=%d\n", err);
		goto destroy_nic_master_ft;
	}
	roce->nic_master_group = g;

	err = ipsec_fs_create_aliased_ft(peer_priv->mdev, mdev, roce->nic_master_ft,
					 &roce->alias_id, roce->key, false);
	if (err) {
		mlx5_core_err(mdev, "Fail to create RoCE IPsec rx alias FT err=%d\n", err);
		goto destroy_group;
	}

	next_ft.id = roce->alias_id;
	ft_attr.max_fte = 1;
	ft_attr.prio = prio;
	ft_attr.level = roce->ft->level + 1;
	ft_attr.flags = MLX5_FLOW_TABLE_UNMANAGED;
	ft_attr.next_ft = &next_ft;
	ft = mlx5_create_flow_table(ns, &ft_attr);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		mlx5_core_err(mdev, "Fail to create RoCE IPsec rx ft at NIC slave err=%d\n", err);
		goto destroy_alias;
	}
	roce->goto_alias_ft = ft;

	kvfree(in);
	mlx5_devcom_for_each_peer_end(*ipsec_roce->devcom);
	return 0;

destroy_alias:
	mlx5_cmd_alias_obj_destroy(mdev, roce->alias_id,
				   MLX5_GENERAL_OBJECT_TYPES_FLOW_TABLE_ALIAS);
destroy_group:
	mlx5_destroy_flow_group(roce->nic_master_group);
destroy_nic_master_ft:
	mlx5_destroy_flow_table(roce->nic_master_ft);
destroy_ft_rdma:
	mlx5_destroy_flow_table(roce->ft_rdma);
free_in:
	kvfree(in);
release_peer:
	mlx5_devcom_for_each_peer_end(*ipsec_roce->devcom);
	return err;
}

void mlx5_ipsec_fs_roce_tx_destroy(struct mlx5_ipsec_fs *ipsec_roce,
				   struct mlx5_core_dev *mdev)
{
	struct mlx5_devcom_comp_dev *tmp = NULL;
	struct mlx5_ipsec_tx_roce *tx_roce;
	struct mlx5e_priv *peer_priv;

	if (!ipsec_roce)
		return;

	tx_roce = &ipsec_roce->tx;

	if (!tx_roce->ft)
		return; /* Incase RoCE was cleaned from MPV event flow */

	mlx5_del_flow_rules(tx_roce->rule);
	mlx5_destroy_flow_group(tx_roce->g);
	mlx5_destroy_flow_table(tx_roce->ft);

	if (!mlx5_core_is_mp_slave(mdev))
		return;

	if (!mlx5_devcom_for_each_peer_begin(*ipsec_roce->devcom))
		return;

	peer_priv = mlx5_devcom_get_next_peer_data(*ipsec_roce->devcom, &tmp);
	if (!peer_priv) {
		mlx5_devcom_for_each_peer_end(*ipsec_roce->devcom);
		return;
	}

	mlx5_destroy_flow_table(tx_roce->goto_alias_ft);
	mlx5_cmd_alias_obj_destroy(peer_priv->mdev, tx_roce->alias_id,
				   MLX5_GENERAL_OBJECT_TYPES_FLOW_TABLE_ALIAS);
	mlx5_devcom_for_each_peer_end(*ipsec_roce->devcom);
	tx_roce->ft = NULL;
}

int mlx5_ipsec_fs_roce_tx_create(struct mlx5_core_dev *mdev,
				 struct mlx5_ipsec_fs *ipsec_roce,
				 struct mlx5_flow_table *pol_ft,
				 bool from_event)
{
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_ipsec_tx_roce *roce;
	struct mlx5_flow_table *ft;
	struct mlx5_flow_group *g;
	int ix = 0;
	int err;
	u32 *in;

	if (!ipsec_roce)
		return 0;

	roce = &ipsec_roce->tx;

	in = kvzalloc(MLX5_ST_SZ_BYTES(create_flow_group_in), GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	if (mlx5_core_is_mp_slave(mdev)) {
		err = ipsec_fs_roce_tx_mpv_create(mdev, ipsec_roce, pol_ft, in, from_event);
		goto free_in;
	}

	ft_attr.max_fte = 1;
	ft_attr.prio = 1;
	ft_attr.level = MLX5_IPSEC_RDMA_TX_FT_LEVEL;
	ft = mlx5_create_flow_table(roce->ns, &ft_attr);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		mlx5_core_err(mdev, "Fail to create RoCE IPsec tx ft err=%d\n", err);
		goto free_in;
	}

	roce->ft = ft;

	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5_TX_ROCE_GROUP_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	g = mlx5_create_flow_group(ft, in);
	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		mlx5_core_err(mdev, "Fail to create RoCE IPsec tx group err=%d\n", err);
		goto destroy_table;
	}
	roce->g = g;

	err = ipsec_fs_roce_tx_rule_setup(mdev, roce, pol_ft);
	if (err) {
		mlx5_core_err(mdev, "Fail to create RoCE IPsec tx rules err=%d\n", err);
		goto destroy_group;
	}

	kvfree(in);
	return 0;

destroy_group:
	mlx5_destroy_flow_group(roce->g);
destroy_table:
	mlx5_destroy_flow_table(ft);
free_in:
	kvfree(in);
	return err;
}

struct mlx5_flow_table *mlx5_ipsec_fs_roce_ft_get(struct mlx5_ipsec_fs *ipsec_roce, u32 family)
{
	struct mlx5_ipsec_rx_roce *rx_roce;

	if (!ipsec_roce)
		return NULL;

	rx_roce = (family == AF_INET) ? &ipsec_roce->ipv4_rx :
					&ipsec_roce->ipv6_rx;

	return rx_roce->ft;
}

void mlx5_ipsec_fs_roce_rx_destroy(struct mlx5_ipsec_fs *ipsec_roce, u32 family,
				   struct mlx5_core_dev *mdev)
{
	bool is_mpv_slave = mlx5_core_is_mp_slave(mdev);
	struct mlx5_ipsec_rx_roce *rx_roce;

	if (!ipsec_roce)
		return;

	rx_roce = (family == AF_INET) ? &ipsec_roce->ipv4_rx :
					&ipsec_roce->ipv6_rx;
	if (!rx_roce->ft)
		return; /* Incase RoCE was cleaned from MPV event flow */

	if (is_mpv_slave)
		mlx5_del_flow_rules(rx_roce->nic_master_rule);
	mlx5_del_flow_rules(rx_roce->roce_miss.rule);
	mlx5_del_flow_rules(rx_roce->rule);
	if (is_mpv_slave)
		roce_rx_mpv_destroy_tables(mdev, rx_roce);
	mlx5_destroy_flow_table(rx_roce->ft_rdma);
	mlx5_destroy_flow_group(rx_roce->roce_miss.group);
	mlx5_destroy_flow_group(rx_roce->g);
	mlx5_destroy_flow_table(rx_roce->ft);
	rx_roce->ft = NULL;
}

int mlx5_ipsec_fs_roce_rx_create(struct mlx5_core_dev *mdev,
				 struct mlx5_ipsec_fs *ipsec_roce,
				 struct mlx5_flow_namespace *ns,
				 struct mlx5_flow_destination *default_dst,
				 u32 family, u32 level, u32 prio)
{
	bool is_mpv_slave = mlx5_core_is_mp_slave(mdev);
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_ipsec_rx_roce *roce;
	struct mlx5_flow_table *ft;
	struct mlx5_flow_group *g;
	void *outer_headers_c;
	int ix = 0;
	u32 *in;
	int err;
	u8 *mc;

	if (!ipsec_roce)
		return 0;

	roce = (family == AF_INET) ? &ipsec_roce->ipv4_rx :
				     &ipsec_roce->ipv6_rx;

	ft_attr.max_fte = 2;
	ft_attr.level = level;
	ft_attr.prio = prio;
	ft = mlx5_create_flow_table(ns, &ft_attr);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		mlx5_core_err(mdev, "Fail to create RoCE IPsec rx ft at nic err=%d\n", err);
		return err;
	}

	roce->ft = ft;

	in = kvzalloc(MLX5_ST_SZ_BYTES(create_flow_group_in), GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		goto fail_nomem;
	}

	mc = MLX5_ADDR_OF(create_flow_group_in, in, match_criteria);
	outer_headers_c = MLX5_ADDR_OF(fte_match_param, mc, outer_headers);
	MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, outer_headers_c, ip_protocol);
	MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, outer_headers_c, udp_dport);

	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5_RX_ROCE_GROUP_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	g = mlx5_create_flow_group(ft, in);
	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		mlx5_core_err(mdev, "Fail to create RoCE IPsec rx group at nic err=%d\n", err);
		goto fail_group;
	}
	roce->g = g;

	memset(in, 0, MLX5_ST_SZ_BYTES(create_flow_group_in));
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5_RX_ROCE_GROUP_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	g = mlx5_create_flow_group(ft, in);
	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		mlx5_core_err(mdev, "Fail to create RoCE IPsec rx miss group at nic err=%d\n", err);
		goto fail_mgroup;
	}
	roce->roce_miss.group = g;

	if (is_mpv_slave) {
		err = ipsec_fs_roce_rx_mpv_create(mdev, ipsec_roce, ns, family, level, prio);
		if (err) {
			mlx5_core_err(mdev, "Fail to create RoCE IPsec rx alias err=%d\n", err);
			goto fail_mpv_create;
		}
	} else {
		memset(&ft_attr, 0, sizeof(ft_attr));
		if (family == AF_INET)
			ft_attr.level = 1;
		ft_attr.max_fte = 1;
		ft = mlx5_create_flow_table(roce->ns_rdma, &ft_attr);
		if (IS_ERR(ft)) {
			err = PTR_ERR(ft);
			mlx5_core_err(mdev,
				      "Fail to create RoCE IPsec rx ft at rdma err=%d\n", err);
			goto fail_rdma_table;
		}

		roce->ft_rdma = ft;
	}

	err = ipsec_fs_roce_rx_rule_setup(mdev, default_dst, roce);
	if (err) {
		mlx5_core_err(mdev, "Fail to create RoCE IPsec rx rules err=%d\n", err);
		goto fail_setup_rule;
	}

	kvfree(in);
	return 0;

fail_setup_rule:
	if (is_mpv_slave)
		roce_rx_mpv_destroy_tables(mdev, roce);
	mlx5_destroy_flow_table(roce->ft_rdma);
fail_mpv_create:
fail_rdma_table:
	mlx5_destroy_flow_group(roce->roce_miss.group);
fail_mgroup:
	mlx5_destroy_flow_group(roce->g);
fail_group:
	kvfree(in);
fail_nomem:
	mlx5_destroy_flow_table(roce->ft);
	return err;
}

bool mlx5_ipsec_fs_is_mpv_roce_supported(struct mlx5_core_dev *mdev)
{
	if (!mlx5_core_mp_enabled(mdev))
		return true;

	if (ipsec_fs_create_alias_supported_one(mdev))
		return true;

	return false;
}

void mlx5_ipsec_fs_roce_cleanup(struct mlx5_ipsec_fs *ipsec_roce)
{
	kfree(ipsec_roce);
}

struct mlx5_ipsec_fs *mlx5_ipsec_fs_roce_init(struct mlx5_core_dev *mdev,
					      struct mlx5_devcom_comp_dev **devcom)
{
	struct mlx5_ipsec_fs *roce_ipsec;
	struct mlx5_flow_namespace *ns;

	ns = mlx5_get_flow_namespace(mdev, MLX5_FLOW_NAMESPACE_RDMA_RX_IPSEC);
	if (!ns) {
		mlx5_core_err(mdev, "Failed to get RoCE rx ns\n");
		return NULL;
	}

	roce_ipsec = kzalloc(sizeof(*roce_ipsec), GFP_KERNEL);
	if (!roce_ipsec)
		return NULL;

	roce_ipsec->ipv4_rx.ns_rdma = ns;
	roce_ipsec->ipv6_rx.ns_rdma = ns;

	ns = mlx5_get_flow_namespace(mdev, MLX5_FLOW_NAMESPACE_RDMA_TX_IPSEC);
	if (!ns) {
		mlx5_core_err(mdev, "Failed to get RoCE tx ns\n");
		goto err_tx;
	}

	roce_ipsec->tx.ns = ns;

	roce_ipsec->devcom = devcom;

	return roce_ipsec;

err_tx:
	kfree(roce_ipsec);
	return NULL;
}
