/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __MLX5_ESW_IPSEC_FS_H__
#define __MLX5_ESW_IPSEC_FS_H__

struct mlx5e_ipsec;
struct mlx5e_ipsec_sa_entry;

#ifdef CONFIG_MLX5_ESWITCH
void mlx5_esw_ipsec_rx_create_attr_set(struct mlx5e_ipsec *ipsec,
				       struct mlx5e_ipsec_rx_create_attr *attr);
int mlx5_esw_ipsec_rx_status_pass_dest_get(struct mlx5e_ipsec *ipsec,
					   struct mlx5_flow_destination *dest);
int mlx5_esw_ipsec_rx_setup_modify_header(struct mlx5e_ipsec_sa_entry *sa_entry,
					  struct mlx5_flow_act *flow_act);
void mlx5_esw_ipsec_rx_id_mapping_remove(struct mlx5e_ipsec_sa_entry *sa_entry);
int mlx5_esw_ipsec_rx_ipsec_obj_id_search(struct mlx5e_priv *priv, u32 id,
					  u32 *ipsec_obj_id);
void mlx5_esw_ipsec_tx_create_attr_set(struct mlx5e_ipsec *ipsec,
				       struct mlx5e_ipsec_tx_create_attr *attr);
void mlx5_esw_ipsec_restore_dest_uplink(struct mlx5_core_dev *mdev);
void mlx5_esw_ipsec_rx_rule_add_match_obj(struct mlx5e_ipsec_sa_entry *sa_entry,
					  struct mlx5_flow_spec *spec);
#else
static inline void mlx5_esw_ipsec_rx_create_attr_set(struct mlx5e_ipsec *ipsec,
						     struct mlx5e_ipsec_rx_create_attr *attr) {}

static inline int mlx5_esw_ipsec_rx_status_pass_dest_get(struct mlx5e_ipsec *ipsec,
							 struct mlx5_flow_destination *dest)
{
	return -EINVAL;
}

static inline int mlx5_esw_ipsec_rx_setup_modify_header(struct mlx5e_ipsec_sa_entry *sa_entry,
							struct mlx5_flow_act *flow_act)
{
	return -EINVAL;
}

static inline void mlx5_esw_ipsec_rx_id_mapping_remove(struct mlx5e_ipsec_sa_entry *sa_entry) {}

static inline int mlx5_esw_ipsec_rx_ipsec_obj_id_search(struct mlx5e_priv *priv, u32 id,
							u32 *ipsec_obj_id)
{
	return -EINVAL;
}

static inline void mlx5_esw_ipsec_tx_create_attr_set(struct mlx5e_ipsec *ipsec,
						     struct mlx5e_ipsec_tx_create_attr *attr) {}

static inline void mlx5_esw_ipsec_restore_dest_uplink(struct mlx5_core_dev *mdev) {}
static inline void
mlx5_esw_ipsec_rx_rule_add_match_obj(struct mlx5e_ipsec_sa_entry *sa_entry,
				     struct mlx5_flow_spec *spec) {}
#endif /* CONFIG_MLX5_ESWITCH */
#endif /* __MLX5_ESW_IPSEC_FS_H__ */
