/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __MLX5_EN_TC_INT_PORT_H__
#define __MLX5_EN_TC_INT_PORT_H__

#include "en.h"

struct mlx5e_tc_int_port;
struct mlx5e_tc_int_port_priv;

enum mlx5e_tc_int_port_type {
	MLX5E_TC_INT_PORT_INGRESS,
	MLX5E_TC_INT_PORT_EGRESS,
};

#if IS_ENABLED(CONFIG_MLX5_CLS_ACT)
bool mlx5e_tc_int_port_supported(const struct mlx5_eswitch *esw);

struct mlx5e_tc_int_port_priv *
mlx5e_tc_int_port_init(struct mlx5e_priv *priv);
void
mlx5e_tc_int_port_cleanup(struct mlx5e_tc_int_port_priv *priv);

void mlx5e_tc_int_port_init_rep_rx(struct mlx5e_priv *priv);
void mlx5e_tc_int_port_cleanup_rep_rx(struct mlx5e_priv *priv);

bool
mlx5e_tc_int_port_dev_fwd(struct mlx5e_tc_int_port_priv *priv,
			  struct sk_buff *skb, u32 int_vport_metadata,
			  bool *forward_tx);
struct mlx5e_tc_int_port *
mlx5e_tc_int_port_get(struct mlx5e_tc_int_port_priv *priv,
		      int ifindex,
		      enum mlx5e_tc_int_port_type type);
void
mlx5e_tc_int_port_put(struct mlx5e_tc_int_port_priv *priv,
		      struct mlx5e_tc_int_port *int_port);

u32 mlx5e_tc_int_port_get_metadata(struct mlx5e_tc_int_port *int_port);
u32 mlx5e_tc_int_port_get_metadata_for_match(struct mlx5e_tc_int_port *int_port);
int mlx5e_tc_int_port_get_flow_source(struct mlx5e_tc_int_port *int_port);
#else /* CONFIG_MLX5_CLS_ACT */
static inline u32
mlx5e_tc_int_port_get_metadata_for_match(struct mlx5e_tc_int_port *int_port)
{
		return 0;
}

static inline int
mlx5e_tc_int_port_get_flow_source(struct mlx5e_tc_int_port *int_port)
{
		return 0;
}

static inline bool mlx5e_tc_int_port_supported(const struct mlx5_eswitch *esw)
{
	return false;
}

static inline void mlx5e_tc_int_port_init_rep_rx(struct mlx5e_priv *priv) {}
static inline void mlx5e_tc_int_port_cleanup_rep_rx(struct mlx5e_priv *priv) {}

#endif /* CONFIG_MLX5_CLS_ACT */
#endif /* __MLX5_EN_TC_INT_PORT_H__ */
