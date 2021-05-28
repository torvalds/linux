// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include "eswitch.h"
#include "esw/qos.h"

/* Minimum supported BW share value by the HW is 1 Mbit/sec */
#define MLX5_MIN_BW_SHARE 1

#define MLX5_RATE_TO_BW_SHARE(rate, divider, limit) \
	min_t(u32, max_t(u32, (rate) / (divider), MLX5_MIN_BW_SHARE), limit)

static int esw_qos_vport_config(struct mlx5_eswitch *esw,
				struct mlx5_vport *vport,
				u32 max_rate, u32 bw_share)
{
	u32 sched_ctx[MLX5_ST_SZ_DW(scheduling_context)] = {};
	struct mlx5_core_dev *dev = esw->dev;
	void *vport_elem;
	u32 bitmask = 0;
	int err;

	if (!MLX5_CAP_GEN(dev, qos) || !MLX5_CAP_QOS(dev, esw_scheduling))
		return -EOPNOTSUPP;

	if (!vport->qos.enabled)
		return -EIO;

	MLX5_SET(scheduling_context, sched_ctx, element_type,
		 SCHEDULING_CONTEXT_ELEMENT_TYPE_VPORT);
	vport_elem = MLX5_ADDR_OF(scheduling_context, sched_ctx,
				  element_attributes);
	MLX5_SET(vport_element, vport_elem, vport_number, vport->vport);
	MLX5_SET(scheduling_context, sched_ctx, parent_element_id, esw->qos.root_tsar_ix);
	MLX5_SET(scheduling_context, sched_ctx, max_average_bw, max_rate);
	MLX5_SET(scheduling_context, sched_ctx, bw_share, bw_share);
	bitmask |= MODIFY_SCHEDULING_ELEMENT_IN_MODIFY_BITMASK_MAX_AVERAGE_BW;
	bitmask |= MODIFY_SCHEDULING_ELEMENT_IN_MODIFY_BITMASK_BW_SHARE;

	err = mlx5_modify_scheduling_element_cmd(dev,
						 SCHEDULING_HIERARCHY_E_SWITCH,
						 sched_ctx,
						 vport->qos.esw_tsar_ix,
						 bitmask);
	if (err) {
		esw_warn(esw->dev, "E-Switch modify TSAR vport element failed (vport=%d,err=%d)\n",
			 vport->vport, err);
		return err;
	}

	return 0;
}

static u32 calculate_vports_min_rate_divider(struct mlx5_eswitch *esw)
{
	u32 fw_max_bw_share = MLX5_CAP_QOS(esw->dev, max_tsar_bw_share);
	struct mlx5_vport *evport;
	u32 max_guarantee = 0;
	unsigned long i;

	mlx5_esw_for_each_vport(esw, i, evport) {
		if (!evport->enabled || evport->qos.min_rate < max_guarantee)
			continue;
		max_guarantee = evport->qos.min_rate;
	}

	if (max_guarantee)
		return max_t(u32, max_guarantee / fw_max_bw_share, 1);
	return 0;
}

static int normalize_vports_min_rate(struct mlx5_eswitch *esw)
{
	u32 fw_max_bw_share = MLX5_CAP_QOS(esw->dev, max_tsar_bw_share);
	u32 divider = calculate_vports_min_rate_divider(esw);
	struct mlx5_vport *evport;
	u32 vport_max_rate;
	u32 vport_min_rate;
	unsigned long i;
	u32 bw_share;
	int err;

	mlx5_esw_for_each_vport(esw, i, evport) {
		if (!evport->enabled)
			continue;
		vport_min_rate = evport->qos.min_rate;
		vport_max_rate = evport->qos.max_rate;
		bw_share = 0;

		if (divider)
			bw_share = MLX5_RATE_TO_BW_SHARE(vport_min_rate,
							 divider,
							 fw_max_bw_share);

		if (bw_share == evport->qos.bw_share)
			continue;

		err = esw_qos_vport_config(esw, evport, vport_max_rate,
					   bw_share);
		if (!err)
			evport->qos.bw_share = bw_share;
		else
			return err;
	}

	return 0;
}

