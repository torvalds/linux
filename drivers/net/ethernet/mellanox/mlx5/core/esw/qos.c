// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include "eswitch.h"
#include "lib/mlx5.h"
#include "esw/qos.h"
#include "en/port.h"
#define CREATE_TRACE_POINTS
#include "diag/qos_tracepoint.h"

/* Minimum supported BW share value by the HW is 1 Mbit/sec */
#define MLX5_MIN_BW_SHARE 1

/* Holds rate groups associated with an E-Switch. */
struct mlx5_qos_domain {
	/* Serializes access to all qos changes in the qos domain. */
	struct mutex lock;
	/* List of all mlx5_esw_rate_groups. */
	struct list_head groups;
};

static void esw_qos_lock(struct mlx5_eswitch *esw)
{
	mutex_lock(&esw->qos.domain->lock);
}

static void esw_qos_unlock(struct mlx5_eswitch *esw)
{
	mutex_unlock(&esw->qos.domain->lock);
}

static void esw_assert_qos_lock_held(struct mlx5_eswitch *esw)
{
	lockdep_assert_held(&esw->qos.domain->lock);
}

static struct mlx5_qos_domain *esw_qos_domain_alloc(void)
{
	struct mlx5_qos_domain *qos_domain;

	qos_domain = kzalloc(sizeof(*qos_domain), GFP_KERNEL);
	if (!qos_domain)
		return NULL;

	mutex_init(&qos_domain->lock);
	INIT_LIST_HEAD(&qos_domain->groups);

	return qos_domain;
}

static int esw_qos_domain_init(struct mlx5_eswitch *esw)
{
	esw->qos.domain = esw_qos_domain_alloc();

	return esw->qos.domain ? 0 : -ENOMEM;
}

static void esw_qos_domain_release(struct mlx5_eswitch *esw)
{
	kfree(esw->qos.domain);
	esw->qos.domain = NULL;
}

struct mlx5_esw_rate_group {
	u32 tsar_ix;
	/* Bandwidth parameters. */
	u32 max_rate;
	u32 min_rate;
	/* A computed value indicating relative min_rate between group members. */
	u32 bw_share;
	/* Membership in the qos domain 'groups' list. */
	struct list_head parent_entry;
	/* The eswitch this group belongs to. */
	struct mlx5_eswitch *esw;
	/* Vport members of this group.*/
	struct list_head members;
};

static void esw_qos_vport_set_group(struct mlx5_vport *vport, struct mlx5_esw_rate_group *group)
{
	list_del_init(&vport->qos.group_entry);
	vport->qos.group = group;
	list_add_tail(&vport->qos.group_entry, &group->members);
}

static int esw_qos_sched_elem_config(struct mlx5_core_dev *dev, u32 sched_elem_ix,
				     u32 max_rate, u32 bw_share)
{
	u32 sched_ctx[MLX5_ST_SZ_DW(scheduling_context)] = {};
	u32 bitmask = 0;

	if (!MLX5_CAP_GEN(dev, qos) || !MLX5_CAP_QOS(dev, esw_scheduling))
		return -EOPNOTSUPP;

	MLX5_SET(scheduling_context, sched_ctx, max_average_bw, max_rate);
	MLX5_SET(scheduling_context, sched_ctx, bw_share, bw_share);
	bitmask |= MODIFY_SCHEDULING_ELEMENT_IN_MODIFY_BITMASK_MAX_AVERAGE_BW;
	bitmask |= MODIFY_SCHEDULING_ELEMENT_IN_MODIFY_BITMASK_BW_SHARE;

	return mlx5_modify_scheduling_element_cmd(dev,
						  SCHEDULING_HIERARCHY_E_SWITCH,
						  sched_ctx,
						  sched_elem_ix,
						  bitmask);
}

static int esw_qos_group_config(struct mlx5_esw_rate_group *group,
				u32 max_rate, u32 bw_share, struct netlink_ext_ack *extack)
{
	struct mlx5_core_dev *dev = group->esw->dev;
	int err;

	err = esw_qos_sched_elem_config(dev, group->tsar_ix, max_rate, bw_share);
	if (err)
		NL_SET_ERR_MSG_MOD(extack, "E-Switch modify group TSAR element failed");

	trace_mlx5_esw_group_qos_config(dev, group, group->tsar_ix, bw_share, max_rate);

	return err;
}

static int esw_qos_vport_config(struct mlx5_vport *vport,
				u32 max_rate, u32 bw_share,
				struct netlink_ext_ack *extack)
{
	struct mlx5_core_dev *dev = vport->qos.group->esw->dev;
	int err;

	err = esw_qos_sched_elem_config(dev, vport->qos.esw_sched_elem_ix, max_rate, bw_share);
	if (err) {
		esw_warn(dev,
			 "E-Switch modify vport scheduling element failed (vport=%d,err=%d)\n",
			 vport->vport, err);
		NL_SET_ERR_MSG_MOD(extack, "E-Switch modify vport scheduling element failed");
		return err;
	}

