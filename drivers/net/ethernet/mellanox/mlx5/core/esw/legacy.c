// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021 Mellanox Technologies Ltd */

#include <linux/etherdevice.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/mlx5_ifc.h>
#include <linux/mlx5/vport.h>
#include <linux/mlx5/fs.h>
#include "esw/acl/lgcy.h"
#include "esw/legacy.h"
#include "mlx5_core.h"
#include "eswitch.h"
#include "fs_core.h"
#include "fs_ft_pool.h"
#include "esw/qos.h"

enum {
	LEGACY_VEPA_PRIO = 0,
	LEGACY_FDB_PRIO,
};

static int esw_create_legacy_vepa_table(struct mlx5_eswitch *esw)
{
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_core_dev *dev = esw->dev;
	struct mlx5_flow_namespace *root_ns;
	struct mlx5_flow_table *fdb;
	int err;

	root_ns = mlx5_get_fdb_sub_ns(dev, 0);
	if (!root_ns) {
		esw_warn(dev, "Failed to get FDB flow namespace\n");
		return -EOPNOTSUPP;
	}

	/* num FTE 2, num FG 2 */
	ft_attr.prio = LEGACY_VEPA_PRIO;
	ft_attr.max_fte = 2;
	ft_attr.autogroup.max_num_groups = 2;
	fdb = mlx5_create_auto_grouped_flow_table(root_ns, &ft_attr);
	if (IS_ERR(fdb)) {
		err = PTR_ERR(fdb);
		esw_warn(dev, "Failed to create VEPA FDB err %d\n", err);
		return err;
	}
	esw->fdb_table.legacy.vepa_fdb = fdb;

	return 0;
}

static void esw_destroy_legacy_fdb_table(struct mlx5_eswitch *esw)
{
	esw_debug(esw->dev, "Destroy FDB Table\n");
	if (!esw->fdb_table.legacy.fdb)
		return;

	if (esw->fdb_table.legacy.promisc_grp)
		mlx5_destroy_flow_group(esw->fdb_table.legacy.promisc_grp);
	if (esw->fdb_table.legacy.allmulti_grp)
		mlx5_destroy_flow_group(esw->fdb_table.legacy.allmulti_grp);
	if (esw->fdb_table.legacy.addr_grp)
		mlx5_destroy_flow_group(esw->fdb_table.legacy.addr_grp);
	mlx5_destroy_flow_table(esw->fdb_table.legacy.fdb);

	esw->fdb_table.legacy.fdb = NULL;
	esw->fdb_table.legacy.addr_grp = NULL;
	esw->fdb_table.legacy.allmulti_grp = NULL;
	esw->fdb_table.legacy.promisc_grp = NULL;
	atomic64_set(&esw->user_count, 0);
}

static int esw_create_legacy_fdb_table(struct mlx5_eswitch *esw)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_core_dev *dev = esw->dev;
	struct mlx5_flow_namespace *root_ns;
	struct mlx5_flow_table *fdb;
	struct mlx5_flow_group *g;
	void *match_criteria;
	int table_size;
	u32 *flow_group_in;
	u8 *dmac;
	int err = 0;

	esw_debug(dev, "Create FDB log_max_size(%d)\n",
		  MLX5_CAP_ESW_FLOWTABLE_FDB(dev, log_max_ft_size));

	root_ns = mlx5_get_fdb_sub_ns(dev, 0);
	if (!root_ns) {
		esw_warn(dev, "Failed to get FDB flow namespace\n");
		return -EOPNOTSUPP;
	}

	flow_group_in = kvzalloc(inlen, GFP_KERNEL);
	if (!flow_group_in)
		return -ENOMEM;

	ft_attr.max_fte = POOL_NEXT_SIZE;
	ft_attr.prio = LEGACY_FDB_PRIO;
	fdb = mlx5_create_flow_table(root_ns, &ft_attr);
	if (IS_ERR(fdb)) {
		err = PTR_ERR(fdb);
		esw_warn(dev, "Failed to create FDB Table err %d\n", err);
		goto out;
	}
	esw->fdb_table.legacy.fdb = fdb;
	table_size = fdb->max_fte;

	/* Addresses group : Full match unicast/multicast addresses */
	MLX5_SET(create_flow_group_in, flow_group_in, match_criteria_enable,
		 MLX5_MATCH_OUTER_HEADERS);
	match_criteria = MLX5_ADDR_OF(create_flow_group_in, flow_group_in, match_criteria);
	dmac = MLX5_ADDR_OF(fte_match_param, match_criteria, outer_headers.dmac_47_16);
	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, 0);
	/* Preserve 2 entries for allmulti and promisc rules*/
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, table_size - 3);
	eth_broadcast_addr(dmac);
	g = mlx5_create_flow_group(fdb, flow_group_in);
	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		esw_warn(dev, "Failed to create flow group err(%d)\n", err);
		goto out;
	}
	esw->fdb_table.legacy.addr_grp = g;

	/* Allmulti group : One rule that forwards any mcast traffic */
	MLX5_SET(create_flow_group_in, flow_group_in, match_criteria_enable,
		 MLX5_MATCH_OUTER_HEADERS);
	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, table_size - 2);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, table_size - 2);
	eth_zero_addr(dmac);
	dmac[0] = 0x01;
	g = mlx5_create_flow_group(fdb, flow_group_in);
	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		esw_warn(dev, "Failed to create allmulti flow group err(%d)\n", err);
		goto out;
	}
	esw->fdb_table.legacy.allmulti_grp = g;

	/* Promiscuous group :
	 * One rule that forward all unmatched traffic from previous groups
	 */
	eth_zero_addr(dmac);
	MLX5_SET(create_flow_group_in, flow_group_in, match_criteria_enable,
		 MLX5_MATCH_MISC_PARAMETERS);
	MLX5_SET_TO_ONES(fte_match_param, match_criteria, misc_parameters.source_port);
	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, table_size - 1);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, table_size - 1);
	g = mlx5_create_flow_group(fdb, flow_group_in);
	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		esw_warn(dev, "Failed to create promisc flow group err(%d)\n", err);
		goto out;
	}
	esw->fdb_table.legacy.promisc_grp = g;

