// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include "mlx5_core.h"
#include "mlx5_irq.h"
#include "pci_irq.h"

/* Creating an IRQ from irq_pool */
static struct mlx5_irq *
irq_pool_request_irq(struct mlx5_irq_pool *pool, const struct cpumask *req_mask)
{
	u32 irq_index;
	int err;

	err = xa_alloc(&pool->irqs, &irq_index, NULL, pool->xa_num_irqs,
		       GFP_KERNEL);
	if (err)
		return ERR_PTR(err);
	return mlx5_irq_alloc(pool, irq_index, req_mask);
}

/* Looking for the IRQ with the smallest refcount and the same mask */
static struct mlx5_irq *
irq_pool_find_least_loaded(struct mlx5_irq_pool *pool, const struct cpumask *req_mask)
{
	int start = pool->xa_num_irqs.min;
	int end = pool->xa_num_irqs.max;
	struct mlx5_irq *irq = NULL;
	struct mlx5_irq *iter;
	int irq_refcount = 0;
	unsigned long index;

	lockdep_assert_held(&pool->lock);
	xa_for_each_range(&pool->irqs, index, iter, start, end) {
		struct cpumask *iter_mask = mlx5_irq_get_affinity_mask(iter);
		int iter_refcount = mlx5_irq_read_locked(iter);

		if (!cpumask_equal(iter_mask, req_mask))
			/* If a user request a mask, skip IRQs that's aren't a match */
			continue;
		if (iter_refcount < pool->min_threshold)
			/* If we found an IRQ with less than min_thres, return it */
			return iter;
		if (!irq || iter_refcount < irq_refcount) {
			/* In case we won't find an IRQ with less than min_thres,
			 * keep a pointer to the least used IRQ
			 */
			irq_refcount = iter_refcount;
			irq = iter;
		}
	}
	return irq;
}

/**
 * mlx5_irq_affinity_request - request an IRQ according to the given mask.
 * @pool: IRQ pool to request from.
 * @req_mask: cpumask requested for this IRQ.
 *
 * This function returns a pointer to IRQ, or ERR_PTR in case of error.
 */
struct mlx5_irq *
mlx5_irq_affinity_request(struct mlx5_irq_pool *pool, const struct cpumask *req_mask)
{
	struct mlx5_irq *least_loaded_irq, *new_irq;

	mutex_lock(&pool->lock);
	least_loaded_irq = irq_pool_find_least_loaded(pool, req_mask);
	if (least_loaded_irq &&
	    mlx5_irq_read_locked(least_loaded_irq) < pool->min_threshold)
		goto out;
	/* We didn't find an IRQ with less than min_thres, try to allocate a new IRQ */
	new_irq = irq_pool_request_irq(pool, req_mask);
	if (IS_ERR(new_irq)) {
		if (!least_loaded_irq) {
			/* We failed to create an IRQ and we didn't find an IRQ */
			mlx5_core_err(pool->dev, "Didn't find a matching IRQ. err = %ld\n",
				      PTR_ERR(new_irq));
			mutex_unlock(&pool->lock);
			return new_irq;
		}
		/* We failed to create a new IRQ for the requested affinity,
		 * sharing existing IRQ.
		 */
		goto out;
	}
	least_loaded_irq = new_irq;
	goto unlock;
out:
	mlx5_irq_get_locked(least_loaded_irq);
	if (mlx5_irq_read_locked(least_loaded_irq) > pool->max_threshold)
		mlx5_core_dbg(pool->dev, "IRQ %u overloaded, pool_name: %s, %u EQs on this irq\n",
			      pci_irq_vector(pool->dev->pdev,
					     mlx5_irq_get_index(least_loaded_irq)), pool->name,
			      mlx5_irq_read_locked(least_loaded_irq) / MLX5_EQ_REFS_PER_IRQ);
unlock:
	mutex_unlock(&pool->lock);
	return least_loaded_irq;
}
