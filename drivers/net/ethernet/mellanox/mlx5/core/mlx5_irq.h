/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021 Mellanox Technologies. */

#ifndef __MLX5_IRQ_H__
#define __MLX5_IRQ_H__

#include <linux/mlx5/driver.h>

#define MLX5_COMP_EQS_PER_SF 8

struct mlx5_irq;
struct cpu_rmap;

int mlx5_irq_table_init(struct mlx5_core_dev *dev);
void mlx5_irq_table_cleanup(struct mlx5_core_dev *dev);
int mlx5_irq_table_create(struct mlx5_core_dev *dev);
void mlx5_irq_table_destroy(struct mlx5_core_dev *dev);
void mlx5_irq_table_free_irqs(struct mlx5_core_dev *dev);
int mlx5_irq_table_get_num_comp(struct mlx5_irq_table *table);
int mlx5_irq_table_get_sfs_vec(struct mlx5_irq_table *table);
struct mlx5_irq_table *mlx5_irq_table_get(struct mlx5_core_dev *dev);

int mlx5_set_msix_vec_count(struct mlx5_core_dev *dev, int devfn,
			    int msix_vec_count);
int mlx5_get_default_msix_vec_count(struct mlx5_core_dev *dev, int num_vfs);

struct mlx5_irq *mlx5_ctrl_irq_request(struct mlx5_core_dev *dev);
void mlx5_ctrl_irq_release(struct mlx5_core_dev *dev, struct mlx5_irq *ctrl_irq);
struct mlx5_irq *mlx5_irq_request(struct mlx5_core_dev *dev, u16 vecidx,
				  struct irq_affinity_desc *af_desc,
				  struct cpu_rmap **rmap);
struct mlx5_irq *mlx5_irq_request_vector(struct mlx5_core_dev *dev, u16 cpu,
					 u16 vecidx, struct cpu_rmap **rmap);
void mlx5_irq_release_vector(struct mlx5_irq *irq);
int mlx5_irq_attach_nb(struct mlx5_irq *irq, struct notifier_block *nb);
int mlx5_irq_detach_nb(struct mlx5_irq *irq, struct notifier_block *nb);
struct cpumask *mlx5_irq_get_affinity_mask(struct mlx5_irq *irq);
int mlx5_irq_get_index(struct mlx5_irq *irq);
int mlx5_irq_get_irq(const struct mlx5_irq *irq);

struct mlx5_irq_pool;
#ifdef CONFIG_MLX5_SF
struct mlx5_irq *mlx5_irq_affinity_irq_request_auto(struct mlx5_core_dev *dev,
						    struct cpumask *used_cpus, u16 vecidx);
struct mlx5_irq *
mlx5_irq_affinity_request(struct mlx5_core_dev *dev, struct mlx5_irq_pool *pool,
			  struct irq_affinity_desc *af_desc);
void mlx5_irq_affinity_irq_release(struct mlx5_core_dev *dev, struct mlx5_irq *irq);
#else
static inline
struct mlx5_irq *mlx5_irq_affinity_irq_request_auto(struct mlx5_core_dev *dev,
						    struct cpumask *used_cpus, u16 vecidx)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline struct mlx5_irq *
mlx5_irq_affinity_request(struct mlx5_core_dev *dev, struct mlx5_irq_pool *pool,
			  struct irq_affinity_desc *af_desc)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline
void mlx5_irq_affinity_irq_release(struct mlx5_core_dev *dev, struct mlx5_irq *irq)
{
	mlx5_irq_release_vector(irq);
}
#endif
#endif /* __MLX5_IRQ_H__ */
