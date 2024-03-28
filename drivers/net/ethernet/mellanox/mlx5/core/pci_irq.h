/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __PCI_IRQ_H__
#define __PCI_IRQ_H__

#include <linux/mlx5/driver.h>

#define MLX5_MAX_IRQ_NAME (32)
#define MLX5_IRQ_NAME_FORMAT_STR ("%s@pci:%s")
#define MLX5_MAX_IRQ_FORMATTED_NAME \
	(MLX5_MAX_IRQ_NAME + sizeof(MLX5_IRQ_NAME_FORMAT_STR))
/* max irq_index is 2047, so four chars */
#define MLX5_MAX_IRQ_IDX_CHARS (4)
#define MLX5_EQ_REFS_PER_IRQ (2)

struct mlx5_irq;

struct mlx5_irq_pool {
	char name[MLX5_MAX_IRQ_NAME - MLX5_MAX_IRQ_IDX_CHARS];
	struct xa_limit xa_num_irqs;
	struct mutex lock; /* sync IRQs creations */
	struct xarray irqs;
	u32 max_threshold;
	u32 min_threshold;
	u16 *irqs_per_cpu;
	struct mlx5_core_dev *dev;
};

struct mlx5_irq_pool *mlx5_irq_pool_get(struct mlx5_core_dev *dev);
static inline bool mlx5_irq_pool_is_sf_pool(struct mlx5_irq_pool *pool)
{
	return !strncmp("mlx5_sf", pool->name, strlen("mlx5_sf"));
}

struct mlx5_irq *mlx5_irq_alloc(struct mlx5_irq_pool *pool, int i,
				const struct cpumask *affinity);
int mlx5_irq_get_locked(struct mlx5_irq *irq);
int mlx5_irq_read_locked(struct mlx5_irq *irq);
int mlx5_irq_put(struct mlx5_irq *irq);

#endif /* __PCI_IRQ_H__ */
