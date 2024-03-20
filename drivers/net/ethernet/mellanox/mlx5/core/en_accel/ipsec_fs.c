// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020, Mellanox Technologies inc. All rights reserved. */

#include <linux/netdevice.h>
#include "en.h"
#include "en/fs.h"
#include "eswitch.h"
#include "ipsec.h"
#include "fs_core.h"
#include "lib/ipsec_fs_roce.h"
#include "lib/fs_chains.h"
#include "esw/ipsec_fs.h"
#include "en_rep.h"

#define NUM_IPSEC_FTE BIT(15)
#define MLX5_REFORMAT_TYPE_ADD_ESP_TRANSPORT_SIZE 16
#define IPSEC_TUNNEL_DEFAULT_TTL 0x40

struct mlx5e_ipsec_fc {
	struct mlx5_fc *cnt;
	struct mlx5_fc *drop;
};

struct mlx5e_ipsec_tx {
	struct mlx5e_ipsec_ft ft;
	struct mlx5e_ipsec_miss pol;
	struct mlx5e_ipsec_miss sa;
	struct mlx5e_ipsec_rule status;
	struct mlx5_flow_namespace *ns;
	struct mlx5e_ipsec_fc *fc;
	struct mlx5_fs_chains *chains;
	u8 allow_tunnel_mode : 1;
};

/* IPsec RX flow steering */
static enum mlx5_traffic_types family2tt(u32 family)
{
	if (family == AF_INET)
		return MLX5_TT_IPV4_IPSEC_ESP;
	return MLX5_TT_IPV6_IPSEC_ESP;
}

static struct mlx5e_ipsec_rx *ipsec_rx(struct mlx5e_ipsec *ipsec, u32 family, int type)
{
	if (ipsec->is_uplink_rep && type == XFRM_DEV_OFFLOAD_PACKET)
		return ipsec->rx_esw;

	if (family == AF_INET)
		return ipsec->rx_ipv4;

	return ipsec->rx_ipv6;
}

static struct mlx5e_ipsec_tx *ipsec_tx(struct mlx5e_ipsec *ipsec, int type)
{
	if (ipsec->is_uplink_rep && type == XFRM_DEV_OFFLOAD_PACKET)
		return ipsec->tx_esw;

	return ipsec->tx;
}

static struct mlx5_fs_chains *
ipsec_chains_create(struct mlx5_core_dev *mdev, struct mlx5_flow_table *miss_ft,
		    enum mlx5_flow_namespace_type ns, int base_prio,
		    int base_level, struct mlx5_flow_table **root_ft)
{
	struct mlx5_chains_attr attr = {};
	struct mlx5_fs_chains *chains;
	struct mlx5_flow_table *ft;
	int err;

	attr.flags = MLX5_CHAINS_AND_PRIOS_SUPPORTED |
		     MLX5_CHAINS_IGNORE_FLOW_LEVEL_SUPPORTED;
	attr.max_grp_num = 2;
	attr.default_ft = miss_ft;
	attr.ns = ns;
	attr.fs_base_prio = base_prio;
	attr.fs_base_level = base_level;
	chains = mlx5_chains_create(mdev, &attr);
	if (IS_ERR(chains))
		return chains;

	/* Create chain 0, prio 1, level 0 to connect chains to prev in fs_core */
	ft = mlx5_chains_get_table(chains, 0, 1, 0);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		goto err_chains_get;
	}

	*root_ft = ft;
	return chains;

err_chains_get:
	mlx5_chains_destroy(chains);
	return ERR_PTR(err);
}

static void ipsec_chains_destroy(struct mlx5_fs_chains *chains)
{
	mlx5_chains_put_table(chains, 0, 1, 0);
	mlx5_chains_destroy(chains);
}

static struct mlx5_flow_table *
ipsec_chains_get_table(struct mlx5_fs_chains *chains, u32 prio)
{
	return mlx5_chains_get_table(chains, 0, prio + 1, 0);
}

static void ipsec_chains_put_table(struct mlx5_fs_chains *chains, u32 prio)
{
	mlx5_chains_put_table(chains, 0, prio + 1, 0);
}

static struct mlx5_flow_table *ipsec_ft_create(struct mlx5_flow_namespace *ns,
					       int level, int prio,
					       int max_num_groups, u32 flags)
{
	struct mlx5_flow_table_attr ft_attr = {};

	ft_attr.autogroup.num_reserved_entries = 1;
	ft_attr.autogroup.max_num_groups = max_num_groups;
	ft_attr.max_fte = NUM_IPSEC_FTE;
	ft_attr.level = level;
	ft_attr.prio = prio;
	ft_attr.flags = flags;

	return mlx5_create_auto_grouped_flow_table(ns, &ft_attr);
}

static void ipsec_rx_status_drop_destroy(struct mlx5e_ipsec *ipsec,
					 struct mlx5e_ipsec_rx *rx)
{
	mlx5_del_flow_rules(rx->status_drop.rule);
	mlx5_destroy_flow_group(rx->status_drop.group);
	mlx5_fc_destroy(ipsec->mdev, rx->status_drop_cnt);
}

static void ipsec_rx_status_pass_destroy(struct mlx5e_ipsec *ipsec,
					 struct mlx5e_ipsec_rx *rx)
{
	mlx5_del_flow_rules(rx->status.rule);

	if (rx != ipsec->rx_esw)
		return;

#ifdef CONFIG_MLX5_ESWITCH
	mlx5_chains_put_table(esw_chains(ipsec->mdev->priv.eswitch), 0, 1, 0);
#endif
}

static int ipsec_rx_status_drop_create(struct mlx5e_ipsec *ipsec,
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
	if (rx == ipsec->rx_esw)
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

static int ipsec_rx_status_pass_create(struct mlx5e_ipsec *ipsec,
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
	if (rx == ipsec->rx_esw)
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

static void mlx5_ipsec_rx_status_destroy(struct mlx5e_ipsec *ipsec,
					 struct mlx5e_ipsec_rx *rx)
{
	ipsec_rx_status_pass_destroy(ipsec, rx);
	ipsec_rx_status_drop_destroy(ipsec, rx);
}

static int mlx5_ipsec_rx_status_create(struct mlx5e_ipsec *ipsec,
				       struct mlx5e_ipsec_rx *rx,
				       struct mlx5_flow_destination *dest)
{
	int err;

	err = ipsec_rx_status_drop_create(ipsec, rx);
	if (err)
		return err;

	err = ipsec_rx_status_pass_create(ipsec, rx, dest);
	if (err)
		goto err_pass_create;

	return 0;

err_pass_create:
	ipsec_rx_status_drop_destroy(ipsec, rx);
	return err;
}

static int ipsec_miss_create(struct mlx5_core_dev *mdev,
			     struct mlx5_flow_table *ft,
			     struct mlx5e_ipsec_miss *miss,
			     struct mlx5_flow_destination *dest)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	MLX5_DECLARE_FLOW_ACT(flow_act);
	struct mlx5_flow_spec *spec;
	u32 *flow_group_in;
	int err = 0;

	flow_group_in = kvzalloc(inlen, GFP_KERNEL);
	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!flow_group_in || !spec) {
		err = -ENOMEM;
		goto out;
	}

	/* Create miss_group */
	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, ft->max_fte - 1);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, ft->max_fte - 1);
	miss->group = mlx5_create_flow_group(ft, flow_group_in);
	if (IS_ERR(miss->group)) {
		err = PTR_ERR(miss->group);
		mlx5_core_err(mdev, "fail to create IPsec miss_group err=%d\n",
			      err);
		goto out;
	}

	/* Create miss rule */
	miss->rule = mlx5_add_flow_rules(ft, spec, &flow_act, dest, 1);
	if (IS_ERR(miss->rule)) {
		mlx5_destroy_flow_group(miss->group);
		err = PTR_ERR(miss->rule);
		mlx5_core_err(mdev, "fail to create IPsec miss_rule err=%d\n",
			      err);
		goto out;
	}
out:
	kvfree(flow_group_in);
	kvfree(spec);
	return err;
}

static void ipsec_rx_ft_disconnect(struct mlx5e_ipsec *ipsec, u32 family)
{
	struct mlx5_ttc_table *ttc = mlx5e_fs_get_ttc(ipsec->fs, false);

	mlx5_ttc_fwd_default_dest(ttc, family2tt(family));
}

static void rx_destroy(struct mlx5_core_dev *mdev, struct mlx5e_ipsec *ipsec,
		       struct mlx5e_ipsec_rx *rx, u32 family)
{
	/* disconnect */
	if (rx != ipsec->rx_esw)
		ipsec_rx_ft_disconnect(ipsec, family);

	if (rx->chains) {
		ipsec_chains_destroy(rx->chains);
	} else {
		mlx5_del_flow_rules(rx->pol.rule);
		mlx5_destroy_flow_group(rx->pol.group);
		mlx5_destroy_flow_table(rx->ft.pol);
	}

	mlx5_del_flow_rules(rx->sa.rule);
	mlx5_destroy_flow_group(rx->sa.group);
	mlx5_destroy_flow_table(rx->ft.sa);
	if (rx->allow_tunnel_mode)
		mlx5_eswitch_unblock_encap(mdev);
	mlx5_ipsec_rx_status_destroy(ipsec, rx);
	mlx5_destroy_flow_table(rx->ft.status);