	trace_mlx5_esw_vport_qos_config(dev, vport, bw_share, max_rate);

	return 0;
}

static u32 esw_qos_calculate_group_min_rate_divider(struct mlx5_esw_rate_group *group)
{
	u32 fw_max_bw_share = MLX5_CAP_QOS(group->esw->dev, max_tsar_bw_share);
	struct mlx5_vport *vport;
	u32 max_guarantee = 0;

	/* Find max min_rate across all vports in this group.
	 * This will correspond to fw_max_bw_share in the final bw_share calculation.
	 */
	list_for_each_entry(vport, &group->members, qos.group_entry) {
		if (vport->qos.min_rate > max_guarantee)
			max_guarantee = vport->qos.min_rate;
	}

	if (max_guarantee)
		return max_t(u32, max_guarantee / fw_max_bw_share, 1);

	/* If vports max min_rate divider is 0 but their group has bw_share
	 * configured, then set bw_share for vports to minimal value.
	 */
	if (group->bw_share)
		return 1;

	/* A divider of 0 sets bw_share for all group vports to 0,
	 * effectively disabling min guarantees.
	 */
	return 0;
}

static u32 esw_qos_calculate_min_rate_divider(struct mlx5_eswitch *esw)
{
	u32 fw_max_bw_share = MLX5_CAP_QOS(esw->dev, max_tsar_bw_share);
	struct mlx5_esw_rate_group *group;
	u32 max_guarantee = 0;

	/* Find max min_rate across all esw groups.
	 * This will correspond to fw_max_bw_share in the final bw_share calculation.
	 */
	list_for_each_entry(group, &esw->qos.domain->groups, parent_entry) {
		if (group->esw == esw && group->tsar_ix != esw->qos.root_tsar_ix &&
		    group->min_rate > max_guarantee)
			max_guarantee = group->min_rate;
	}

	if (max_guarantee)
		return max_t(u32, max_guarantee / fw_max_bw_share, 1);

	/* If no group has min_rate configured, a divider of 0 sets all
	 * groups' bw_share to 0, effectively disabling min guarantees.
	 */
	return 0;
}

static u32 esw_qos_calc_bw_share(u32 min_rate, u32 divider, u32 fw_max)
{
	if (!divider)
		return 0;
	return min_t(u32, max_t(u32, DIV_ROUND_UP(min_rate, divider), MLX5_MIN_BW_SHARE), fw_max);
}

static int esw_qos_normalize_group_min_rate(struct mlx5_esw_rate_group *group,
					    struct netlink_ext_ack *extack)
{
	u32 fw_max_bw_share = MLX5_CAP_QOS(group->esw->dev, max_tsar_bw_share);
	u32 divider = esw_qos_calculate_group_min_rate_divider(group);
	struct mlx5_vport *vport;
	u32 bw_share;
	int err;

	list_for_each_entry(vport, &group->members, qos.group_entry) {
		bw_share = esw_qos_calc_bw_share(vport->qos.min_rate, divider, fw_max_bw_share);

		if (bw_share == vport->qos.bw_share)
			continue;

		err = esw_qos_vport_config(vport, vport->qos.max_rate, bw_share, extack);
		if (err)
			return err;

		vport->qos.bw_share = bw_share;
	}

	return 0;
}

static int esw_qos_normalize_min_rate(struct mlx5_eswitch *esw, struct netlink_ext_ack *extack)
{
	u32 fw_max_bw_share = MLX5_CAP_QOS(esw->dev, max_tsar_bw_share);
	u32 divider = esw_qos_calculate_min_rate_divider(esw);
	struct mlx5_esw_rate_group *group;
	u32 bw_share;
	int err;

	list_for_each_entry(group, &esw->qos.domain->groups, parent_entry) {
		if (group->esw != esw || group->tsar_ix == esw->qos.root_tsar_ix)
			continue;
		bw_share = esw_qos_calc_bw_share(group->min_rate, divider, fw_max_bw_share);

		if (bw_share == group->bw_share)
			continue;

		err = esw_qos_group_config(group, group->max_rate, bw_share, extack);
		if (err)
			return err;

		group->bw_share = bw_share;

		/* All the group's vports need to be set with default bw_share
		 * to enable them with QOS
		 */
		err = esw_qos_normalize_group_min_rate(group, extack);

		if (err)
			return err;
	}

	return 0;
}