int mlx5_esw_qos_set_vport_rate(struct mlx5_eswitch *esw, struct mlx5_vport *evport,
				u32 max_rate, u32 min_rate)
{
	bool min_rate_supported;
	bool max_rate_supported;
	u32 previous_min_rate;
	u32 fw_max_bw_share;
	int err;

	fw_max_bw_share = MLX5_CAP_QOS(esw->dev, max_tsar_bw_share);
	min_rate_supported = MLX5_CAP_QOS(esw->dev, esw_bw_share) &&
				fw_max_bw_share >= MLX5_MIN_BW_SHARE;
	max_rate_supported = MLX5_CAP_QOS(esw->dev, esw_rate_limit);

	if (!esw->qos.enabled || !evport->enabled || !evport->qos.enabled)
		return -EOPNOTSUPP;

	if ((min_rate && !min_rate_supported) || (max_rate && !max_rate_supported))
		return -EOPNOTSUPP;

	if (min_rate == evport->qos.min_rate)
		goto set_max_rate;

	previous_min_rate = evport->qos.min_rate;
	evport->qos.min_rate = min_rate;
	err = normalize_vports_min_rate(esw);
	if (err) {
		evport->qos.min_rate = previous_min_rate;
		return err;
	}

set_max_rate:
	if (max_rate == evport->qos.max_rate)
		return 0;

	err = esw_qos_vport_config(esw, evport, max_rate, evport->qos.bw_share);
	if (!err)
		evport->qos.max_rate = max_rate;

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

	if (esw->qos.enabled)
		return;

	MLX5_SET(scheduling_context, tsar_ctx, element_type,
		 SCHEDULING_CONTEXT_ELEMENT_TYPE_TSAR);

	attr = MLX5_ADDR_OF(scheduling_context, tsar_ctx, element_attributes);
	*attr = cpu_to_be32(TSAR_ELEMENT_TSAR_TYPE_DWRR << 16);

	err = mlx5_create_scheduling_element_cmd(dev,
						 SCHEDULING_HIERARCHY_E_SWITCH,
						 tsar_ctx,
						 &esw->qos.root_tsar_ix);
	if (err) {
		esw_warn(dev, "E-Switch create TSAR failed (%d)\n", err);
		return;
	}

	esw->qos.enabled = true;
}

void mlx5_esw_qos_destroy(struct mlx5_eswitch *esw)
{
	int err;

	if (!esw->qos.enabled)
		return;

	err = mlx5_destroy_scheduling_element_cmd(esw->dev,
						  SCHEDULING_HIERARCHY_E_SWITCH,
						  esw->qos.root_tsar_ix);
	if (err)
		esw_warn(esw->dev, "E-Switch destroy TSAR failed (%d)\n", err);

	esw->qos.enabled = false;
}

int mlx5_esw_qos_vport_enable(struct mlx5_eswitch *esw, struct mlx5_vport *vport,
			      u32 max_rate, u32 bw_share)
{
	u32 sched_ctx[MLX5_ST_SZ_DW(scheduling_context)] = {};
	struct mlx5_core_dev *dev = esw->dev;
	void *vport_elem;
	int err;

	lockdep_assert_held(&esw->state_lock);
	if (!esw->qos.enabled)
		return 0;

	if (vport->qos.enabled)
		return -EEXIST;

	MLX5_SET(scheduling_context, sched_ctx, element_type,
		 SCHEDULING_CONTEXT_ELEMENT_TYPE_VPORT);
	vport_elem = MLX5_ADDR_OF(scheduling_context, sched_ctx, element_attributes);
	MLX5_SET(vport_element, vport_elem, vport_number, vport->vport);
	MLX5_SET(scheduling_context, sched_ctx, parent_element_id, esw->qos.root_tsar_ix);
	MLX5_SET(scheduling_context, sched_ctx, max_average_bw, max_rate);
	MLX5_SET(scheduling_context, sched_ctx, bw_share, bw_share);

	err = mlx5_create_scheduling_element_cmd(dev,
						 SCHEDULING_HIERARCHY_E_SWITCH,
						 sched_ctx,
						 &vport->qos.esw_tsar_ix);
	if (err)
		esw_warn(dev, "E-Switch create TSAR vport element failed (vport=%d,err=%d)\n",
			 vport->vport, err);
	else
		vport->qos.enabled = true;

	return err;
}

void mlx5_esw_qos_vport_disable(struct mlx5_eswitch *esw, struct mlx5_vport *vport)
{
	int err;

	lockdep_assert_held(&esw->state_lock);
	if (!esw->qos.enabled || !vport->qos.enabled)
		return;

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