	mlx5_ipsec_fs_roce_rx_destroy(ipsec->roce, family);
}

static void ipsec_rx_create_attr_set(struct mlx5e_ipsec *ipsec,
				     struct mlx5e_ipsec_rx *rx,
				     u32 family,
				     struct mlx5e_ipsec_rx_create_attr *attr)
{
	if (rx == ipsec->rx_esw) {
		/* For packet offload in switchdev mode, RX & TX use FDB namespace */
		attr->ns = ipsec->tx_esw->ns;
		mlx5_esw_ipsec_rx_create_attr_set(ipsec, attr);
		return;
	}

	attr->ns = mlx5e_fs_get_ns(ipsec->fs, false);
	attr->ttc = mlx5e_fs_get_ttc(ipsec->fs, false);
	attr->family = family;
	attr->prio = MLX5E_NIC_PRIO;
	attr->pol_level = MLX5E_ACCEL_FS_POL_FT_LEVEL;
	attr->sa_level = MLX5E_ACCEL_FS_ESP_FT_LEVEL;
	attr->status_level = MLX5E_ACCEL_FS_ESP_FT_ERR_LEVEL;
	attr->chains_ns = MLX5_FLOW_NAMESPACE_KERNEL;
}

static int ipsec_rx_status_pass_dest_get(struct mlx5e_ipsec *ipsec,
					 struct mlx5e_ipsec_rx *rx,
					 struct mlx5e_ipsec_rx_create_attr *attr,
					 struct mlx5_flow_destination *dest)
{
	struct mlx5_flow_table *ft;
	int err;

	if (rx == ipsec->rx_esw)
		return mlx5_esw_ipsec_rx_status_pass_dest_get(ipsec, dest);

	*dest = mlx5_ttc_get_default_dest(attr->ttc, family2tt(attr->family));
	err = mlx5_ipsec_fs_roce_rx_create(ipsec->mdev, ipsec->roce, attr->ns, dest,
					   attr->family, MLX5E_ACCEL_FS_ESP_FT_ROCE_LEVEL,
					   attr->prio);
	if (err)
		return err;

	ft = mlx5_ipsec_fs_roce_ft_get(ipsec->roce, attr->family);
	if (ft) {
		dest->type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
		dest->ft = ft;
	}

	return 0;
}

static void ipsec_rx_ft_connect(struct mlx5e_ipsec *ipsec,
				struct mlx5e_ipsec_rx *rx,
				struct mlx5e_ipsec_rx_create_attr *attr)
{
	struct mlx5_flow_destination dest = {};

	dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest.ft = rx->ft.pol;
	mlx5_ttc_fwd_dest(attr->ttc, family2tt(attr->family), &dest);
}

static int rx_create(struct mlx5_core_dev *mdev, struct mlx5e_ipsec *ipsec,
		     struct mlx5e_ipsec_rx *rx, u32 family)
{
	struct mlx5e_ipsec_rx_create_attr attr;
	struct mlx5_flow_destination dest[2];
	struct mlx5_flow_table *ft;
	u32 flags = 0;
	int err;

	ipsec_rx_create_attr_set(ipsec, rx, family, &attr);

	err = ipsec_rx_status_pass_dest_get(ipsec, rx, &attr, &dest[0]);
	if (err)
		return err;

	ft = ipsec_ft_create(attr.ns, attr.status_level, attr.prio, 1, 0);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		goto err_fs_ft_status;
	}
	rx->ft.status = ft;

	dest[1].type = MLX5_FLOW_DESTINATION_TYPE_COUNTER;
	dest[1].counter_id = mlx5_fc_id(rx->fc->cnt);
	err = mlx5_ipsec_rx_status_create(ipsec, rx, dest);
	if (err)
		goto err_add;

	/* Create FT */
	if (mlx5_ipsec_device_caps(mdev) & MLX5_IPSEC_CAP_TUNNEL)
		rx->allow_tunnel_mode = mlx5_eswitch_block_encap(mdev);
	if (rx->allow_tunnel_mode)
		flags = MLX5_FLOW_TABLE_TUNNEL_EN_REFORMAT;
	ft = ipsec_ft_create(attr.ns, attr.sa_level, attr.prio, 2, flags);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		goto err_fs_ft;
	}
	rx->ft.sa = ft;

	err = ipsec_miss_create(mdev, rx->ft.sa, &rx->sa, dest);
	if (err)
		goto err_fs;

	if (mlx5_ipsec_device_caps(mdev) & MLX5_IPSEC_CAP_PRIO) {
		rx->chains = ipsec_chains_create(mdev, rx->ft.sa,
						 attr.chains_ns,
						 attr.prio,
						 attr.pol_level,
						 &rx->ft.pol);
		if (IS_ERR(rx->chains)) {
			err = PTR_ERR(rx->chains);
			goto err_pol_ft;
		}

		goto connect;
	}

	ft = ipsec_ft_create(attr.ns, attr.pol_level, attr.prio, 2, 0);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		goto err_pol_ft;
	}
	rx->ft.pol = ft;
	memset(dest, 0x00, 2 * sizeof(*dest));
	dest[0].type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest[0].ft = rx->ft.sa;
	err = ipsec_miss_create(mdev, rx->ft.pol, &rx->pol, dest);
	if (err)
		goto err_pol_miss;

connect:
	/* connect */
	if (rx != ipsec->rx_esw)
		ipsec_rx_ft_connect(ipsec, rx, &attr);
	return 0;

err_pol_miss:
	mlx5_destroy_flow_table(rx->ft.pol);
err_pol_ft:
	mlx5_del_flow_rules(rx->sa.rule);
	mlx5_destroy_flow_group(rx->sa.group);
err_fs:
	mlx5_destroy_flow_table(rx->ft.sa);
err_fs_ft:
	if (rx->allow_tunnel_mode)
		mlx5_eswitch_unblock_encap(mdev);
	mlx5_del_flow_rules(rx->status.rule);
	mlx5_modify_header_dealloc(mdev, rx->status.modify_hdr);
err_add:
	mlx5_destroy_flow_table(rx->ft.status);
err_fs_ft_status:
	mlx5_ipsec_fs_roce_rx_destroy(ipsec->roce, family);
	return err;
}

static int rx_get(struct mlx5_core_dev *mdev, struct mlx5e_ipsec *ipsec,
		  struct mlx5e_ipsec_rx *rx, u32 family)
{
	int err;

	if (rx->ft.refcnt)
		goto skip;

	err = mlx5_eswitch_block_mode(mdev);
	if (err)
		return err;

	err = rx_create(mdev, ipsec, rx, family);
	if (err) {
		mlx5_eswitch_unblock_mode(mdev);
		return err;
	}

skip:
	rx->ft.refcnt++;
	return 0;
}

static void rx_put(struct mlx5e_ipsec *ipsec, struct mlx5e_ipsec_rx *rx,
		   u32 family)
{
	if (--rx->ft.refcnt)
		return;

	rx_destroy(ipsec->mdev, ipsec, rx, family);
	mlx5_eswitch_unblock_mode(ipsec->mdev);
}

static struct mlx5e_ipsec_rx *rx_ft_get(struct mlx5_core_dev *mdev,
					struct mlx5e_ipsec *ipsec, u32 family,
					int type)
{
	struct mlx5e_ipsec_rx *rx = ipsec_rx(ipsec, family, type);
	int err;

	mutex_lock(&rx->ft.mutex);
	err = rx_get(mdev, ipsec, rx, family);
	mutex_unlock(&rx->ft.mutex);
	if (err)
		return ERR_PTR(err);

	return rx;
}

static struct mlx5_flow_table *rx_ft_get_policy(struct mlx5_core_dev *mdev,
						struct mlx5e_ipsec *ipsec,
						u32 family, u32 prio, int type)
{
	struct mlx5e_ipsec_rx *rx = ipsec_rx(ipsec, family, type);
	struct mlx5_flow_table *ft;
	int err;

	mutex_lock(&rx->ft.mutex);
	err = rx_get(mdev, ipsec, rx, family);
	if (err)
		goto err_get;

	ft = rx->chains ? ipsec_chains_get_table(rx->chains, prio) : rx->ft.pol;
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		goto err_get_ft;
	}

	mutex_unlock(&rx->ft.mutex);
	return ft;

err_get_ft:
	rx_put(ipsec, rx, family);
err_get:
	mutex_unlock(&rx->ft.mutex);
	return ERR_PTR(err);
}

static void rx_ft_put(struct mlx5e_ipsec *ipsec, u32 family, int type)
{
	struct mlx5e_ipsec_rx *rx = ipsec_rx(ipsec, family, type);

	mutex_lock(&rx->ft.mutex);
	rx_put(ipsec, rx, family);
	mutex_unlock(&rx->ft.mutex);
}

static void rx_ft_put_policy(struct mlx5e_ipsec *ipsec, u32 family, u32 prio, int type)
{
	struct mlx5e_ipsec_rx *rx = ipsec_rx(ipsec, family, type);

	mutex_lock(&rx->ft.mutex);
	if (rx->chains)
		ipsec_chains_put_table(rx->chains, prio);

	rx_put(ipsec, rx, family);
	mutex_unlock(&rx->ft.mutex);
}

