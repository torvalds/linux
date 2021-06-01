// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include "eswitch.h"
#include "esw/qos.h"
#include "en/port.h"

/* Minimum supported BW share value by the HW is 1 Mbit/sec */
#define MLX5_MIN_BW_SHARE 1

#define MLX5_RATE_TO_BW_SHARE(rate, divider, limit) \
	min_t(u32, max_t(u32, DIV_ROUND_UP(rate, divider), MLX5_MIN_BW_SHARE), limit)

struct mlx5_esw_rate_group {
	u32 tsar_ix;
	u32 max_rate;
	u32 min_rate;
	u32 bw_share;
	struct list_head list;
};

static int esw_qos_tsar_config(struct mlx5_core_dev *dev, u32 *sched_ctx,
			       u32 parent_ix, u32 tsar_ix,
			       u32 max_rate, u32 bw_share)
{
	u32 bitmask = 0;

	if (!MLX5_CAP_GEN(dev, qos) || !MLX5_CAP_QOS(dev, esw_scheduling))
		return -EOPNOTSUPP;

	MLX5_SET(scheduling_context, sched_ctx, parent_element_id, parent_ix);
	MLX5_SET(scheduling_context, sched_ctx, max_average_bw, max_rate);
	MLX5_SET(scheduling_context, sched_ctx, bw_share, bw_share);
	bitmask |= MODIFY_SCHEDULING_ELEMENT_IN_MODIFY_BITMASK_MAX_AVERAGE_BW;
	bitmask |= MODIFY_SCHEDULING_ELEMENT_IN_MODIFY_BITMASK_BW_SHARE;

	return mlx5_modify_scheduling_element_cmd(dev,
						  SCHEDULING_HIERARCHY_E_SWITCH,
						  sched_ctx,
						  tsar_ix,
						  bitmask);
}

static int esw_qos_group_config(struct mlx5_eswitch *esw, struct mlx5_esw_rate_group *group,
				u32 max_rate, u32 bw_share, struct netlink_ext_ack *extack)
{
	u32 sched_ctx[MLX5_ST_SZ_DW(scheduling_context)] = {};
	struct mlx5_core_dev *dev = esw->dev;
	int err;

	err = esw_qos_tsar_config(dev, sched_ctx,
				  esw->qos.root_tsar_ix, group->tsar_ix,
				  max_rate, bw_share);
	if (err)
		NL_SET_ERR_MSG_MOD(extack, "E-Switch modify group TSAR element failed");

	return err;
}

static int esw_qos_vport_config(struct mlx5_eswitch *esw,
				struct mlx5_vport *vport,
				u32 max_rate, u32 bw_share,
				struct netlink_ext_ack *extack)
{
	u32 sched_ctx[MLX5_ST_SZ_DW(scheduling_context)] = {};
	struct mlx5_esw_rate_group *group = vport->qos.group;
	struct mlx5_core_dev *dev = esw->dev;
	u32 parent_tsar_ix;
	void *vport_elem;
	int err;

	if (!vport->qos.enabled)
		return -EIO;

	parent_tsar_ix = group ? group->tsar_ix : esw->qos.root_tsar_ix;
	MLX5_SET(scheduling_context, sched_ctx, element_type,
		 SCHEDULING_CONTEXT_ELEMENT_TYPE_VPORT);
	vport_elem = MLX5_ADDR_OF(scheduling_context, sched_ctx,
				  element_attributes);
	MLX5_SET(vport_element, vport_elem, vport_number, vport->vport);

	err = esw_qos_tsar_config(dev, sched_ctx, parent_tsar_ix, vport->qos.esw_tsar_ix,
				  max_rate, bw_share);
	if (err) {
		esw_warn(esw->dev,
			 "E-Switch modify TSAR vport element failed (vport=%d,err=%d)\n",
			 vport->vport, err);
		NL_SET_ERR_MSG_MOD(extack, "E-Switch modify TSAR vport element failed");
		return err;
	}

	return 0;
}