static int esw_qos_set_vport_min_rate(struct mlx5_vport *vport,
				      u32 min_rate, struct netlink_ext_ack *extack)
{
	struct mlx5_eswitch *esw = vport->dev->priv.eswitch;
	u32 fw_max_bw_share, previous_min_rate;
	bool min_rate_supported;
	int err;

	esw_assert_qos_lock_held(esw);
	fw_max_bw_share = MLX5_CAP_QOS(vport->dev, max_tsar_bw_share);
	min_rate_supported = MLX5_CAP_QOS(vport->dev, esw_bw_share) &&
				fw_max_bw_share >= MLX5_MIN_BW_SHARE;
	if (min_rate && !min_rate_supported)
		return -EOPNOTSUPP;
	if (min_rate == vport->qos.min_rate)
		return 0;

	previous_min_rate = vport->qos.min_rate;
	vport->qos.min_rate = min_rate;
	err = esw_qos_normalize_group_min_rate(vport->qos.group, extack);
	if (err)
		vport->qos.min_rate = previous_min_rate;

	return err;
}

static int esw_qos_set_vport_max_rate(struct mlx5_vport *vport,
				      u32 max_rate, struct netlink_ext_ack *extack)
{
	struct mlx5_eswitch *esw = vport->dev->priv.eswitch;
	u32 act_max_rate = max_rate;
	bool max_rate_supported;
	int err;

	esw_assert_qos_lock_held(esw);
	max_rate_supported = MLX5_CAP_QOS(vport->dev, esw_rate_limit);

	if (max_rate && !max_rate_supported)
		return -EOPNOTSUPP;
	if (max_rate == vport->qos.max_rate)
		return 0;

	/* Use parent group limit if new max rate is 0. */
	if (!max_rate)
		act_max_rate = vport->qos.group->max_rate;

	err = esw_qos_vport_config(vport, act_max_rate, vport->qos.bw_share, extack);

	if (!err)
		vport->qos.max_rate = max_rate;

	return err;
}

static int esw_qos_set_group_min_rate(struct mlx5_esw_rate_group *group,
				      u32 min_rate, struct netlink_ext_ack *extack)
{
	struct mlx5_eswitch *esw = group->esw;
	u32 previous_min_rate;
	int err;

	if (!MLX5_CAP_QOS(esw->dev, esw_bw_share) ||
	    MLX5_CAP_QOS(esw->dev, max_tsar_bw_share) < MLX5_MIN_BW_SHARE)
		return -EOPNOTSUPP;

	if (min_rate == group->min_rate)
		return 0;

	previous_min_rate = group->min_rate;
	group->min_rate = min_rate;
	err = esw_qos_normalize_min_rate(esw, extack);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "E-Switch group min rate setting failed");

		/* Attempt restoring previous configuration */
		group->min_rate = previous_min_rate;
		if (esw_qos_normalize_min_rate(esw, extack))
			NL_SET_ERR_MSG_MOD(extack, "E-Switch BW share restore failed");
	}

	return err;
}

static int esw_qos_set_group_max_rate(struct mlx5_esw_rate_group *group,
				      u32 max_rate, struct netlink_ext_ack *extack)
{
	struct mlx5_vport *vport;
	int err;

	if (group->max_rate == max_rate)
		return 0;

	err = esw_qos_group_config(group, max_rate, group->bw_share, extack);
	if (err)
		return err;

	group->max_rate = max_rate;

	/* Any unlimited vports in the group should be set with the value of the group. */
	list_for_each_entry(vport, &group->members, qos.group_entry) {
		if (vport->qos.max_rate)
			continue;

		err = esw_qos_vport_config(vport, max_rate, vport->qos.bw_share, extack);
		if (err)
			NL_SET_ERR_MSG_MOD(extack,
					   "E-Switch vport implicit rate limit setting failed");
	}

	return err;
}

static int esw_qos_vport_create_sched_element(struct mlx5_vport *vport,
					      u32 max_rate, u32 bw_share)
{
	u32 sched_ctx[MLX5_ST_SZ_DW(scheduling_context)] = {};
	struct mlx5_esw_rate_group *group = vport->qos.group;
	struct mlx5_core_dev *dev = group->esw->dev;
	void *attr;
	int err;

	if (!mlx5_qos_element_type_supported(dev,
					     SCHEDULING_CONTEXT_ELEMENT_TYPE_VPORT,
					     SCHEDULING_HIERARCHY_E_SWITCH))
		return -EOPNOTSUPP;

	MLX5_SET(scheduling_context, sched_ctx, element_type,
		 SCHEDULING_CONTEXT_ELEMENT_TYPE_VPORT);
	attr = MLX5_ADDR_OF(scheduling_context, sched_ctx, element_attributes);
	MLX5_SET(vport_element, attr, vport_number, vport->vport);
	MLX5_SET(scheduling_context, sched_ctx, parent_element_id, group->tsar_ix);
	MLX5_SET(scheduling_context, sched_ctx, max_average_bw, max_rate);
	MLX5_SET(scheduling_context, sched_ctx, bw_share, bw_share);

	err = mlx5_create_scheduling_element_cmd(dev,
						 SCHEDULING_HIERARCHY_E_SWITCH,
						 sched_ctx,
						 &vport->qos.esw_sched_elem_ix);
	if (err) {
		esw_warn(dev,
			 "E-Switch create vport scheduling element failed (vport=%d,err=%d)\n",
			 vport->vport, err);
		return err;
	}

	return 0;
}