static int ipsec_counter_rule_tx(struct mlx5_core_dev *mdev, struct mlx5e_ipsec_tx *tx)
{
	struct mlx5_flow_destination dest = {};
	struct mlx5_flow_act flow_act = {};
	struct mlx5_flow_handle *fte;
	struct mlx5_flow_spec *spec;
	int err;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	/* create fte */
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_ALLOW |
			  MLX5_FLOW_CONTEXT_ACTION_COUNT;
	dest.type = MLX5_FLOW_DESTINATION_TYPE_COUNTER;
	dest.counter_id = mlx5_fc_id(tx->fc->cnt);
	fte = mlx5_add_flow_rules(tx->ft.status, spec, &flow_act, &dest, 1);
	if (IS_ERR(fte)) {
		err = PTR_ERR(fte);
		mlx5_core_err(mdev, "Fail to add ipsec tx counter rule err=%d\n", err);
		goto err_rule;
	}

	kvfree(spec);
	tx->status.rule = fte;
	return 0;

err_rule:
	kvfree(spec);
	return err;
}

/* IPsec TX flow steering */
static void tx_destroy(struct mlx5e_ipsec *ipsec, struct mlx5e_ipsec_tx *tx,
		       struct mlx5_ipsec_fs *roce)
{
	mlx5_ipsec_fs_roce_tx_destroy(roce);
	if (tx->chains) {
		ipsec_chains_destroy(tx->chains);
	} else {
		mlx5_del_flow_rules(tx->pol.rule);
		mlx5_destroy_flow_group(tx->pol.group);
		mlx5_destroy_flow_table(tx->ft.pol);
	}

	if (tx == ipsec->tx_esw) {
		mlx5_del_flow_rules(tx->sa.rule);
		mlx5_destroy_flow_group(tx->sa.group);
	}
	mlx5_destroy_flow_table(tx->ft.sa);
	if (tx->allow_tunnel_mode)
		mlx5_eswitch_unblock_encap(ipsec->mdev);
	mlx5_del_flow_rules(tx->status.rule);
	mlx5_destroy_flow_table(tx->ft.status);
}

static void ipsec_tx_create_attr_set(struct mlx5e_ipsec *ipsec,
				     struct mlx5e_ipsec_tx *tx,
				     struct mlx5e_ipsec_tx_create_attr *attr)
{
	if (tx == ipsec->tx_esw) {
		mlx5_esw_ipsec_tx_create_attr_set(ipsec, attr);
		return;
	}

	attr->prio = 0;
	attr->pol_level = 0;
	attr->sa_level = 1;
	attr->cnt_level = 2;
	attr->chains_ns = MLX5_FLOW_NAMESPACE_EGRESS_IPSEC;
}

static int tx_create(struct mlx5e_ipsec *ipsec, struct mlx5e_ipsec_tx *tx,
		     struct mlx5_ipsec_fs *roce)
{
	struct mlx5_core_dev *mdev = ipsec->mdev;
	struct mlx5e_ipsec_tx_create_attr attr;
	struct mlx5_flow_destination dest = {};
	struct mlx5_flow_table *ft;
	u32 flags = 0;
	int err;

	ipsec_tx_create_attr_set(ipsec, tx, &attr);
	ft = ipsec_ft_create(tx->ns, attr.cnt_level, attr.prio, 1, 0);
	if (IS_ERR(ft))
		return PTR_ERR(ft);
	tx->ft.status = ft;

	err = ipsec_counter_rule_tx(mdev, tx);
	if (err)
		goto err_status_rule;

	if (mlx5_ipsec_device_caps(mdev) & MLX5_IPSEC_CAP_TUNNEL)
		tx->allow_tunnel_mode = mlx5_eswitch_block_encap(mdev);
	if (tx->allow_tunnel_mode)
		flags = MLX5_FLOW_TABLE_TUNNEL_EN_REFORMAT;
	ft = ipsec_ft_create(tx->ns, attr.sa_level, attr.prio, 4, flags);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		goto err_sa_ft;
	}
	tx->ft.sa = ft;

	if (tx == ipsec->tx_esw) {
		dest.type = MLX5_FLOW_DESTINATION_TYPE_VPORT;
		dest.vport.num = MLX5_VPORT_UPLINK;
		err = ipsec_miss_create(mdev, tx->ft.sa, &tx->sa, &dest);
		if (err)
			goto err_sa_miss;
		memset(&dest, 0, sizeof(dest));
	}

	if (mlx5_ipsec_device_caps(mdev) & MLX5_IPSEC_CAP_PRIO) {
		tx->chains = ipsec_chains_create(
			mdev, tx->ft.sa, attr.chains_ns, attr.prio, attr.pol_level,
			&tx->ft.pol);
		if (IS_ERR(tx->chains)) {
			err = PTR_ERR(tx->chains);
			goto err_pol_ft;
		}

		goto connect_roce;
	}

	ft = ipsec_ft_create(tx->ns, attr.pol_level, attr.prio, 2, 0);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		goto err_pol_ft;
	}
	tx->ft.pol = ft;
	dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest.ft = tx->ft.sa;
	err = ipsec_miss_create(mdev, tx->ft.pol, &tx->pol, &dest);
	if (err) {
		mlx5_destroy_flow_table(tx->ft.pol);
		goto err_pol_ft;
	}

connect_roce:
	err = mlx5_ipsec_fs_roce_tx_create(mdev, roce, tx->ft.pol);
	if (err)
		goto err_roce;
	return 0;

err_roce:
	if (tx->chains) {
		ipsec_chains_destroy(tx->chains);
	} else {
		mlx5_del_flow_rules(tx->pol.rule);
		mlx5_destroy_flow_group(tx->pol.group);
		mlx5_destroy_flow_table(tx->ft.pol);
	}
err_pol_ft:
	if (tx == ipsec->tx_esw) {
		mlx5_del_flow_rules(tx->sa.rule);
		mlx5_destroy_flow_group(tx->sa.group);
	}
err_sa_miss:
	mlx5_destroy_flow_table(tx->ft.sa);
err_sa_ft:
	if (tx->allow_tunnel_mode)
		mlx5_eswitch_unblock_encap(mdev);
	mlx5_del_flow_rules(tx->status.rule);
err_status_rule:
	mlx5_destroy_flow_table(tx->ft.status);
	return err;
}

static void ipsec_esw_tx_ft_policy_set(struct mlx5_core_dev *mdev,
				       struct mlx5_flow_table *ft)
{
#ifdef CONFIG_MLX5_ESWITCH
	struct mlx5_eswitch *esw = mdev->priv.eswitch;
	struct mlx5e_rep_priv *uplink_rpriv;
	struct mlx5e_priv *priv;

	esw->offloads.ft_ipsec_tx_pol = ft;
	uplink_rpriv = mlx5_eswitch_get_uplink_priv(esw, REP_ETH);
	priv = netdev_priv(uplink_rpriv->netdev);
	if (!priv->channels.num)
		return;

	mlx5e_rep_deactivate_channels(priv);
	mlx5e_rep_activate_channels(priv);
#endif
}

static int tx_get(struct mlx5_core_dev *mdev, struct mlx5e_ipsec *ipsec,
		  struct mlx5e_ipsec_tx *tx)
{
	int err;

	if (tx->ft.refcnt)
		goto skip;

	err = mlx5_eswitch_block_mode(mdev);
	if (err)
		return err;

	err = tx_create(ipsec, tx, ipsec->roce);
	if (err) {
		mlx5_eswitch_unblock_mode(mdev);
		return err;
	}

	if (tx == ipsec->tx_esw)
		ipsec_esw_tx_ft_policy_set(mdev, tx->ft.pol);

skip:
	tx->ft.refcnt++;
	return 0;
}

static void tx_put(struct mlx5e_ipsec *ipsec, struct mlx5e_ipsec_tx *tx)
{
	if (--tx->ft.refcnt)
		return;

	if (tx == ipsec->tx_esw) {
		mlx5_esw_ipsec_restore_dest_uplink(ipsec->mdev);
		ipsec_esw_tx_ft_policy_set(ipsec->mdev, NULL);
	}

	tx_destroy(ipsec, tx, ipsec->roce);
	mlx5_eswitch_unblock_mode(ipsec->mdev);
}

static struct mlx5_flow_table *tx_ft_get_policy(struct mlx5_core_dev *mdev,
						struct mlx5e_ipsec *ipsec,
						u32 prio, int type)
{
	struct mlx5e_ipsec_tx *tx = ipsec_tx(ipsec, type);
	struct mlx5_flow_table *ft;
	int err;

	mutex_lock(&tx->ft.mutex);
	err = tx_get(mdev, ipsec, tx);
	if (err)
		goto err_get;

	ft = tx->chains ? ipsec_chains_get_table(tx->chains, prio) : tx->ft.pol;
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		goto err_get_ft;
	}

	mutex_unlock(&tx->ft.mutex);
	return ft;

err_get_ft:
	tx_put(ipsec, tx);
err_get:
	mutex_unlock(&tx->ft.mutex);
	return ERR_PTR(err);
}

static struct mlx5e_ipsec_tx *tx_ft_get(struct mlx5_core_dev *mdev,
					struct mlx5e_ipsec *ipsec, int type)
{
	struct mlx5e_ipsec_tx *tx = ipsec_tx(ipsec, type);
	int err;