static u32 esw_qos_calculate_min_rate_divider(struct mlx5_eswitch *esw,
					      struct mlx5_esw_rate_group *group,
					      bool group_level)
{
	u32 fw_max_bw_share = MLX5_CAP_QOS(esw->dev, max_tsar_bw_share);
	struct mlx5_vport *evport;
	u32 max_guarantee = 0;
	unsigned long i;

	if (group_level) {
		struct mlx5_esw_rate_group *group;

		list_for_each_entry(group, &esw->qos.groups, list) {
			if (group->min_rate < max_guarantee)
				continue;
			max_guarantee = group->min_rate;
		}
	} else {
		mlx5_esw_for_each_vport(esw, i, evport) {
			if (!evport->enabled || !evport->qos.enabled ||
			    evport->qos.group != group || evport->qos.min_rate < max_guarantee)
				continue;
			max_guarantee = evport->qos.min_rate;
		}
	}

	if (max_guarantee)
		return max_t(u32, max_guarantee / fw_max_bw_share, 1);

	/* If vports min rate divider is 0 but their group has bw_share configured, then
	 * need to set bw_share for vports to minimal value.
	 */
	if (!group_level && !max_guarantee && group->bw_share)
		return 1;
	return 0;
}

static u32 esw_qos_calc_bw_share(u32 min_rate, u32 divider, u32 fw_max)
{
	if (divider)
		return MLX5_RATE_TO_BW_SHARE(min_rate, divider, fw_max);

	return 0;
}

static int esw_qos_normalize_vports_min_rate(struct mlx5_eswitch *esw,
					     struct mlx5_esw_rate_group *group,
					     struct netlink_ext_ack *extack)
{
	u32 fw_max_bw_share = MLX5_CAP_QOS(esw->dev, max_tsar_bw_share);
	u32 divider = esw_qos_calculate_min_rate_divider(esw, group, false);
	struct mlx5_vport *evport;
	unsigned long i;
	u32 bw_share;
	int err;

	mlx5_esw_for_each_vport(esw, i, evport) {
		if (!evport->enabled || !evport->qos.enabled || evport->qos.group != group)
			continue;
		bw_share = esw_qos_calc_bw_share(evport->qos.min_rate, divider, fw_max_bw_share);

		if (bw_share == evport->qos.bw_share)
			continue;

		err = esw_qos_vport_config(esw, evport, evport->qos.max_rate, bw_share, extack);
		if (err)
			return err;

		evport->qos.bw_share = bw_share;
	}

	return 0;
}

static int esw_qos_normalize_groups_min_rate(struct mlx5_eswitch *esw, u32 divider,
					     struct netlink_ext_ack *extack)
{
	u32 fw_max_bw_share = MLX5_CAP_QOS(esw->dev, max_tsar_bw_share);
	struct mlx5_esw_rate_group *group;
	u32 bw_share;
	int err;

	list_for_each_entry(group, &esw->qos.groups, list) {
		bw_share = esw_qos_calc_bw_share(group->min_rate, divider, fw_max_bw_share);

		if (bw_share == group->bw_share)
			continue;

		err = esw_qos_group_config(esw, group, group->max_rate, bw_share, extack);
		if (err)
			return err;

		group->bw_share = bw_share;

		/* All the group's vports need to be set with default bw_share
		 * to enable them with QOS
		 */
		err = esw_qos_normalize_vports_min_rate(esw, group, extack);

		if (err)
			return err;
	}

	return 0;
}

int mlx5_esw_qos_set_vport_min_rate(struct mlx5_eswitch *esw,
				    struct mlx5_vport *evport,
				    u32 min_rate,
				    struct netlink_ext_ack *extack)
{
	u32 fw_max_bw_share, previous_min_rate;
	bool min_rate_supported;
	int err;

	lockdep_assert_held(&esw->state_lock);
	fw_max_bw_share = MLX5_CAP_QOS(esw->dev, max_tsar_bw_share);
	min_rate_supported = MLX5_CAP_QOS(esw->dev, esw_bw_share) &&
				fw_max_bw_share >= MLX5_MIN_BW_SHARE;
	if (min_rate && !min_rate_supported)
		return -EOPNOTSUPP;
	if (min_rate == evport->qos.min_rate)
		return 0;

	previous_min_rate = evport->qos.min_rate;
	evport->qos.min_rate = min_rate;
	err = esw_qos_normalize_vports_min_rate(esw, evport->qos.group, extack);
	if (err)
		evport->qos.min_rate = previous_min_rate;

