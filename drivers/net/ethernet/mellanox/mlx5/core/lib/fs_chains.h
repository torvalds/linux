/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2020 Mellanox Technologies. */

#ifndef __ML5_ESW_CHAINS_H__
#define __ML5_ESW_CHAINS_H__

#include <linux/mlx5/fs.h>

struct mlx5_fs_chains;
struct mlx5_mapped_obj;

enum mlx5_chains_flags {
	MLX5_CHAINS_AND_PRIOS_SUPPORTED = BIT(0),
	MLX5_CHAINS_IGNORE_FLOW_LEVEL_SUPPORTED = BIT(1),
	MLX5_CHAINS_FT_TUNNEL_SUPPORTED = BIT(2),
};

struct mlx5_chains_attr {
	enum mlx5_flow_namespace_type ns;
	int fs_base_prio;
	int fs_base_level;
	u32 flags;
	u32 max_grp_num;
	struct mlx5_flow_table *default_ft;
	struct mapping_ctx *mapping;
};

#if IS_ENABLED(CONFIG_MLX5_CLS_ACT)

bool
mlx5_chains_prios_supported(struct mlx5_fs_chains *chains);
bool mlx5_chains_ignore_flow_level_supported(struct mlx5_fs_chains *chains);
bool
mlx5_chains_backwards_supported(struct mlx5_fs_chains *chains);
u32
mlx5_chains_get_prio_range(struct mlx5_fs_chains *chains);
u32
mlx5_chains_get_chain_range(struct mlx5_fs_chains *chains);
u32
mlx5_chains_get_nf_ft_chain(struct mlx5_fs_chains *chains);

struct mlx5_flow_table *
mlx5_chains_get_table(struct mlx5_fs_chains *chains, u32 chain, u32 prio,
		      u32 level);
void
mlx5_chains_put_table(struct mlx5_fs_chains *chains, u32 chain, u32 prio,
		      u32 level);

struct mlx5_flow_table *
mlx5_chains_get_tc_end_ft(struct mlx5_fs_chains *chains);

struct mlx5_flow_table *
mlx5_chains_create_global_table(struct mlx5_fs_chains *chains);
void
mlx5_chains_destroy_global_table(struct mlx5_fs_chains *chains,
				 struct mlx5_flow_table *ft);

int
mlx5_chains_get_chain_mapping(struct mlx5_fs_chains *chains, u32 chain,
			      u32 *chain_mapping);
int
mlx5_chains_put_chain_mapping(struct mlx5_fs_chains *chains,
			      u32 chain_mapping);

struct mlx5_fs_chains *
mlx5_chains_create(struct mlx5_core_dev *dev, struct mlx5_chains_attr *attr);
void mlx5_chains_destroy(struct mlx5_fs_chains *chains);

void
mlx5_chains_set_end_ft(struct mlx5_fs_chains *chains,
		       struct mlx5_flow_table *ft);
void
mlx5_chains_print_info(struct mlx5_fs_chains *chains);

#else /* CONFIG_MLX5_CLS_ACT */

static inline bool
mlx5_chains_ignore_flow_level_supported(struct mlx5_fs_chains *chains)
{ return false; }

static inline struct mlx5_flow_table *
mlx5_chains_get_table(struct mlx5_fs_chains *chains, u32 chain, u32 prio,
		      u32 level) { return ERR_PTR(-EOPNOTSUPP); }
static inline void
mlx5_chains_put_table(struct mlx5_fs_chains *chains, u32 chain, u32 prio,
		      u32 level) {};

static inline struct mlx5_flow_table *
mlx5_chains_get_tc_end_ft(struct mlx5_fs_chains *chains) { return ERR_PTR(-EOPNOTSUPP); }

static inline struct mlx5_fs_chains *
mlx5_chains_create(struct mlx5_core_dev *dev, struct mlx5_chains_attr *attr)
{ return NULL; }
static inline void
mlx5_chains_destroy(struct mlx5_fs_chains *chains) {}
static inline void
mlx5_chains_print_info(struct mlx5_fs_chains *chains) {}

#endif /* CONFIG_MLX5_CLS_ACT */

#endif /* __ML5_ESW_CHAINS_H__ */
