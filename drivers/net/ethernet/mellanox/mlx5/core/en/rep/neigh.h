/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2020 Mellanox Technologies. */

#ifndef __MLX5_EN_REP_NEIGH__
#define __MLX5_EN_REP_NEIGH__

#include "en.h"
#include "en_rep.h"

int mlx5e_rep_neigh_init(struct mlx5e_rep_priv *rpriv);
void mlx5e_rep_neigh_cleanup(struct mlx5e_rep_priv *rpriv);

struct mlx5e_neigh_hash_entry *
mlx5e_rep_neigh_entry_lookup(struct mlx5e_priv *priv,
			     struct mlx5e_neigh *m_neigh);
int mlx5e_rep_neigh_entry_create(struct mlx5e_priv *priv,
				 struct mlx5e_encap_entry *e,
				 struct mlx5e_neigh_hash_entry **nhe);
void mlx5e_rep_neigh_entry_release(struct mlx5e_neigh_hash_entry *nhe);

void mlx5e_rep_queue_neigh_stats_work(struct mlx5e_priv *priv);

#endif /* __MLX5_EN_REP_NEIGH__ */