	return err;
}

int mlx5_esw_qos_set_vport_max_rate(struct mlx5_eswitch *esw,
				    struct mlx5_vport *evport,
				    u32 max_rate,
				    struct netlink_ext_ack *extack)
{
	u32 act_max_rate = max_rate;
	bool max_rate_supported;
	int err;

	lockdep_assert_held(&esw->state_lock);
	max_rate_supported = MLX5_CAP_QOS(esw->dev, esw_rate_limit);

	if (max_rate && !max_rate_supported)
		return -EOPNOTSUPP;
	if (max_rate == evport->qos.max_rate)
		return 0;

	/* If parent group has rate limit need to set to group
	 * value when new max rate is 0.
	 */
	if (evport->qos.group && !max_rate)
		act_max_rate = evport->qos.group->max_rate;

	err = esw_qos_vport_config(esw, evport, act_max_rate, evport->qos.bw_share, extack);

	if (!err)
		evport->qos.max_rate = max_rate;

	return err;
}

static int esw_qos_set_group_min_rate(struct mlx5_eswitch *esw, struct mlx5_esw_rate_group *group,
				      u32 min_rate, struct netlink_ext_ack *extack)
{
	u32 fw_max_bw_share = MLX5_CAP_QOS(esw->dev, max_tsar_bw_share);
	struct mlx5_core_dev *dev = esw->dev;
	u32 previous_min_rate, divider;
	int err;

	if (!(MLX5_CAP_QOS(dev, esw_bw_share) && fw_max_bw_share >= MLX5_MIN_BW_SHARE))
		return -EOPNOTSUPP;

	if (min_rate == group->min_rate)
		return 0;

	previous_min_rate = group->min_rate;
	group->min_rate = min_rate;
	divider = esw_qos_calculate_min_rate_divider(esw, group, true);
	err = esw_qos_normalize_groups_min_rate(esw, divider, extack);
	if (err) {
		group->min_rate = previous_min_rate;
		NL_SET_ERR_MSG_MOD(extack, "E-Switch group min rate setting failed");

		/* Attempt restoring previous configuration */
		divider = esw_qos_calculate_min_rate_divider(esw, group, true);
		if (esw_qos_normalize_groups_min_rate(esw, divider, extack))
			NL_SET_ERR_MSG_MOD(extack, "E-Switch BW share restore failed");
	}

	return err;
}

static int esw_qos_set_group_max_rate(struct mlx5_eswitch *esw,
				      struct mlx5_esw_rate_group *group,
				      u32 max_rate, struct netlink_ext_ack *extack)
{
	struct mlx5_vport *vport;
	unsigned long i;
	int err;

	if (group->max_rate == max_rate)
		return 0;

	err = esw_qos_group_config(esw, group, max_rate, group->bw_share, extack);
	if (err)
		return err;

	group->max_rate = max_rate;

	/* Any unlimited vports in the group should be set
	 * with the value of the group.
	 */
	mlx5_esw_for_each_vport(esw, i, vport) {
		if (!vport->enabled || !vport->qos.enabled ||
		    vport->qos.group != group || vport->qos.max_rate)
			continue;

		err = esw_qos_vport_config(esw, vport, max_rate, vport->qos.bw_share, extack);
		if (err)
			NL_SET_ERR_MSG_MOD(extack,
					   "E-Switch vport implicit rate limit setting failed");
	}

	return err;
}

static int esw_qos_vport_create_sched_element(struct mlx5_eswitch *esw,
					      struct mlx5_vport *vport,
					      u32 max_rate, u32 bw_share)
{
	u32 sched_ctx[MLX5_ST_SZ_DW(scheduling_context)] = {};
	struct mlx5_esw_rate_group *group = vport->qos.group;
	struct mlx5_core_dev *dev = esw->dev;
	u32 parent_tsar_ix;
	void *vport_elem;
	int err;

	parent_tsar_ix = group ? group->tsar_ix : esw->qos.root_tsar_ix;
	MLX5_SET(scheduling_context, sched_ctx, element_type,
		 SCHEDULING_CONTEXT_ELEMENT_TYPE_VPORT);
	vport_elem = MLX5_ADDR_OF(scheduling_context, sched_ctx, element_attributes);
	MLX5_SET(vport_element, vport_elem, vport_number, vport->vport);
	MLX5_SET(scheduling_context, sched_ctx, parent_element_id, parent_tsar_ix);
	MLX5_SET(scheduling_context, sched_ctx, max_average_bw, max_rate);
	MLX5_SET(scheduling_context, sched_ctx, bw_share, bw_share);

	err = mlx5_create_scheduling_element_cmd(dev,
						 SCHEDULING_HIERARCHY_E_SWITCH,
						 sched_ctx,
						 &vport->qos.esw_tsar_ix);
	if (err) {
		esw_warn(esw->dev, "E-Switch create TSAR vport element failed (vport=%d,err=%d)\n",
			 vport->vport, err);
		return err;
	}

	return 0;
}