out:
	if (err)
		esw_destroy_legacy_fdb_table(esw);

	kvfree(flow_group_in);
	return err;
}

static void esw_destroy_legacy_vepa_table(struct mlx5_eswitch *esw)
{
	esw_debug(esw->dev, "Destroy VEPA Table\n");
	if (!esw->fdb_table.legacy.vepa_fdb)
		return;

	mlx5_destroy_flow_table(esw->fdb_table.legacy.vepa_fdb);
	esw->fdb_table.legacy.vepa_fdb = NULL;
}

static int esw_create_legacy_table(struct mlx5_eswitch *esw)
{
	int err;

	memset(&esw->fdb_table.legacy, 0, sizeof(struct legacy_fdb));
	atomic64_set(&esw->user_count, 0);

	err = esw_create_legacy_vepa_table(esw);
	if (err)
		return err;

	err = esw_create_legacy_fdb_table(esw);
	if (err)
		esw_destroy_legacy_vepa_table(esw);

	return err;
}

static void esw_cleanup_vepa_rules(struct mlx5_eswitch *esw)
{
	if (esw->fdb_table.legacy.vepa_uplink_rule)
		mlx5_del_flow_rules(esw->fdb_table.legacy.vepa_uplink_rule);

	if (esw->fdb_table.legacy.vepa_star_rule)
		mlx5_del_flow_rules(esw->fdb_table.legacy.vepa_star_rule);

	esw->fdb_table.legacy.vepa_uplink_rule = NULL;
	esw->fdb_table.legacy.vepa_star_rule = NULL;
}

static void esw_destroy_legacy_table(struct mlx5_eswitch *esw)
{
	esw_cleanup_vepa_rules(esw);
	esw_destroy_legacy_fdb_table(esw);
	esw_destroy_legacy_vepa_table(esw);
}

#define MLX5_LEGACY_SRIOV_VPORT_EVENTS (MLX5_VPORT_UC_ADDR_CHANGE | \
					MLX5_VPORT_MC_ADDR_CHANGE | \
					MLX5_VPORT_PROMISC_CHANGE)

int esw_legacy_enable(struct mlx5_eswitch *esw)
{
	struct mlx5_vport *vport;
	unsigned long i;
	int ret;

	ret = esw_create_legacy_table(esw);
	if (ret)
		return ret;

	mlx5_esw_for_each_vf_vport(esw, i, vport, esw->esw_funcs.num_vfs)
		vport->info.link_state = MLX5_VPORT_ADMIN_STATE_AUTO;

	ret = mlx5_eswitch_enable_pf_vf_vports(esw, MLX5_LEGACY_SRIOV_VPORT_EVENTS);
	if (ret)
		esw_destroy_legacy_table(esw);
	return ret;
}

void esw_legacy_disable(struct mlx5_eswitch *esw)
{
	struct esw_mc_addr *mc_promisc;

	mlx5_eswitch_disable_pf_vf_vports(esw);

	mc_promisc = &esw->mc_promisc;
	if (mc_promisc->uplink_rule)
		mlx5_del_flow_rules(mc_promisc->uplink_rule);

	esw_destroy_legacy_table(esw);
}

