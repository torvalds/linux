// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2013-2018, Mellanox Technologies inc.  All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/srq.h>

static int srq_event_notifier(struct notifier_block *nb,
			      unsigned long type, void *data)
{
	struct mlx5_srq_table *table;
	struct mlx5_core_srq *srq;
	struct mlx5_eqe *eqe;
	u32 srqn;

	if (type != MLX5_EVENT_TYPE_SRQ_CATAS_ERROR &&
	    type != MLX5_EVENT_TYPE_SRQ_RQ_LIMIT)
		return NOTIFY_DONE;

	table = container_of(nb, struct mlx5_srq_table, nb);

	eqe = data;
	srqn = be32_to_cpu(eqe->data.qp_srq.qp_srq_n) & 0xffffff;

	spin_lock(&table->lock);

	srq = radix_tree_lookup(&table->tree, srqn);
	if (srq)
		atomic_inc(&srq->refcount);

	spin_unlock(&table->lock);

	if (!srq)
		return NOTIFY_OK;

	srq->event(srq, eqe->type);

	if (atomic_dec_and_test(&srq->refcount))
		complete(&srq->free);

	return NOTIFY_OK;
}

void mlx5_init_srq_table(struct mlx5_core_dev *dev)
{
	struct mlx5_srq_table *table = &dev->priv.srq_table;

	memset(table, 0, sizeof(*table));
	spin_lock_init(&table->lock);
	INIT_RADIX_TREE(&table->tree, GFP_ATOMIC);

	table->nb.notifier_call = srq_event_notifier;
	mlx5_notifier_register(dev, &table->nb);
}

void mlx5_cleanup_srq_table(struct mlx5_core_dev *dev)
{
	struct mlx5_srq_table *table = &dev->priv.srq_table;

	mlx5_notifier_unregister(dev, &table->nb);
}
