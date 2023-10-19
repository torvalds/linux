/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021 Mellanox Technologies. */

#ifndef __MLX5_ESW_FT_H__
#define __MLX5_ESW_FT_H__

#ifdef CONFIG_MLX5_CLS_ACT

struct mlx5_esw_indir_table *
mlx5_esw_indir_table_init(void);
void
mlx5_esw_indir_table_destroy(struct mlx5_esw_indir_table *indir);

struct mlx5_flow_table *mlx5_esw_indir_table_get(struct mlx5_eswitch *esw,
						 struct mlx5_flow_attr *attr,
						 u16 vport, bool decap);
void mlx5_esw_indir_table_put(struct mlx5_eswitch *esw,
			      u16 vport, bool decap);

bool
mlx5_esw_indir_table_needed(struct mlx5_eswitch *esw,
			    struct mlx5_flow_attr *attr,
			    u16 vport_num,
			    struct mlx5_core_dev *dest_mdev);

u16
mlx5_esw_indir_table_decap_vport(struct mlx5_flow_attr *attr);

#else
/* indir API stubs */
static inline struct mlx5_esw_indir_table *
mlx5_esw_indir_table_init(void)
{
	return NULL;
}

static inline void
mlx5_esw_indir_table_destroy(struct mlx5_esw_indir_table *indir)
{
}

static inline struct mlx5_flow_table *
mlx5_esw_indir_table_get(struct mlx5_eswitch *esw,
			 struct mlx5_flow_attr *attr,
			 u16 vport, bool decap)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline void
mlx5_esw_indir_table_put(struct mlx5_eswitch *esw,
			 u16 vport, bool decap)
{
}

static inline bool
mlx5_esw_indir_table_needed(struct mlx5_eswitch *esw,
			    struct mlx5_flow_attr *attr,
			    u16 vport_num,
			    struct mlx5_core_dev *dest_mdev)
{
	return false;
}

static inline u16
mlx5_esw_indir_table_decap_vport(struct mlx5_flow_attr *attr)
{
	return 0;
}
#endif

#endif /* __MLX5_ESW_FT_H__ */