static int _mlx5_eswitch_set_vepa_locked(struct mlx5_eswitch *esw,
					 u8 setting)
{
	struct mlx5_flow_destination dest = {};
	struct mlx5_flow_act flow_act = {};
	struct mlx5_flow_handle *flow_rule;
	struct mlx5_flow_spec *spec;
	int err = 0;
	void *misc;

	if (!setting) {
		esw_cleanup_vepa_rules(esw);
		return 0;
	}

	if (esw->fdb_table.legacy.vepa_uplink_rule)
		return 0;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	/* Uplink rule forward uplink traffic to FDB */
	misc = MLX5_ADDR_OF(fte_match_param, spec->match_value, misc_parameters);
	MLX5_SET(fte_match_set_misc, misc, source_port, MLX5_VPORT_UPLINK);

	misc = MLX5_ADDR_OF(fte_match_param, spec->match_criteria, misc_parameters);
	MLX5_SET_TO_ONES(fte_match_set_misc, misc, source_port);

	spec->match_criteria_enable = MLX5_MATCH_MISC_PARAMETERS;
	dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest.ft = esw->fdb_table.legacy.fdb;
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	flow_rule = mlx5_add_flow_rules(esw->fdb_table.legacy.vepa_fdb, spec,
					&flow_act, &dest, 1);
	if (IS_ERR(flow_rule)) {
		err = PTR_ERR(flow_rule);
		goto out;
	}
	esw->fdb_table.legacy.vepa_uplink_rule = flow_rule;

	/* Star rule to forward all traffic to uplink vport */
	memset(&dest, 0, sizeof(dest));
	dest.type = MLX5_FLOW_DESTINATION_TYPE_VPORT;
	dest.vport.num = MLX5_VPORT_UPLINK;
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	flow_rule = mlx5_add_flow_rules(esw->fdb_table.legacy.vepa_fdb, NULL,
					&flow_act, &dest, 1);
	if (IS_ERR(flow_rule)) {
		err = PTR_ERR(flow_rule);
		goto out;
	}
	esw->fdb_table.legacy.vepa_star_rule = flow_rule;

out:
	kvfree(spec);
	if (err)
		esw_cleanup_vepa_rules(esw);
	return err;
}

int mlx5_eswitch_set_vepa(struct mlx5_eswitch *esw, u8 setting)
{
	int err = 0;

	if (!esw)
		return -EOPNOTSUPP;

	if (!mlx5_esw_allowed(esw))
		return -EPERM;

	mutex_lock(&esw->state_lock);
	if (esw->mode != MLX5_ESWITCH_LEGACY || !mlx5_esw_is_fdb_created(esw)) {
		err = -EOPNOTSUPP;
		goto out;
	}

	err = _mlx5_eswitch_set_vepa_locked(esw, setting);

out:
	mutex_unlock(&esw->state_lock);
	return err;
}

int mlx5_eswitch_get_vepa(struct mlx5_eswitch *esw, u8 *setting)
{
	if (!esw)
		return -EOPNOTSUPP;

	if (!mlx5_esw_allowed(esw))
		return -EPERM;

	if (esw->mode != MLX5_ESWITCH_LEGACY || !mlx5_esw_is_fdb_created(esw))
		return -EOPNOTSUPP;

	*setting = esw->fdb_table.legacy.vepa_uplink_rule ? 1 : 0;
	return 0;
}

int esw_legacy_vport_acl_setup(struct mlx5_eswitch *esw, struct mlx5_vport *vport)
{
	int ret;

	/* Only non manager vports need ACL in legacy mode */
	if (mlx5_esw_is_manager_vport(esw, vport->vport))
		return 0;

	ret = esw_acl_ingress_lgcy_setup(esw, vport);
	if (ret)
		goto ingress_err;

	ret = esw_acl_egress_lgcy_setup(esw, vport);
	if (ret)
		goto egress_err;

	return 0;

egress_err:
	esw_acl_ingress_lgcy_cleanup(esw, vport);
ingress_err:
	return ret;
}

void esw_legacy_vport_acl_cleanup(struct mlx5_eswitch *esw, struct mlx5_vport *vport)
{
	if (mlx5_esw_is_manager_vport(esw, vport->vport))
		return;

	esw_acl_egress_lgcy_cleanup(esw, vport);
	esw_acl_ingress_lgcy_cleanup(esw, vport);
}

int mlx5_esw_query_vport_drop_stats(struct mlx5_core_dev *dev,
				    struct mlx5_vport *vport,
				    struct mlx5_vport_drop_stats *stats)
{
	u64 rx_discard_vport_down, tx_discard_vport_down;
	struct mlx5_eswitch *esw = dev->priv.eswitch;
	u64 bytes = 0;
	int err = 0;

