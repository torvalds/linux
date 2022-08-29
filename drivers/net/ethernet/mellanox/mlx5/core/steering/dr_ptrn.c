// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "dr_types.h"

struct mlx5dr_ptrn_mgr {
	struct mlx5dr_domain *dmn;
	struct mlx5dr_icm_pool *ptrn_icm_pool;
};

struct mlx5dr_ptrn_mgr *mlx5dr_ptrn_mgr_create(struct mlx5dr_domain *dmn)
{
	struct mlx5dr_ptrn_mgr *mgr;

	if (!mlx5dr_domain_is_support_ptrn_arg(dmn))
		return NULL;

	mgr = kzalloc(sizeof(*mgr), GFP_KERNEL);
	if (!mgr)
		return NULL;

	mgr->dmn = dmn;
	mgr->ptrn_icm_pool = mlx5dr_icm_pool_create(dmn, DR_ICM_TYPE_MODIFY_HDR_PTRN);
	if (!mgr->ptrn_icm_pool) {
		mlx5dr_err(dmn, "Couldn't get modify-header-pattern memory\n");
		goto free_mgr;
	}

	return mgr;

free_mgr:
	kfree(mgr);
	return NULL;
}

void mlx5dr_ptrn_mgr_destroy(struct mlx5dr_ptrn_mgr *mgr)
{
	if (!mgr)
		return;

	mlx5dr_icm_pool_destroy(mgr->ptrn_icm_pool);
	kfree(mgr);
}
