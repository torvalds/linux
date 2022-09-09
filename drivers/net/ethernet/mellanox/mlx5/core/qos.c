// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020, Mellanox Technologies inc. All rights reserved. */

#include "qos.h"

#define MLX5_QOS_DEFAULT_DWRR_UID 0

bool mlx5_qos_is_supported(struct mlx5_core_dev *mdev)
{
	if (!MLX5_CAP_GEN(mdev, qos))
		return false;
	if (!MLX5_CAP_QOS(mdev, nic_sq_scheduling))
		return false;
	if (!MLX5_CAP_QOS(mdev, nic_bw_share))
		return false;
	if (!MLX5_CAP_QOS(mdev, nic_rate_limit))
		return false;
	return true;
}

int mlx5_qos_max_leaf_nodes(struct mlx5_core_dev *mdev)
{
	return 1 << MLX5_CAP_QOS(mdev, log_max_qos_nic_queue_group);
}

int mlx5_qos_create_leaf_node(struct mlx5_core_dev *mdev, u32 parent_id,
			      u32 bw_share, u32 max_avg_bw, u32 *id)
{
	u32 sched_ctx[MLX5_ST_SZ_DW(scheduling_context)] = {0};

	MLX5_SET(scheduling_context, sched_ctx, parent_element_id, parent_id);
	MLX5_SET(scheduling_context, sched_ctx, element_type,
		 SCHEDULING_CONTEXT_ELEMENT_TYPE_QUEUE_GROUP);
	MLX5_SET(scheduling_context, sched_ctx, bw_share, bw_share);
	MLX5_SET(scheduling_context, sched_ctx, max_average_bw, max_avg_bw);

	return mlx5_create_scheduling_element_cmd(mdev, SCHEDULING_HIERARCHY_NIC,
						  sched_ctx, id);
}

int mlx5_qos_create_inner_node(struct mlx5_core_dev *mdev, u32 parent_id,
			       u32 bw_share, u32 max_avg_bw, u32 *id)
{
	u32 sched_ctx[MLX5_ST_SZ_DW(scheduling_context)] = {0};
	void *attr;

	MLX5_SET(scheduling_context, sched_ctx, parent_element_id, parent_id);
	MLX5_SET(scheduling_context, sched_ctx, element_type,
		 SCHEDULING_CONTEXT_ELEMENT_TYPE_TSAR);
	MLX5_SET(scheduling_context, sched_ctx, bw_share, bw_share);
	MLX5_SET(scheduling_context, sched_ctx, max_average_bw, max_avg_bw);

	attr = MLX5_ADDR_OF(scheduling_context, sched_ctx, element_attributes);
	MLX5_SET(tsar_element, attr, tsar_type, TSAR_ELEMENT_TSAR_TYPE_DWRR);

	return mlx5_create_scheduling_element_cmd(mdev, SCHEDULING_HIERARCHY_NIC,
						  sched_ctx, id);
}

int mlx5_qos_create_root_node(struct mlx5_core_dev *mdev, u32 *id)
{
	return mlx5_qos_create_inner_node(mdev, MLX5_QOS_DEFAULT_DWRR_UID, 0, 0, id);
}

int mlx5_qos_update_node(struct mlx5_core_dev *mdev, u32 parent_id,
			 u32 bw_share, u32 max_avg_bw, u32 id)
{
	u32 sched_ctx[MLX5_ST_SZ_DW(scheduling_context)] = {0};
	u32 bitmask = 0;

	MLX5_SET(scheduling_context, sched_ctx, parent_element_id, parent_id);
	MLX5_SET(scheduling_context, sched_ctx, bw_share, bw_share);
	MLX5_SET(scheduling_context, sched_ctx, max_average_bw, max_avg_bw);

	bitmask |= MODIFY_SCHEDULING_ELEMENT_IN_MODIFY_BITMASK_BW_SHARE;
	bitmask |= MODIFY_SCHEDULING_ELEMENT_IN_MODIFY_BITMASK_MAX_AVERAGE_BW;

	return mlx5_modify_scheduling_element_cmd(mdev, SCHEDULING_HIERARCHY_NIC,
						  sched_ctx, id, bitmask);
}

int mlx5_qos_destroy_node(struct mlx5_core_dev *mdev, u32 id)
{
	return mlx5_destroy_scheduling_element_cmd(mdev, SCHEDULING_HIERARCHY_NIC, id);
}