	if (esw->mode != MLX5_ESWITCH_LEGACY)
		return 0;

	mutex_lock(&esw->state_lock);
	if (!vport->enabled)
		goto unlock;

	if (!IS_ERR_OR_NULL(vport->egress.legacy.drop_counter))
		mlx5_fc_query(dev, vport->egress.legacy.drop_counter,
			      &stats->rx_dropped, &bytes);

	if (vport->ingress.legacy.drop_counter)
		mlx5_fc_query(dev, vport->ingress.legacy.drop_counter,
			      &stats->tx_dropped, &bytes);

	if (!MLX5_CAP_GEN(dev, receive_discard_vport_down) &&
	    !MLX5_CAP_GEN(dev, transmit_discard_vport_down))
		goto unlock;

	err = mlx5_query_vport_down_stats(dev, vport->vport, 1,
					  &rx_discard_vport_down,
					  &tx_discard_vport_down);
	if (err)
		goto unlock;

	if (MLX5_CAP_GEN(dev, receive_discard_vport_down))
		stats->rx_dropped += rx_discard_vport_down;
	if (MLX5_CAP_GEN(dev, transmit_discard_vport_down))
		stats->tx_dropped += tx_discard_vport_down;

unlock:
	mutex_unlock(&esw->state_lock);
	return err;
}

int mlx5_eswitch_set_vport_vlan(struct mlx5_eswitch *esw,
				u16 vport, u16 vlan, u8 qos)
{
	u8 set_flags = 0;
	int err = 0;

	if (!mlx5_esw_allowed(esw))
		return vlan ? -EPERM : 0;

	if (vlan || qos)
		set_flags = SET_VLAN_STRIP | SET_VLAN_INSERT;

	mutex_lock(&esw->state_lock);
	if (esw->mode != MLX5_ESWITCH_LEGACY) {
		if (!vlan)
			goto unlock; /* compatibility with libvirt */

		err = -EOPNOTSUPP;
		goto unlock;
	}

	err = __mlx5_eswitch_set_vport_vlan(esw, vport, vlan, qos, set_flags);

unlock:
	mutex_unlock(&esw->state_lock);
	return err;
}

int mlx5_eswitch_set_vport_spoofchk(struct mlx5_eswitch *esw,
				    u16 vport, bool spoofchk)
{
	struct mlx5_vport *evport = mlx5_eswitch_get_vport(esw, vport);
	bool pschk;
	int err = 0;

	if (!mlx5_esw_allowed(esw))
		return -EPERM;
	if (IS_ERR(evport))
		return PTR_ERR(evport);

	mutex_lock(&esw->state_lock);
	if (esw->mode != MLX5_ESWITCH_LEGACY) {
		err = -EOPNOTSUPP;
		goto unlock;
	}
	pschk = evport->info.spoofchk;
	evport->info.spoofchk = spoofchk;
	if (pschk && !is_valid_ether_addr(evport->info.mac))
		mlx5_core_warn(esw->dev,
			       "Spoofchk in set while MAC is invalid, vport(%d)\n",
			       evport->vport);
	if (evport->enabled && esw->mode == MLX5_ESWITCH_LEGACY)
		err = esw_acl_ingress_lgcy_setup(esw, evport);
	if (err)
		evport->info.spoofchk = pschk;

unlock:
	mutex_unlock(&esw->state_lock);
	return err;
}

int mlx5_eswitch_set_vport_trust(struct mlx5_eswitch *esw,
				 u16 vport, bool setting)
{
	struct mlx5_vport *evport = mlx5_eswitch_get_vport(esw, vport);
	int err = 0;

	if (!mlx5_esw_allowed(esw))
		return -EPERM;
	if (IS_ERR(evport))
		return PTR_ERR(evport);

	mutex_lock(&esw->state_lock);
	if (esw->mode != MLX5_ESWITCH_LEGACY) {
		err = -EOPNOTSUPP;
		goto unlock;
	}
	evport->info.trusted = setting;
	if (evport->enabled)
		esw_vport_change_handle_locked(evport);

unlock:
	mutex_unlock(&esw->state_lock);
	return err;
}

int mlx5_eswitch_set_vport_rate(struct mlx5_eswitch *esw, u16 vport,
				u32 max_rate, u32 min_rate)
{
	struct mlx5_vport *evport = mlx5_eswitch_get_vport(esw, vport);
	int err;

	if (!mlx5_esw_allowed(esw))
		return -EPERM;
	if (IS_ERR(evport))
		return PTR_ERR(evport);

	mutex_lock(&esw->state_lock);
	err = mlx5_esw_qos_set_vport_rate(esw, evport, max_rate, min_rate);
	mutex_unlock(&esw->state_lock);
	return err;
}