static int esw_qos_update_group_scheduling_element(struct mlx5_vport *vport,
						   struct mlx5_esw_rate_group *curr_group,
						   struct mlx5_esw_rate_group *new_group,
						   struct netlink_ext_ack *extack)
{
	u32 max_rate;
	int err;

	err = mlx5_destroy_scheduling_element_cmd(curr_group->esw->dev,
						  SCHEDULING_HIERARCHY_E_SWITCH,
						  vport->qos.esw_sched_elem_ix);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "E-Switch destroy vport scheduling element failed");
		return err;
	}

	esw_qos_vport_set_group(vport, new_group);
	/* Use new group max rate if vport max rate is unlimited. */
	max_rate = vport->qos.max_rate ? vport->qos.max_rate : new_group->max_rate;
	err = esw_qos_vport_create_sched_element(vport, max_rate, vport->qos.bw_share);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "E-Switch vport group set failed.");
		goto err_sched;
	}

	return 0;

err_sched:
	esw_qos_vport_set_group(vport, curr_group);
	max_rate = vport->qos.max_rate ? vport->qos.max_rate : curr_group->max_rate;
	if (esw_qos_vport_create_sched_element(vport, max_rate, vport->qos.bw_share))
		esw_warn(curr_group->esw->dev, "E-Switch vport group restore failed (vport=%d)\n",
			 vport->vport);

	return err;
}

static int esw_qos_vport_update_group(struct mlx5_vport *vport,
				      struct mlx5_esw_rate_group *group,
				      struct netlink_ext_ack *extack)
{
	struct mlx5_eswitch *esw = vport->dev->priv.eswitch;
	struct mlx5_esw_rate_group *new_group, *curr_group;
	int err;

	esw_assert_qos_lock_held(esw);
	curr_group = vport->qos.group;
	new_group = group ?: esw->qos.group0;
	if (curr_group == new_group)
		return 0;

	err = esw_qos_update_group_scheduling_element(vport, curr_group, new_group, extack);
	if (err)
		return err;

	/* Recalculate bw share weights of old and new groups */
	if (vport->qos.bw_share || new_group->bw_share) {
		esw_qos_normalize_group_min_rate(curr_group, extack);
		esw_qos_normalize_group_min_rate(new_group, extack);
	}

	return 0;
}

static struct mlx5_esw_rate_group *
__esw_qos_alloc_rate_group(struct mlx5_eswitch *esw, u32 tsar_ix)
{
	struct mlx5_esw_rate_group *group;

	group = kzalloc(sizeof(*group), GFP_KERNEL);
	if (!group)
		return NULL;

	group->esw = esw;
	group->tsar_ix = tsar_ix;
	INIT_LIST_HEAD(&group->members);
	list_add_tail(&group->parent_entry, &esw->qos.domain->groups);
	return group;
}

static void __esw_qos_free_rate_group(struct mlx5_esw_rate_group *group)
{
	list_del(&group->parent_entry);
	kfree(group);
}

static struct mlx5_esw_rate_group *
__esw_qos_create_rate_group(struct mlx5_eswitch *esw, struct netlink_ext_ack *extack)
{
	u32 tsar_ctx[MLX5_ST_SZ_DW(scheduling_context)] = {};
	struct mlx5_esw_rate_group *group;
	int tsar_ix, err;
	void *attr;

	MLX5_SET(scheduling_context, tsar_ctx, element_type,
		 SCHEDULING_CONTEXT_ELEMENT_TYPE_TSAR);
	MLX5_SET(scheduling_context, tsar_ctx, parent_element_id,
		 esw->qos.root_tsar_ix);
	attr = MLX5_ADDR_OF(scheduling_context, tsar_ctx, element_attributes);
	MLX5_SET(tsar_element, attr, tsar_type, TSAR_ELEMENT_TSAR_TYPE_DWRR);
	err = mlx5_create_scheduling_element_cmd(esw->dev,
						 SCHEDULING_HIERARCHY_E_SWITCH,
						 tsar_ctx,
						 &tsar_ix);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "E-Switch create TSAR for group failed");
		return ERR_PTR(err);
	}

	group = __esw_qos_alloc_rate_group(esw, tsar_ix);
	if (!group) {
		NL_SET_ERR_MSG_MOD(extack, "E-Switch alloc group failed");
		err = -ENOMEM;
		goto err_alloc_group;
	}

	err = esw_qos_normalize_min_rate(esw, extack);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "E-Switch groups normalization failed");
		goto err_min_rate;
	}
	trace_mlx5_esw_group_qos_create(esw->dev, group, group->tsar_ix);

	return group;

err_min_rate:
	__esw_qos_free_rate_group(group);
err_alloc_group:
	if (mlx5_destroy_scheduling_element_cmd(esw->dev,
						SCHEDULING_HIERARCHY_E_SWITCH,
						tsar_ix))
		NL_SET_ERR_MSG_MOD(extack, "E-Switch destroy TSAR for group failed");
	return ERR_PTR(err);
}