	mutex_lock(&tx->ft.mutex);
	err = tx_get(mdev, ipsec, tx);
	mutex_unlock(&tx->ft.mutex);
	if (err)
		return ERR_PTR(err);

	return tx;
}

static void tx_ft_put(struct mlx5e_ipsec *ipsec, int type)
{
	struct mlx5e_ipsec_tx *tx = ipsec_tx(ipsec, type);

	mutex_lock(&tx->ft.mutex);
	tx_put(ipsec, tx);
	mutex_unlock(&tx->ft.mutex);
}

static void tx_ft_put_policy(struct mlx5e_ipsec *ipsec, u32 prio, int type)
{
	struct mlx5e_ipsec_tx *tx = ipsec_tx(ipsec, type);

	mutex_lock(&tx->ft.mutex);
	if (tx->chains)
		ipsec_chains_put_table(tx->chains, prio);

	tx_put(ipsec, tx);
	mutex_unlock(&tx->ft.mutex);
}

static void setup_fte_addr4(struct mlx5_flow_spec *spec, __be32 *saddr,
			    __be32 *daddr)
{
	if (!*saddr && !*daddr)
		return;

	spec->match_criteria_enable |= MLX5_MATCH_OUTER_HEADERS;

	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.ip_version);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.ip_version, 4);

	if (*saddr) {
		memcpy(MLX5_ADDR_OF(fte_match_param, spec->match_value,
				    outer_headers.src_ipv4_src_ipv6.ipv4_layout.ipv4), saddr, 4);
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
				 outer_headers.src_ipv4_src_ipv6.ipv4_layout.ipv4);
	}

	if (*daddr) {
		memcpy(MLX5_ADDR_OF(fte_match_param, spec->match_value,
				    outer_headers.dst_ipv4_dst_ipv6.ipv4_layout.ipv4), daddr, 4);
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
				 outer_headers.dst_ipv4_dst_ipv6.ipv4_layout.ipv4);
	}
}

static void setup_fte_addr6(struct mlx5_flow_spec *spec, __be32 *saddr,
			    __be32 *daddr)
{
	if (addr6_all_zero(saddr) && addr6_all_zero(daddr))
		return;

	spec->match_criteria_enable |= MLX5_MATCH_OUTER_HEADERS;

	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.ip_version);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.ip_version, 6);

	if (!addr6_all_zero(saddr)) {
		memcpy(MLX5_ADDR_OF(fte_match_param, spec->match_value,
				    outer_headers.src_ipv4_src_ipv6.ipv6_layout.ipv6), saddr, 16);
		memset(MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
				    outer_headers.src_ipv4_src_ipv6.ipv6_layout.ipv6), 0xff, 16);
	}

	if (!addr6_all_zero(daddr)) {
		memcpy(MLX5_ADDR_OF(fte_match_param, spec->match_value,
				    outer_headers.dst_ipv4_dst_ipv6.ipv6_layout.ipv6), daddr, 16);
		memset(MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
				    outer_headers.dst_ipv4_dst_ipv6.ipv6_layout.ipv6), 0xff, 16);
	}
}

static void setup_fte_esp(struct mlx5_flow_spec *spec)
{
	/* ESP header */
	spec->match_criteria_enable |= MLX5_MATCH_MISC_PARAMETERS;

	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.ip_protocol);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.ip_protocol, IPPROTO_ESP);
}

static void setup_fte_spi(struct mlx5_flow_spec *spec, u32 spi, bool encap)
{
	/* SPI number */
	spec->match_criteria_enable |= MLX5_MATCH_MISC_PARAMETERS;

	if (encap) {
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
				 misc_parameters.inner_esp_spi);
		MLX5_SET(fte_match_param, spec->match_value,
			 misc_parameters.inner_esp_spi, spi);
	} else {
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
				 misc_parameters.outer_esp_spi);
		MLX5_SET(fte_match_param, spec->match_value,
			 misc_parameters.outer_esp_spi, spi);
	}
}

static void setup_fte_no_frags(struct mlx5_flow_spec *spec)
{
	/* Non fragmented */
	spec->match_criteria_enable |= MLX5_MATCH_OUTER_HEADERS;

	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.frag);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.frag, 0);
}

static void setup_fte_reg_a(struct mlx5_flow_spec *spec)
{
	/* Add IPsec indicator in metadata_reg_a */
	spec->match_criteria_enable |= MLX5_MATCH_MISC_PARAMETERS_2;

	MLX5_SET(fte_match_param, spec->match_criteria,
		 misc_parameters_2.metadata_reg_a, MLX5_ETH_WQE_FT_META_IPSEC);
	MLX5_SET(fte_match_param, spec->match_value,
		 misc_parameters_2.metadata_reg_a, MLX5_ETH_WQE_FT_META_IPSEC);
}

static void setup_fte_reg_c4(struct mlx5_flow_spec *spec, u32 reqid)
{
	/* Pass policy check before choosing this SA */
	spec->match_criteria_enable |= MLX5_MATCH_MISC_PARAMETERS_2;

	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
			 misc_parameters_2.metadata_reg_c_4);
	MLX5_SET(fte_match_param, spec->match_value,
		 misc_parameters_2.metadata_reg_c_4, reqid);
}

static void setup_fte_upper_proto_match(struct mlx5_flow_spec *spec, struct upspec *upspec)
{
	switch (upspec->proto) {
	case IPPROTO_UDP:
		if (upspec->dport) {
			MLX5_SET(fte_match_set_lyr_2_4, spec->match_criteria,
				 udp_dport, upspec->dport_mask);
			MLX5_SET(fte_match_set_lyr_2_4, spec->match_value,
				 udp_dport, upspec->dport);
		}
		if (upspec->sport) {
			MLX5_SET(fte_match_set_lyr_2_4, spec->match_criteria,
				 udp_sport, upspec->sport_mask);
			MLX5_SET(fte_match_set_lyr_2_4, spec->match_value,
				 udp_sport, upspec->sport);
		}
		break;
	case IPPROTO_TCP:
		if (upspec->dport) {
			MLX5_SET(fte_match_set_lyr_2_4, spec->match_criteria,
				 tcp_dport, upspec->dport_mask);
			MLX5_SET(fte_match_set_lyr_2_4, spec->match_value,
				 tcp_dport, upspec->dport);
		}
		if (upspec->sport) {
			MLX5_SET(fte_match_set_lyr_2_4, spec->match_criteria,
				 tcp_sport, upspec->sport_mask);
			MLX5_SET(fte_match_set_lyr_2_4, spec->match_value,
				 tcp_sport, upspec->sport);
		}
		break;
	default:
		return;
	}

	spec->match_criteria_enable |= MLX5_MATCH_OUTER_HEADERS;
	MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, spec->match_criteria, ip_protocol);
	MLX5_SET(fte_match_set_lyr_2_4, spec->match_value, ip_protocol, upspec->proto);
}

static enum mlx5_flow_namespace_type ipsec_fs_get_ns(struct mlx5e_ipsec *ipsec,
						     int type, u8 dir)
{
	if (ipsec->is_uplink_rep && type == XFRM_DEV_OFFLOAD_PACKET)
		return MLX5_FLOW_NAMESPACE_FDB;

	if (dir == XFRM_DEV_OFFLOAD_IN)
		return MLX5_FLOW_NAMESPACE_KERNEL;

	return MLX5_FLOW_NAMESPACE_EGRESS;
}

static int setup_modify_header(struct mlx5e_ipsec *ipsec, int type, u32 val, u8 dir,
			       struct mlx5_flow_act *flow_act)
{
	enum mlx5_flow_namespace_type ns_type = ipsec_fs_get_ns(ipsec, type, dir);
	u8 action[MLX5_UN_SZ_BYTES(set_add_copy_action_in_auto)] = {};
	struct mlx5_core_dev *mdev = ipsec->mdev;
	struct mlx5_modify_hdr *modify_hdr;

	MLX5_SET(set_action_in, action, action_type, MLX5_ACTION_TYPE_SET);
	switch (dir) {
	case XFRM_DEV_OFFLOAD_IN:
		MLX5_SET(set_action_in, action, field,
			 MLX5_ACTION_IN_FIELD_METADATA_REG_B);
		break;
	case XFRM_DEV_OFFLOAD_OUT:
		MLX5_SET(set_action_in, action, field,
			 MLX5_ACTION_IN_FIELD_METADATA_REG_C_4);
		break;
	default:
		return -EINVAL;
	}

	MLX5_SET(set_action_in, action, data, val);
	MLX5_SET(set_action_in, action, offset, 0);
	MLX5_SET(set_action_in, action, length, 32);

	modify_hdr = mlx5_modify_header_alloc(mdev, ns_type, 1, action);
	if (IS_ERR(modify_hdr)) {
		mlx5_core_err(mdev, "Failed to allocate modify_header %ld\n",
			      PTR_ERR(modify_hdr));
		return PTR_ERR(modify_hdr);
	}

	flow_act->modify_hdr = modify_hdr;
	flow_act->action |= MLX5_FLOW_CONTEXT_ACTION_MOD_HDR;
	return 0;
}