static int esw_qos_update_group_scheduling_element(struct mlx5_eswitch *esw,
						   struct mlx5_vport *vport,
						   struct mlx5_esw_rate_group *curr_group,
						   struct mlx5_esw_rate_group *new_group,
						   struct netlink_ext_ack *extack)
{
	u32 max_rate;
	int err;

	err = mlx5_destroy_scheduling_element_cmd(esw->dev,
						  SCHEDULING_HIERARCHY_E_SWITCH,
						  vport->qos.esw_tsar_ix);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "E-Switch destroy TSAR vport element failed");
		return err;
	}

	vport->qos.group = new_group;
	max_rate = vport->qos.max_rate ? vport->qos.max_rate : new_group->max_rate;

	/* If vport is unlimited, we set the group's value.
	 * Therefore, if the group is limited it will apply to
	 * the vport as well and if not, vport will remain unlimited.
	 */
	err = esw_qos_vport_create_sched_element(esw, vport, max_rate, vport->qos.bw_share);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "E-Switch vport group set failed.");
		goto err_sched;
	}

	return 0;

err_sched:
	vport->qos.group = curr_group;
	max_rate = vport->qos.max_rate ? vport->qos.max_rate : curr_group->max_rate;
	if (esw_qos_vport_create_sched_element(esw, vport, max_rate, vport->qos.bw_share))
		esw_warn(esw->dev, "E-Switch vport group restore failed (vport=%d)\n",
			 vport->vport);

	return err;
}

static int esw_qos_vport_update_group(struct mlx5_eswitch *esw,
				      struct mlx5_vport *vport,
				      struct mlx5_esw_rate_group *group,
				      struct netlink_ext_ack *extack)
{
	struct mlx5_esw_rate_group *new_group, *curr_group;
	int err;

	if (!vport->enabled)
		return -EINVAL;

	curr_group = vport->qos.group;
	new_group = group ?: esw->qos.group0;
	if (curr_group == new_group)
		return 0;

	err = esw_qos_update_group_scheduling_element(esw, vport, curr_group, new_group, extack);
	if (err)
		return err;

	/* Recalculate bw share weights of old and new groups */
	if (vport->qos.bw_share) {
		esw_qos_normalize_vports_min_rate(esw, curr_group, extack);
		esw_qos_normalize_vports_min_rate(esw, new_group, extack);
	}

	return 0;
}

static struct mlx5_esw_rate_group *
esw_qos_create_rate_group(struct mlx5_eswitch *esw, struct netlink_ext_ack *extack)
{
	u32 tsar_ctx[MLX5_ST_SZ_DW(scheduling_context)] = {};
	struct mlx5_esw_rate_group *group;
	u32 divider;
	int err;

	if (!MLX5_CAP_QOS(esw->dev, log_esw_max_sched_depth))
		return ERR_PTR(-EOPNOTSUPP);

	group = kzalloc(sizeof(*group), GFP_KERNEL);
	if (!group)
		return ERR_PTR(-ENOMEM);

	MLX5_SET(scheduling_context, tsar_ctx, parent_element_id,
		 esw->qos.root_tsar_ix);
	err = mlx5_create_scheduling_element_cmd(esw->dev,
						 SCHEDULING_HIERARCHY_E_SWITCH,
						 tsar_ctx,
						 &group->tsar_ix);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "E-Switch create TSAR for group failed");
		goto err_sched_elem;
	}

	list_add_tail(&group->list, &esw->qos.groups);

	divider = esw_qos_calculate_min_rate_divider(esw, group, true);
	if (divider) {
		err = esw_qos_normalize_groups_min_rate(esw, divider, extack);
		if (err) {
			NL_SET_ERR_MSG_MOD(extack, "E-Switch groups normalization failed");
			goto err_min_rate;
		}
	}

	return group;

