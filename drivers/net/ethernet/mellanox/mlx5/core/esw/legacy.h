/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021 Mellanox Technologies Ltd */

#ifndef __MLX5_ESW_LEGACY_H__
#define __MLX5_ESW_LEGACY_H__

#define MLX5_LEGACY_SRIOV_VPORT_EVENTS (MLX5_VPORT_UC_ADDR_CHANGE | \
					MLX5_VPORT_MC_ADDR_CHANGE | \
					MLX5_VPORT_PROMISC_CHANGE)

struct mlx5_eswitch;

int esw_legacy_enable(struct mlx5_eswitch *esw);
void esw_legacy_disable(struct mlx5_eswitch *esw);

int esw_legacy_vport_acl_setup(struct mlx5_eswitch *esw, struct mlx5_vport *vport);
void esw_legacy_vport_acl_cleanup(struct mlx5_eswitch *esw, struct mlx5_vport *vport);

int mlx5_esw_query_vport_drop_stats(struct mlx5_core_dev *dev,
				    struct mlx5_vport *vport,
				    struct mlx5_vport_drop_stats *stats);
#endif