static int
setup_pkt_tunnel_reformat(struct mlx5_core_dev *mdev,
			  struct mlx5_accel_esp_xfrm_attrs *attrs,
			  struct mlx5_pkt_reformat_params *reformat_params)
{
	struct ip_esp_hdr *esp_hdr;
	struct ipv6hdr *ipv6hdr;
	struct ethhdr *eth_hdr;
	struct iphdr *iphdr;
	char *reformatbf;
	size_t bfflen;
	void *hdr;

	bfflen = sizeof(*eth_hdr);

	if (attrs->dir == XFRM_DEV_OFFLOAD_OUT) {
		bfflen += sizeof(*esp_hdr) + 8;

		switch (attrs->family) {
		case AF_INET:
			bfflen += sizeof(*iphdr);
			break;
		case AF_INET6:
			bfflen += sizeof(*ipv6hdr);
			break;
		default:
			return -EINVAL;
		}
	}

	reformatbf = kzalloc(bfflen, GFP_KERNEL);
	if (!reformatbf)
		return -ENOMEM;

	eth_hdr = (struct ethhdr *)reformatbf;
	switch (attrs->family) {
	case AF_INET:
		eth_hdr->h_proto = htons(ETH_P_IP);
		break;
	case AF_INET6:
		eth_hdr->h_proto = htons(ETH_P_IPV6);
		break;
	default:
		goto free_reformatbf;
	}

	ether_addr_copy(eth_hdr->h_dest, attrs->dmac);
	ether_addr_copy(eth_hdr->h_source, attrs->smac);

	switch (attrs->dir) {
	case XFRM_DEV_OFFLOAD_IN:
		reformat_params->type = MLX5_REFORMAT_TYPE_L3_ESP_TUNNEL_TO_L2;
		break;
	case XFRM_DEV_OFFLOAD_OUT:
		reformat_params->type = MLX5_REFORMAT_TYPE_L2_TO_L3_ESP_TUNNEL;
		reformat_params->param_0 = attrs->authsize;

		hdr = reformatbf + sizeof(*eth_hdr);
		switch (attrs->family) {
		case AF_INET:
			iphdr = (struct iphdr *)hdr;
			memcpy(&iphdr->saddr, &attrs->saddr.a4, 4);
			memcpy(&iphdr->daddr, &attrs->daddr.a4, 4);
			iphdr->version = 4;
			iphdr->ihl = 5;
			iphdr->ttl = IPSEC_TUNNEL_DEFAULT_TTL;
			iphdr->protocol = IPPROTO_ESP;
			hdr += sizeof(*iphdr);
			break;
		case AF_INET6:
			ipv6hdr = (struct ipv6hdr *)hdr;
			memcpy(&ipv6hdr->saddr, &attrs->saddr.a6, 16);
			memcpy(&ipv6hdr->daddr, &attrs->daddr.a6, 16);
			ipv6hdr->nexthdr = IPPROTO_ESP;
			ipv6hdr->version = 6;
			ipv6hdr->hop_limit = IPSEC_TUNNEL_DEFAULT_TTL;
			hdr += sizeof(*ipv6hdr);
			break;
		default:
			goto free_reformatbf;
		}

		esp_hdr = (struct ip_esp_hdr *)hdr;
		esp_hdr->spi = htonl(attrs->spi);
		break;
	default:
		goto free_reformatbf;
	}

	reformat_params->size = bfflen;
	reformat_params->data = reformatbf;
	return 0;

free_reformatbf:
	kfree(reformatbf);
	return -EINVAL;
}

static int get_reformat_type(struct mlx5_accel_esp_xfrm_attrs *attrs)
{
	switch (attrs->dir) {
	case XFRM_DEV_OFFLOAD_IN:
		if (attrs->encap)
			return MLX5_REFORMAT_TYPE_DEL_ESP_TRANSPORT_OVER_UDP;
		return MLX5_REFORMAT_TYPE_DEL_ESP_TRANSPORT;
	case XFRM_DEV_OFFLOAD_OUT:
		if (attrs->family == AF_INET) {
			if (attrs->encap)
				return MLX5_REFORMAT_TYPE_ADD_ESP_TRANSPORT_OVER_UDPV4;
			return MLX5_REFORMAT_TYPE_ADD_ESP_TRANSPORT_OVER_IPV4;
		}

		if (attrs->encap)
			return MLX5_REFORMAT_TYPE_ADD_ESP_TRANSPORT_OVER_UDPV6;
		return MLX5_REFORMAT_TYPE_ADD_ESP_TRANSPORT_OVER_IPV6;
	default:
		WARN_ON(true);
	}

	return -EINVAL;
}

static int
setup_pkt_transport_reformat(struct mlx5_accel_esp_xfrm_attrs *attrs,
			     struct mlx5_pkt_reformat_params *reformat_params)
{
	struct udphdr *udphdr;
	char *reformatbf;
	size_t bfflen;
	__be32 spi;
	void *hdr;

	reformat_params->type = get_reformat_type(attrs);
	if (reformat_params->type < 0)
		return reformat_params->type;

	switch (attrs->dir) {
	case XFRM_DEV_OFFLOAD_IN:
		break;
	case XFRM_DEV_OFFLOAD_OUT:
		bfflen = MLX5_REFORMAT_TYPE_ADD_ESP_TRANSPORT_SIZE;
		if (attrs->encap)
			bfflen += sizeof(*udphdr);

		reformatbf = kzalloc(bfflen, GFP_KERNEL);
		if (!reformatbf)
			return -ENOMEM;

		hdr = reformatbf;
		if (attrs->encap) {
			udphdr = (struct udphdr *)reformatbf;
			udphdr->source = attrs->sport;
			udphdr->dest = attrs->dport;
			hdr += sizeof(*udphdr);
		}

		/* convert to network format */
		spi = htonl(attrs->spi);
		memcpy(hdr, &spi, sizeof(spi));

		reformat_params->param_0 = attrs->authsize;
		reformat_params->size = bfflen;
		reformat_params->data = reformatbf;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int setup_pkt_reformat(struct mlx5e_ipsec *ipsec,
			      struct mlx5_accel_esp_xfrm_attrs *attrs,
			      struct mlx5_flow_act *flow_act)
{
	enum mlx5_flow_namespace_type ns_type = ipsec_fs_get_ns(ipsec, attrs->type,
								attrs->dir);
	struct mlx5_pkt_reformat_params reformat_params = {};
	struct mlx5_core_dev *mdev = ipsec->mdev;
	struct mlx5_pkt_reformat *pkt_reformat;
	int ret;

	switch (attrs->mode) {
	case XFRM_MODE_TRANSPORT:
		ret = setup_pkt_transport_reformat(attrs, &reformat_params);
		break;
	case XFRM_MODE_TUNNEL:
		ret = setup_pkt_tunnel_reformat(mdev, attrs, &reformat_params);
		break;
	default:
		ret = -EINVAL;
	}

	if (ret)
		return ret;

	pkt_reformat =
		mlx5_packet_reformat_alloc(mdev, &reformat_params, ns_type);
	kfree(reformat_params.data);
	if (IS_ERR(pkt_reformat))
		return PTR_ERR(pkt_reformat);

	flow_act->pkt_reformat = pkt_reformat;
	flow_act->action |= MLX5_FLOW_CONTEXT_ACTION_PACKET_REFORMAT;
	return 0;
}

static int rx_add_rule(struct mlx5e_ipsec_sa_entry *sa_entry)
{
	struct mlx5_accel_esp_xfrm_attrs *attrs = &sa_entry->attrs;
	struct mlx5_core_dev *mdev = mlx5e_ipsec_sa2dev(sa_entry);
	struct mlx5e_ipsec *ipsec = sa_entry->ipsec;
	struct mlx5_flow_destination dest[2];
	struct mlx5_flow_act flow_act = {};
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_spec *spec;
	struct mlx5e_ipsec_rx *rx;
	struct mlx5_fc *counter;
	int err = 0;

	rx = rx_ft_get(mdev, ipsec, attrs->family, attrs->type);
	if (IS_ERR(rx))
		return PTR_ERR(rx);

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec) {
		err = -ENOMEM;
		goto err_alloc;
	}

	if (attrs->family == AF_INET)
		setup_fte_addr4(spec, &attrs->saddr.a4, &attrs->daddr.a4);
	else
		setup_fte_addr6(spec, attrs->saddr.a6, attrs->daddr.a6);

	setup_fte_spi(spec, attrs->spi, attrs->encap);
	if (!attrs->encap)
		setup_fte_esp(spec);
	setup_fte_no_frags(spec);
	setup_fte_upper_proto_match(spec, &attrs->upspec);

	if (rx != ipsec->rx_esw)
		err = setup_modify_header(ipsec, attrs->type,
					  sa_entry->ipsec_obj_id | BIT(31),
					  XFRM_DEV_OFFLOAD_IN, &flow_act);
	else
		err = mlx5_esw_ipsec_rx_setup_modify_header(sa_entry, &flow_act);

	if (err)
		goto err_mod_header;

	switch (attrs->type) {
	case XFRM_DEV_OFFLOAD_PACKET:
		err = setup_pkt_reformat(ipsec, attrs, &flow_act);
		if (err)
			goto err_pkt_reformat;
		break;
	default:
		break;
	}

	counter = mlx5_fc_create(mdev, true);
	if (IS_ERR(counter)) {
		err = PTR_ERR(counter);
		goto err_add_cnt;
	}
	flow_act.crypto.type = MLX5_FLOW_CONTEXT_ENCRYPT_DECRYPT_TYPE_IPSEC;
	flow_act.crypto.obj_id = sa_entry->ipsec_obj_id;
	flow_act.flags |= FLOW_ACT_NO_APPEND;
	flow_act.action |= MLX5_FLOW_CONTEXT_ACTION_CRYPTO_DECRYPT |
			   MLX5_FLOW_CONTEXT_ACTION_COUNT;
	if (attrs->drop)
		flow_act.action |= MLX5_FLOW_CONTEXT_ACTION_DROP;
	else
		flow_act.action |= MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	dest[0].type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest[0].ft = rx->ft.status;
	dest[1].type = MLX5_FLOW_DESTINATION_TYPE_COUNTER;
	dest[1].counter_id = mlx5_fc_id(counter);
	rule = mlx5_add_flow_rules(rx->ft.sa, spec, &flow_act, dest, 2);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(mdev, "fail to add RX ipsec rule err=%d\n", err);
		goto err_add_flow;
	}
	kvfree(spec);