err_min_rate:
	list_del(&group->list);
	err = mlx5_destroy_scheduling_element_cmd(esw->dev,
						  SCHEDULING_HIERARCHY_E_SWITCH,
						  group->tsar_ix);
	if (err)
		NL_SET_ERR_MSG_MOD(extack, "E-Switch destroy TSAR for group failed");
err_sched_elem:
	kfree(group);
	return ERR_PTR(err);
}

static int esw_qos_destroy_rate_group(struct mlx5_eswitch *esw,
				      struct mlx5_esw_rate_group *group,
				      struct netlink_ext_ack *extack)
{
	u32 divider;
	int err;

	list_del(&group->list);

	divider = esw_qos_calculate_min_rate_divider(esw, NULL, true);
	err = esw_qos_normalize_groups_min_rate(esw, divider, extack);
	if (err)
		NL_SET_ERR_MSG_MOD(extack, "E-Switch groups' normalization failed");

	err = mlx5_destroy_scheduling_element_cmd(esw->dev,
						  SCHEDULING_HIERARCHY_E_SWITCH,
						  group->tsar_ix);
	if (err)
		NL_SET_ERR_MSG_MOD(extack, "E-Switch destroy TSAR_ID failed");

	kfree(group);
	return err;
}

static bool esw_qos_element_type_supported(struct mlx5_core_dev *dev, int type)
{
	switch (type) {
	case SCHEDULING_CONTEXT_ELEMENT_TYPE_TSAR:
		return MLX5_CAP_QOS(dev, esw_element_type) &
		       ELEMENT_TYPE_CAP_MASK_TASR;
	case SCHEDULING_CONTEXT_ELEMENT_TYPE_VPORT:
		return MLX5_CAP_QOS(dev, esw_element_type) &
		       ELEMENT_TYPE_CAP_MASK_VPORT;
	case SCHEDULING_CONTEXT_ELEMENT_TYPE_VPORT_TC:
		return MLX5_CAP_QOS(dev, esw_element_type) &
		       ELEMENT_TYPE_CAP_MASK_VPORT_TC;
	case SCHEDULING_CONTEXT_ELEMENT_TYPE_PARA_VPORT_TC:
		return MLX5_CAP_QOS(dev, esw_element_type) &
		       ELEMENT_TYPE_CAP_MASK_PARA_VPORT_TC;
	}
	return false;
}

void mlx5_esw_qos_create(struct mlx5_eswitch *esw)
{
	u32 tsar_ctx[MLX5_ST_SZ_DW(scheduling_context)] = {};
	struct mlx5_core_dev *dev = esw->dev;
	__be32 *attr;
	int err;

	if (!MLX5_CAP_GEN(dev, qos) || !MLX5_CAP_QOS(dev, esw_scheduling))
		return;

	if (!esw_qos_element_type_supported(dev, SCHEDULING_CONTEXT_ELEMENT_TYPE_TSAR))
		return;

	mutex_lock(&esw->state_lock);
	if (esw->qos.enabled)
		goto unlock;

	MLX5_SET(scheduling_context, tsar_ctx, element_type,
		 SCHEDULING_CONTEXT_ELEMENT_TYPE_TSAR);

	attr = MLX5_ADDR_OF(scheduling_context, tsar_ctx, element_attributes);
	*attr = cpu_to_be32(TSAR_ELEMENT_TSAR_TYPE_DWRR << 16);

	err = mlx5_create_scheduling_element_cmd(dev,
						 SCHEDULING_HIERARCHY_E_SWITCH,
						 tsar_ctx,
						 &esw->qos.root_tsar_ix);
	if (err) {
		esw_warn(dev, "E-Switch create root TSAR failed (%d)\n", err);
		goto unlock;
	}

	INIT_LIST_HEAD(&esw->qos.groups);
	if (MLX5_CAP_QOS(dev, log_esw_max_sched_depth)) {
		esw->qos.group0 = esw_qos_create_rate_group(esw, NULL);
		if (IS_ERR(esw->qos.group0)) {
			esw_warn(dev, "E-Switch create rate group 0 failed (%ld)\n",
				 PTR_ERR(esw->qos.group0));
			goto err_group0;
		}
	}
	esw->qos.enabled = true;
unlock:
	mutex_unlock(&esw->state_lock);
	return;

err_group0:
	err = mlx5_destroy_scheduling_element_cmd(esw->dev,
						  SCHEDULING_HIERARCHY_E_SWITCH,
						  esw->qos.root_tsar_ix);
	if (err)
		esw_warn(esw->dev, "E-Switch destroy root TSAR failed (%d)\n", err);
	mutex_unlock(&esw->state_lock);
}

