/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2020 Mellanox Technologies */

#ifndef __MLX5E_EN_MOD_HDR_H__
#define __MLX5E_EN_MOD_HDR_H__

#include <linux/hashtable.h>
#include <linux/mlx5/fs.h>

struct mlx5e_mod_hdr_handle;

struct mlx5e_tc_mod_hdr_acts {
	int num_actions;
	int max_actions;
	void *actions;
};

struct mlx5e_mod_hdr_handle *
mlx5e_mod_hdr_attach(struct mlx5_core_dev *mdev,
		     struct mod_hdr_tbl *tbl,
		     enum mlx5_flow_namespace_type namespace,
		     struct mlx5e_tc_mod_hdr_acts *mod_hdr_acts);
void mlx5e_mod_hdr_detach(struct mlx5_core_dev *mdev,
			  struct mod_hdr_tbl *tbl,
			  struct mlx5e_mod_hdr_handle *mh);
struct mlx5_modify_hdr *mlx5e_mod_hdr_get(struct mlx5e_mod_hdr_handle *mh);

void mlx5e_mod_hdr_tbl_init(struct mod_hdr_tbl *tbl);
void mlx5e_mod_hdr_tbl_destroy(struct mod_hdr_tbl *tbl);

#endif /* __MLX5E_EN_MOD_HDR_H__ */