	sa_entry->ipsec_rule.rule = rule;
	sa_entry->ipsec_rule.modify_hdr = flow_act.modify_hdr;
	sa_entry->ipsec_rule.fc = counter;
	sa_entry->ipsec_rule.pkt_reformat = flow_act.pkt_reformat;
	return 0;

err_add_flow:
	mlx5_fc_destroy(mdev, counter);
err_add_cnt:
	if (flow_act.pkt_reformat)
		mlx5_packet_reformat_dealloc(mdev, flow_act.pkt_reformat);
err_pkt_reformat:
	mlx5_modify_header_dealloc(mdev, flow_act.modify_hdr);
err_mod_header:
	kvfree(spec);
err_alloc:
	rx_ft_put(ipsec, attrs->family, attrs->type);
	return err;
}

static int tx_add_rule(struct mlx5e_ipsec_sa_entry *sa_entry)
{
	struct mlx5_accel_esp_xfrm_attrs *attrs = &sa_entry->attrs;
	struct mlx5_core_dev *mdev = mlx5e_ipsec_sa2dev(sa_entry);
	struct mlx5e_ipsec *ipsec = sa_entry->ipsec;
	struct mlx5_flow_destination dest[2];
	struct mlx5_flow_act flow_act = {};
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_spec *spec;
	struct mlx5e_ipsec_tx *tx;
	struct mlx5_fc *counter;
	int err;

	tx = tx_ft_get(mdev, ipsec, attrs->type);
	if (IS_ERR(tx))
		return PTR_ERR(tx);

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec) {
		err = -ENOMEM;
		goto err_alloc;
	}

	if (attrs->family == AF_INET)
		setup_fte_addr4(spec, &attrs->saddr.a4, &attrs->daddr.a4);
	else
		setup_fte_addr6(spec, attrs->saddr.a6, attrs->daddr.a6);

	setup_fte_no_frags(spec);
	setup_fte_upper_proto_match(spec, &attrs->upspec);

	switch (attrs->type) {
	case XFRM_DEV_OFFLOAD_CRYPTO:
		setup_fte_spi(spec, attrs->spi, false);
		setup_fte_esp(spec);
		setup_fte_reg_a(spec);
		break;
	case XFRM_DEV_OFFLOAD_PACKET:
		if (attrs->reqid)
			setup_fte_reg_c4(spec, attrs->reqid);
		err = setup_pkt_reformat(ipsec, attrs, &flow_act);
		if (err)
			goto err_pkt_reformat;
		break;
	default:
		break;
	}

	counter = mlx5_fc_create(mdev, true);
	if (IS_ERR(counter)) {
		err = PTR_ERR(counter);
		goto err_add_cnt;
	}

	flow_act.crypto.type = MLX5_FLOW_CONTEXT_ENCRYPT_DECRYPT_TYPE_IPSEC;
	flow_act.crypto.obj_id = sa_entry->ipsec_obj_id;
	flow_act.flags |= FLOW_ACT_NO_APPEND;
	flow_act.action |= MLX5_FLOW_CONTEXT_ACTION_CRYPTO_ENCRYPT |
			   MLX5_FLOW_CONTEXT_ACTION_COUNT;
	if (attrs->drop)
		flow_act.action |= MLX5_FLOW_CONTEXT_ACTION_DROP;
	else
		flow_act.action |= MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;

	dest[0].ft = tx->ft.status;
	dest[0].type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest[1].type = MLX5_FLOW_DESTINATION_TYPE_COUNTER;
	dest[1].counter_id = mlx5_fc_id(counter);
	rule = mlx5_add_flow_rules(tx->ft.sa, spec, &flow_act, dest, 2);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(mdev, "fail to add TX ipsec rule err=%d\n", err);
		goto err_add_flow;
	}

	kvfree(spec);
	sa_entry->ipsec_rule.rule = rule;
	sa_entry->ipsec_rule.fc = counter;
	sa_entry->ipsec_rule.pkt_reformat = flow_act.pkt_reformat;
	return 0;

err_add_flow:
	mlx5_fc_destroy(mdev, counter);
err_add_cnt:
	if (flow_act.pkt_reformat)
		mlx5_packet_reformat_dealloc(mdev, flow_act.pkt_reformat);
err_pkt_reformat:
	kvfree(spec);
err_alloc:
	tx_ft_put(ipsec, attrs->type);
	return err;
}

static int tx_add_policy(struct mlx5e_ipsec_pol_entry *pol_entry)
{
	struct mlx5_accel_pol_xfrm_attrs *attrs = &pol_entry->attrs;
	struct mlx5_core_dev *mdev = mlx5e_ipsec_pol2dev(pol_entry);
	struct mlx5e_ipsec *ipsec = pol_entry->ipsec;
	struct mlx5_flow_destination dest[2] = {};
	struct mlx5_flow_act flow_act = {};
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_spec *spec;
	struct mlx5_flow_table *ft;
	struct mlx5e_ipsec_tx *tx;
	int err, dstn = 0;

	ft = tx_ft_get_policy(mdev, ipsec, attrs->prio, attrs->type);
	if (IS_ERR(ft))
		return PTR_ERR(ft);

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec) {
		err = -ENOMEM;
		goto err_alloc;
	}

	tx = ipsec_tx(ipsec, attrs->type);
	if (attrs->family == AF_INET)
		setup_fte_addr4(spec, &attrs->saddr.a4, &attrs->daddr.a4);
	else
		setup_fte_addr6(spec, attrs->saddr.a6, attrs->daddr.a6);

	setup_fte_no_frags(spec);
	setup_fte_upper_proto_match(spec, &attrs->upspec);

	switch (attrs->action) {
	case XFRM_POLICY_ALLOW:
		flow_act.action |= MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
		if (!attrs->reqid)
			break;

		err = setup_modify_header(ipsec, attrs->type, attrs->reqid,
					  XFRM_DEV_OFFLOAD_OUT, &flow_act);
		if (err)
			goto err_mod_header;
		break;
	case XFRM_POLICY_BLOCK:
		flow_act.action |= MLX5_FLOW_CONTEXT_ACTION_DROP |
				   MLX5_FLOW_CONTEXT_ACTION_COUNT;
		dest[dstn].type = MLX5_FLOW_DESTINATION_TYPE_COUNTER;
		dest[dstn].counter_id = mlx5_fc_id(tx->fc->drop);
		dstn++;
		break;
	default:
		WARN_ON(true);
		err = -EINVAL;
		goto err_mod_header;
	}

	flow_act.flags |= FLOW_ACT_NO_APPEND;
	if (tx == ipsec->tx_esw && tx->chains)
		flow_act.flags |= FLOW_ACT_IGNORE_FLOW_LEVEL;
	dest[dstn].ft = tx->ft.sa;
	dest[dstn].type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dstn++;
	rule = mlx5_add_flow_rules(ft, spec, &flow_act, dest, dstn);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(mdev, "fail to add TX ipsec rule err=%d\n", err);
		goto err_action;
	}

	kvfree(spec);
	pol_entry->ipsec_rule.rule = rule;
	pol_entry->ipsec_rule.modify_hdr = flow_act.modify_hdr;
	return 0;

err_action:
	if (flow_act.modify_hdr)
		mlx5_modify_header_dealloc(mdev, flow_act.modify_hdr);
err_mod_header:
	kvfree(spec);
err_alloc:
	tx_ft_put_policy(ipsec, attrs->prio, attrs->type);
	return err;
}