void mlx5_esw_qos_destroy(struct mlx5_eswitch *esw)
{
	struct devlink *devlink = priv_to_devlink(esw->dev);
	int err;

	devlink_rate_nodes_destroy(devlink);
	mutex_lock(&esw->state_lock);
	if (!esw->qos.enabled)
		goto unlock;

	if (esw->qos.group0)
		esw_qos_destroy_rate_group(esw, esw->qos.group0, NULL);

	err = mlx5_destroy_scheduling_element_cmd(esw->dev,
						  SCHEDULING_HIERARCHY_E_SWITCH,
						  esw->qos.root_tsar_ix);
	if (err)
		esw_warn(esw->dev, "E-Switch destroy root TSAR failed (%d)\n", err);

	esw->qos.enabled = false;
unlock:
	mutex_unlock(&esw->state_lock);
}

int mlx5_esw_qos_vport_enable(struct mlx5_eswitch *esw, struct mlx5_vport *vport,
			      u32 max_rate, u32 bw_share)
{
	int err;

	lockdep_assert_held(&esw->state_lock);
	if (!esw->qos.enabled)
		return 0;

	if (vport->qos.enabled)
		return -EEXIST;

	vport->qos.group = esw->qos.group0;

	err = esw_qos_vport_create_sched_element(esw, vport, max_rate, bw_share);
	if (!err)
		vport->qos.enabled = true;

	return err;
}

void mlx5_esw_qos_vport_disable(struct mlx5_eswitch *esw, struct mlx5_vport *vport)
{
	int err;

	lockdep_assert_held(&esw->state_lock);
	if (!esw->qos.enabled || !vport->qos.enabled)
		return;
	WARN(vport->qos.group && vport->qos.group != esw->qos.group0,
	     "Disabling QoS on port before detaching it from group");

	err = mlx5_destroy_scheduling_element_cmd(esw->dev,
						  SCHEDULING_HIERARCHY_E_SWITCH,
						  vport->qos.esw_tsar_ix);
	if (err)
		esw_warn(esw->dev, "E-Switch destroy TSAR vport element failed (vport=%d,err=%d)\n",
			 vport->vport, err);

	vport->qos.enabled = false;
}

int mlx5_esw_qos_modify_vport_rate(struct mlx5_eswitch *esw, u16 vport_num, u32 rate_mbps)
{
	u32 ctx[MLX5_ST_SZ_DW(scheduling_context)] = {};
	struct mlx5_vport *vport;
	u32 bitmask;

	vport = mlx5_eswitch_get_vport(esw, vport_num);
	if (IS_ERR(vport))
		return PTR_ERR(vport);

	if (!vport->qos.enabled)
		return -EOPNOTSUPP;

	MLX5_SET(scheduling_context, ctx, max_average_bw, rate_mbps);
	bitmask = MODIFY_SCHEDULING_ELEMENT_IN_MODIFY_BITMASK_MAX_AVERAGE_BW;

	return mlx5_modify_scheduling_element_cmd(esw->dev,
						  SCHEDULING_HIERARCHY_E_SWITCH,
						  ctx,
						  vport->qos.esw_tsar_ix,
						  bitmask);
}

#define MLX5_LINKSPEED_UNIT 125000 /* 1Mbps in Bps */

/* Converts bytes per second value passed in a pointer into megabits per
 * second, rewriting last. If converted rate exceed link speed or is not a
 * fraction of Mbps - returns error.
 */
