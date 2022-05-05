/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __LIB_HV_VHCA_H__
#define __LIB_HV_VHCA_H__

#include "en.h"
#include "lib/hv.h"

struct mlx5_hv_vhca_agent;
struct mlx5_hv_vhca;
struct mlx5_hv_vhca_control_block;

enum mlx5_hv_vhca_agent_type {
	MLX5_HV_VHCA_AGENT_CONTROL = 0,
	MLX5_HV_VHCA_AGENT_STATS   = 1,
	MLX5_HV_VHCA_AGENT_MAX = 32,
};

#if IS_ENABLED(CONFIG_PCI_HYPERV_INTERFACE)

struct mlx5_hv_vhca_control_block {
	u32     capabilities;
	u32     control;
	u16     command;
	u16     command_ack;
	u16     version;
	u16     rings;
	u32     reserved1[28];
};

struct mlx5_hv_vhca *mlx5_hv_vhca_create(struct mlx5_core_dev *dev);
void mlx5_hv_vhca_destroy(struct mlx5_hv_vhca *hv_vhca);
int mlx5_hv_vhca_init(struct mlx5_hv_vhca *hv_vhca);
void mlx5_hv_vhca_cleanup(struct mlx5_hv_vhca *hv_vhca);
void mlx5_hv_vhca_invalidate(void *context, u64 block_mask);

struct mlx5_hv_vhca_agent *
mlx5_hv_vhca_agent_create(struct mlx5_hv_vhca *hv_vhca,
			  enum mlx5_hv_vhca_agent_type type,
			  void (*control)(struct mlx5_hv_vhca_agent*,
					  struct mlx5_hv_vhca_control_block *block),
			  void (*invalidate)(struct mlx5_hv_vhca_agent*,
					     u64 block_mask),
			  void (*cleanup)(struct mlx5_hv_vhca_agent *agent),
			  void *context);

void mlx5_hv_vhca_agent_destroy(struct mlx5_hv_vhca_agent *agent);
int mlx5_hv_vhca_agent_write(struct mlx5_hv_vhca_agent *agent,
			     void *buf, int len);
void *mlx5_hv_vhca_agent_priv(struct mlx5_hv_vhca_agent *agent);

#else

static inline struct mlx5_hv_vhca *
mlx5_hv_vhca_create(struct mlx5_core_dev *dev)
{
	return NULL;
}

static inline void mlx5_hv_vhca_destroy(struct mlx5_hv_vhca *hv_vhca)
{
}

static inline int mlx5_hv_vhca_init(struct mlx5_hv_vhca *hv_vhca)
{
	return 0;
}

static inline void mlx5_hv_vhca_cleanup(struct mlx5_hv_vhca *hv_vhca)
{
}

static inline void mlx5_hv_vhca_invalidate(void *context,
					   u64 block_mask)
{
}

static inline struct mlx5_hv_vhca_agent *
mlx5_hv_vhca_agent_create(struct mlx5_hv_vhca *hv_vhca,
			  enum mlx5_hv_vhca_agent_type type,
			  void (*control)(struct mlx5_hv_vhca_agent*,
					  struct mlx5_hv_vhca_control_block *block),
			  void (*invalidate)(struct mlx5_hv_vhca_agent*,
					     u64 block_mask),
			  void (*cleanup)(struct mlx5_hv_vhca_agent *agent),
			  void *context)
{
	return NULL;
}

static inline void mlx5_hv_vhca_agent_destroy(struct mlx5_hv_vhca_agent *agent)
{
}
#endif

#endif /* __LIB_HV_VHCA_H__ */