static int rx_add_policy(struct mlx5e_ipsec_pol_entry *pol_entry)
{
	struct mlx5_accel_pol_xfrm_attrs *attrs = &pol_entry->attrs;
	struct mlx5_core_dev *mdev = mlx5e_ipsec_pol2dev(pol_entry);
	struct mlx5e_ipsec *ipsec = pol_entry->ipsec;
	struct mlx5_flow_destination dest[2];
	struct mlx5_flow_act flow_act = {};
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_spec *spec;
	struct mlx5_flow_table *ft;
	struct mlx5e_ipsec_rx *rx;
	int err, dstn = 0;

	ft = rx_ft_get_policy(mdev, pol_entry->ipsec, attrs->family, attrs->prio,
			      attrs->type);
	if (IS_ERR(ft))
		return PTR_ERR(ft);

	rx = ipsec_rx(pol_entry->ipsec, attrs->family, attrs->type);

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec) {
		err = -ENOMEM;
		goto err_alloc;
	}

	if (attrs->family == AF_INET)
		setup_fte_addr4(spec, &attrs->saddr.a4, &attrs->daddr.a4);
	else
		setup_fte_addr6(spec, attrs->saddr.a6, attrs->daddr.a6);

	setup_fte_no_frags(spec);
	setup_fte_upper_proto_match(spec, &attrs->upspec);

	switch (attrs->action) {
	case XFRM_POLICY_ALLOW:
		flow_act.action |= MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
		break;
	case XFRM_POLICY_BLOCK:
		flow_act.action |= MLX5_FLOW_CONTEXT_ACTION_DROP | MLX5_FLOW_CONTEXT_ACTION_COUNT;
		dest[dstn].type = MLX5_FLOW_DESTINATION_TYPE_COUNTER;
		dest[dstn].counter_id = mlx5_fc_id(rx->fc->drop);
		dstn++;
		break;
	default:
		WARN_ON(true);
		err = -EINVAL;
		goto err_action;
	}

	flow_act.flags |= FLOW_ACT_NO_APPEND;
	if (rx == ipsec->rx_esw && rx->chains)
		flow_act.flags |= FLOW_ACT_IGNORE_FLOW_LEVEL;
	dest[dstn].type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest[dstn].ft = rx->ft.sa;
	dstn++;
	rule = mlx5_add_flow_rules(ft, spec, &flow_act, dest, dstn);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(mdev, "Fail to add RX IPsec policy rule err=%d\n", err);
		goto err_action;
	}

	kvfree(spec);
	pol_entry->ipsec_rule.rule = rule;
	return 0;

err_action:
	kvfree(spec);
err_alloc:
	rx_ft_put_policy(pol_entry->ipsec, attrs->family, attrs->prio, attrs->type);
	return err;
}

static void ipsec_fs_destroy_single_counter(struct mlx5_core_dev *mdev,
					    struct mlx5e_ipsec_fc *fc)
{
	mlx5_fc_destroy(mdev, fc->drop);
	mlx5_fc_destroy(mdev, fc->cnt);
	kfree(fc);
}

static void ipsec_fs_destroy_counters(struct mlx5e_ipsec *ipsec)
{
	struct mlx5_core_dev *mdev = ipsec->mdev;

	ipsec_fs_destroy_single_counter(mdev, ipsec->tx->fc);
	ipsec_fs_destroy_single_counter(mdev, ipsec->rx_ipv4->fc);
	if (ipsec->is_uplink_rep) {
		ipsec_fs_destroy_single_counter(mdev, ipsec->tx_esw->fc);
		ipsec_fs_destroy_single_counter(mdev, ipsec->rx_esw->fc);
	}
}

static struct mlx5e_ipsec_fc *ipsec_fs_init_single_counter(struct mlx5_core_dev *mdev)
{
	struct mlx5e_ipsec_fc *fc;
	struct mlx5_fc *counter;
	int err;

	fc = kzalloc(sizeof(*fc), GFP_KERNEL);
	if (!fc)
		return ERR_PTR(-ENOMEM);

	counter = mlx5_fc_create(mdev, false);
	if (IS_ERR(counter)) {
		err = PTR_ERR(counter);
		goto err_cnt;
	}
	fc->cnt = counter;

	counter = mlx5_fc_create(mdev, false);
	if (IS_ERR(counter)) {
		err = PTR_ERR(counter);
		goto err_drop;
	}
	fc->drop = counter;

	return fc;

err_drop:
	mlx5_fc_destroy(mdev, fc->cnt);
err_cnt:
	kfree(fc);
	return ERR_PTR(err);
}

static int ipsec_fs_init_counters(struct mlx5e_ipsec *ipsec)
{
	struct mlx5_core_dev *mdev = ipsec->mdev;
	struct mlx5e_ipsec_fc *fc;
	int err;

	fc = ipsec_fs_init_single_counter(mdev);
	if (IS_ERR(fc)) {
		err = PTR_ERR(fc);
		goto err_rx_cnt;
	}
	ipsec->rx_ipv4->fc = fc;

	fc = ipsec_fs_init_single_counter(mdev);
	if (IS_ERR(fc)) {
		err = PTR_ERR(fc);
		goto err_tx_cnt;
	}
	ipsec->tx->fc = fc;

	if (ipsec->is_uplink_rep) {
		fc = ipsec_fs_init_single_counter(mdev);
		if (IS_ERR(fc)) {
			err = PTR_ERR(fc);
			goto err_rx_esw_cnt;
		}
		ipsec->rx_esw->fc = fc;

		fc = ipsec_fs_init_single_counter(mdev);
		if (IS_ERR(fc)) {
			err = PTR_ERR(fc);
			goto err_tx_esw_cnt;
		}
		ipsec->tx_esw->fc = fc;
	}

	/* Both IPv4 and IPv6 point to same flow counters struct. */
	ipsec->rx_ipv6->fc = ipsec->rx_ipv4->fc;
	return 0;

err_tx_esw_cnt:
	ipsec_fs_destroy_single_counter(mdev, ipsec->rx_esw->fc);
err_rx_esw_cnt:
	ipsec_fs_destroy_single_counter(mdev, ipsec->tx->fc);
err_tx_cnt:
	ipsec_fs_destroy_single_counter(mdev, ipsec->rx_ipv4->fc);
err_rx_cnt:
	return err;
}

void mlx5e_accel_ipsec_fs_read_stats(struct mlx5e_priv *priv, void *ipsec_stats)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5e_ipsec *ipsec = priv->ipsec;
	struct mlx5e_ipsec_hw_stats *stats;
	struct mlx5e_ipsec_fc *fc;
	u64 packets, bytes;

	stats = (struct mlx5e_ipsec_hw_stats *)ipsec_stats;

	stats->ipsec_rx_pkts = 0;
	stats->ipsec_rx_bytes = 0;
	stats->ipsec_rx_drop_pkts = 0;
	stats->ipsec_rx_drop_bytes = 0;
	stats->ipsec_tx_pkts = 0;
	stats->ipsec_tx_bytes = 0;
	stats->ipsec_tx_drop_pkts = 0;
	stats->ipsec_tx_drop_bytes = 0;

	fc = ipsec->rx_ipv4->fc;
	mlx5_fc_query(mdev, fc->cnt, &stats->ipsec_rx_pkts, &stats->ipsec_rx_bytes);
	mlx5_fc_query(mdev, fc->drop, &stats->ipsec_rx_drop_pkts,
		      &stats->ipsec_rx_drop_bytes);

	fc = ipsec->tx->fc;
	mlx5_fc_query(mdev, fc->cnt, &stats->ipsec_tx_pkts, &stats->ipsec_tx_bytes);
	mlx5_fc_query(mdev, fc->drop, &stats->ipsec_tx_drop_pkts,
		      &stats->ipsec_tx_drop_bytes);

	if (ipsec->is_uplink_rep) {
		fc = ipsec->rx_esw->fc;
		if (!mlx5_fc_query(mdev, fc->cnt, &packets, &bytes)) {
			stats->ipsec_rx_pkts += packets;
			stats->ipsec_rx_bytes += bytes;
		}

		if (!mlx5_fc_query(mdev, fc->drop, &packets, &bytes)) {
			stats->ipsec_rx_drop_pkts += packets;
			stats->ipsec_rx_drop_bytes += bytes;
		}

		fc = ipsec->tx_esw->fc;
		if (!mlx5_fc_query(mdev, fc->cnt, &packets, &bytes)) {
			stats->ipsec_tx_pkts += packets;
			stats->ipsec_tx_bytes += bytes;
		}

		if (!mlx5_fc_query(mdev, fc->drop, &packets, &bytes)) {
			stats->ipsec_tx_drop_pkts += packets;
			stats->ipsec_tx_drop_bytes += bytes;
		}
	}
}

#ifdef CONFIG_MLX5_ESWITCH
static int mlx5e_ipsec_block_tc_offload(struct mlx5_core_dev *mdev)
{
	struct mlx5_eswitch *esw = mdev->priv.eswitch;
	int err = 0;

	if (esw) {
		err = mlx5_esw_lock(esw);
		if (err)
			return err;
	}

	if (mdev->num_block_ipsec) {
		err = -EBUSY;
		goto unlock;
	}

	mdev->num_block_tc++;

unlock:
	if (esw)
		mlx5_esw_unlock(esw);

	return err;
}
#else
static int mlx5e_ipsec_block_tc_offload(struct mlx5_core_dev *mdev)
{
	if (mdev->num_block_ipsec)
		return -EBUSY;

	mdev->num_block_tc++;
	return 0;
}
#endif

static void mlx5e_ipsec_unblock_tc_offload(struct mlx5_core_dev *mdev)
{
	mdev->num_block_tc--;
}

