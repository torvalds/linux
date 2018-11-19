/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2018 Mellanox Technologies */

#ifndef __LIB_MLX5_EQ_H__
#define __LIB_MLX5_EQ_H__
#include <linux/mlx5/driver.h>

#define MLX5_MAX_IRQ_NAME       (32)

struct mlx5_eq_tasklet {
	struct list_head      list;
	struct list_head      process_list;
	struct tasklet_struct task;
	spinlock_t            lock; /* lock completion tasklet list */
};

struct mlx5_cq_table {
	spinlock_t              lock;	/* protect radix tree */
	struct radix_tree_root  tree;
};

struct mlx5_eq {
	struct mlx5_core_dev    *dev;
	struct mlx5_cq_table    cq_table;
	__be32 __iomem	        *doorbell;
	u32                     cons_index;
	struct mlx5_frag_buf    buf;
	int                     size;
	unsigned int            vecidx;
	unsigned int            irqn;
	u8                      eqn;
	int                     nent;
	struct mlx5_rsc_debug   *dbg;
};

struct mlx5_eq_comp {
	struct mlx5_eq          core; /* Must be first */
	struct mlx5_eq_tasklet  tasklet_ctx;
	struct list_head        list;
};

struct mlx5_eq_pagefault {
	struct mlx5_eq          *core;
	struct work_struct       work;
	spinlock_t               lock; /* Pagefaults spinlock */
	struct workqueue_struct  *wq;
	mempool_t                *pool;
};

int mlx5_eq_table_init(struct mlx5_core_dev *dev);
void mlx5_eq_table_cleanup(struct mlx5_core_dev *dev);
int mlx5_eq_table_create(struct mlx5_core_dev *dev);
void mlx5_eq_table_destroy(struct mlx5_core_dev *dev);

int mlx5_eq_add_cq(struct mlx5_eq *eq, struct mlx5_core_cq *cq);
int mlx5_eq_del_cq(struct mlx5_eq *eq, struct mlx5_core_cq *cq);
struct mlx5_eq_comp *mlx5_eqn2comp_eq(struct mlx5_core_dev *dev, int eqn);
struct mlx5_eq *mlx5_get_async_eq(struct mlx5_core_dev *dev);
void mlx5_cq_tasklet_cb(unsigned long data);
struct cpumask *mlx5_eq_comp_cpumask(struct mlx5_core_dev *dev, int ix);

u32 mlx5_eq_poll_irq_disabled(struct mlx5_eq_comp *eq);
void mlx5_eq_synchronize_async_irq(struct mlx5_core_dev *dev);
void mlx5_eq_synchronize_cmd_irq(struct mlx5_core_dev *dev);

int mlx5_debug_eq_add(struct mlx5_core_dev *dev, struct mlx5_eq *eq);
void mlx5_debug_eq_remove(struct mlx5_core_dev *dev, struct mlx5_eq *eq);
int mlx5_eq_debugfs_init(struct mlx5_core_dev *dev);
void mlx5_eq_debugfs_cleanup(struct mlx5_core_dev *dev);

/* This function should only be called after mlx5_cmd_force_teardown_hca */
void mlx5_core_eq_free_irqs(struct mlx5_core_dev *dev);

#ifdef CONFIG_RFS_ACCEL
struct cpu_rmap *mlx5_eq_table_get_rmap(struct mlx5_core_dev *dev);
#endif

#endif