static int esw_qos_get(struct mlx5_eswitch *esw, struct netlink_ext_ack *extack);
static void esw_qos_put(struct mlx5_eswitch *esw);

static struct mlx5_esw_rate_group *
esw_qos_create_rate_group(struct mlx5_eswitch *esw, struct netlink_ext_ack *extack)
{
	struct mlx5_esw_rate_group *group;
	int err;

	esw_assert_qos_lock_held(esw);
	if (!MLX5_CAP_QOS(esw->dev, log_esw_max_sched_depth))
		return ERR_PTR(-EOPNOTSUPP);

	err = esw_qos_get(esw, extack);
	if (err)
		return ERR_PTR(err);

	group = __esw_qos_create_rate_group(esw, extack);
	if (IS_ERR(group))
		esw_qos_put(esw);

	return group;
}

static int __esw_qos_destroy_rate_group(struct mlx5_esw_rate_group *group,
					struct netlink_ext_ack *extack)
{
	struct mlx5_eswitch *esw = group->esw;
	int err;

	trace_mlx5_esw_group_qos_destroy(esw->dev, group, group->tsar_ix);

	err = mlx5_destroy_scheduling_element_cmd(esw->dev,
						  SCHEDULING_HIERARCHY_E_SWITCH,
						  group->tsar_ix);
	if (err)
		NL_SET_ERR_MSG_MOD(extack, "E-Switch destroy TSAR_ID failed");
	__esw_qos_free_rate_group(group);

	err = esw_qos_normalize_min_rate(esw, extack);
	if (err)
		NL_SET_ERR_MSG_MOD(extack, "E-Switch groups normalization failed");


	return err;
}

static int esw_qos_create(struct mlx5_eswitch *esw, struct netlink_ext_ack *extack)
{
	u32 tsar_ctx[MLX5_ST_SZ_DW(scheduling_context)] = {};
	struct mlx5_core_dev *dev = esw->dev;
	void *attr;
	int err;

	if (!MLX5_CAP_GEN(dev, qos) || !MLX5_CAP_QOS(dev, esw_scheduling))
		return -EOPNOTSUPP;

	if (!mlx5_qos_element_type_supported(dev,
					     SCHEDULING_CONTEXT_ELEMENT_TYPE_TSAR,
					     SCHEDULING_HIERARCHY_E_SWITCH) ||
	    !mlx5_qos_tsar_type_supported(dev,
					  TSAR_ELEMENT_TSAR_TYPE_DWRR,
					  SCHEDULING_HIERARCHY_E_SWITCH))
		return -EOPNOTSUPP;

	MLX5_SET(scheduling_context, tsar_ctx, element_type,
		 SCHEDULING_CONTEXT_ELEMENT_TYPE_TSAR);

	attr = MLX5_ADDR_OF(scheduling_context, tsar_ctx, element_attributes);
	MLX5_SET(tsar_element, attr, tsar_type, TSAR_ELEMENT_TSAR_TYPE_DWRR);

	err = mlx5_create_scheduling_element_cmd(dev,
						 SCHEDULING_HIERARCHY_E_SWITCH,
						 tsar_ctx,
						 &esw->qos.root_tsar_ix);
	if (err) {
		esw_warn(dev, "E-Switch create root TSAR failed (%d)\n", err);
		return err;
	}

	if (MLX5_CAP_QOS(dev, log_esw_max_sched_depth)) {
		esw->qos.group0 = __esw_qos_create_rate_group(esw, extack);
	} else {
		/* The eswitch doesn't support scheduling groups.
		 * Create a software-only group0 using the root TSAR to attach vport QoS to.
		 */
		if (!__esw_qos_alloc_rate_group(esw, esw->qos.root_tsar_ix))
			esw->qos.group0 = ERR_PTR(-ENOMEM);
	}
	if (IS_ERR(esw->qos.group0)) {
		err = PTR_ERR(esw->qos.group0);
		esw_warn(dev, "E-Switch create rate group 0 failed (%d)\n", err);
		goto err_group0;
	}
	refcount_set(&esw->qos.refcnt, 1);

	return 0;

err_group0:
	if (mlx5_destroy_scheduling_element_cmd(esw->dev, SCHEDULING_HIERARCHY_E_SWITCH,
						esw->qos.root_tsar_ix))
		esw_warn(esw->dev, "E-Switch destroy root TSAR failed.\n");

	return err;
}

static void esw_qos_destroy(struct mlx5_eswitch *esw)
{
	int err;

	if (esw->qos.group0->tsar_ix != esw->qos.root_tsar_ix)
		__esw_qos_destroy_rate_group(esw->qos.group0, NULL);
	else
		__esw_qos_free_rate_group(esw->qos.group0);
	esw->qos.group0 = NULL;

	err = mlx5_destroy_scheduling_element_cmd(esw->dev,
						  SCHEDULING_HIERARCHY_E_SWITCH,
						  esw->qos.root_tsar_ix);
	if (err)
		esw_warn(esw->dev, "E-Switch destroy root TSAR failed (%d)\n", err);
}

