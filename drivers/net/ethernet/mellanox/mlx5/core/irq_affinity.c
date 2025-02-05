// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include "mlx5_core.h"
#include "mlx5_irq.h"
#include "pci_irq.h"

static void cpu_put(struct mlx5_irq_pool *pool, int cpu)
{
	pool->irqs_per_cpu[cpu]--;
}

static void cpu_get(struct mlx5_irq_pool *pool, int cpu)
{
	pool->irqs_per_cpu[cpu]++;
}

/* Gets the least loaded CPU. e.g.: the CPU with least IRQs bound to it */
static int cpu_get_least_loaded(struct mlx5_irq_pool *pool,
				const struct cpumask *req_mask)
{
	int best_cpu = -1;
	int cpu;

	for_each_cpu_and(cpu, req_mask, cpu_online_mask) {
		/* CPU has zero IRQs on it. No need to search any more CPUs. */
		if (!pool->irqs_per_cpu[cpu]) {
			best_cpu = cpu;
			break;
		}
		if (best_cpu < 0)
			best_cpu = cpu;
		if (pool->irqs_per_cpu[cpu] < pool->irqs_per_cpu[best_cpu])
			best_cpu = cpu;
	}
	if (best_cpu == -1) {
		/* There isn't online CPUs in req_mask */
		mlx5_core_err(pool->dev, "NO online CPUs in req_mask (%*pbl)\n",
			      cpumask_pr_args(req_mask));
		best_cpu = cpumask_first(cpu_online_mask);
	}
	pool->irqs_per_cpu[best_cpu]++;
	return best_cpu;
}

/* Creating an IRQ from irq_pool */
static struct mlx5_irq *
irq_pool_request_irq(struct mlx5_irq_pool *pool, struct irq_affinity_desc *af_desc)
{
	struct irq_affinity_desc auto_desc = {};
	struct mlx5_irq *irq;
	u32 irq_index;
	int err;

	err = xa_alloc(&pool->irqs, &irq_index, NULL, pool->xa_num_irqs, GFP_KERNEL);
	if (err)
		return ERR_PTR(err);
	if (pool->irqs_per_cpu) {
		if (cpumask_weight(&af_desc->mask) > 1)
			/* if req_mask contain more then one CPU, set the least loadad CPU
			 * of req_mask
			 */
			cpumask_set_cpu(cpu_get_least_loaded(pool, &af_desc->mask),
					&auto_desc.mask);
		else
			cpu_get(pool, cpumask_first(&af_desc->mask));
	}
	irq = mlx5_irq_alloc(pool, irq_index,
			     cpumask_empty(&auto_desc.mask) ? af_desc : &auto_desc,
			     NULL);
	if (IS_ERR(irq))
		xa_erase(&pool->irqs, irq_index);
	return irq;
}

/* Looking for the IRQ with the smallest refcount that fits req_mask.
 * If pool is sf_comp_pool, then we are looking for an IRQ with any of the
 * requested CPUs in req_mask.
 * for example: req_mask = 0xf, irq0_mask = 0x10, irq1_mask = 0x1. irq0_mask
 * isn't subset of req_mask, so we will skip it. irq1_mask is subset of req_mask,
 * we don't skip it.
 * If pool is sf_ctrl_pool, then all IRQs have the same mask, so any IRQ will
 * fit. And since mask is subset of itself, we will pass the first if bellow.
 */
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

		if (!cpumask_subset(iter_mask, req_mask))
			/* skip IRQs with a mask which is not subset of req_mask */
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
 * @dev: mlx5 core device which is requesting the IRQ.
 * @pool: IRQ pool to request from.
 * @af_desc: affinity descriptor for this IRQ.
 *
 * This function returns a pointer to IRQ, or ERR_PTR in case of error.
 */
struct mlx5_irq *
mlx5_irq_affinity_request(struct mlx5_core_dev *dev, struct mlx5_irq_pool *pool,
			  struct irq_affinity_desc *af_desc)
{
	struct mlx5_irq *least_loaded_irq, *new_irq;
	int ret;

	mutex_lock(&pool->lock);
	least_loaded_irq = irq_pool_find_least_loaded(pool, &af_desc->mask);
	if (least_loaded_irq &&
	    mlx5_irq_read_locked(least_loaded_irq) < pool->min_threshold)
		goto out;
	/* We didn't find an IRQ with less than min_thres, try to allocate a new IRQ */
	new_irq = irq_pool_request_irq(pool, af_desc);
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
	if (mlx5_irq_pool_is_sf_pool(pool)) {
		ret = auxiliary_device_sysfs_irq_add(mlx5_sf_coredev_to_adev(dev),
						     mlx5_irq_get_irq(least_loaded_irq));
		if (ret) {
			mlx5_core_err(dev, "Failed to create sysfs entry for irq %d, ret = %d\n",
				      mlx5_irq_get_irq(least_loaded_irq), ret);
			mlx5_irq_put(least_loaded_irq);
			least_loaded_irq = ERR_PTR(ret);
		}
	}
	return least_loaded_irq;
}

void mlx5_irq_affinity_irq_release(struct mlx5_core_dev *dev, struct mlx5_irq *irq)
{
	struct mlx5_irq_pool *pool = mlx5_irq_pool_get(dev);
	int cpu;

	cpu = cpumask_first(mlx5_irq_get_affinity_mask(irq));
	synchronize_irq(pci_irq_vector(pool->dev->pdev,
				       mlx5_irq_get_index(irq)));
	if (mlx5_irq_pool_is_sf_pool(pool))
		auxiliary_device_sysfs_irq_remove(mlx5_sf_coredev_to_adev(dev),
						  mlx5_irq_get_irq(irq));
	if (mlx5_irq_put(irq))
		if (pool->irqs_per_cpu)
			cpu_put(pool, cpu);
}
