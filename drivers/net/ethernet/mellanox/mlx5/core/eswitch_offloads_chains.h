/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2020 Mellanox Technologies. */

#ifndef __ML5_ESW_CHAINS_H__
#define __ML5_ESW_CHAINS_H__

bool
mlx5_esw_chains_prios_supported(struct mlx5_eswitch *esw);
bool
mlx5_esw_chains_backwards_supported(struct mlx5_eswitch *esw);
u32
mlx5_esw_chains_get_prio_range(struct mlx5_eswitch *esw);
u32
mlx5_esw_chains_get_chain_range(struct mlx5_eswitch *esw);
u32
mlx5_esw_chains_get_ft_chain(struct mlx5_eswitch *esw);

struct mlx5_flow_table *
mlx5_esw_chains_get_table(struct mlx5_eswitch *esw, u32 chain, u32 prio,
			  u32 level);
void
mlx5_esw_chains_put_table(struct mlx5_eswitch *esw, u32 chain, u32 prio,
			  u32 level);

struct mlx5_flow_table *
mlx5_esw_chains_get_tc_end_ft(struct mlx5_eswitch *esw);

int mlx5_esw_chains_create(struct mlx5_eswitch *esw);
void mlx5_esw_chains_destroy(struct mlx5_eswitch *esw);

#endif /* __ML5_ESW_CHAINS_H__ */