static int esw_qos_get(struct mlx5_eswitch *esw, struct netlink_ext_ack *extack)
{
	int err = 0;

	esw_assert_qos_lock_held(esw);
	if (!refcount_inc_not_zero(&esw->qos.refcnt)) {
		/* esw_qos_create() set refcount to 1 only on success.
		 * No need to decrement on failure.
		 */
		err = esw_qos_create(esw, extack);
	}

	return err;
}

static void esw_qos_put(struct mlx5_eswitch *esw)
{
	esw_assert_qos_lock_held(esw);
	if (refcount_dec_and_test(&esw->qos.refcnt))
		esw_qos_destroy(esw);
}

static int esw_qos_vport_enable(struct mlx5_vport *vport,
				u32 max_rate, u32 bw_share, struct netlink_ext_ack *extack)
{
	struct mlx5_eswitch *esw = vport->dev->priv.eswitch;
	int err;

	esw_assert_qos_lock_held(esw);
	if (vport->qos.enabled)
		return 0;

	err = esw_qos_get(esw, extack);
	if (err)
		return err;

	INIT_LIST_HEAD(&vport->qos.group_entry);
	esw_qos_vport_set_group(vport, esw->qos.group0);

	err = esw_qos_vport_create_sched_element(vport, max_rate, bw_share);
	if (err)
		goto err_out;

	vport->qos.enabled = true;
	trace_mlx5_esw_vport_qos_create(vport->dev, vport, bw_share, max_rate);

	return 0;

err_out:
	esw_qos_put(esw);

	return err;
}

void mlx5_esw_qos_vport_disable(struct mlx5_vport *vport)
{
	struct mlx5_eswitch *esw = vport->dev->priv.eswitch;
	struct mlx5_core_dev *dev;
	int err;

	lockdep_assert_held(&esw->state_lock);
	esw_qos_lock(esw);
	if (!vport->qos.enabled)
		goto unlock;
	WARN(vport->qos.group != esw->qos.group0,
	     "Disabling QoS on port before detaching it from group");

	dev = vport->qos.group->esw->dev;
	err = mlx5_destroy_scheduling_element_cmd(dev,
						  SCHEDULING_HIERARCHY_E_SWITCH,
						  vport->qos.esw_sched_elem_ix);
	if (err)
		esw_warn(dev,
			 "E-Switch destroy vport scheduling element failed (vport=%d,err=%d)\n",
			 vport->vport, err);

	memset(&vport->qos, 0, sizeof(vport->qos));
	trace_mlx5_esw_vport_qos_destroy(dev, vport);

	esw_qos_put(esw);
unlock:
	esw_qos_unlock(esw);
}

int mlx5_esw_qos_set_vport_rate(struct mlx5_vport *vport, u32 max_rate, u32 min_rate)
{
	struct mlx5_eswitch *esw = vport->dev->priv.eswitch;
	int err;

	esw_qos_lock(esw);
	err = esw_qos_vport_enable(vport, 0, 0, NULL);
	if (err)
		goto unlock;

	err = esw_qos_set_vport_min_rate(vport, min_rate, NULL);
	if (!err)
		err = esw_qos_set_vport_max_rate(vport, max_rate, NULL);
unlock:
	esw_qos_unlock(esw);
	return err;
}

bool mlx5_esw_qos_get_vport_rate(struct mlx5_vport *vport, u32 *max_rate, u32 *min_rate)
{
	struct mlx5_eswitch *esw = vport->dev->priv.eswitch;
	bool enabled;

	esw_qos_lock(esw);
	enabled = vport->qos.enabled;
	if (enabled) {
		*max_rate = vport->qos.max_rate;
		*min_rate = vport->qos.min_rate;
	}
	esw_qos_unlock(esw);
	return enabled;
}

static u32 mlx5_esw_qos_lag_link_speed_get_locked(struct mlx5_core_dev *mdev)
{
	struct ethtool_link_ksettings lksettings;
	struct net_device *slave, *master;
	u32 speed = SPEED_UNKNOWN;

	/* Lock ensures a stable reference to master and slave netdevice
	 * while port speed of master is queried.
	 */
	ASSERT_RTNL();

	slave = mlx5_uplink_netdev_get(mdev);
	if (!slave)
		goto out;

	master = netdev_master_upper_dev_get(slave);
	if (master && !__ethtool_get_link_ksettings(master, &lksettings))
		speed = lksettings.base.speed;

out:
	return speed;
}

