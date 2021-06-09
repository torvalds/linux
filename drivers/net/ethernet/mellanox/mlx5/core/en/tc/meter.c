// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "lib/aso.h"
#include "en/tc/post_act.h"
#include "meter.h"

struct mlx5e_flow_meters {
	enum mlx5_flow_namespace_type ns_type;
	struct mlx5_aso *aso;
	struct mutex aso_lock; /* Protects aso operations */
	int log_granularity;
	u32 pdn;

	struct mlx5_core_dev *mdev;
	struct mlx5e_post_act *post_act;
};

struct mlx5e_flow_meters *
mlx5e_flow_meters_init(struct mlx5e_priv *priv,
		       enum mlx5_flow_namespace_type ns_type,
		       struct mlx5e_post_act *post_act)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5e_flow_meters *flow_meters;
	int err;

	if (!(MLX5_CAP_GEN_64(mdev, general_obj_types) &
	      MLX5_HCA_CAP_GENERAL_OBJECT_TYPES_FLOW_METER_ASO))
		return ERR_PTR(-EOPNOTSUPP);

	if (IS_ERR_OR_NULL(post_act)) {
		netdev_dbg(priv->netdev,
			   "flow meter offload is not supported, post action is missing\n");
		return ERR_PTR(-EOPNOTSUPP);
	}

	flow_meters = kzalloc(sizeof(*flow_meters), GFP_KERNEL);
	if (!flow_meters)
		return ERR_PTR(-ENOMEM);

	err = mlx5_core_alloc_pd(mdev, &flow_meters->pdn);
	if (err) {
		mlx5_core_err(mdev, "Failed to alloc pd for flow meter aso, err=%d\n", err);
		goto err_out;
	}

	flow_meters->aso = mlx5_aso_create(mdev, flow_meters->pdn);
	if (IS_ERR(flow_meters->aso)) {
		mlx5_core_warn(mdev, "Failed to create aso wqe for flow meter\n");
		err = PTR_ERR(flow_meters->aso);
		goto err_sq;
	}

	flow_meters->ns_type = ns_type;
	flow_meters->mdev = mdev;
	flow_meters->post_act = post_act;
	mutex_init(&flow_meters->aso_lock);
	flow_meters->log_granularity = min_t(int, 6,
					     MLX5_CAP_QOS(mdev, log_meter_aso_max_alloc));

	return flow_meters;

err_sq:
	mlx5_core_dealloc_pd(mdev, flow_meters->pdn);
err_out:
	kfree(flow_meters);
	return ERR_PTR(err);
}

void
mlx5e_flow_meters_cleanup(struct mlx5e_flow_meters *flow_meters)
{
	if (IS_ERR_OR_NULL(flow_meters))
		return;

	mlx5_aso_destroy(flow_meters->aso);
	mlx5_core_dealloc_pd(flow_meters->mdev, flow_meters->pdn);

	kfree(flow_meters);
}