static int esw_qos_devlink_rate_to_mbps(struct mlx5_core_dev *mdev, const char *name,
					u64 *rate, struct netlink_ext_ack *extack)
{
	u32 link_speed_max, reminder;
	u64 value;
	int err;

	err = mlx5e_port_max_linkspeed(mdev, &link_speed_max);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to get link maximum speed");
		return err;
	}

	value = div_u64_rem(*rate, MLX5_LINKSPEED_UNIT, &reminder);
	if (reminder) {
		pr_err("%s rate value %lluBps not in link speed units of 1Mbps.\n",
		       name, *rate);
		NL_SET_ERR_MSG_MOD(extack, "TX rate value not in link speed units of 1Mbps");
		return -EINVAL;
	}

	if (value > link_speed_max) {
		pr_err("%s rate value %lluMbps exceed link maximum speed %u.\n",
		       name, value, link_speed_max);
		NL_SET_ERR_MSG_MOD(extack, "TX rate value exceed link maximum speed");
		return -EINVAL;
	}

	*rate = value;
	return 0;
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

	mutex_lock(&esw->state_lock);
	err = mlx5_esw_qos_set_vport_min_rate(esw, vport, tx_share, extack);
	mutex_unlock(&esw->state_lock);
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

	mutex_lock(&esw->state_lock);
	err = mlx5_esw_qos_set_vport_max_rate(esw, vport, tx_max, extack);
	mutex_unlock(&esw->state_lock);
	return err;
}

int mlx5_esw_devlink_rate_node_tx_share_set(struct devlink_rate *rate_node, void *priv,
					    u64 tx_share, struct netlink_ext_ack *extack)
{
	struct mlx5_core_dev *dev = devlink_priv(rate_node->devlink);
	struct mlx5_eswitch *esw = dev->priv.eswitch;
	struct mlx5_esw_rate_group *group = priv;
	int err;

	err = esw_qos_devlink_rate_to_mbps(dev, "tx_share", &tx_share, extack);
	if (err)
		return err;

	mutex_lock(&esw->state_lock);
	err = esw_qos_set_group_min_rate(esw, group, tx_share, extack);
	mutex_unlock(&esw->state_lock);
	return err;
}

int mlx5_esw_devlink_rate_node_tx_max_set(struct devlink_rate *rate_node, void *priv,
					  u64 tx_max, struct netlink_ext_ack *extack)
{
	struct mlx5_core_dev *dev = devlink_priv(rate_node->devlink);
	struct mlx5_eswitch *esw = dev->priv.eswitch;
	struct mlx5_esw_rate_group *group = priv;
	int err;

	err = esw_qos_devlink_rate_to_mbps(dev, "tx_max", &tx_max, extack);
	if (err)
		return err;

	mutex_lock(&esw->state_lock);
	err = esw_qos_set_group_max_rate(esw, group, tx_max, extack);
	mutex_unlock(&esw->state_lock);
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

	mutex_lock(&esw->state_lock);
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
	mutex_unlock(&esw->state_lock);
	return err;
}

int mlx5_esw_devlink_rate_node_del(struct devlink_rate *rate_node, void *priv,
				   struct netlink_ext_ack *extack)
{
	struct mlx5_esw_rate_group *group = priv;
	struct mlx5_eswitch *esw;
	int err;

	esw = mlx5_devlink_eswitch_get(rate_node->devlink);
	if (IS_ERR(esw))
		return PTR_ERR(esw);

	mutex_lock(&esw->state_lock);
	err = esw_qos_destroy_rate_group(esw, group, extack);
	mutex_unlock(&esw->state_lock);
	return err;
}

int mlx5_esw_qos_vport_update_group(struct mlx5_eswitch *esw,
				    struct mlx5_vport *vport,
				    struct mlx5_esw_rate_group *group,
				    struct netlink_ext_ack *extack)
{
	int err;

	mutex_lock(&esw->state_lock);
	err = esw_qos_vport_update_group(esw, vport, group, extack);
	mutex_unlock(&esw->state_lock);
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
		return mlx5_esw_qos_vport_update_group(vport->dev->priv.eswitch,
						       vport, NULL, extack);

	group = parent_priv;
	return mlx5_esw_qos_vport_update_group(vport->dev->priv.eswitch, vport, group, extack);
}