static int mlx5_esw_qos_max_link_speed_get(struct mlx5_core_dev *mdev, u32 *link_speed_max,
					   bool hold_rtnl_lock, struct netlink_ext_ack *extack)
{
	int err;

	if (!mlx5_lag_is_active(mdev))
		goto skip_lag;

	if (hold_rtnl_lock)
		rtnl_lock();

	*link_speed_max = mlx5_esw_qos_lag_link_speed_get_locked(mdev);

	if (hold_rtnl_lock)
		rtnl_unlock();

	if (*link_speed_max != (u32)SPEED_UNKNOWN)
		return 0;

skip_lag:
	err = mlx5_port_max_linkspeed(mdev, link_speed_max);
	if (err)
		NL_SET_ERR_MSG_MOD(extack, "Failed to get link maximum speed");

	return err;
}

static int mlx5_esw_qos_link_speed_verify(struct mlx5_core_dev *mdev,
					  const char *name, u32 link_speed_max,
					  u64 value, struct netlink_ext_ack *extack)
{
	if (value > link_speed_max) {
		pr_err("%s rate value %lluMbps exceed link maximum speed %u.\n",
		       name, value, link_speed_max);
		NL_SET_ERR_MSG_MOD(extack, "TX rate value exceed link maximum speed");
		return -EINVAL;
	}

	return 0;
}

int mlx5_esw_qos_modify_vport_rate(struct mlx5_eswitch *esw, u16 vport_num, u32 rate_mbps)
{
	u32 ctx[MLX5_ST_SZ_DW(scheduling_context)] = {};
	struct mlx5_vport *vport;
	u32 link_speed_max;
	u32 bitmask;
	int err;

	vport = mlx5_eswitch_get_vport(esw, vport_num);
	if (IS_ERR(vport))
		return PTR_ERR(vport);

	if (rate_mbps) {
		err = mlx5_esw_qos_max_link_speed_get(esw->dev, &link_speed_max, false, NULL);
		if (err)
			return err;

		err = mlx5_esw_qos_link_speed_verify(esw->dev, "Police",
						     link_speed_max, rate_mbps, NULL);
		if (err)
			return err;
	}

	esw_qos_lock(esw);
	if (!vport->qos.enabled) {
		/* Eswitch QoS wasn't enabled yet. Enable it and vport QoS. */
		err = esw_qos_vport_enable(vport, rate_mbps, vport->qos.bw_share, NULL);
	} else {
		struct mlx5_core_dev *dev = vport->qos.group->esw->dev;

		MLX5_SET(scheduling_context, ctx, max_average_bw, rate_mbps);
		bitmask = MODIFY_SCHEDULING_ELEMENT_IN_MODIFY_BITMASK_MAX_AVERAGE_BW;
		err = mlx5_modify_scheduling_element_cmd(dev,
							 SCHEDULING_HIERARCHY_E_SWITCH,
							 ctx,
							 vport->qos.esw_sched_elem_ix,
							 bitmask);
	}
	esw_qos_unlock(esw);

	return err;
}

#define MLX5_LINKSPEED_UNIT 125000 /* 1Mbps in Bps */

/* Converts bytes per second value passed in a pointer into megabits per
 * second, rewriting last. If converted rate exceed link speed or is not a
 * fraction of Mbps - returns error.
 */
static int esw_qos_devlink_rate_to_mbps(struct mlx5_core_dev *mdev, const char *name,
					u64 *rate, struct netlink_ext_ack *extack)
{
	u32 link_speed_max, remainder;
	u64 value;
	int err;

	value = div_u64_rem(*rate, MLX5_LINKSPEED_UNIT, &remainder);
	if (remainder) {
		pr_err("%s rate value %lluBps not in link speed units of 1Mbps.\n",
		       name, *rate);
		NL_SET_ERR_MSG_MOD(extack, "TX rate value not in link speed units of 1Mbps");
		return -EINVAL;
	}

	err = mlx5_esw_qos_max_link_speed_get(mdev, &link_speed_max, true, extack);
	if (err)
		return err;

	err = mlx5_esw_qos_link_speed_verify(mdev, name, link_speed_max, value, extack);
	if (err)
		return err;

	*rate = value;
	return 0;
}

int mlx5_esw_qos_init(struct mlx5_eswitch *esw)
{
	return esw_qos_domain_init(esw);
}

void mlx5_esw_qos_cleanup(struct mlx5_eswitch *esw)
{
	if (esw->qos.domain)
		esw_qos_domain_release(esw);
}

/* Eswitch devlink rate API */

int mlx5_esw_devlink_rate_leaf_tx_share_set(struct devlink_rate *rate_leaf, void *priv,
					    u64 tx_share, struct netlink_ext_ack *extack)
{
	struct mlx5_vport *vport = priv;
	struct mlx5_eswitch *esw;
	int err;

	esw = vport->dev->priv.eswitch;
	if (!mlx5_esw_allowed(esw))
		return -EPERM;

	err = esw_qos_devlink_rate_to_mbps(vport->dev, "tx_share", &tx_share, extack);
	if (err)
		return err;

	esw_qos_lock(esw);
	err = esw_qos_vport_enable(vport, 0, 0, extack);
	if (err)
		goto unlock;

	err = esw_qos_set_vport_min_rate(vport, tx_share, extack);
unlock:
	esw_qos_unlock(esw);
	return err;
}