int mlx5e_accel_ipsec_fs_add_rule(struct mlx5e_ipsec_sa_entry *sa_entry)
{
	int err;

	if (sa_entry->attrs.type == XFRM_DEV_OFFLOAD_PACKET) {
		err = mlx5e_ipsec_block_tc_offload(sa_entry->ipsec->mdev);
		if (err)
			return err;
	}

	if (sa_entry->attrs.dir == XFRM_DEV_OFFLOAD_OUT)
		err = tx_add_rule(sa_entry);
	else
		err = rx_add_rule(sa_entry);

	if (err)
		goto err_out;

	return 0;

err_out:
	if (sa_entry->attrs.type == XFRM_DEV_OFFLOAD_PACKET)
		mlx5e_ipsec_unblock_tc_offload(sa_entry->ipsec->mdev);
	return err;
}

void mlx5e_accel_ipsec_fs_del_rule(struct mlx5e_ipsec_sa_entry *sa_entry)
{
	struct mlx5e_ipsec_rule *ipsec_rule = &sa_entry->ipsec_rule;
	struct mlx5_core_dev *mdev = mlx5e_ipsec_sa2dev(sa_entry);

	mlx5_del_flow_rules(ipsec_rule->rule);
	mlx5_fc_destroy(mdev, ipsec_rule->fc);
	if (ipsec_rule->pkt_reformat)
		mlx5_packet_reformat_dealloc(mdev, ipsec_rule->pkt_reformat);

	if (sa_entry->attrs.type == XFRM_DEV_OFFLOAD_PACKET)
		mlx5e_ipsec_unblock_tc_offload(mdev);

	if (sa_entry->attrs.dir == XFRM_DEV_OFFLOAD_OUT) {
		tx_ft_put(sa_entry->ipsec, sa_entry->attrs.type);
		return;
	}

	mlx5_modify_header_dealloc(mdev, ipsec_rule->modify_hdr);
	mlx5_esw_ipsec_rx_id_mapping_remove(sa_entry);
	rx_ft_put(sa_entry->ipsec, sa_entry->attrs.family, sa_entry->attrs.type);
}

int mlx5e_accel_ipsec_fs_add_pol(struct mlx5e_ipsec_pol_entry *pol_entry)
{
	int err;

	err = mlx5e_ipsec_block_tc_offload(pol_entry->ipsec->mdev);
	if (err)
		return err;

	if (pol_entry->attrs.dir == XFRM_DEV_OFFLOAD_OUT)
		err = tx_add_policy(pol_entry);
	else
		err = rx_add_policy(pol_entry);

	if (err)
		goto err_out;

	return 0;

err_out:
	mlx5e_ipsec_unblock_tc_offload(pol_entry->ipsec->mdev);
	return err;
}

void mlx5e_accel_ipsec_fs_del_pol(struct mlx5e_ipsec_pol_entry *pol_entry)
{
	struct mlx5e_ipsec_rule *ipsec_rule = &pol_entry->ipsec_rule;
	struct mlx5_core_dev *mdev = mlx5e_ipsec_pol2dev(pol_entry);

	mlx5_del_flow_rules(ipsec_rule->rule);

	mlx5e_ipsec_unblock_tc_offload(pol_entry->ipsec->mdev);

	if (pol_entry->attrs.dir == XFRM_DEV_OFFLOAD_IN) {
		rx_ft_put_policy(pol_entry->ipsec, pol_entry->attrs.family,
				 pol_entry->attrs.prio, pol_entry->attrs.type);
		return;
	}

	if (ipsec_rule->modify_hdr)
		mlx5_modify_header_dealloc(mdev, ipsec_rule->modify_hdr);

	tx_ft_put_policy(pol_entry->ipsec, pol_entry->attrs.prio, pol_entry->attrs.type);
}

void mlx5e_accel_ipsec_fs_cleanup(struct mlx5e_ipsec *ipsec)
{
	if (!ipsec->tx)
		return;

	if (ipsec->roce)
		mlx5_ipsec_fs_roce_cleanup(ipsec->roce);

	ipsec_fs_destroy_counters(ipsec);
	mutex_destroy(&ipsec->tx->ft.mutex);
	WARN_ON(ipsec->tx->ft.refcnt);
	kfree(ipsec->tx);

	mutex_destroy(&ipsec->rx_ipv4->ft.mutex);
	WARN_ON(ipsec->rx_ipv4->ft.refcnt);
	kfree(ipsec->rx_ipv4);

	mutex_destroy(&ipsec->rx_ipv6->ft.mutex);
	WARN_ON(ipsec->rx_ipv6->ft.refcnt);
	kfree(ipsec->rx_ipv6);

	if (ipsec->is_uplink_rep) {
		xa_destroy(&ipsec->rx_esw->ipsec_obj_id_map);

		mutex_destroy(&ipsec->tx_esw->ft.mutex);
		WARN_ON(ipsec->tx_esw->ft.refcnt);
		kfree(ipsec->tx_esw);

		mutex_destroy(&ipsec->rx_esw->ft.mutex);
		WARN_ON(ipsec->rx_esw->ft.refcnt);
		kfree(ipsec->rx_esw);
	}
}

int mlx5e_accel_ipsec_fs_init(struct mlx5e_ipsec *ipsec)
{
	struct mlx5_core_dev *mdev = ipsec->mdev;
	struct mlx5_flow_namespace *ns, *ns_esw;
	int err = -ENOMEM;

	ns = mlx5_get_flow_namespace(ipsec->mdev,
				     MLX5_FLOW_NAMESPACE_EGRESS_IPSEC);
	if (!ns)
		return -EOPNOTSUPP;

	if (ipsec->is_uplink_rep) {
		ns_esw = mlx5_get_flow_namespace(mdev, MLX5_FLOW_NAMESPACE_FDB);
		if (!ns_esw)
			return -EOPNOTSUPP;

		ipsec->tx_esw = kzalloc(sizeof(*ipsec->tx_esw), GFP_KERNEL);
		if (!ipsec->tx_esw)
			return -ENOMEM;

		ipsec->rx_esw = kzalloc(sizeof(*ipsec->rx_esw), GFP_KERNEL);
		if (!ipsec->rx_esw)
			goto err_rx_esw;
	}

	ipsec->tx = kzalloc(sizeof(*ipsec->tx), GFP_KERNEL);
	if (!ipsec->tx)
		goto err_tx;

	ipsec->rx_ipv4 = kzalloc(sizeof(*ipsec->rx_ipv4), GFP_KERNEL);
	if (!ipsec->rx_ipv4)
		goto err_rx_ipv4;

	ipsec->rx_ipv6 = kzalloc(sizeof(*ipsec->rx_ipv6), GFP_KERNEL);
	if (!ipsec->rx_ipv6)
		goto err_rx_ipv6;

	err = ipsec_fs_init_counters(ipsec);
	if (err)
		goto err_counters;

	mutex_init(&ipsec->tx->ft.mutex);
	mutex_init(&ipsec->rx_ipv4->ft.mutex);
	mutex_init(&ipsec->rx_ipv6->ft.mutex);
	ipsec->tx->ns = ns;

	if (ipsec->is_uplink_rep) {
		mutex_init(&ipsec->tx_esw->ft.mutex);
		mutex_init(&ipsec->rx_esw->ft.mutex);
		ipsec->tx_esw->ns = ns_esw;
		xa_init_flags(&ipsec->rx_esw->ipsec_obj_id_map, XA_FLAGS_ALLOC1);
	} else if (mlx5_ipsec_device_caps(mdev) & MLX5_IPSEC_CAP_ROCE) {
		ipsec->roce = mlx5_ipsec_fs_roce_init(mdev);
	}

	return 0;

err_counters:
	kfree(ipsec->rx_ipv6);
err_rx_ipv6:
	kfree(ipsec->rx_ipv4);
err_rx_ipv4:
	kfree(ipsec->tx);
err_tx:
	kfree(ipsec->rx_esw);
err_rx_esw:
	kfree(ipsec->tx_esw);
	return err;
}

void mlx5e_accel_ipsec_fs_modify(struct mlx5e_ipsec_sa_entry *sa_entry)
{
	struct mlx5e_ipsec_sa_entry sa_entry_shadow = {};
	int err;

	memcpy(&sa_entry_shadow, sa_entry, sizeof(*sa_entry));
	memset(&sa_entry_shadow.ipsec_rule, 0x00, sizeof(sa_entry->ipsec_rule));

	err = mlx5e_accel_ipsec_fs_add_rule(&sa_entry_shadow);
	if (err)
		return;

	mlx5e_accel_ipsec_fs_del_rule(sa_entry);
	memcpy(sa_entry, &sa_entry_shadow, sizeof(*sa_entry));
}

bool mlx5e_ipsec_fs_tunnel_enabled(struct mlx5e_ipsec_sa_entry *sa_entry)
{
	struct mlx5_accel_esp_xfrm_attrs *attrs = &sa_entry->attrs;
	struct mlx5e_ipsec_rx *rx;
	struct mlx5e_ipsec_tx *tx;

	rx = ipsec_rx(sa_entry->ipsec, attrs->family, attrs->type);
	tx = ipsec_tx(sa_entry->ipsec, attrs->type);
	if (sa_entry->attrs.dir == XFRM_DEV_OFFLOAD_OUT)
		return tx->allow_tunnel_mode;

	return rx->allow_tunnel_mode;
}