int mlx5_esw_devlink_rate_leaf_tx_max_set(struct devlink_rate *rate_leaf, void *priv,
					  u64 tx_max, struct netlink_ext_ack *extack)
{
	struct mlx5_vport *vport = priv;
	struct mlx5_eswitch *esw;
	int err;

	esw = vport->dev->priv.eswitch;
	if (!mlx5_esw_allowed(esw))
		return -EPERM;

	err = esw_qos_devlink_rate_to_mbps(vport->dev, "tx_max", &tx_max, extack);
	if (err)
		return err;

	esw_qos_lock(esw);
	err = esw_qos_vport_enable(vport, 0, 0, extack);
	if (err)
		goto unlock;

	err = esw_qos_set_vport_max_rate(vport, tx_max, extack);
unlock:
	esw_qos_unlock(esw);
	return err;
}

int mlx5_esw_devlink_rate_node_tx_share_set(struct devlink_rate *rate_node, void *priv,
					    u64 tx_share, struct netlink_ext_ack *extack)
{
	struct mlx5_esw_rate_group *group = priv;
	struct mlx5_eswitch *esw = group->esw;
	int err;

	err = esw_qos_devlink_rate_to_mbps(esw->dev, "tx_share", &tx_share, extack);
	if (err)
		return err;

	esw_qos_lock(esw);
	err = esw_qos_set_group_min_rate(group, tx_share, extack);
	esw_qos_unlock(esw);
	return err;
}

int mlx5_esw_devlink_rate_node_tx_max_set(struct devlink_rate *rate_node, void *priv,
					  u64 tx_max, struct netlink_ext_ack *extack)
{
	struct mlx5_esw_rate_group *group = priv;
	struct mlx5_eswitch *esw = group->esw;
	int err;

	err = esw_qos_devlink_rate_to_mbps(esw->dev, "tx_max", &tx_max, extack);
	if (err)
		return err;

	esw_qos_lock(esw);
	err = esw_qos_set_group_max_rate(group, tx_max, extack);
	esw_qos_unlock(esw);
	return err;
}

int mlx5_esw_devlink_rate_node_new(struct devlink_rate *rate_node, void **priv,
				   struct netlink_ext_ack *extack)
{
	struct mlx5_esw_rate_group *group;
	struct mlx5_eswitch *esw;
	int err = 0;

	esw = mlx5_devlink_eswitch_get(rate_node->devlink);
	if (IS_ERR(esw))
		return PTR_ERR(esw);

	esw_qos_lock(esw);
	if (esw->mode != MLX5_ESWITCH_OFFLOADS) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Rate node creation supported only in switchdev mode");
		err = -EOPNOTSUPP;
		goto unlock;
	}

	group = esw_qos_create_rate_group(esw, extack);
	if (IS_ERR(group)) {
		err = PTR_ERR(group);
		goto unlock;
	}

	*priv = group;
unlock:
	esw_qos_unlock(esw);
	return err;
}

int mlx5_esw_devlink_rate_node_del(struct devlink_rate *rate_node, void *priv,
				   struct netlink_ext_ack *extack)
{
	struct mlx5_esw_rate_group *group = priv;
	struct mlx5_eswitch *esw = group->esw;
	int err;

	esw_qos_lock(esw);
	err = __esw_qos_destroy_rate_group(group, extack);
	esw_qos_put(esw);
	esw_qos_unlock(esw);
	return err;
}

int mlx5_esw_qos_vport_update_group(struct mlx5_vport *vport,
				    struct mlx5_esw_rate_group *group,
				    struct netlink_ext_ack *extack)
{
	struct mlx5_eswitch *esw = vport->dev->priv.eswitch;
	int err = 0;

	if (group && group->esw != esw) {
		NL_SET_ERR_MSG_MOD(extack, "Cross E-Switch scheduling is not supported");
		return -EOPNOTSUPP;
	}

	esw_qos_lock(esw);
	if (!vport->qos.enabled && !group)
		goto unlock;

	err = esw_qos_vport_enable(vport, 0, 0, extack);
	if (!err)
		err = esw_qos_vport_update_group(vport, group, extack);
unlock:
	esw_qos_unlock(esw);
	return err;
}

int mlx5_esw_devlink_rate_parent_set(struct devlink_rate *devlink_rate,
				     struct devlink_rate *parent,
				     void *priv, void *parent_priv,
				     struct netlink_ext_ack *extack)
{
	struct mlx5_esw_rate_group *group;
	struct mlx5_vport *vport = priv;

	if (!parent)
		return mlx5_esw_qos_vport_update_group(vport, NULL, extack);

	group = parent_priv;
	return mlx5_esw_qos_vport_update_group(vport, group, extack);
}
